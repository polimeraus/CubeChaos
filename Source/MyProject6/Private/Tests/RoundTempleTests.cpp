#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ChunkedDestructibleRoundTemple.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/Convex.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRoundTempleTest,"MyProject6.Destructible.RoundTemple.GeometryDamagePersistence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRoundTempleTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* A=World->SpawnActor<AChunkedDestructibleRoundTemple>();
    if (!TestNotNull(TEXT("Native actor spawns"),A)) return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    auto Triangles=[&]() {int32 N=0;for (auto& C:A->ChunkComponents) N+=C->GetDynamicMesh()->GetTriangleCount();return N;};
    auto Trace=[&](FVector S,FVector E) {return World->LineTraceTestByChannel(S,E,ECC_Visibility);};
    const int32 Count=A->GeneratedPieceCount,Tris=Triangles(),Steps=A->GeneratedStepPieces;
    TestTrue(TEXT("Reference fits a modest cell budget"),Count>2000 && Count<12000 && A->GenerationWarning.IsEmpty());
    TestTrue(TEXT("Both cone and partial stairs are damageable"),A->GeneratedRoofPieces>500 && Steps>100);
    TestTrue(TEXT("Chunk batching avoids a component per cell"),A->ChunkComponents.Num()<Count/50);
    for (const auto& C:A->ChunkComponents)
    {
        for (const auto& Hull:C->GetBodySetup()->AggGeom.ConvexElems)
            if (!TestTrue(TEXT("Every cell cooks a valid convex collider"),Hull.GetChaosConvexMesh() && Hull.GetChaosConvexMesh()->IsValidGeometry())) return false;
        for (int32 T:C->GetDynamicMesh()->GetMeshRef().TriangleIndicesItr())
            if (!TestTrue(TEXT("Arches and cone apex contain no degenerate faces"),C->GetDynamicMesh()->GetMeshRef().GetTriArea(T)>1.e-8)) return false;
    }
    for (const auto& P:A->Pieces)
    {
        FVector Center=FVector::ZeroVector;for (const auto& V:P.V) Center+=V;Center/=8;
        if (!TestTrue(TEXT("Convex cells contain their centroid"),P.Contains(Center,.001))) return false;
    }
    FHitResult Hit;
    TestFalse(TEXT("Entrance admits a standing player capsule"),World->SweepSingleByChannel(Hit,{0,-975,319},{0,-760,319},
        FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96)));
    const double Angle=-UE_DOUBLE_HALF_PI+UE_DOUBLE_TWO_PI/A->LowerArchCount;
    const FVector N(FMath::Cos(Angle),FMath::Sin(Angle),0);
    TestFalse(TEXT("Rounded arcade is a real hole"),Trace(N*960+FVector(0,0,400),N*800+FVector(0,0,400)));
    TestTrue(TEXT("Arch crown is solid above the hole"),Trace(N*960+FVector(0,0,530),N*800+FVector(0,0,530)));
    TestFalse(TEXT("Open gallery has no invisible wall"),Trace({0,-960,920},{0,-750,920}));
    for (int32 S=0;S<A->StepCount;++S)
    {
        const double R=A->BodyRadius+A->PlatformMargin+(S+.5)*A->StepDepth;
        const double H=A->PlatformHeight*(A->StepCount-S)/(A->StepCount+1.);
        if (TestTrue(TEXT("Every tread is physically walkable"),World->LineTraceSingleByChannel(Hit,{9,-R,H+8},{9,-R,H-8},ECC_Visibility)))
            TestTrue(TEXT("Tread collision matches step height"),FMath::Abs(Hit.ImpactPoint.Z-H)<.1);
    }
    TestFalse(TEXT("Staircase covers only the front sector"),Trace({0,1100,300},{0,1100,-10}));
    const FVector Start(115,-960,337),End(115,-800,337);
    if (!TestTrue(TEXT("Weapon hits front pier"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility))) return false;
    FIntPoint Cell;
    if (!TestTrue(TEXT("Pier hit resolves to a cell"),A->ResolveExistingCellFromHit(Hit,Cell))) return false;
    const int32 Index=Cell.X;
    A->DamageCellFromHit(Hit,5);
    FChunkedFloorCellInfo Info;A->GetCellInfo(Cell,Info);TestEqual(TEXT("Shared health decreases"),Info.Health,20.f);
    TestEqual(TEXT("Nonlethal shot keeps geometry"),Triangles(),Tris);
    A->DamageCellFromHit(Hit,100);A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestEqual(TEXT("Lethal shot removes one cell"),Triangles(),Tris-12);
    TestFalse(TEXT("Destroyed cell loses its collider"),Trace(Start,End));
    A->RestoreCell({Index,0});TestTrue(TEXT("Cell restore restores collision"),Trace(Start,End));
    for (FVector Center:{FVector(50,-600,1350),FVector(80,-1150,90)})
    {
        const int32 Before=Triangles();
        TestTrue(TEXT("Blast damages roof and steps"),A->DestroyCellsInRadius(Center,180,EChunkedFloorBlastShape::Sphere)>0);
        A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
        TestTrue(TEXT("Blast updates visible cells"),Triangles()<Before);
    }
    A->RebuildTemple();TestEqual(TEXT("Rebuild fully restores structure"),Triangles(),Tris);
    const auto& P=A->Pieces[Index];FVector Center=FVector::ZeroVector;for (const auto& V:P.V) Center+=V;Center/=8;
    const FTransform Transform(FRotator(0,37,0),FVector(200,-400,100),FVector(1.3,.7,1.1));
    A->SetActorTransform(Transform);
    TestTrue(TEXT("Rotated scaled instance resolves the original cell"),A->WorldLocationToCell(Transform.TransformPosition(Center),Cell) && Cell.X==Index);
    A->SetActorTransform(FTransform::Identity);A->bFrontSteps=false;A->RebuildTemple();
    TestEqual(TEXT("Disabling steps removes just the staircase"),A->GeneratedPieceCount,Count-Steps);
    TestEqual(TEXT("Step statistic clears"),A->GeneratedStepPieces,0);
    const int32 Before=Triangles();A->MaxGeneratedCells=5;
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->RebuildTemple();TestEqual(TEXT("Budget rejection preserves geometry"),Triangles(),Before);
    A->Destroy();
    UClass* BP=LoadClass<AChunkedDestructibleRoundTemple>(nullptr,TEXT("/Game/Structures/Destructible/RoundTemple/BP_ChunkedDestructibleRoundTemple.BP_ChunkedDestructibleRoundTemple_C"));
    if (!TestNotNull(TEXT("Saved Blueprint reloads"),BP)) return false;
    auto* Fresh=World->SpawnActor<AChunkedDestructibleRoundTemple>(BP);
    TestTrue(TEXT("New Blueprint instance inherits the reference geometry and materials"),Fresh->GeneratedPieceCount==Count && Fresh->StoneMaterial && Fresh->RoofMaterial && Fresh->StepMaterial);
    World->BeginPlay();
    TestEqual(TEXT("BeginPlay keeps the reference geometry"),Fresh->GeneratedPieceCount,Count);
    TestFalse(TEXT("Idle temple does not tick"),Fresh->IsActorTickEnabled());
    auto* Runtime=World->SpawnActor<AChunkedDestructibleRoundTemple>(BP,FVector(10000,0,0),FRotator::ZeroRotator);
    TestEqual(TEXT("Runtime spawn generates the same cells"),Runtime->GeneratedPieceCount,Count);
    AddInfo(FString::Printf(TEXT("Round temple: %d cells, %d triangles, %d stair cells"),Count,Tris,Steps));
    World->EndPlay(EEndPlayReason::Quit);
    return true;
}
#endif
