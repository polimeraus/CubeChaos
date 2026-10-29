#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricPlatform.h"
#include "Components/DynamicMeshComponent.h"
#include "Polygon2.h"
#include "PropertyEditorModule.h"
#include "IPropertyRowGenerator.h"
#include "IDetailTreeNode.h"
#include "PropertyHandle.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPlatformPadPathTest, "MyProject6.Platform.CirclesAndPaths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPlatformPadPathTest::RunTest(const FString& Parameters)
{
    using namespace UE::Geometry;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Platform = World->SpawnActor<AParametricPlatform>();
    if (!TestNotNull(TEXT("Pattern material is available in native defaults"), Platform->PadPathSettings.SurfaceMaterial.Get())) return false;
    Platform->BorderStyle = EPlatformBorder::None;
    Platform->bUseDestructionRipple = false;
    Platform->TileSize = FVector2D(150, 150);
    Platform->SideNeon.Mode = EFloorSideNeonMode::FullSide;
    auto TriangleCount = [&]()
    {
        int32 Count = 0;
        for (const auto& C : Platform->ChunkComponents) Count += C->GetMesh()->TriangleCount();
        return Count;
    };
    auto VerifySurface = [&]()
    {
        bool Correct = true;
        int32 Painted = 0, Unpainted = 0;
        for (const auto& Component : Platform->ChunkComponents)
        {
            Correct &= Component->GetMaterial(Platform->PadPathMaterialIndex()) == Platform->PadPathMaterialInstance;
            const FDynamicMesh3& Mesh = *Component->GetMesh();
            for (int32 ID : Mesh.TriangleIndicesItr())
            {
                const FIndex3i V = Mesh.GetTriangle(ID);
                const bool Top = FMath::IsNearlyZero(Mesh.GetVertex(V.A).Z) && FMath::IsNearlyZero(Mesh.GetVertex(V.B).Z) &&
                    FMath::IsNearlyZero(Mesh.GetVertex(V.C).Z);
                const bool Pattern = Mesh.Attributes()->GetMaterialID()->GetValue(ID) == Platform->PadPathMaterialIndex();
                Correct &= Top == Pattern;
                if (!Pattern) { ++Unpainted; continue; }
                ++Painted;
                const FIndex3i UV = Mesh.Attributes()->PrimaryUV()->GetTriangle(ID);
                for (int32 I = 0; I < 3; ++I)
                {
                    const FVector3d P = Mesh.GetVertex(V[I]);
                    Correct &= Mesh.Attributes()->PrimaryUV()->GetElement(UV[I]).Equals(
                        FVector2f(P.X, P.Y) / Platform->UVWorldUnits, 1.e-5f);
                }
            }
        }
        TestTrue(TEXT("Only upper faces are painted; cell UVs are continuous; side neon has its own material"), Correct && Painted > 0 && Unpainted > 0);
    };
    for (auto Mode : {EPlatformMode::Solid, EPlatformMode::Cellular})
    for (auto Shape : {EChunkedDecorativeFloorShape::Grid, EChunkedDecorativeFloorShape::Triangle,
        EChunkedDecorativeFloorShape::Circle, EChunkedDecorativeFloorShape::Square, EChunkedDecorativeFloorShape::Star,
        EChunkedDecorativeFloorShape::RoundedBlob, EChunkedDecorativeFloorShape::Hexagon})
    {
        Platform->FloorPattern = EChunkedFloorPattern::Solid;
        Platform->ConfigurePlatform(Shape, FVector2D(1800, 1500), 70, Mode);
        const int32 PlainCount = TriangleCount();
        Platform->SetFloorPattern(EChunkedFloorPattern::CirclesAndPaths);
        TestEqual(TEXT("Painting adds no triangles"), TriangleCount(), PlainCount);
        VerifySurface();
        auto* M = Platform->PadPathMaterialInstance.Get();
        if (!TestNotNull(TEXT("Pattern MID exists in both modes"), M)) return false;
        TestEqual(TEXT("Reference layout has three visible pads"), M->K2_GetScalarParameterValue(TEXT("PadCount")), 3.f);
        TestEqual(TEXT("Reference layout has four connections"), M->K2_GetScalarParameterValue(TEXT("SegmentCount")), 4.f);
        for (int32 I = 0; I < 3; ++I)
        {
            const FLinearColor P = M->K2_GetVectorParameterValue(FName(*FString::Printf(TEXT("Pad%d"), I)));
            bool Fits = true;
            for (int32 Step = 0; Step < 64; ++Step)
            {
                const double Angle = Step * UE_DOUBLE_TWO_PI / 64;
                Fits &= FPolygon2d(Platform->CachedShapeOutline).Contains(FVector2D(P.R, P.G) +
                    FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (P.B - .02));
            }
            TestTrue(TEXT("Fitted circles remain within even concave boundaries"), Fits);
        }
        if (Mode == EPlatformMode::Cellular)
        {
            FHitResult Hit;
            TestTrue(TEXT("Painted surface still has collision"), World->LineTraceSingleByChannel(Hit,
                FVector(23,17,300), FVector(23,17,-300), ECC_GameTraceChannel1));
            TestTrue(TEXT("Painted cell is damageable"), Platform->DamageCellFromHit(Hit, 10000));
            Platform->PadPathSettings.PatternColor = FLinearColor::Green;
            Platform->ApplyMaterialPattern();
            FHitResult Hole;
            TestFalse(TEXT("Editing the pattern preserves the destroyed cell"), World->LineTraceSingleByChannel(Hole,
                FVector(23,17,300), FVector(23,17,-300), ECC_GameTraceChannel1));
            VerifySurface();
            Platform->RestoreAllCells();
        }
    }
    // Inspect the same customized tree used by the Details panel, rather than just metadata.
    {
    auto& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
    auto Rows = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
    Rows->SetObjects({Platform});
    bool FoundPadsBelowPattern = false, FoundPathsBelowPattern = false;
    TFunction<void(TSharedRef<IDetailTreeNode>, bool)> Visit = [&](TSharedRef<IDetailTreeNode> Node, bool UnderPattern)
    {
        UnderPattern |= Node->GetNodeName() == TEXT("FloorPatternWithMotifs");
        const auto Handle = Node->CreatePropertyHandle();
        if (UnderPattern && Handle && Handle->GetProperty())
        {
            FoundPadsBelowPattern |= Handle->GetProperty()->GetFName() == TEXT("Pads");
            FoundPathsBelowPattern |= Handle->GetProperty()->GetFName() == TEXT("Paths");
        }
        TArray<TSharedRef<IDetailTreeNode>> Children;
        Node->GetChildren(Children);
        for (const auto& Child : Children) Visit(Child, UnderPattern);
    };
    for (const auto& Root : Rows->GetRootTreeNodes()) Visit(Root, false);
    TestTrue(TEXT("Details panel places Pads and Paths beneath Floor Pattern"), FoundPadsBelowPattern && FoundPathsBelowPattern);
    }
    Platform->PadPathSettings.bShowLayout = false;
    Platform->ApplyMaterialPattern();
    TestEqual(TEXT("Show Layout reaches shader"), Platform->PadPathMaterialInstance->K2_GetScalarParameterValue(TEXT("ShowLayout")), 0.f);
    const FPlatformPatternPad Duplicate = Platform->PadPathSettings.Pads[0];
    Platform->PadPathSettings.Pads.Add(Duplicate);
    Platform->ApplyMaterialPattern();
    TestTrue(TEXT("Duplicate names are reported"), Platform->PadPathWarning.Contains(TEXT("duplicate")));
    TestEqual(TEXT("Paths with ambiguous IDs are not painted"), Platform->PadPathMaterialInstance->K2_GetScalarParameterValue(TEXT("SegmentCount")), 0.f);
    Platform->PadPathSettings.Pads.Reset();
    Platform->PadPathSettings.Paths.Reset();
    Platform->ApplyMaterialPattern();
    TestEqual(TEXT("Deleting pads clears the old layout"), Platform->PadPathMaterialInstance->K2_GetScalarParameterValue(TEXT("PadCount")), 0.f);
    TestTrue(TEXT("Stale pad data is cleared"), Platform->PadPathMaterialInstance->K2_GetVectorParameterValue(TEXT("Pad0")).Equals(FLinearColor::Transparent));
    Platform->SetFloorPattern(EChunkedFloorPattern::HeartMotif);
    TestNull(TEXT("Switching pattern releases the old material"), Platform->PadPathMaterialInstance.Get());
    Platform->SetFloorPattern(EChunkedFloorPattern::Solid);
    TestTrue(TEXT("Original plain pattern still builds"), TriangleCount() > 0);
    return true;
}
#endif
