#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricPlatform.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPlatformReferenceShapeTest, "MyProject6.Platform.ReferenceShapes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPlatformReferenceShapeTest::RunTest(const FString& Parameters)
{
    using namespace UE::Geometry;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* P = World->SpawnActor<AParametricPlatform>();
    P->BorderStyle = EPlatformBorder::None;
    P->FillRule = EChunkedFloorFillRule::StretchCells;
    P->bUseDestructionRipple = false;
    P->FlashDuration = 0;
    const TSet<EChunkedDecorativeFloorShape> Holed = {
        EChunkedDecorativeFloorShape::PiercedSteppedBlock,EChunkedDecorativeFloorShape::PiercedTrapezoid,
        EChunkedDecorativeFloorShape::PiercedCapsule,EChunkedDecorativeFloorShape::InterlockTab,
        EChunkedDecorativeFloorShape::InterlockSocket,EChunkedDecorativeFloorShape::Ring,
        EChunkedDecorativeFloorShape::PiercedDiamond};

    auto Check = [&](const FString& Context, bool Collision)
    {
        if (!TestTrue(Context+TEXT(" valid outline"),P->ShapeWarning.IsEmpty() && P->CachedShapeOutline.Num()>=3)) return;
        const FGeneralPolygon2d Polygon=P->GetPlatformPolygon();
        TestEqual(Context+TEXT(" hole count"),P->GetPlatformHoleCount(),Holed.Contains(P->OuterShape)?1:0);
        double TopArea=0;
        bool Valid=true,Closed=true,PaintFits=true;
        for (const auto& C : P->ChunkComponents)
        {
            const FDynamicMesh3& Mesh=*C->GetMesh();
            Valid &= Mesh.CheckValidity();
            if (P->PlatformMode==EPlatformMode::Solid && P->FloorPattern==EChunkedFloorPattern::Solid)
                Closed &= Mesh.IsClosed();
            for (const int32 ID : Mesh.TriangleIndicesItr())
            {
                FVector3d A,B,Cv; Mesh.GetTriVertices(ID,A,B,Cv);
                if (FMath::IsNearlyZero(A.Z) && FMath::IsNearlyZero(B.Z) && FMath::IsNearlyZero(Cv.Z))
                    TopArea += FVector3d::CrossProduct(B-A,Cv-A).Length()*.5;
                if (A.Z>=0 && B.Z>=0 && Cv.Z>=0)
                {
                    const FVector3d Center=(A+B+Cv)/3;
                    const FVector2D XY(Center.X,Center.Y);
                    int Hole,Edge; double T;
                    PaintFits &= Polygon.Contains(XY) || Polygon.DistanceSquared(XY,Hole,Edge,T)<.001;
                }
            }
        }
        TestTrue(Context+TEXT(" valid mesh"),Valid);
        TestTrue(Context+TEXT(" watertight solid including hole walls"),Closed);
        TestTrue(Context+TEXT(" area excludes holes"),FMath::Abs(TopArea-Polygon.SignedArea())<FMath::Max(2.,Polygon.SignedArea()*.0002));
        TestTrue(Context+TEXT(" motifs respect all voids"),PaintFits);
        if (!Collision) return;
        int32 Wrong=0;
        for (int32 Y=0; Y<19; ++Y) for (int32 X=0; X<19; ++X)
        {
            const FVector2D XY(-450+(X+.231)*900/19,-350+(Y+.613)*700/19);
            int Hole,Edge; double T;
            if (Polygon.DistanceSquared(XY,Hole,Edge,T)<1) continue;
            FHitResult Hit;
            const bool Hits=World->LineTraceSingleByChannel(Hit,FVector(XY.X,XY.Y,200),FVector(XY.X,XY.Y,-200),ECC_GameTraceChannel1);
            Wrong += Hits!=Polygon.Contains(XY);
        }
        TestEqual(Context+TEXT(" collision matches silhouette and holes"),Wrong,0);
    };
    TestEqual(TEXT("Old enum entries retain their serialized values"),int32(EChunkedDecorativeFloorShape::CustomBlocks),14);
    for (int32 Value=int32(EChunkedDecorativeFloorShape::DiagonalLink); Value<=int32(EChunkedDecorativeFloorShape::SteppedBranch); ++Value)
    {
        const auto Shape=EChunkedDecorativeFloorShape(Value);
        const FString Name=StaticEnum<EChunkedDecorativeFloorShape>()->GetNameStringByValue(Value);
        for (const auto Mode : {EPlatformMode::Solid,EPlatformMode::Cellular})
        {
            P->ReferenceShape=FPlatformReferenceShape();
            P->TileSize=FVector2D(113,127);
            P->FloorPattern=EChunkedFloorPattern::Solid;
            P->ConfigurePlatform(Shape,{900,700},60,Mode);
            Check(Name+FString::Printf(TEXT(" mode %d"),int32(Mode)),true);
            if (Mode==EPlatformMode::Cellular)
            {
                FHitResult Hit;
                // Symmetric anchors may fall exactly on a destruction-cell seam. Probe
                // slightly off that seam so a ray selects one unambiguous cell.
                const auto XY=P->CachedShapeFanCenter+FVector2D(.231,.617);
                TestTrue(Name+TEXT(" interior anchor"),World->LineTraceSingleByChannel(Hit,FVector(XY.X,XY.Y,200),FVector(XY.X,XY.Y,-200),ECC_GameTraceChannel1));
                TestTrue(Name+TEXT(" destructible"),P->DamageCellFromHit(Hit,10000));
                FHitResult Removed;
                TestFalse(Name+TEXT(" damaged cell collision removed"),World->LineTraceSingleByChannel(Removed,FVector(XY.X,XY.Y,200),FVector(XY.X,XY.Y,-200),ECC_GameTraceChannel1));
                P->RestoreAllCells();
                P->TileSize=FVector2D(2000); P->RebuildPlatform();
                Check(Name+TEXT(" single cell enclosing a hole"),true);
            }
        }
        for (int32 Turn=0; Turn<4; ++Turn) for (bool Mirror : {false,true})
        {
            P->ReferenceShape.QuarterTurns=Turn; P->ReferenceShape.bMirror=Mirror;
            P->ReferenceShape.OpeningRatio=Turn%2 ? .25f : .7f;
            P->ConfigurePlatform(Shape,{900,700},60,EPlatformMode::Solid);
            Check(Name+FString::Printf(TEXT(" turn %d mirror %d"),Turn,Mirror),false);
        }
    }
    for (auto Mode : {EPlatformMode::Solid,EPlatformMode::Cellular})
    for (auto Pattern : {EChunkedFloorPattern::Checkerboard,EChunkedFloorPattern::HypnoticSpiral,EChunkedFloorPattern::HeartMotif,
        EChunkedFloorPattern::StarMotif,EChunkedFloorPattern::FlowerMotif,EChunkedFloorPattern::CirclesAndPaths})
    {
        P->ReferenceShape=FPlatformReferenceShape(); P->TileSize=FVector2D(140);
        P->BorderStyle=EPlatformBorder::InsetBand; P->BoundaryInset=20; P->BorderWidth=12;
        P->FloorPattern=Pattern;
        P->ConfigurePlatform(EChunkedDecorativeFloorShape::Ring,{900,700},60,Mode);
        Check(FString::Printf(TEXT("Ring motif %d mode %d"),int32(Pattern),int32(Mode)),true);
    }
    // Switching to an old shape must discard every cached hole.
    P->ConfigurePlatform(EChunkedDecorativeFloorShape::Grid,{900,700},60,EPlatformMode::Solid);
    TestEqual(TEXT("Changing shape clears holes"),P->GetPlatformHoleCount(),0);
    return true;
}
#endif
