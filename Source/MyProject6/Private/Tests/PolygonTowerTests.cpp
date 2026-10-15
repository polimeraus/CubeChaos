#if WITH_DEV_AUTOMATION_TESTS
#include "ChunkedDestructiblePolygonTower.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPolygonTowerTest,"MyProject6.Destructible.PolygonTower",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPolygonTowerTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* A=World->SpawnActor<AChunkedDestructiblePolygonTower>();
    if (!TestNotNull(TEXT("Polygon tower spawns"),A)) return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    TestTrue(TEXT("Default twelve-tier octagon fits budget"),A->GenerationWarning.IsEmpty() && A->TierCount==12 && A->SideCount==8);
    TestEqual(TEXT("Every face has a real aperture"),A->GeneratedOpeningCount,96);
    auto Triangles=[&]() {int32 N=0;for(auto& C:A->ChunkComponents)N+=C->GetDynamicMesh()->GetTriangleCount();return N;};
    const int32 Count=A->GeneratedPieceCount,Original=Triangles();
    for (const auto& C:A->ChunkComponents) for (int32 T:C->GetDynamicMesh()->GetMeshRef().TriangleIndicesItr())
        if (!TestTrue(TEXT("No degenerate wedge faces"),C->GetDynamicMesh()->GetMeshRef().GetTriArea(T)>1.e-8)) return false;
    FHitResult Hit;
    TestFalse(TEXT("Ground door is a collision-free aperture"),World->LineTraceSingleByChannel(Hit,FVector(0,-1800,200),FVector(0,-1500,200),ECC_Visibility));
    const double R=A->BaseRadius*(1-(1-A->TopScale)/(A->TierCount-1));
    const double Z=A->FoundationHeight+A->TierHeight+A->CorniceHeight+A->FloorThickness+A->WindowSillHeight+70;
    TestFalse(TEXT("Upper window has no back panel"),World->LineTraceSingleByChannel(Hit,FVector(0,-R, Z),FVector(0,-R*.8,Z),ECC_Visibility));
    int32 Target=INDEX_NONE;
    for(int32 I=0;I<A->Pieces.Num();++I)
    {const auto& P=A->Pieces[I];auto C=P.Bounds.GetCenter();if(P.Material==0 && C.Y<-1500 && C.X>200 && C.X<400 && C.Z>150 && C.Z<250){Target=I;break;}}
    if (!TestTrue(TEXT("Found a front pier"),Target!=INDEX_NONE))return false;
    FVector Center=FVector::ZeroVector;for(const auto& V:A->Pieces[Target].V)Center+=V;Center/=8;
    FIntPoint Cell;TestTrue(TEXT("Convex piece center resolves"),A->WorldLocationToCell(Center,Cell));
    TestEqual(TEXT("Resolved piece identity"),Cell.X,Target);
    const FVector Start=Center-FVector(0,100,0),End=Center+FVector(0,100,0);
    if(TestTrue(TEXT("Pier collision matches geometry"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility)))
    {
        TestTrue(TEXT("Nonlethal damage retains fragment"),A->DamageCellFromHit(Hit,5));
        TestEqual(TEXT("Nonlethal geometry"),Triangles(),Original);
        TestTrue(TEXT("Lethal shot destroys fragment"),A->DamageCellFromHit(Hit,100));
        A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
        TestEqual(TEXT("One complete wall fragment disappears"),Triangles(),Original-12);
        TestFalse(TEXT("Collision disappears with fragment"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    }
    A->RebuildTower();TestEqual(TEXT("Rebuild repairs geometry"),Triangles(),Original);
    A->SetActorTransform(FTransform(FRotator(0,37,0),FVector(300,200,100),FVector(1.3,.8,1.5)));
    TestTrue(TEXT("Transformed hit mapping"),A->WorldLocationToCell(A->GetActorTransform().TransformPosition(Center),Cell)&&Cell.X==Target);
    TestTrue(TEXT("Explosion destroys multiple pieces"),A->DestroyCellsInRadius(A->GetActorTransform().TransformPosition(Center),200,EChunkedFloorBlastShape::Circle)>1);
    for(int32 S:{3,5,12,16})
    {
        A->SideCount=S;A->TierCount=3;A->BaseRadius=900;A->PieceSize=160;A->RebuildTower();
        TestTrue(TEXT("Polygon side-count variant is valid"),A->GenerationWarning.IsEmpty());
        for(const auto& P:A->Pieces)
        {FVector C=FVector::ZeroVector;for(auto V:P.V)C+=V;C/=8;if(!TestTrue(TEXT("Wedge half-planes enclose their center"),P.Contains(C,.01)))return false;}
    }
    A->TierCount=1;A->FoundationHeight=0;A->EntranceCount=0;A->WindowsPerSide=0;A->RebuildTower();
    TestTrue(TEXT("Single tier without plinth remains valid"),A->GenerationWarning.IsEmpty());
    TestEqual(TEXT("Openings can be disabled"),A->GeneratedOpeningCount,0);
    const int32 Before=Triangles();A->MaxGeneratedCells=5;
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->RebuildTower();TestEqual(TEXT("Budget rejection retains geometry"),Triangles(),Before);
    AddInfo(FString::Printf(TEXT("Default polygon: %d pieces, %d triangles"),Count,Original));
    return true;
}
#endif
