#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ChunkedDestructibleZiggurat.h"
#include "ChunkedDestructibleZigguratPyramit.h"
#include "Components/ChildActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FZigguratTraversalTest,"MyProject6.Destructible.ZigguratTraversal",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FZigguratTraversalTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto Advance=[&](float Seconds)
    { for (int32 I=0;I<FMath::CeilToInt(Seconds*120);++I) { ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/120); } };
    auto Check=[&]<typename T>(T* Temple)
    {
        auto Rebuild=[&]()
        {
            if constexpr (std::is_same_v<T,AChunkedDestructibleZiggurat>) Temple->RebuildZiggurat();
            else Temple->RebuildZigguratPyramit();
        };
        AddInfo(Temple->GetClass()->GetName());
        TestTrue(TEXT("Both traversal flags default on"),Temple->Traversal.bFloorOpenings && Temple->Traversal.bGroundLaunchPad);
        auto* Pad=Cast<ALaunchPad>(Temple->GroundLaunchPad->GetChildActor());
        if (!TestNotNull(TEXT("Ground launch pad is part of the actor"),Pad)) return;
        TestTrue(TEXT("Default animated material exists"),Pad->PadMaterial!=nullptr);
        TestTrue(TEXT("Pad is at solid foundation height"),FMath::IsNearlyEqual(Pad->GetActorLocation().Z,double(Temple->FoundationHeight),.1));
        TestEqual(TEXT("Default shaft clear width"),Temple->EffectiveOpeningWidth,200.f);
        FCollisionQueryParams Q;Q.AddIgnoredActor(Pad);
        FHitResult Hit;
        const FVector Start(0,0,Temple->FoundationHeight+130),End(0,0,Temple->GeneratedHeight+250);
        TestFalse(TEXT("Full player capsule clears ALL floors, cornices and crown"),World->SweepSingleByChannel(Hit,
            Start,End,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96),Q));
        TestTrue(TEXT("Ground floor remains solid"),World->LineTraceSingleByChannel(Hit,
            FVector(1.1,1.1,Temple->FoundationHeight+50),FVector(1.1,1.1,-10),ECC_Visibility,Q));
        TestTrue(TEXT("Walkable slab remains immediately outside opening"),World->LineTraceSingleByChannel(Hit,
            FVector(130,1.1,Temple->FoundationHeight+150),FVector(130,1.1,Temple->GeneratedHeight+100),ECC_Visibility,Q));

        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Character=World->SpawnActor<ACharacter>(FVector(1000,0,300),FRotator::ZeroRotator,Params);
        Character->GetCapsuleComponent()->SetCapsuleSize(34,96);
        Character->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        auto* Movement=Character->GetCharacterMovement();Movement->bRunPhysicsWithNoController=true;
        // Gravity differs from the project default; height mode must adapt to it.
        Movement->GravityScale=1.4f;Movement->SetMovementMode(MOVE_Falling);
        Character->SetActorLocation(FVector(55,0,Temple->FoundationHeight+8+96+2));
        Advance(.08f);
        TestTrue(TEXT("Walking onto pad launches actual character"),Character->GetVelocity().Z>1500);
        TestTrue(TEXT("Launch is centered and vertical"),Character->GetActorLocation().Size2D()<1 && Character->GetVelocity().Size2D()<1);
        double Peak=0;
        for (int32 I=0;I<360;++I)
        {
            Advance(1.f/120);
            Peak=FMath::Max(Peak,Character->GetActorLocation().Z-96);
        }
        AddInfo(FString::Printf(TEXT("Roof %.1f; actual character feet apex %.1f"),Temple->GeneratedHeight,Peak));
        TestTrue(TEXT("Actual flight emerges above the crown"),Peak>Temple->GeneratedHeight+200);
        Character->Destroy();

        Temple->Traversal.bGroundLaunchPad=false;Rebuild();
        TestNull(TEXT("Pad flag removes pad and trigger"),Temple->GroundLaunchPad->GetChildActor());
        TestFalse(TEXT("Pad flag leaves shaft open"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
        Temple->Traversal.bFloorOpenings=false;Rebuild();
        TestTrue(TEXT("Opening flag restores upper-floor collision"),World->SweepSingleByChannel(Hit,Start,End,
            FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96)));
        TestEqual(TEXT("Closed-floor effective width"),Temple->EffectiveOpeningWidth,0.f);
        Temple->Traversal.bGroundLaunchPad=true;Rebuild();
        TestNotNull(TEXT("Pad flag is independent of floor flag"),Temple->GroundLaunchPad->GetChildActor());
        Temple->Traversal.bFloorOpenings=true;Temple->Traversal.OpeningWidth=360;
        Temple->Traversal.PadWidth=240;Temple->Traversal.AnimationSpeed=2.5;
        Temple->Traversal.Brightness=6;Temple->Traversal.bAutoLaunchHeight=false;
        Temple->Traversal.LaunchUpSpeed=1500;Rebuild();
        Pad=Cast<ALaunchPad>(Temple->GroundLaunchPad->GetChildActor());
        TestEqual(TEXT("Custom width rebuilds exact opening"),Temple->EffectiveOpeningWidth,360.f);
        TestFalse(TEXT("Wider shaft remains clear through narrow crown"),World->SweepSingleByChannel(Hit,
            Start+FVector(120,0,0),End+FVector(120,0,0),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96)));
        TestTrue(TEXT("Manual speed propagates"),!Pad->bUseTargetHeight && Pad->LaunchUpSpeed==1500);
        UMaterialInstanceDynamic* MID=nullptr;
        TArray<UStaticMeshComponent*> Meshes;Pad->GetComponents(Meshes);
        for (auto* Mesh:Meshes) if (Mesh->GetFName()==TEXT("AnimatedArrows")) MID=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
        TestNotNull(TEXT("Animated material is instantiated per pad"),MID);
        if (MID)
        {
            TestEqual(TEXT("Animation speed parameter reaches shader"),MID->K2_GetScalarParameterValue(TEXT("AnimationSpeed")),2.5f);
            TestEqual(TEXT("Brightness parameter reaches shader"),MID->K2_GetScalarParameterValue(TEXT("Brightness")),6.f);
        }
        Temple->SetActorTransform(FTransform(FRotator(0,37,0),FVector(400,200,50),FVector(1.2,1.2,1.5)));
        Temple->Traversal.bAutoLaunchHeight=true;Rebuild();
        Pad=Cast<ALaunchPad>(Temple->GroundLaunchPad->GetChildActor());
        const FVector Expected=Temple->GetActorTransform().TransformPosition(FVector(0,0,Temple->FoundationHeight));
        TestTrue(TEXT("Pad follows moved/scaled building"),Pad->GetActorLocation().Equals(Expected,.1));
        TestTrue(TEXT("Automatic height follows vertical scale"),Pad->TargetRiseHeight>
            (Temple->GeneratedHeight-Temple->FoundationHeight)*1.4);
        Temple->Destroy();
    };
    Check(World->SpawnActor<AChunkedDestructibleZiggurat>());
    Check(World->SpawnActor<AChunkedDestructibleZigguratPyramit>());
    return true;
}
#endif
