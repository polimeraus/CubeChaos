#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricPlatform.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Polygon2.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPlatformBlockShapeTest, "MyProject6.Platform.BlockShapesAndRoundedCorners",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPlatformBlockShapeTest::RunTest(const FString& Parameters)
{
    using namespace UE::Geometry;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* P = World->SpawnActor<AParametricPlatform>();
    P->BorderStyle = EPlatformBorder::None;
    P->TileSize = FVector2D(130, 130);
    P->FillRule = EChunkedFloorFillRule::StretchCells;
    P->bUseDestructionRipple = false;
    P->FlashDuration = 0;
    auto Triangles = [&]()
    {
        int32 Count = 0;
        for (const auto& Component : P->ChunkComponents) Count += Component->GetMesh()->TriangleCount();
        return Count;
    };
    auto Check = [&](const FString& Context)
    {
        if (!TestTrue(Context + TEXT(" has a valid outline"), P->CachedShapeOutline.Num() >= 3)) return;
        const FPolygon2d Outline(P->CachedShapeOutline);
        double TopArea = 0;
        bool MeshValid = true, NoPaintInVoids = true;
        for (const auto& Component : P->ChunkComponents)
        {
            const FDynamicMesh3& Mesh = *Component->GetMesh();
            MeshValid &= Mesh.CheckValidity();
            if (P->PlatformMode == EPlatformMode::Solid && P->FloorPattern == EChunkedFloorPattern::Solid)
                MeshValid &= Mesh.IsClosed();
            for (int32 ID : Mesh.TriangleIndicesItr())
            {
                FVector3d A, B, C; Mesh.GetTriVertices(ID, A, B, C);
                if (FMath::IsNearlyZero(A.Z) && FMath::IsNearlyZero(B.Z) && FMath::IsNearlyZero(C.Z))
                    TopArea += FVector3d::CrossProduct(B - A, C - A).Length() * .5;
                if (A.Z >= 0 && B.Z >= 0 && C.Z >= 0)
                {
                    const FVector3d Center = (A + B + C) / 3;
                    const FVector2D XY(Center.X, Center.Y);
                    NoPaintInVoids &= Outline.Contains(XY) || Outline.DistanceSquared(XY) < .01;
                }
            }
        }
        TestTrue(Context + TEXT(" has valid closed solid topology"), MeshValid);
        TestTrue(Context + TEXT(" neither fills nor overlaps its open notches"), FMath::Abs(TopArea - Outline.Area()) < FMath::Max(2., Outline.Area() * .0001));
        TestTrue(Context + TEXT(" clips all motifs to the silhouette"), NoPaintInVoids);
        int32 WrongCollision = 0;
        for (int32 Y = 0; Y < 13; ++Y) for (int32 X = 0; X < 13; ++X)
        {
            const FVector2D Point(-510 + (X + .231) * 1020 / 13, -510 + (Y + .617) * 1020 / 13);
            if (Outline.DistanceSquared(Point) < 1) continue;
            FHitResult Hit;
            const bool Hits = World->LineTraceSingleByChannel(Hit, FVector(Point.X, Point.Y, 200), FVector(Point.X, Point.Y, -200), ECC_GameTraceChannel1);
            WrongCollision += Hits != Outline.Contains(Point);
        }
        TestEqual(Context + TEXT(" has collision only on the actual shape"), WrongCollision, 0);
    };
    const EChunkedDecorativeFloorShape Shapes[] = {EChunkedDecorativeFloorShape::LShape, EChunkedDecorativeFloorShape::TShape,
        EChunkedDecorativeFloorShape::Zigzag, EChunkedDecorativeFloorShape::UShape, EChunkedDecorativeFloorShape::Cross,
        EChunkedDecorativeFloorShape::CustomBlocks, EChunkedDecorativeFloorShape::Grid, EChunkedDecorativeFloorShape::Square,
        EChunkedDecorativeFloorShape::Triangle, EChunkedDecorativeFloorShape::Star, EChunkedDecorativeFloorShape::Hexagon};
    for (auto Mode : {EPlatformMode::Solid, EPlatformMode::Cellular})
    for (auto Shape : Shapes)
    for (float Radius : {0.f, 65.f})
    {
        P->CornerRadius = Radius;
        P->FloorPattern = EChunkedFloorPattern::Solid;
        P->ConfigurePlatform(Shape, FVector2D(900, 900), 60, Mode);
        const FString Context = FString::Printf(TEXT("Shape %d mode %d radius %.0f"), int32(Shape), int32(Mode), Radius);
        TestTrue(Context + TEXT(" has no shape warnings"), P->ShapeWarning.IsEmpty());
        Check(Context);
        const int32 PlainTriangles = Triangles();
        P->SetFloorPattern(EChunkedFloorPattern::CirclesAndPaths);
        TestEqual(Context + TEXT(" painting adds no triangles"), Triangles(), PlainTriangles);
        TestTrue(Context + TEXT(" retains persistent circle/path settings"), P->PadPathSettings.Pads.Num() == 5 && P->PadPathMaterialInstance != nullptr);
        if (Mode == EPlatformMode::Cellular)
        {
            const FVector2D Center = P->CachedShapeFanCenter;
            FHitResult Hit;
            TestTrue(Context + TEXT(" contains a valid interior anchor"), World->LineTraceSingleByChannel(Hit,
                FVector(Center.X, Center.Y, 200), FVector(Center.X, Center.Y, -200), ECC_GameTraceChannel1));
            TestTrue(Context + TEXT(" supports cell destruction"), P->DamageCellFromHit(Hit, 10000));
            FHitResult Hole;
            TestFalse(Context + TEXT(" removes hit collision immediately"), World->LineTraceSingleByChannel(Hole,
                FVector(Center.X, Center.Y, 200), FVector(Center.X, Center.Y, -200), ECC_GameTraceChannel1));
            P->RestoreAllCells();
        }
    }
    // Concave shapes must remain open for both continuous and cell-clipped decorative motifs.
    for (auto Mode : {EPlatformMode::Solid, EPlatformMode::Cellular})
    for (auto Pattern : {EChunkedFloorPattern::Checkerboard, EChunkedFloorPattern::HypnoticSpiral,
        EChunkedFloorPattern::HeartMotif, EChunkedFloorPattern::StarMotif, EChunkedFloorPattern::FlowerMotif})
    {
        P->FloorPattern = Pattern; P->CornerRadius = 60;
        P->ConfigurePlatform(EChunkedDecorativeFloorShape::UShape, FVector2D(900,900), 60, Mode);
        Check(FString::Printf(TEXT("U motif %d mode %d"), int32(Pattern), int32(Mode)));
    }
    P->FloorPattern = EChunkedFloorPattern::Solid;
    P->ConfigurePlatform(EChunkedDecorativeFloorShape::LShape, FVector2D(900,900), 60, EPlatformMode::Solid);
    const double Area = FPolygon2d(P->CachedShapeOutline).Area();
    for (bool Mirror : {false, true}) for (int32 Rotation = 0; Rotation < 4; ++Rotation)
    {
        P->BlockShape.bMirror = Mirror; P->BlockShape.QuarterTurns = Rotation; P->RebuildPlatform();
        TestTrue(TEXT("Mirroring/quarter turns preserve area"), FMath::IsNearlyEqual(FPolygon2d(P->CachedShapeOutline).Area(), Area, .01));
        Check(TEXT("Rotated/mirrored L/J"));
    }
    P->BlockShape = FPlatformBlockShape();
    P->BlockShape.Blocks = {{0,1},{1,0},{2,0}};
    P->BlockShape.DiagonalBridgeWidth = 100;
    P->ConfigurePlatform(EChunkedDecorativeFloorShape::CustomBlocks, FVector2D(900,900), 60, EPlatformMode::Solid);
    Check(TEXT("Reference diagonal connection"));
    P->BlockShape.DiagonalBridgeWidth = 0; P->RebuildPlatform();
    TestTrue(TEXT("Zero-width corner contacts are reported"), !P->ShapeWarning.IsEmpty());
    TestEqual(TEXT("Invalid outline does not leave old geometry behind"), Triangles(), 0);
    P->BlockShape.Blocks = {{0,0},{1,0},{2,0},{0,1},{2,1},{0,2},{1,2},{2,2}}; P->RebuildPlatform();
    TestTrue(TEXT("Unsupported enclosed hole is reported instead of filled"), !P->ShapeWarning.IsEmpty());
    TestEqual(TEXT("Enclosed hole validation leaves no misleading slab"), Triangles(), 0);
    P->BlockShape.Blocks.Reset(); P->RebuildPlatform();
    TestEqual(TEXT("Empty custom layout is safe"), Triangles(), 0);
    return true;
}
#endif
