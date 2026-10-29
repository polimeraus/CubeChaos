#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ChunkedDestructibleTorus.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleTorusTest,"MyProject6.Destructible.Torus.GeometryDamageAndSavedSpawn",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDestructibleTorusTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* A=World->SpawnActor<AChunkedDestructibleTorus>();
    if (!TestNotNull(TEXT("Torus spawns"),A))return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    auto Triangles=[&]() {int32 N=0;for(auto& C:A->ChunkComponents)N+=C->GetDynamicMesh()->GetTriangleCount();return N;};
    auto ValidSolids=[&]()
    {
        for(int32 I=0;I<A->Pieces.Num();++I)
            if(!TestTrue(TEXT("Convex fragments enclose their centroids"),A->Pieces[I].Contains(A->GetCellLocalCenter(FIntPoint(I,0)),.01)))return false;
        for(const auto& C:A->ChunkComponents)for(int32 T:C->GetDynamicMesh()->GetMeshRef().TriangleIndicesItr())
            if(!TestTrue(TEXT("No degenerate faces"),C->GetDynamicMesh()->GetMeshRef().GetTriArea(T)>1.e-8))return false;
        return true;
    };
    const int32 Original=Triangles(),Count=A->GeneratedPieceCount;
    TestTrue(TEXT("Reference torus fits budget"),A->GenerationWarning.IsEmpty() && Count>1000);
    TestEqual(TEXT("Every fragment is a closed hexahedron"),Original,Count*12);
    TestTrue(TEXT("Both panel families exist"),A->GeneratedMeridianPieces>0 && A->GeneratedParallelPieces>0);
    if(!ValidSolids())return false;
    FHitResult Hit;
    TestFalse(TEXT("Front entrance passes through to the center"),World->SweepSingleByChannel(Hit,
        FVector(0,-3500,750),FVector(0,0,750),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96)));
    for(int32 I=0;I<12;++I)
    {
        const double Angle=UE_TWO_PI*I/12,Radius=A->CenterOpeningWidth*.5-50;
        const FVector XY(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),0);
        TestFalse(TEXT("Center remains vertically open"),World->SweepSingleByChannel(Hit,XY+FVector(0,0,110),
            XY+FVector(0,0,A->GeneratedHeight+200),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96)));
    }
    for(bool Shelf:{false,true})
    {
        int32 Target=INDEX_NONE;FVector Start,End;
        for(int32 I=0;I<A->Pieces.Num();++I)
        {
            const auto& P=A->Pieces[I];const FVector C=A->GetCellLocalCenter(FIntPoint(I,0));
            if(P.bShelf!=Shelf || C.Y<500 || C.Z<400 || C.Z>1100)continue;
            const FVector Ray=Shelf?FVector(0,0,40):FVector(-C.Y,C.X,0).GetSafeNormal()*40;
            FIntPoint Cell;
            if(World->LineTraceSingleByChannel(Hit,C-Ray,C+Ray,ECC_Visibility) && A->ResolveExistingCellFromHit(Hit,Cell) && Cell.X==I)
            {Target=I;Start=C-Ray;End=C+Ray;break;}
        }
        if(!TestTrue(TEXT("Found an isolated physical hit on this panel family"),Target!=INDEX_NONE))return false;
        const int32 Before=Triangles();
        TestTrue(TEXT("Nonlethal shot accepted"),A->DamageCellFromHit(Hit,5));
        TestEqual(TEXT("Nonlethal geometry unchanged"),Triangles(),Before);
        TestTrue(TEXT("Lethal shot accepted"),A->DamageCellFromHit(Hit,100));
        A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
        TestEqual(TEXT("One fragment is removed"),Triangles(),Before-12);
        TestFalse(TEXT("Its collider is removed too"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    }
    A->RebuildTorus();TestEqual(TEXT("Rebuild repairs damage"),Triangles(),Original);
    const FVector Center=A->GetCellLocalCenter(FIntPoint(40,0));FIntPoint Cell;
    A->SetActorTransform(FTransform(FRotator(0,37,0),FVector(300,200,100),FVector(1.2,.8,1.5)));
    TestTrue(TEXT("Transformed hit identity"),A->WorldLocationToCell(A->GetActorTransform().TransformPosition(Center),Cell) && Cell.X==40);
    TestTrue(TEXT("Blast removes multiple fragments"),A->DestroyCellsInRadius(A->GetActorTransform().TransformPosition(Center),350,EChunkedFloorBlastShape::Circle)>1);
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestTrue(TEXT("Blast updates geometry"),Triangles()<Original-12);
    A->SetActorTransform(FTransform::Identity);A->OpeningAngle=0;A->RebuildTorus();
    TestTrue(TEXT("Zero angle blocks the front passage"),World->SweepSingleByChannel(Hit,
        FVector(0,-3500,750),FVector(0,0,750),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96)));
    // Offset the zero-width ray from the shared angular seam between convex fragments.
    TestTrue(TEXT("Closed front shelf blocks shots"),World->LineTraceSingleByChannel(Hit,FVector(17,-3500,750),FVector(17,0,750),ECC_Visibility));
    TestFalse(TEXT("Closed torus still has a center hole"),World->LineTraceSingleByChannel(Hit,FVector(0,0,100),FVector(0,0,2000),ECC_Visibility));
    if(!ValidSolids())return false;
    A->RingRadius=1100;A->TubeRadius=400;A->MeridianCount=8;A->ParallelCount=3;A->CurveSegments=24;A->RingSegments=64;
    A->OpeningAngle=120;A->OpeningDirection=30;A->bRoundedEnds=false;A->MeridianThickness=8;A->ParallelThickness=24;A->RebuildTorus();
    TestTrue(TEXT("Flat-ended custom variant fits budget"),A->GenerationWarning.IsEmpty());
    TestEqual(TEXT("Center hole follows ring and tube radii"),A->CenterOpeningWidth,1400.f);
    for(const auto& P:A->Pieces)
        if(!TestTrue(TEXT("Independent panel thicknesses reach the geometry"),P.bShelf?
            FMath::IsNearlyEqual(P.Bounds.GetSize().Z,24.,.001):FMath::IsNearlyEqual(FVector::Distance(P.V[0],P.V[3]),8.,.001)))return false;
    const FVector Entrance=FVector(FMath::Cos(UE_PI/6),FMath::Sin(UE_PI/6),0)*2000;
    TestFalse(TEXT("Opening direction rotates the actual passage"),World->LineTraceSingleByChannel(Hit,Entrance+FVector(0,0,400),FVector(0,0,400),ECC_Visibility));
    if(!ValidSolids())return false;
    A->ParallelCount=0;A->RebuildTorus();TestEqual(TEXT("Shelves can be disabled"),A->GeneratedParallelPieces,0);
    A->bRoundedEnds=true;A->OpeningAngle=1;A->RebuildTorus();
    TestTrue(TEXT("Rounded ends are prevented from overlapping"),A->OpeningAngle>2*FMath::RadiansToDegrees(FMath::Asin(A->TubeRadius/A->RingRadius)));
    const int32 Before=Triangles();A->MaxGeneratedCells=5;
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->RebuildTorus();TestEqual(TEXT("Budget rejection preserves existing geometry"),Triangles(),Before);
    A->Destroy();
    UClass* Blueprint=LoadClass<AChunkedDestructibleTorus>(nullptr,TEXT("/Game/Structures/Destructible/Torus/BP_ChunkedDestructibleTorus.BP_ChunkedDestructibleTorus_C"));
    if(!TestNotNull(TEXT("Saved Blueprint loads in a fresh process"),Blueprint))return false;
    auto* Fresh=World->SpawnActor<AChunkedDestructibleTorus>(Blueprint);
    TestTrue(TEXT("Blueprint spawn inherits geometry and metal materials"),Fresh->GeneratedPieceCount==Count && Fresh->PanelMaterial && Fresh->EdgeMaterial && Fresh->bRoundedEnds && Fresh->OpeningAngle==90);
    World->BeginPlay();
    TestEqual(TEXT("BeginPlay regenerates the same solid structure"),Fresh->GeneratedPieceCount,Count);
    TestFalse(TEXT("Idle actor does not tick"),Fresh->IsActorTickEnabled());
    AddInfo(FString::Printf(TEXT("Default torus: %d pieces, %d triangles"),Count,Original));
    World->EndPlay(EEndPlayReason::Quit);
    return true;
}
#endif
