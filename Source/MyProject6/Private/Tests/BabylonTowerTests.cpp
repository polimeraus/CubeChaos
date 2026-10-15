#if WITH_DEV_AUTOMATION_TESTS
#include "ChunkedDestructibleBabylonTower.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/Convex.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBabylonTowerTest,"MyProject6.Destructible.BabylonTower",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBabylonTowerTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* A=World->SpawnActor<AChunkedDestructibleBabylonTower>();
    if (!TestNotNull(TEXT("Native actor spawns"),A)) return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    A->Traversal.bGroundLaunchPad=false;A->RebuildTower();
    auto Triangles=[&]() {int32 N=0;for (auto& C:A->ChunkComponents) N+=C->GetDynamicMesh()->GetTriangleCount();return N;};
    auto PieceMesh=[&](const auto& P)
    {
        UE::Geometry::FDynamicMesh3 M;M.EnableVertexNormals(FVector3f::UnitZ());M.EnableAttributes();
        M.Attributes()->SetNumUVLayers(1);M.Attributes()->EnableMaterialID();A->AppendPiece(M,P,P.Material);return M;
    };
    auto Trace=[&](FVector S,FVector E) {return World->LineTraceTestByChannel(S,E,ECC_Visibility);};
    const int32 DefaultCount=A->GeneratedPieceCount,DefaultTriangles=Triangles();
    TestEqual(TEXT("Reference preset is octagonal"),A->SideCount,8);
    TestEqual(TEXT("Reference preset has fifteen tiers"),A->TierCount,15);
    TestFalse(TEXT("Reference has no entrance staircase"),A->bEntranceStairs);
    TestFalse(TEXT("Reference has no decorative cornice bands"),A->bCornices);
    TestTrue(TEXT("Default geometry fits budget"),DefaultCount>100 && DefaultCount<50000 && A->GenerationWarning.IsEmpty());
    for (const auto& Component:A->ChunkComponents)
        for (const auto& Convex:Component->GetBodySetup()->AggGeom.ConvexElems)
            if (!TestTrue(TEXT("Every visible fragment has a valid cooked collision hull"),
                Convex.GetChaosConvexMesh() && Convex.GetChaosConvexMesh()->IsValidGeometry()))
            {
                for (const auto& V:Convex.VertexData) AddInfo(V.ToString());
                return false;
            }
    TestEqual(TEXT("Each base face has a door, each upper face has one window"),A->GeneratedOpeningCount,120);
    TestTrue(TEXT("Default height includes actual enabled features"),FMath::IsNearlyEqual(A->GeneratedHeight,
        A->FoundationHeight+14*A->TierHeight+A->SummitHeight+15*A->FloorThickness));
    for (const auto& P:A->Pieces)
    {
        const auto Mesh=PieceMesh(P);
        if (!TestTrue(TEXT("Every fragment has positive height"),P.Bounds.GetSize().Z>0)) return false;
        for (int32 T:Mesh.TriangleIndicesItr())
            if (!TestTrue(TEXT("No degenerate triangles in polygon walls or slabs"),Mesh.GetTriArea(T)>1.e-8)) return false;
        FVector Center=FVector::ZeroVector;for (const auto& V:P.V) Center+=V;Center/=8;
        if (!TestTrue(TEXT("Convex containment includes fragment centroid"),P.Contains(Center,.001))) return false;
    }
    TestFalse(TEXT("Base door is open"),Trace({0,-1950,100},{0,-1700,100}));
    const double UpperZ=A->FoundationHeight+A->TierHeight+A->FloorThickness;
    const double UpperDepth=A->BaseDepth*.5*(1-(1-A->TopScale)/(A->TierCount-1));
    TestFalse(TEXT("Upper window is open"),Trace({0,-UpperDepth-100,UpperZ+100},{0,-UpperDepth+100,UpperZ+100}));
    TestTrue(TEXT("Upper window has solid sill, never a doorway"),Trace({0,-UpperDepth-100,UpperZ+15},{0,-UpperDepth+100,UpperZ+15}));
    TestTrue(TEXT("Window width leaves adjacent wall intact"),Trace({100,-UpperDepth-100,UpperZ+100},{100,-UpperDepth+100,UpperZ+100}));
    FHitResult Hit;
    const FVector Start(400,-1950,110),End(400,-1650,110);
    if (!TestTrue(TEXT("Shot hits a polygon pier"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility))) return false;
    FIntPoint Cell;
    if (!TestTrue(TEXT("Hit maps to a destructible fragment"),A->ResolveExistingCellFromHit(Hit,Cell))) return false;
    const int32 Target=Cell.X;const auto Piece=A->Pieces[Target];const int32 RemovedTriangles=PieceMesh(Piece).TriangleCount();
    TestTrue(TEXT("Nonlethal shot applies health"),A->DamageCellFromHit(Hit,5));
    TestEqual(TEXT("Nonlethal damage retains mesh"),Triangles(),DefaultTriangles);
    TestTrue(TEXT("Lethal shot removes piece"),A->DamageCellFromHit(Hit,100));
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestEqual(TEXT("Only hit fragment triangles disappear"),Triangles(),DefaultTriangles-RemovedTriangles);
    TestFalse(TEXT("Destroyed convex collision disappears"),Trace(Start,End));
    A->RestoreCell({Target,0});TestTrue(TEXT("Restored piece collides"),Trace(Start,End));
    const FTransform Transform(FRotator(0,41,0),FVector(700,-250,100),FVector(1.4,.8,1.2));
    A->SetActorTransform(Transform);
    FVector Center=FVector::ZeroVector;for (const auto& V:Piece.V) Center+=V;Center/=8;
    TestTrue(TEXT("Transformed convex piece maps correctly"),A->WorldLocationToCell(Transform.TransformPosition(Center),Cell));
    TestEqual(TEXT("Transformed identity"),Cell.X,Target);
    TestTrue(TEXT("Explosion affects multiple pieces"),A->DestroyCellsInRadius(Transform.TransformPosition(Center),230,EChunkedFloorBlastShape::Circle)>1);
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    A->SetActorTransform(FTransform::Identity);A->RebuildTower();
    TestEqual(TEXT("Rebuild deterministically restores geometry"),Triangles(),DefaultTriangles);

    A->TierCount=3;A->TopScale=.45f;A->EntranceCount=1;A->WindowsPerSide=2;
    for (int32 Sides:{3,4,5,7,8,12,32})
    {
        A->SideCount=Sides;A->RebuildTower();
        TestTrue(TEXT("Polygon variant generates without warnings"),A->GenerationWarning.IsEmpty());
        TestEqual(TEXT("Window count uses every requested polygon face"),A->GeneratedOpeningCount,1+4*Sides);
        const double Z=A->FoundationHeight+10;
        for (int32 Face=0;Face<Sides;++Face)
        {
            const double Angle=-UE_HALF_PI+Face*UE_TWO_PI/Sides;
            const FVector N(FMath::Cos(Angle),FMath::Sin(Angle),0);
            TestEqual(TEXT("Every polygon edge has solid ground wall except entrance"),Trace(N*1600+FVector(0,0,Z),N*1900+FVector(0,0,Z)),Face!=0);
        }
        // Square traversal aperture must remain clear through every polygon slab.
        TestFalse(TEXT("Polygon slabs preserve square shaft corner"),Trace({85,85,100},{85,85,A->GeneratedHeight+100}));
    }
    A->SideCount=7;A->WindowsPerSide=1;A->TierWindowCounts={0,3,0};A->TierWindowWidths={0,180};A->RebuildTower();
    TestEqual(TEXT("Per-tier window counts override the global count including zero"),A->GeneratedOpeningCount,22);
    A->TierWindowCounts.Reset();A->WindowsPerSide=1;A->RebuildTower();
    const double Z=A->FoundationHeight+A->TierHeight+A->FloorThickness+100;
    const double Depth=A->BaseDepth*.5*(1+A->TopScale)*.5;
    TestFalse(TEXT("Per-tier width opens wall at 75 cm from center"),Trace({75,-Depth-100,Z},{75,-Depth+100,Z}));
    A->TierWindowWidths.Reset();A->WindowWidth=60;A->RebuildTower();
    TestTrue(TEXT("Reducing width restores wall at same point"),Trace({75,-Depth-100,Z},{75,-Depth+100,Z}));
    const int32 Plain=A->GeneratedPieceCount;const float PlainHeight=A->GeneratedHeight;
    A->bCornices=true;A->RebuildTower();
    TestTrue(TEXT("Cornice toggle adds damageable bands"),A->GeneratedPieceCount>Plain);
    TestTrue(TEXT("Band height is parameterized"),FMath::IsNearlyEqual(A->GeneratedHeight,PlainHeight+3*A->CorniceHeight));
    A->bCornices=false;A->bEntranceStairs=true;A->RebuildTower();
    TestTrue(TEXT("Stair toggle adds steps outside ground entrance"),Trace({0,-2000,100},{0,-2000,-10}));
    A->bEntranceStairs=false;A->RebuildTower();
    TestFalse(TEXT("Stair toggle removes both mesh and collision"),Trace({0,-2000,100},{0,-2000,-10}));
    A->bEntrance=false;A->WindowsPerSide=0;A->RebuildTower();
    TestEqual(TEXT("Zero count disables all windows"),A->GeneratedOpeningCount,0);
    const int32 Previous=A->GeneratedPieceCount,PreviousTriangles=Triangles();
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->MaxGeneratedCells=5;A->RebuildTower();
    TestEqual(TEXT("Budget failure preserves geometry count"),A->GeneratedPieceCount,Previous);
    TestEqual(TEXT("Budget failure preserves visible mesh"),Triangles(),PreviousTriangles);
    A->MaxGeneratedCells=50000;A->BaseWidth=600;A->BaseDepth=900;A->TopScale=.05f;
    A->SideCount=3;A->TierCount=32;A->WallThickness=180;A->WallBatter=.08f;A->RebuildTower();
    TestTrue(TEXT("Narrow stretched polygon at parameter limits is valid"),A->GenerationWarning.IsEmpty());
    A->TierCount=1;A->FoundationHeight=0;A->bEntrance=true;A->RebuildTower();
    TestEqual(TEXT("Single storey has only ground entrance"),A->GeneratedOpeningCount,1);
    A->DispatchBeginPlay();TestTrue(TEXT("Runtime rebuild generates geometry"),Triangles()>0);
    TestFalse(TEXT("Idle actor does not tick"),A->IsActorTickEnabled());
    AddInfo(FString::Printf(TEXT("Reference preset: %d pieces, %d triangles"),DefaultCount,DefaultTriangles));
    return true;
}
#endif
