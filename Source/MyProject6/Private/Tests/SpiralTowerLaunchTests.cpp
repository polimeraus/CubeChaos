#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ChunkedDestructibleSpiralTower.h"
#include "Components/ChildActorComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpiralTowerLaunchTest,"MyProject6.Destructible.SpiralTowerLaunch",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSpiralTowerLaunchTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    for (bool Foundation:{false,true})
    {
        UClass* Class=Foundation?LoadClass<AChunkedDestructibleSpiralTower>(nullptr,
            TEXT("/Game/Structures/Destructible/SpiralTower/BP_ChunkedDestructibleSpiralTower.BP_ChunkedDestructibleSpiralTower_C")):
            AChunkedDestructibleSpiralTower::StaticClass();
        if (!TestNotNull(TEXT("Native and saved Blueprint classes load for runtime spawning"),Class)) return false;
        auto* Tower=World->SpawnActor<AChunkedDestructibleSpiralTower>(Class);
        TestTrue(TEXT("Runtime spawns inherit glow and launch defaults"),Tower->bFlowingGlow && Tower->GlowMaterial && Tower->Traversal.bGroundLaunchPad);
        Tower->bFoundation=Foundation;
        Tower->SetActorScale3D(Foundation?FVector(1.2,1.2,1.5):FVector::OneVector);
        Tower->RebuildTower();
        auto* Pad=Cast<ALaunchPad>(Tower->GroundLaunchPad->GetChildActor());
        if (!TestNotNull(TEXT("Runtime launch pad exists"),Pad)) return false;
        FCollisionQueryParams Q;Q.AddIgnoredActor(Pad);FHitResult Hit;
        const double Roof=Tower->GeneratedHeight*Tower->GetActorScale3D().Z;
        TestFalse(TEXT("Standing player capsule clears the full center shaft"),World->SweepSingleByChannel(Hit,
            FVector(0,0,125),FVector(0,0,Roof+250),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(34,96),Q));
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Character=World->SpawnActor<ACharacter>(FVector(1000,0,300),FRotator::ZeroRotator,Params);
        Character->GetCapsuleComponent()->SetCapsuleSize(34,96);
        Character->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
        auto* Movement=Character->GetCharacterMovement();Movement->bRunPhysicsWithNoController=true;
        Movement->GravityScale=1.4f;Movement->SetMovementMode(MOVE_Falling);
        Character->SetActorLocation(FVector(40,0,8*Tower->GetActorScale3D().Z+98));
        double Peak=0;bool Launched=false;
        for (int32 I=0;I<600;++I)
        {
            ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/120);
            Peak=FMath::Max(Peak,Character->GetActorLocation().Z-96);
            Launched|=Character->GetVelocity().Z>1500;
        }
        TestTrue(TEXT("Stepping onto pad launches the actual character"),Launched);
        TestTrue(TEXT("Actual ballistic flight clears the highest wall with custom gravity and actor scale"),Peak>Roof+200);
        AddInfo(FString::Printf(TEXT("Foundation %d, roof %.1f, character feet apex %.1f"),Foundation,Roof,Peak));
        Character->Destroy();
        Tower->Destroy();
    }
    return true;
}
#endif
