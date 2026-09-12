#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "LaunchPad.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLaunchPadTest, "MyProject6.Gameplay.LaunchPad.ContactAndFlight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLaunchPadTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    auto Advance = [World](float Time)
    {
        for (int32 I = 0; I < FMath::CeilToInt(Time * 60); ++I) { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f / 60); }
    };
    ALaunchPad* Pad = World->SpawnActor<ALaunchPad>();
    Pad->ForwardSpeed = 250;
    Pad->bPreserveHorizontalVelocity = false;
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACharacter* Character = World->SpawnActor<ACharacter>(FVector(300, 0, 120), FRotator::ZeroRotator, Params);
    Character->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    Character->GetCharacterMovement()->bRunPhysicsWithNoController = true;
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    const float Half = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Character->SetActorLocation(FVector(0, 0, Half + 10));
    AddInfo(FString::Printf(TEXT("Before: feet %.1f, top %.1f, overlap %d, trigger %s"),
        Character->GetActorLocation().Z - Half, Pad->FindComponentByClass<UStaticMeshComponent>()->Bounds.GetBox().Max.Z,
        Pad->FindComponentByClass<UBoxComponent>()->IsOverlappingActor(Character), *Pad->FindComponentByClass<UBoxComponent>()->GetComponentLocation().ToString()));
    Advance(.12f);
    AddInfo(FString::Printf(TEXT("After: position %s, velocity %s, pending %s"), *Character->GetActorLocation().ToString(),
        *Character->GetVelocity().ToString(), *Character->GetCharacterMovement()->PendingLaunchVelocity.ToString()));
    TestTrue(TEXT("Walking onto top launches upward"), Character->GetVelocity().Z > 900);
    TestTrue(TEXT("Optional forward speed"), Character->GetVelocity().X > 240);
    TestTrue(TEXT("Character actually rises"), Character->GetActorLocation().Z > Half + 80);
    auto Reset = [&]()
    {
        Character->SetActorLocation(FVector(300, 0, Half + 10));
        Character->GetCharacterMovement()->StopMovementImmediately();
        Character->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
        Advance(.6f);
    };
    Reset();
    Pad->bEnabled = false;
    Character->SetActorLocation(FVector(0, 0, Half + 10));
    Advance(.12f);
    TestTrue(TEXT("Disabled pad does not launch"), Character->GetVelocity().Z < 100);
    Reset();
    Pad->bEnabled = true;
    Pad->bAffectNPCs = false;
    Character->SetActorLocation(FVector(0, 0, Half + 10));
    Advance(.12f);
    TestTrue(TEXT("NPC filter prevents launch"), Character->GetVelocity().Z < 100);
    Reset();
    Pad->bAffectNPCs = true;
    Character->SetActorLocation(FVector(90, 0, 15));
    Advance(.1f);
    TestTrue(TEXT("Side contact does not launch"), Character->GetVelocity().Z < 100);
    Reset();
    Character->SetActorLocation(FVector(0, 0, Half + 10));
    Advance(.1f);
    TestTrue(TEXT("Reentering re-arms launch"), Character->GetVelocity().Z > 900);
    World->EndPlay(EEndPlayReason::Quit);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
