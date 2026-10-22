#if WITH_DEV_AUTOMATION_TESTS
#include "ChunkedDestructibleSpireTemple.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/Convex.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpireTempleTest,"MyProject6.Destructible.SpireTemple",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSpireTempleTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* A=World->SpawnActor<AChunkedDestructibleSpireTemple>();
    if (!TestNotNull(TEXT("Native spire temple spawns"),A)) return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    auto Triangles=[&]() {int32 N=0;for (auto& C:A->ChunkComponents) N+=C->GetDynamicMesh()->GetTriangleCount();return N;};
    auto Trace=[&](FVector S,FVector E) {return World->LineTraceTestByChannel(S,E,ECC_Visibility);};
    const int32 Count=A->GeneratedPieceCount,Tris=Triangles(),WallCount=A->GeneratedEnclosurePieces;
    TestTrue(TEXT("Reference preset fits fragment budget"),Count>1000 && Count<15000 && A->GenerationWarning.IsEmpty());
    TestEqual(TEXT("Thirty courses of spire blocks plus collar"),A->GeneratedSpirePieces,30*48+64);
    TestTrue(TEXT("Reference dimensions include drums and tall spire"),FMath::IsNearlyEqual(A->GeneratedHeight,3180.f));
    TestTrue(TEXT("Geometry uses chunks instead of one component per stone"),A->ChunkComponents.Num()<Count/50);
    for (const auto& C:A->ChunkComponents)
        for (const auto& Hull:C->GetBodySetup()->AggGeom.ConvexElems)
            if (!TestTrue(TEXT("Every fragment has valid cooked collision"),Hull.GetChaosConvexMesh() && Hull.GetChaosConvexMesh()->IsValidGeometry())) return false;
    for (const auto& P:A->Pieces)
    {
        FVector Center=FVector::ZeroVector;for (const auto& V:P.V) Center+=V;Center/=8;
        if (!TestTrue(TEXT("Convex containment accepts stone centroid"),P.Contains(Center,.001))) return false;
        UE::Geometry::FDynamicMesh3 M;M.EnableVertexNormals(FVector3f::UnitZ());M.EnableAttributes();
        M.Attributes()->SetNumUVLayers(1);M.Attributes()->EnableMaterialID();A->AppendPiece(M,P,P.Material);
        for (int32 T:M.TriangleIndicesItr())
            if (!TestTrue(TEXT("Spire apex and all other fragments have nondegenerate triangles"),M.GetTriArea(T)>1.e-8)) return false;
    }
    TestFalse(TEXT("Enclosure gate is a true opening"),Trace({0,-1600,120},{0,-1380,120}));
    FHitResult CapsuleHit;
    TestFalse(TEXT("Standing player capsule fits gate"),World->SweepSingleByChannel(CapsuleHit,{0,-1650,100},{0,-1320,100},
        FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96)));
    TestFalse(TEXT("Lower drum doorway is open"),Trace({0,-850,150},{0,-650,150}));
    TestTrue(TEXT("Gate lintel remains solid"),Trace({20,-1650,620},{20,-1380,620}));
    const double Angle=-UE_DOUBLE_HALF_PI-UE_DOUBLE_PI+UE_DOUBLE_PI/A->DrumWindowsAround;
    const FVector N(FMath::Cos(Angle),FMath::Sin(Angle),0);
    const double WindowZ=A->PlinthHeight+A->LowerDrumHeight+A->CorniceHeight+A->UpperDrumHeight/4;
    TestFalse(TEXT("Drum window has no hidden collision"),Trace(N*750+FVector(0,0,WindowZ),N*550+FVector(0,0,WindowZ)));
    TestTrue(TEXT("Drum sill is solid below window"),Trace(N*750+FVector(0,0,WindowZ-90),N*550+FVector(0,0,WindowZ-90)));
    FHitResult Hit;
    // Avoid the perpendicular corner wall: the ray must cross just one facade block.
    const FVector Start(-1545,-1650,350),End(-1545,-1380,350);
    if (!TestTrue(TEXT("Weapon trace hits enclosure pier"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility))) return false;
    FIntPoint Cell;
    if (!TestTrue(TEXT("Collision maps to a damageable stone"),A->ResolveExistingCellFromHit(Hit,Cell))) return false;
    const int32 Index=Cell.X;
    TestTrue(TEXT("Nonlethal weapon damage accepted"),A->DamageCellFromHit(Hit,5));
    FChunkedFloorCellInfo Info;A->GetCellInfo(Cell,Info);
    TestEqual(TEXT("Shared piece health decreases"),Info.Health,20.f);
    TestEqual(TEXT("Nonlethal shot retains geometry"),Triangles(),Tris);
    TestTrue(TEXT("Lethal weapon damage accepted"),A->DamageCellFromHit(Hit,100));
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestEqual(TEXT("Only one block is removed"),Triangles(),Tris-12);
    TestFalse(TEXT("Destroyed collision disappears with stone"),Trace(Start,End));
    A->RestoreCell({Index,0});TestTrue(TEXT("Restore brings collision back"),Trace(Start,End));
    const auto& P=A->Pieces[Index];FVector Center=FVector::ZeroVector;for (const auto& V:P.V) Center+=V;Center/=8;
    const FTransform Transform(FRotator(0,37,0),FVector(200,-400,100),FVector(1.3,.7,1.1));
    A->SetActorTransform(Transform);
    TestTrue(TEXT("Scaled rotated instance resolves cells"),A->WorldLocationToCell(Transform.TransformPosition(Center),Cell));
    TestEqual(TEXT("Transformed hit resolves original block"),Cell.X,Index);
    TestTrue(TEXT("Blast destroys several pieces"),A->DestroyCellsInRadius(Transform.TransformPosition(Center),260,EChunkedFloorBlastShape::Circle)>1);
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();TestTrue(TEXT("Blast removes rendered geometry"),Triangles()<Tris);
    A->SetActorTransform(FTransform::Identity);A->RebuildTemple();TestEqual(TEXT("Rebuild restores exact preset"),Triangles(),Tris);
    A->bRectangularEnclosure=false;A->RebuildTemple();
    TestEqual(TEXT("Optional enclosure removes only its fragments"),A->GeneratedPieceCount,Count-WallCount);
    TestEqual(TEXT("Disabled enclosure reports zero pieces"),A->GeneratedEnclosurePieces,0);
    TestFalse(TEXT("Enclosure off removes collision"),Trace(Start,End));
    const int32 NoWall=A->GeneratedPieceCount,SpireCount=A->GeneratedSpirePieces;
    A->bSpire=false;A->RebuildTemple();TestEqual(TEXT("Spire is independently optional"),A->GeneratedPieceCount,NoWall-SpireCount);
    A->bSpire=true;A->SpireTipRatio=.3f;A->SpireCourses=12;A->SpireSides=24;
    A->bCourtyardFloor=true;A->bDrumWindows=false;A->RebuildTemple();
    TestTrue(TEXT("Truncated cone and courtyard floor variant builds"),A->GenerationWarning.IsEmpty());
    TestEqual(TEXT("Closed window variant only retains drum doorway"),A->GeneratedOpeningCount,1);
    TestTrue(TEXT("Courtyard floor is real collision"),Trace({1000,37,30},{1000,37,-50}));
    const int32 Previous=A->GeneratedPieceCount,PreviousTris=Triangles();
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->MaxGeneratedCells=5;A->RebuildTemple();
    TestEqual(TEXT("Budget failure retains previous geometry"),Triangles(),PreviousTris);
    TestEqual(TEXT("Budget failure retains previous piece count"),A->GeneratedPieceCount,Previous);
    A->MaxGeneratedCells=50000;A->bRectangularEnclosure=true;A->bEnclosureOpenings=false;A->bRearGate=true;A->RebuildTemple();
    TestFalse(TEXT("Optional rear gate opens rear wall"),Trace({0,1650,100},{0,1380,100}));
    A->DispatchBeginPlay();TestTrue(TEXT("Runtime construction works"),Triangles()>0);
    TestFalse(TEXT("Idle structure does not tick"),A->IsActorTickEnabled());
    AddInfo(FString::Printf(TEXT("Reference: %d stones, %d triangles, %d enclosure stones"),Count,Tris,WallCount));
    return true;
}
#endif
