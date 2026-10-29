#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricPlatform.h"
#include "ParametricGround.h"
#include "ChunkedDestructibleTriangle.h"
#include "ChunkedDestructibleRoundedBlob.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPlatformTest, "MyProject6.Platform.UnifiedGeometryModesAndCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPlatformTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Platform = World->SpawnActor<AParametricPlatform>();
    if (!TestNotNull(TEXT("New actor spawns"), Platform)) return false;
    const EChunkedDecorativeFloorShape Shapes[] = {EChunkedDecorativeFloorShape::Grid, EChunkedDecorativeFloorShape::Triangle,
        EChunkedDecorativeFloorShape::RoundedBlob, EChunkedDecorativeFloorShape::Square,
        EChunkedDecorativeFloorShape::Circle, EChunkedDecorativeFloorShape::Star, EChunkedDecorativeFloorShape::Hexagon};
    Platform->BorderStyle = EPlatformBorder::None;
    Platform->CircleEdgeSegments = 32;
    Platform->TileSize = FVector2D(100, 100);
    Platform->bUseDestructionRipple = false;
    for (EPlatformMode Mode : {EPlatformMode::Solid, EPlatformMode::Cellular})
    {
        for (auto Shape : Shapes)
        {
            Platform->FloorPattern = EChunkedFloorPattern::Solid;
            Platform->ConfigurePlatform(Shape, FVector2D(600, 500), 80, Mode);
            TestTrue(TEXT("Every shape produces geometry"), Platform->ChunkComponents.Num() > 0);
            FHitResult Hit;
            TestTrue(TEXT("Every shape has a walkable surface"), World->LineTraceSingleByChannel(Hit,
                FVector(23, 17, 300), FVector(23, 17, -300), ECC_GameTraceChannel1));
            TestTrue(TEXT("Surface hit belongs to the unified actor"), Hit.GetActor() == Platform);
            TestTrue(TEXT("Surface remains at local Z zero"), FMath::Abs(Hit.ImpactPoint.Z) < 1);
            if (Mode == EPlatformMode::Solid)
            {
                TestEqual(TEXT("Solid mode owns one mesh"), Platform->GetPlatformMeshCount(), 1);
                TestEqual(TEXT("Solid has no destructible grid"), Platform->GetDestructibleCellCount(), 0);
                TestTrue(TEXT("Solid slab topology is closed"), Platform->ChunkComponents[0]->GetMesh()->IsClosed());
                TestFalse(TEXT("Railgun hit cannot damage solid slab"), Platform->DamageCellFromHit(Hit, 10000));
                Platform->SetAllCellsDestructible(true);
                TestFalse(TEXT("Legacy cell API cannot bypass solid mode"), Platform->DestroyCell(FIntPoint::ZeroValue));
                TestEqual(TEXT("Bomb cannot destroy a solid slab"), Platform->ApplyExplosionFromHit(Hit, 500, 10000), 0);
            }
            else
            {
                TestTrue(TEXT("Cellular mode has independently damageable cells"), Platform->GetDestructibleCellCount() > 1);
                TestTrue(TEXT("Railgun damage path accepts cellular hit"), Platform->DamageCellFromHit(Hit, 10000));
                FHitResult Hole;
                TestFalse(TEXT("Destroyed cell immediately removes collision"), World->LineTraceSingleByChannel(Hole,
                    Hit.ImpactPoint + FVector(0, 0, 100), Hit.ImpactPoint - FVector(0, 0, 150), ECC_GameTraceChannel1));
                Platform->RestoreAllCells();
            }
            for (int32 Pattern = 0; Pattern <= int32(EChunkedFloorPattern::CirclesAndPaths); ++Pattern)
            {
                Platform->HeartMotifSettings.Size = FVector2D(350, 300);
                Platform->StarMotifSettings.Size = FVector2D(350, 300);
                Platform->FlowerMotifSettings.FlowerSize = FVector2D(350, 300);
                Platform->SetFloorPattern(static_cast<EChunkedFloorPattern>(Pattern));
                if (Mode == EPlatformMode::Cellular && Pattern == int32(EChunkedFloorPattern::Checkerboard))
                    TestTrue(TEXT("Runtime pattern selection updates cell materials"), Platform->Cells.ContainsByPredicate(
                        [&](const auto& Cell) { return Cell.bExists && Cell.MaterialIndex == Platform->SecondaryMaterialIndex; }));
                for (UDynamicMeshComponent* Component : Platform->ChunkComponents)
                {
                    const auto* Mesh = Component->GetMesh();
                    TestTrue(TEXT("Pattern geometry is valid in both modes"), Mesh->CheckValidity());
                    TestTrue(TEXT("Pattern retains the selected shape"), Mesh->TriangleCount() > 0);
                }
            }
        }
    }
    // Original Ground uses the same slab builder, including its exact coplanar boundary.
    auto* Ground = World->SpawnActor<AParametricGround>();
    Ground->SetActorLocation(FVector(20000, 0, 0));
    Ground->Size = 1600; Ground->Thickness = 72; Ground->BoundaryInset = 140; Ground->BoundaryWidth = 65;
    Ground->GroundColor = FLinearColor(.1f, .2f, .3f); Ground->BoundaryColor = FLinearColor::Green;
    for (auto Shape : {EParametricGroundShape::Circle, EParametricGroundShape::Square, EParametricGroundShape::Hexagon})
    {
        Ground->Shape = Shape;
        Ground->RebuildGround();
        TestTrue(TEXT("Ground settings import"), Platform->CopyPlatformSettingsFrom(Ground));
        const auto* Original = Ground->GetDynamicMeshComponent()->GetMesh();
        const auto* Unified = Platform->ChunkComponents[0]->GetMesh();
        TestEqual(TEXT("Ground and unified slab have identical triangle count"), Unified->TriangleCount(), Original->TriangleCount());
        TestTrue(TEXT("Ground bounds are preserved"), Unified->GetBounds().Min.Equals(Original->GetBounds().Min, .001) &&
            Unified->GetBounds().Max.Equals(Original->GetBounds().Max, .001));
        TestTrue(TEXT("Ground transform is not imported"), Platform->GetActorLocation().IsNearlyZero());
        auto* MID = Cast<UMaterialInstanceDynamic>(Platform->ChunkComponents[0]->GetMaterial(1));
        TestTrue(TEXT("Boundary tint survives import"), MID && MID->K2_GetVectorParameterValue(TEXT("Color")).Equals(Ground->BoundaryColor));
    }
    Ground->Destroy();
    auto* Triangle = World->SpawnActor<AChunkedDestructibleTriangle>();
    Triangle->ConfigureTriangle(700, 500, .45f, FVector(100, 100, 60), FVector2D::ZeroVector, false);
    Platform->CopyPlatformSettingsFrom(Triangle);
    TestTrue(TEXT("Triangle dimensions import"), Platform->ShapeSize.Equals(FVector2D(700, 500)));
    TestEqual(TEXT("Triangle apex imports"), Platform->TriangleApexOffset, .45f);
    Triangle->Destroy();
    auto* Blob = World->SpawnActor<AChunkedDestructibleRoundedBlob>();
    Blob->ConfigureRoundedBlob(FVector2D(700, 500), 5, .3f, .8f, 91, 12, FVector(100, 100, 60), FVector2D::ZeroVector, false);
    TArray<FVector2D> OriginalOutline;
    Blob->GetRoundedBlobOutline(OriginalOutline);
    Platform->CopyPlatformSettingsFrom(Blob);
    TArray<FVector2D> NewOutline;
    Platform->GetPlatformOutline(NewOutline);
    TestEqual(TEXT("Original blob profile retains its sample count"), NewOutline.Num(), OriginalOutline.Num());
    bool SameOutline = NewOutline.Num() == OriginalOutline.Num();
    for (int32 I = 0; SameOutline && I < NewOutline.Num(); ++I) SameOutline &= NewOutline[I].Equals(OriginalOutline[I], .01);
    TestTrue(TEXT("Original blob profile retains its silhouette"), SameOutline);
    Blob->Destroy();
    Platform->SetPlatformMode(EPlatformMode::Solid);
    Platform->SetPlatformMode(EPlatformMode::Cellular);
    TestTrue(TEXT("Switching back to cellular restores damageable cells"), Platform->DestroyCell(FIntPoint(3, 2)));

    Platform->FloorPattern = EChunkedFloorPattern::Solid;
    Platform->BorderStyle = EPlatformBorder::None;
    Platform->ConfigurePlatform(EChunkedDecorativeFloorShape::Circle, FVector2D(2000, 2000), 50, EPlatformMode::Solid);
    const int32 SmallTriangles = Platform->ChunkComponents[0]->GetMesh()->TriangleCount();
    Platform->ConfigurePlatform(EChunkedDecorativeFloorShape::Circle, FVector2D(1000000, 1000000), 50, EPlatformMode::Solid);
    TestEqual(TEXT("Ten kilometre ground has the same geometry cost"), Platform->ChunkComponents[0]->GetMesh()->TriangleCount(), SmallTriangles);
    TestEqual(TEXT("Ten kilometre ground still uses only one surface record"), Platform->Cells.Num(), 1);
    TestEqual(TEXT("Ten kilometre ground still uses only one component"), Platform->GetPlatformMeshCount(), 1);
    Platform->MaxSolidPatternTiles = 64;
    Platform->SetFloorPattern(EChunkedFloorPattern::Checkerboard);
    TestTrue(TEXT("Large tiled solid has bounded surface geometry"), Platform->ChunkComponents[0]->GetMesh()->TriangleCount() < 15000);

    // Insets on concave shapes remain clipped to intact cells after a hit.
    Platform->BorderStyle = EPlatformBorder::InsetBand; Platform->BoundaryInset = 30; Platform->BorderWidth = 40;
    Platform->FloorPattern = EChunkedFloorPattern::Solid;
    Platform->ConfigurePlatform(EChunkedDecorativeFloorShape::Star, FVector2D(1000, 1000), 50, EPlatformMode::Cellular);
    for (UDynamicMeshComponent* Component : Platform->ChunkComponents)
        TestTrue(TEXT("Concave inset border is valid"), Component->GetMesh()->CheckValidity());
    Platform->bGenerateCollision = false; Platform->RebuildPlatform();
    FHitResult NoCollision;
    TestFalse(TEXT("Collision switch is respected"), World->LineTraceSingleByChannel(NoCollision, FVector(0, 0, 500), FVector(0, 0, -500), ECC_GameTraceChannel1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPlatformSurfaceTest, "MyProject6.Platform.SolidSurfaceOptimization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPlatformSurfaceTest::RunTest(const FString& Parameters)
{
    using namespace UE::Geometry;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Platform = World->SpawnActor<AParametricPlatform>();
    Platform->BorderStyle = EPlatformBorder::None;
    Platform->bGenerateCollision = false;
    Platform->CircleEdgeSegments = 96;
    auto NewMesh = []()
    {
        FDynamicMesh3 Mesh;
        Mesh.EnableAttributes(); Mesh.Attributes()->EnableMaterialID();
        return Mesh;
    };
    auto Area = [](const FDynamicMesh3& Mesh)
    {
        double Result = 0;
        for (int32 ID : Mesh.TriangleIndicesItr())
        {
            FVector3d A, B, C; Mesh.GetTriVertices(ID, A, B, C);
            Result += FVector3d::CrossProduct(B - A, C - A).Length() * .5;
        }
        return Result;
    };
    auto Covers = [](const FDynamicMesh3& Mesh, FVector2D P)
    {
        for (int32 ID : Mesh.TriangleIndicesItr())
        {
            FVector3d A3, B3, C3; Mesh.GetTriVertices(ID, A3, B3, C3);
            FVector2D A(A3.X, A3.Y), B(B3.X, B3.Y), C(C3.X, C3.Y);
            const double AB = FVector2D::CrossProduct(B - A, P - A);
            const double BC = FVector2D::CrossProduct(C - B, P - B);
            const double CA = FVector2D::CrossProduct(A - C, P - C);
            if ((AB >= -1.e-5 && BC >= -1.e-5 && CA >= -1.e-5) ||
                (AB <= 1.e-5 && BC <= 1.e-5 && CA <= 1.e-5)) return true;
        }
        return false;
    };
    auto CheckCoverage = [&](const FDynamicMesh3& Old, const FDynamicMesh3& Optimized, const FString& Context)
    {
        int32 Mismatches = 0;
        // Off-grid probes test filled regions, clipped edges, disconnected islands and empty gaps.
        for (int32 Y = 0; Y < 43; ++Y) for (int32 X = 0; X < 43; ++X)
        {
            const FVector2D Point(-650 + (X + .317) * 1300 / 43, -650 + (Y + .713) * 1300 / 43);
            Mismatches += Covers(Old, Point) != Covers(Optimized, Point);
        }
        TestEqual(Context + TEXT(" preserves filled and empty regions"), Mismatches, 0);
        TestTrue(Context + TEXT(" has valid geometry"), Optimized.CheckValidity());
        bool AttributesMatch = true;
        for (int32 ID : Optimized.TriangleIndicesItr())
        {
            const FIndex3i V = Optimized.GetTriangle(ID);
            const FIndex3i UV = Optimized.Attributes()->PrimaryUV()->GetTriangle(ID);
            const FIndex3i N = Optimized.Attributes()->PrimaryNormals()->GetTriangle(ID);
            AttributesMatch &= Optimized.Attributes()->GetMaterialID()->GetValue(ID) == 1;
            for (int32 I = 0; I < 3; ++I)
            {
                const FVector3d P = Optimized.GetVertex(V[I]);
                AttributesMatch &= FMath::IsNearlyEqual(P.Z, .2) &&
                    Optimized.Attributes()->PrimaryNormals()->GetElement(N[I]).Equals(FVector3f::UnitZ(), 1.e-5f) &&
                    Optimized.Attributes()->PrimaryUV()->GetElement(UV[I]).Equals(
                        FVector2f(P.X / Platform->UVWorldUnits, P.Y / Platform->UVWorldUnits), 1.e-5f);
            }
        }
        TestTrue(Context + TEXT(" preserves material, height, normals and world UVs"), AttributesMatch);
    };
    const TArray<TArray<FVector2D>> Motifs = {
        {{-850,-700}, {850,-700}, {850,700}, {-850,700}}, // Crosses every edge.
        {{-700,-60}, {100,-60}, {100,-650}, {250,-650}, {250,220}, {-700,220}}, // Concave, crosses boundary.
        {{-180,-110}, {120,-110}, {120,160}, {-180,160}},
        {{1800,0}, {1900,0}, {1900,100}, {1800,100}} // Fully outside.
    };
    for (auto Shape : {EChunkedDecorativeFloorShape::Circle, EChunkedDecorativeFloorShape::Star,
        EChunkedDecorativeFloorShape::RoundedBlob, EChunkedDecorativeFloorShape::Triangle})
    {
        Platform->FloorPattern = EChunkedFloorPattern::Solid;
        Platform->ConfigurePlatform(Shape, FVector2D(1000, 900), 60, EPlatformMode::Solid);
        for (int32 I = 0; I < Motifs.Num(); ++I)
        {
            FDynamicMesh3 Old = NewMesh(), Optimized = NewMesh();
            Platform->PatternSurfaceCaches.Reset();
            Platform->AChunkedDestructibleDecorativeFloor::AppendPolygonSurface(Old, 0, Motifs[I], 1, .2);
            Platform->PatternSurfaceCaches.Reset();
            Platform->AppendPolygonSurface(Optimized, 0, Motifs[I], 1, .2);
            const FString Context = FString::Printf(TEXT("Shape %d motif %d"), int32(Shape), I);
            CheckCoverage(Old, Optimized, Context);
            TestTrue(Context + TEXT(" preserves painted area"), FMath::Abs(Area(Old) - Area(Optimized)) <= FMath::Max(1.0, Area(Old) * 1.e-5));
            TestTrue(Context + TEXT(" does not add triangles"), Optimized.TriangleCount() <= Old.TriangleCount());
            FDynamicMesh3 Cached = NewMesh();
            Platform->AppendPolygonSurface(Cached, 0, Motifs[I], 1, .2);
            TestEqual(TEXT("Cached surface retains geometry"), Cached.TriangleCount(), Optimized.TriangleCount());
        }
        for (bool Clockwise : {false, true})
        {
            Platform->SpiralSettings.bClockwise = Clockwise;
            Platform->SpiralSettings.CenterOffset = FVector2D(140, -60);
            Platform->SpiralSettings.RotationDegrees = Clockwise ? 23.f : -81.f;
            Platform->SetFloorPattern(EChunkedFloorPattern::HypnoticSpiral);
            TArray<TArray<FVector2D>> Fragments;
            Platform->GetClippedCellFragments(FIntPoint::ZeroValue, Fragments);
            TArray<FChunkedCachedSpiralTriangle> OldTriangles, OptimizedTriangles;
            Platform->AChunkedDestructibleFloor::ClipSpiralSurface(Fragments, OldTriangles);
            Platform->ClipSpiralSurface(Fragments, OptimizedTriangles);
            FDynamicMesh3 Old = NewMesh(), Optimized = NewMesh();
            for (const auto& T : OldTriangles) Platform->AppendSolidSurfaceTriangle(Old, T.A, T.B, T.C, 1, .2);
            for (const auto& T : OptimizedTriangles) Platform->AppendSolidSurfaceTriangle(Optimized, T.A, T.B, T.C, 1, .2);
            const FString Context = FString::Printf(TEXT("Shape %d spiral %d"), int32(Shape), Clockwise);
            CheckCoverage(Old, Optimized, Context);
            TestTrue(Context + TEXT(" removes internal cuts and overlapping cap faces"), Optimized.TriangleCount() < Old.TriangleCount());
        }
    }
    return true;
}
#endif
