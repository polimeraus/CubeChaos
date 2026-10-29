#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ChunkedDestructibleGlobe.h"
#include "LaunchPad.h"
#include "Components/ChildActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleGlobeTest,"MyProject6.Destructible.Globe.GeometryAndDamage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDestructibleGlobeTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* A=World->SpawnActor<AChunkedDestructibleGlobe>();
    if (!TestNotNull(TEXT("Globe spawns"),A)) return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    auto Triangles=[&]() {int32 N=0;for(auto& C:A->ChunkComponents)N+=C->GetDynamicMesh()->GetTriangleCount();return N;};
    const int32 Original=Triangles(),Count=A->GeneratedPieceCount;
    TestTrue(TEXT("Default globe fits budget"),A->GenerationWarning.IsEmpty() && Count>1000);
    TestEqual(TEXT("Every fragment is a closed solid"),Original,12*Count);
    TestEqual(TEXT("Both piece groups accounted for"),Count,A->GeneratedMeridianPieces+A->GeneratedParallelPieces);
    for(const auto& C:A->ChunkComponents) for(int32 T:C->GetDynamicMesh()->GetMeshRef().TriangleIndicesItr())
        if (!TestTrue(TEXT("No degenerate triangles"),C->GetDynamicMesh()->GetMeshRef().GetTriArea(T)>1.e-8))return false;
    FHitResult Hit;FCollisionQueryParams Q;Q.AddIgnoredActor(A->GroundLaunchPad->GetChildActor());
    for(int32 I=0;I<A->MeridianCount;++I)
    {
        const double Angle=UE_TWO_PI*(I+.5)/A->MeridianCount;
        const double Offset=A->ShaftWidth*.5-40;
        const FVector XY(Offset*FMath::Cos(Angle),Offset*FMath::Sin(Angle),0);
        TestFalse(TEXT("Off-center player capsules clear the bore at every angle"),World->SweepSingleByChannel(Hit,
            XY+FVector(0,0,110),XY+FVector(0,0,A->GeneratedHeight+200),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96),Q));
    }
    // Exercise real collision hits on each orientation, then confirm only that collider is removed.
    for(bool Shelf:{false,true})
    {
        int32 Target=INDEX_NONE;
        for(int32 I=0;I<A->Pieces.Num();++I)
        {
            const FVector C=A->GetCellLocalCenter(FIntPoint(I,0));
            if(A->Pieces[I].bShelf==Shelf && C.X>700 && C.Z>900 && C.Z<1500 && (Shelf?C.Y>150:FMath::Abs(C.Y)<1))
            {Target=I;break;}
        }
        if(!TestTrue(TEXT("Found test fragment"),Target!=INDEX_NONE))return false;
        const FVector Center=A->GetCellLocalCenter(FIntPoint(Target,0));
        const FVector Ray=Shelf?FVector(0,0,50):FVector(0,50,0);
        FIntPoint Cell;
        TestTrue(TEXT("Fragment interior resolves"),A->WorldLocationToCell(Center,Cell)&&Cell.X==Target);
        const int32 Before=Triangles();
        if(TestTrue(TEXT("Piece collision matches geometry"),World->LineTraceSingleByChannel(Hit,Center-Ray,Center+Ray,ECC_Visibility)))
        {
            TestTrue(TEXT("Nonlethal hit accepted"),A->DamageCellFromHit(Hit,5));
            TestEqual(TEXT("Nonlethal hit keeps mesh"),Triangles(),Before);
            TestTrue(TEXT("Lethal hit accepted"),A->DamageCellFromHit(Hit,100));
            A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
            TestEqual(TEXT("Exactly one piece disappears"),Triangles(),Before-12);
            TestFalse(TEXT("Destroyed piece collider disappears"),World->LineTraceSingleByChannel(Hit,Center-Ray,Center+Ray,ECC_Visibility));
        }
    }
    A->RebuildGlobe();TestEqual(TEXT("Rebuild restores fragments"),Triangles(),Original);
    A->SetActorTransform(FTransform(FRotator(0,37,0),FVector(300,200,100),FVector(1.3,.8,1.5)));
    const FVector Center=A->GetCellLocalCenter(FIntPoint(20,0));FIntPoint Cell;
    TestTrue(TEXT("Transformed cell mapping"),A->WorldLocationToCell(A->GetActorTransform().TransformPosition(Center),Cell)&&Cell.X==20);
    TestTrue(TEXT("Explosion removes several pieces"),A->DestroyCellsInRadius(A->GetActorTransform().TransformPosition(Center),350,EChunkedFloorBlastShape::Circle)>1);
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestTrue(TEXT("Blast rebuilds visible chunks"),Triangles()<Original-12);
    A->SetActorTransform(FTransform::Identity);
    // Separate thickness controls must change the actual solids and leave the requested bore clear.
    A->MeridianThickness=40;A->ParallelThickness=10;A->ShaftWidth=600;A->RebuildGlobe();
    for (const auto& P:A->Pieces)
    {
        if (P.bShelf)
        {
            if (!TestTrue(TEXT("Parallel thickness is independent"),FMath::IsNearlyEqual(P.Bounds.GetSize().Z,10.,.001)))return false;
        }
        else if (!TestTrue(TEXT("Meridian thickness is independent"),FMath::IsNearlyEqual(FVector::Distance(P.V[0],P.V[3]),40.,.001)))return false;
    }
    TestFalse(TEXT("Widened shaft has no hidden collider"),World->SweepSingleByChannel(Hit,
        FVector(250,0,110),FVector(250,0,A->GeneratedHeight+200),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96),Q));
    A->ShaftRadius_DEPRECATED=240;A->PlateThickness_DEPRECATED=24;A->MigrateLegacyShapeSettings();
    TestEqual(TEXT("Legacy radius becomes full width"),A->ShaftWidth,480.f);
    TestEqual(TEXT("Legacy thickness migrates to ribs"),A->MeridianThickness,24.f);
    TestEqual(TEXT("Legacy thickness migrates to shelves"),A->ParallelThickness,24.f);
    A->MeridianThickness=16;A->MigrateLegacyShapeSettings();
    TestEqual(TEXT("Migration never overwrites subsequent edits"),A->MeridianThickness,16.f);
    A->ShaftWidth=360;A->MeridianThickness=18;A->ParallelThickness=18;
    for(const FIntPoint Counts:{FIntPoint(3,0),FIntPoint(7,3),FIntPoint(32,24)})
    {
        A->MeridianCount=Counts.X;A->ParallelCount=Counts.Y;A->SphereRadius=600;A->PieceSize=300;A->CurveSegments=24;A->RebuildGlobe();
        TestTrue(TEXT("Count variants remain valid"),A->GenerationWarning.IsEmpty());
        TestEqual(TEXT("Zero parallels removes all shelves"),A->GeneratedParallelPieces==0,Counts.Y==0);
        for(int32 I=0;I<A->Pieces.Num();++I)
            if(!TestTrue(TEXT("Convex pieces enclose their centroids"),A->Pieces[I].Contains(A->GetCellLocalCenter(FIntPoint(I,0)),.01)))return false;
    }
    A->SphereRadius=300;A->ShaftWidth=150;A->MeridianThickness=100;A->ParallelThickness=100;A->RebuildGlobe();
    TestTrue(TEXT("Dense inner ribs retain space between them"),A->MeridianThickness<A->ShaftWidth*FMath::Tan(UE_PI/A->MeridianCount));
    TestTrue(TEXT("Smallest sphere with maximum counts is valid"),A->GenerationWarning.IsEmpty());
    A->bEnableLaunchPad=false;A->RebuildGlobe();
    TestNull(TEXT("Pad can be disabled"),A->GroundLaunchPad->GetChildActor());
    const int32 Before=Triangles();A->MaxGeneratedCells=5;
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->RebuildGlobe();TestEqual(TEXT("Budget failure retains old geometry"),Triangles(),Before);
    AddInfo(FString::Printf(TEXT("Default globe: %d pieces, %d triangles"),Count,Original));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleGlobeLaunchTest,"MyProject6.Destructible.Globe.LaunchAndSavedBlueprint",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDestructibleGlobeLaunchTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT {World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    for(bool Blueprint:{false,true})
    {
        UClass* Class=Blueprint?LoadClass<AChunkedDestructibleGlobe>(nullptr,
            TEXT("/Game/Structures/Destructible/Globe/BP_ChunkedDestructibleGlobe.BP_ChunkedDestructibleGlobe_C")):
            AChunkedDestructibleGlobe::StaticClass();
        if(!TestNotNull(TEXT("Saved class loads in fresh process"),Class))return false;
        auto* Globe=World->SpawnActor<AChunkedDestructibleGlobe>(Class);
        if(Blueprint)Globe->SetActorScale3D(FVector(1.2,1.2,1.5));
        Globe->RebuildGlobe();
        TestTrue(TEXT("Class presentation defaults persist"),Globe->PanelMaterial && Globe->EdgeMaterial && Globe->MeridianCount==16 && Globe->ParallelCount==5);
        TestTrue(TEXT("Reference thickness and opening defaults persist"),Globe->MeridianThickness==36 && Globe->ParallelThickness==28 && Globe->ShaftWidth==480);
        auto* Pad=Cast<ALaunchPad>(Globe->GroundLaunchPad->GetChildActor());
        if(!TestNotNull(TEXT("Integrated platform spawns"),Pad))return false;
        TestTrue(TEXT("Round bottom pad uses animated material and automatic height"),Pad->Shape==ELaunchPadShape::Circle && Pad->RingsMaterial && Pad->bUseTargetHeight && Pad->GetActorLocation().IsNearlyZero());
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Character=World->SpawnActor<ACharacter>(FVector(0,0,4000),FRotator::ZeroRotator,Params);
        Character->GetCapsuleComponent()->SetCapsuleSize(34,96);Character->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        auto* Movement=Character->GetCharacterMovement();Movement->bRunPhysicsWithNoController=true;
        Movement->GravityScale=Blueprint?1.4f:1.f;Movement->SetMovementMode(MOVE_Falling);
        Character->SetActorLocation(FVector(40,0,8*Globe->GetActorScale3D().Z+98));
        double Peak=0;bool Launched=false;
        for(int32 I=0;I<720;++I)
        {
            ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/120);
            Peak=FMath::Max(Peak,Character->GetActorLocation().Z-96);
            Launched|=Character->GetVelocity().Z>1500;
        }
        const double Top=Globe->GeneratedHeight*Globe->GetActorScale3D().Z;
        TestTrue(TEXT("Contact automatically launches character"),Launched);
        TestTrue(TEXT("Actual flight clears globe with custom scale/gravity"),Peak>Top+200);
        AddInfo(FString::Printf(TEXT("Blueprint %d, top %.1f, feet apex %.1f"),Blueprint,Top,Peak));
        Character->Destroy();Globe->Destroy();
    }
    return true;
}
#endif
