#if WITH_DEV_AUTOMATION_TESTS
#include "ChunkedDestructibleZigguratPyramit.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZigguratPyramitTest,"MyProject6.Destructible.ZigguratPyramit",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FZigguratPyramitTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* A=World->SpawnActor<AChunkedDestructibleZigguratPyramit>();
    if (!TestNotNull(TEXT("New native actor spawns"),A)) return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    const int32 Count=A->GeneratedPieceCount;
    TestEqual(TEXT("Reference has three occupied tiers"),A->TierCount,3);
    TestEqual(TEXT("Reference has four crown steps"),A->CrownStepCount,4);
    TestEqual(TEXT("Three/four base openings and 3-3 / 3-2 upper windows on paired facades"),A->GeneratedOpeningCount,36);
    TestFalse(TEXT("Reference has no railings"),A->bRailings);
    TestTrue(TEXT("Continuous stone has no joint gaps"),A->PieceGap==0);
    TestTrue(TEXT("Default geometry fits budget"),Count>100 && Count<18000);
    auto Triangles=[&]() { int32 N=0;for (auto& C:A->ChunkComponents) N+=C->GetDynamicMesh()->GetTriangleCount();return N; };
    TestEqual(TEXT("Every piece has six quadrilateral faces"),Triangles(),Count*12);
    int32 Sloping=0;
    for (const auto& P:A->Pieces)
    {
        FVector V[8];P.Vertices(V);
        if (!P.BottomMin.Equals(P.TopMin,.001)) ++Sloping;
        if (!TestTrue(TEXT("Fragments have positive bottom/top volume"),
            P.Z1>P.Z0 && (P.BottomMax-P.BottomMin).GetMin()>0 && (P.TopMax-P.TopMin).GetMin()>0)) return false;
        FDynamicMesh3 Mesh;Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
        Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
        A->AppendPiece(Mesh,P,P.Material);
        for (int32 T:Mesh.TriangleIndicesItr())
            if (!TestTrue(TEXT("No degenerate triangles at tapering corners"),Mesh.GetTriArea(T)>1.e-8)) return false;
    }
    TestTrue(TEXT("Walls and crown have genuine continuous slopes"),Sloping>100);

    // A ray through a real base aperture must reach the room without any blocking hull.
    FHitResult Hit;
    TestFalse(TEXT("Central base passage is open"),World->LineTraceSingleByChannel(Hit,
        FVector(0,-1300,300),FVector(0,-800,300),ECC_Visibility));
    int32 Target=INDEX_NONE;
    for (int32 I=0;I<A->Pieces.Num();++I)
    {
        const auto& P=A->Pieces[I];const FVector C=P.Bounds.GetCenter();
        if (P.Material==0 && C.Y<-900 && C.Z>250 && C.Z<500 && C.X>200 && C.X<350 &&
            P.Bounds.GetSize().X>30 && P.Z1-P.Z0>20) { Target=I;break; }
    }
    if (!TestTrue(TEXT("Found a sloping front pier fragment"),Target!=INDEX_NONE)) return false;
    const auto P=A->Pieces[Target];const FVector Center=P.Bounds.GetCenter();
    const FVector Start=Center-FVector(0,200,0),End=Center+FVector(0,150,0);
    TestTrue(TEXT("Shot collides with sloping pier"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    FIntPoint Cell;
    TestTrue(TEXT("Slope hit resolves to exact convex fragment"),A->ResolveExistingCellFromHit(Hit,Cell));
    TestEqual(TEXT("Correct fragment selected"),Cell.X,Target);
    const FVector Empty(P.Bounds.GetCenter().X,P.Bounds.Min.Y+.01,P.Z1-.01);
    TestTrue(TEXT("Air sample lies inside broad-phase box"),P.Bounds.IsInside(Empty));
    TestFalse(TEXT("Air outside sloped solid does not resolve as that piece"),P.Contains(Empty,.01));
    TestTrue(TEXT("Nonlethal shot applies health"),A->DamageCellFromHit(Hit,5));
    TestEqual(TEXT("Nonlethal shot retains mesh"),Triangles(),Count*12);
    TestTrue(TEXT("Lethal shot removes a piece"),A->DamageCellFromHit(Hit,100));
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestEqual(TEXT("Exactly twelve triangles disappear"),Triangles(),Count*12-12);
    TestFalse(TEXT("Convex collision disappears with fragment"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    A->RestoreCell(FIntPoint(Target,0));
    TestTrue(TEXT("Restored fragment collides"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));

    const FTransform Transform(FRotator(0,41,0),FVector(700,-250,100),FVector(1.4,.8,1.2));
    A->SetActorTransform(Transform);
    TestTrue(TEXT("Transformed piece center maps correctly"),A->WorldLocationToCell(Transform.TransformPosition(Center),Cell));
    TestEqual(TEXT("Transformed identity"),Cell.X,Target);
    const int32 Removed=A->DestroyCellsInRadius(Transform.TransformPosition(Center),230,EChunkedFloorBlastShape::Circle);
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestTrue(TEXT("Explosion affects several fragments"),Removed>1);
    TestEqual(TEXT("Explosion mesh matches removed fragments"),Triangles(),(Count-Removed)*12);
    A->RebuildZigguratPyramit();
    TestEqual(TEXT("Rebuild is deterministic and repairs damage"),Triangles(),Count*12);

    A->TierCount=5;A->bRailings=true;A->bEntranceStairs=true;A->CrownStepCount=6;
    A->RebuildZigguratPyramit();
    TestTrue(TEXT("Five-tier variant rebuilds"),A->GenerationWarning.IsEmpty());
    TestTrue(TEXT("Upper count arrays repeat for additional floors"),A->GeneratedOpeningCount==56);
    A->GroundFrontOpenings=0;A->GroundSideOpenings=0;A->FrontWindowCounts.Reset();A->SideWindowCounts.Reset();
    A->bPyramidCrown=false;A->bRailings=false;A->bWindowLintels=false;A->RebuildZigguratPyramit();
    TestEqual(TEXT("Zero counts close all openings"),A->GeneratedOpeningCount,0);
    for (const auto& Q:A->Pieces) if (!TestTrue(TEXT("Crown and lintels are disabled"),Q.Material!=3 && Q.Material!=5)) return false;
    const int32 PreviousCount=A->GeneratedPieceCount;
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->MaxGeneratedCells=5;A->RebuildZigguratPyramit();
    TestEqual(TEXT("Budget rejection preserves previous geometry"),A->GeneratedPieceCount,PreviousCount);
    A->MaxGeneratedCells=50000;A->BaseWidth=600;A->BaseDepth=600;A->TopTierScale=.2f;A->WallThickness=200;
    A->WallBatter=.16f;A->TierCount=3;A->RebuildZigguratPyramit();
    TestTrue(TEXT("Narrow steep tiers remain valid"),A->GenerationWarning.IsEmpty());
    AddInfo(FString::Printf(TEXT("Reference preset: %d pieces, %d triangles"),Count,Count*12));
    return true;
}
#endif
