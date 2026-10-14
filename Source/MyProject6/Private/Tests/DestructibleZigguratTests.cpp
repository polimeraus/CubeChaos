#if WITH_DEV_AUTOMATION_TESTS
#include "ChunkedDestructibleZiggurat.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleZigguratTest,"MyProject6.Destructible.Ziggurat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleZigguratTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Actor=World->SpawnActor<AChunkedDestructibleZiggurat>();
    if (!TestNotNull(TEXT("Runtime actor spawns"),Actor)) return false;
    Actor->FlashDuration=0; Actor->bPlayDestroyTickSound=false; Actor->bUseDestructionRipple=false;
    TestTrue(TEXT("Default has real windows"),Actor->GeneratedWindowCount>30);
    TestTrue(TEXT("Default is below piece budget"),Actor->GeneratedPieceCount>100 && Actor->GeneratedPieceCount<12000);
    const int32 OriginalCount=Actor->GeneratedPieceCount;
    TSet<int32> UsedMaterials;
    for (const auto& Piece : Actor->Pieces)
    {
        UsedMaterials.Add(Piece.Material);
        if (!TestTrue(TEXT("Every fragment is a finite positive box"), !Piece.Bounds.Min.ContainsNaN() &&
            !Piece.Bounds.Max.ContainsNaN() && Piece.Bounds.GetSize().GetMin()>0)) return false;
    }
    TestEqual(TEXT("Six architectural material slots are used"),UsedMaterials.Num(),6);
    auto Triangles = [&]()
    {
        int32 Count=0;
        for (const auto& Component : Actor->ChunkComponents) Count+=Component->GetDynamicMesh()->GetTriangleCount();
        return Count;
    };
    TestEqual(TEXT("All fragments are six-faced boxes"),Triangles(),OriginalCount*12);

    // A real physics ray and the same hit damage API used by the projectile weapons.
    int32 Target=INDEX_NONE;
    for (int32 I=0; I<Actor->Pieces.Num(); ++I)
    {
        const auto& P=Actor->Pieces[I]; const FVector C=P.Bounds.GetCenter();
        if (P.Material==0 && C.Y < -Actor->BaseDepth*.5+Actor->WallThickness &&
            C.Z>Actor->FoundationHeight+50 && C.Z<Actor->FoundationHeight+90 &&
            FMath::Abs(C.X)>Actor->EntranceWidth && FMath::Abs(C.X)<Actor->BaseWidth*.4)
        { Target=I; break; }
    }
    if (!TestTrue(TEXT("Found an isolated front wall tile"),Target!=INDEX_NONE)) return false;
    const FVector Center=Actor->Pieces[Target].Bounds.GetCenter();
    const FVector Start=Center-FVector(0,200,0), End=Center+FVector(0,100,0);
    FHitResult Hit;
    TestTrue(TEXT("Projectile trace hits intact wall"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    TestTrue(TEXT("Trace belongs to ziggurat"),Hit.GetActor()==Actor);
    FIntPoint Cell;
    TestTrue(TEXT("Hit resolves its exact tile"),Actor->ResolveExistingCellFromHit(Hit,Cell));
    TestEqual(TEXT("Resolved tile identity"),Cell.X,Target);
    const int32 Chunk=Target/FMath::Clamp(Actor->ChunkSideLength,4,128);
    TArray<UDynamicMesh*> BeforeMeshes;
    for (auto& C : Actor->ChunkComponents) BeforeMeshes.Add(C->GetDynamicMesh());
    TestTrue(TEXT("Sublethal hit applies health damage"),Actor->DamageCellFromHit(Hit,5));
    TestTrue(TEXT("Sublethal damage retains geometry"),Actor->Cells[Target].bExists);
    TestTrue(TEXT("Lethal projectile damage is accepted"),Actor->DamageCellFromHit(Hit,100));
    Actor->ProcessQueuedDestructions(1.e10); Actor->FlushDirtyChunks();
    TestFalse(TEXT("Hit fragment is removed"),Actor->Cells[Target].bExists);
    TestEqual(TEXT("Only one box disappears from rendering"),Triangles(),OriginalCount*12-12);
    TestFalse(TEXT("No stale collision remains in the hole"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    for (int32 I=0; I<BeforeMeshes.Num(); ++I)
        TestEqual(TEXT("Only damaged chunk is rebuilt"),BeforeMeshes[I]!=Actor->ChunkComponents[I]->GetDynamicMesh(),I==Chunk);
    TestTrue(TEXT("Restore cell works"),Actor->RestoreCell(FIntPoint(Target,0)));
    TestTrue(TEXT("Restored wall collides again"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    // The entrance must remain a genuine passage through front wall and collision.
    const double DoorZ=Actor->FoundationHeight+Actor->EntranceHeight*.5;
    TestFalse(TEXT("Door is unobstructed"),World->LineTraceSingleByChannel(Hit,
        FVector(0,-Actor->BaseDepth*.5-100,DoorZ),FVector(0,-Actor->BaseDepth*.5+100,DoorZ),ECC_Visibility));
    TestFalse(TEXT("Window is a real collision opening"),World->LineTraceSingleByChannel(Hit,
        FVector(-662.5,-Actor->BaseDepth*.5-100,327.5),FVector(-662.5,-Actor->BaseDepth*.5+100,327.5),ECC_Visibility));

    const FTransform Transform(FRotator(0,37,0),FVector(420,-760,150),FVector(1.6,.7,1.2));
    Actor->SetActorTransform(Transform);
    TestTrue(TEXT("Rotated and nonuniformly scaled point resolves"),Actor->WorldLocationToCell(Transform.TransformPosition(Center),Cell));
    TestEqual(TEXT("Transform preserves identity"),Cell.X,Target);
    for (auto Shape : {EChunkedFloorBlastShape::Circle,EChunkedFloorBlastShape::Square,EChunkedFloorBlastShape::Diamond})
    {
        TSet<int32> Actual,Expected;
        Actor->SelectCellsInRadius(Transform.TransformPosition(Center),200,Shape,
            [&](int32 I,float Distance) { Actual.Add(I); });
        for (int32 I=0; I<Actor->Pieces.Num(); ++I)
        {
            const FVector D=((Actor->Pieces[I].Bounds.GetCenter()-Center)*Transform.GetScale3D()).GetAbs();
            const double R=Shape==EChunkedFloorBlastShape::Circle ? D.Length() :
                (Shape==EChunkedFloorBlastShape::Square ? D.GetMax() : D.X+D.Y+D.Z);
            if (R<=200) Expected.Add(I);
        }
        TestEqual(TEXT("3D explosion selection matches physical distance"),Actual.Difference(Expected).Num()+Expected.Difference(Actual).Num(),0);
    }
    Actor->DestroyCellsInRadius(Transform.TransformPosition(Center),180,EChunkedFloorBlastShape::Circle);
    Actor->ProcessQueuedDestructions(1.e10); Actor->FlushDirtyChunks();
    TestTrue(TEXT("Explosion removes multiple pieces"),Triangles()<OriginalCount*12-12);
    Actor->RebuildZiggurat();
    TestEqual(TEXT("Rebuild restores deterministic geometry"),Triangles(),OriginalCount*12);

    // Turning details off must remove their geometry, not merely hide a component.
    Actor->FrontWindowCount=0; Actor->SideWindowCount=0; Actor->bRailings=false;
    Actor->bCrown=false; Actor->bCornerPillars=false; Actor->bEntrance=false;
    Actor->bUpperTierDoors=false; Actor->bBalconies=false; Actor->TierCount=2;
    Actor->RebuildZiggurat();
    TestEqual(TEXT("Zero window count removes all openings"),Actor->GeneratedWindowCount,0);
    for (const auto& Piece : Actor->Pieces)
        if (!TestTrue(TEXT("Disabled decorative categories produce no fragments"),Piece.Material!=1 && Piece.Material!=3 && Piece.Material!=5)) return false;
    // Exhaustion keeps the prior mesh AND its hit mapping usable.
    const int32 BeforeBudget=Actor->GeneratedPieceCount;
    const FVector BeforePoint=Actor->GetCellLocalCenter(FIntPoint(0,0));
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    Actor->MaxGeneratedCells=10; Actor->RebuildZiggurat();
    TestEqual(TEXT("Rejected generation retains previous layout"),Actor->GeneratedPieceCount,BeforeBudget);
    TestTrue(TEXT("Budget failure is explained in Details"),!Actor->GenerationWarning.IsEmpty());
    TestTrue(TEXT("Budget failure retains hit mapping"),Actor->WorldLocationToCell(Transform.TransformPosition(BeforePoint),Cell));
    Actor->MaxGeneratedCells=50000;
    Actor->BaseWidth=400; Actor->BaseDepth=400; Actor->TopScale=.15f; Actor->WallThickness=150;
    Actor->TierCount=12; Actor->RebuildZiggurat();
    TestTrue(TEXT("Extreme small top still generates valid chambers"),Actor->GenerationWarning.IsEmpty());
    TestTrue(TEXT("Top scale clamps to avoid inverted walls"),Actor->TopScale>=1);
    AddInfo(FString::Printf(TEXT("Default: %d destructible boxes, %d triangles"),OriginalCount,OriginalCount*12));
    return true;
}
#endif
