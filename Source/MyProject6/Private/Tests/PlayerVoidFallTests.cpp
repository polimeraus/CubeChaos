#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "MyProject6Character.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FVoidFallTestWorld
	{
		UWorld* World;
		AMyProject6Character* Player;
		FVoidFallTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->SetGameInstance(NewObject<UGameInstance>(GEngine));
			World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			UClass* PlayerClass = LoadClass<AMyProject6Character>(nullptr,
				TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
			Player = PlayerClass ? World->SpawnActor<AMyProject6Character>(PlayerClass, FVector(0, 0, 10000), FRotator::ZeroRotator) : nullptr;
			if (Player)
			{
				Player->GetCharacterMovement()->bRunPhysicsWithNoController = true;
				Player->GetCharacterMovement()->GravityScale = 0.0f;
				Player->GetCharacterMovement()->BrakingDecelerationFalling = 0.0f;
				Fall();
			}
		}
		~FVoidFallTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		void SetFloat(FName Name, float Value)
		{
			FindFProperty<FFloatProperty>(Player->GetClass(), Name)->SetPropertyValue_InContainer(Player, Value);
		}
		void Advance(float Seconds, int32 FPS = 60)
		{
			const int32 Frames = FMath::Max(1, FMath::CeilToInt(Seconds * FPS));
			for (int32 Frame = 0; Frame < Frames; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All, Seconds / Frames); }
		}
		void Fall(float VerticalSpeed = -100.0f)
		{
			Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
			Player->GetCharacterMovement()->Velocity = FVector(0, 0, VerticalSpeed);
		}
		AActor* Box(FVector Location, FVector Extent)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Shape = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Shape);
			Shape->SetBoxExtent(Extent);
			Shape->SetCollisionProfileName(TEXT("BlockAll"));
			Shape->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Actor;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVoidFallTimingTest, "MyProject6.Gameplay.VoidFall.TimingAndRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerVoidFallTimingTest::RunTest(const FString& Parameters)
{
	FVoidFallTestWorld TestWorld;
	AMyProject6Character* Player = TestWorld.Player;
	if (!TestNotNull(TEXT("Actual player Blueprint spawns"), Player)) { return false; }
	TestEqual(TEXT("Initial default delay is ten seconds"),
		FindFProperty<FFloatProperty>(Player->GetClass(), TEXT("VoidFallDelay"))->GetPropertyValue_InContainer(Player), 10.0f);
	TestWorld.Advance(9.5f);
	TestEqual(TEXT("An empty void alone does not drain before ten seconds"), Player->GetCurrentEnergy(), 100.0f);
	TestWorld.Fall(100);
	TestWorld.Advance(0.2f);
	TestWorld.SetFloat(TEXT("VoidFallDelay"), 0.4f);
	TestWorld.Fall();
	TestWorld.Advance(0.25f);
	TestWorld.Fall(100);
	TestWorld.Advance(0.1f);
	TestWorld.Fall();
	TestWorld.Advance(0.25f);
	TestEqual(TEXT("Ascending resets the uninterrupted fall timer"), Player->GetCurrentEnergy(), 100.0f);
	TestWorld.Fall(0);
	TestWorld.Advance(0.1f);
	TestWorld.Fall();
	TestWorld.Advance(0.25f);
	TestEqual(TEXT("Stationary midair also resets the timer"), Player->GetCurrentEnergy(), 100.0f);
	TestWorld.Advance(0.35f);
	TestEqual(TEXT("Energy drains gradually at the configured rate after the delay"), Player->GetCurrentEnergy(), 80.0f, 3.0f);
	Player->LaunchCharacter(FVector(0, 0, 100), true, true);
	const float BeforeLaunch = Player->GetCurrentEnergy();
	TestWorld.Advance(0.2f);
	TestEqual(TEXT("Upward launch stops void drain immediately"), Player->GetCurrentEnergy(), BeforeLaunch);
	TestWorld.Fall();
	TestWorld.Advance(0.25f);
	TestEqual(TEXT("A fresh fall gets a full new delay"), Player->GetCurrentEnergy(), BeforeLaunch);
	Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	TestWorld.Advance(0.1f);
	TestWorld.Fall();
	TestWorld.Advance(0.25f);
	TestEqual(TEXT("Leaving falling mode resets the timer"), Player->GetCurrentEnergy(), BeforeLaunch);
	FindFProperty<FBoolProperty>(Player->GetClass(), TEXT("bEnableVoidFallDeath"))->SetPropertyValue_InContainer(Player, false);
	TestWorld.Advance(1.0f);
	TestEqual(TEXT("Feature can be disabled"), Player->GetCurrentEnergy(), BeforeLaunch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVoidFallSupportTest, "MyProject6.Gameplay.VoidFall.StructuresAndDeath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerVoidFallSupportTest::RunTest(const FString& Parameters)
{
	FVoidFallTestWorld TestWorld;
	AMyProject6Character* Player = TestWorld.Player;
	if (!TestNotNull(TEXT("Player spawns"), Player)) { return false; }
	TestWorld.SetFloat(TEXT("VoidFallDelay"), 0.2f);
	// More than two kilometres below: a short ground ray would incorrectly declare a void.
	AActor* Floor = TestWorld.Box(FVector(0, 0, -200000), FVector(1000, 1000, 50));
	TestWorld.Advance(0.6f);
	TestEqual(TEXT("A distant blocking platform prevents depletion"), Player->GetCurrentEnergy(), 100.0f);
	AActor* Wall = TestWorld.Box(FVector(38, 0, 9000), FVector(10, 100, 2000));
	TestWorld.Advance(0.3f);
	TestEqual(TEXT("A wall brushing the capsule cannot hide the floor below"), Player->GetCurrentEnergy(), 100.0f);
	Wall->Destroy();
	Floor->SetActorEnableCollision(false);
	TestWorld.Advance(0.3f);
	TestTrue(TEXT("Disabled collision cannot rescue a fall"), Player->GetCurrentEnergy() < 85.0f);
	Floor->SetActorEnableCollision(true);
	TestWorld.Advance(0.15f);
	const float RescuedEnergy = Player->GetCurrentEnergy();
	TestWorld.Advance(0.3f);
	TestEqual(TEXT("A newly available platform stops ongoing depletion"), Player->GetCurrentEnergy(), RescuedEnergy);
	Floor->Destroy();
	// A structure under the capsule edge still supports the character, even when the center ray misses.
	AActor* Edge = TestWorld.Box(FVector(30, 0, -50000), FVector(10, 100, 20));
	TestWorld.Advance(0.3f);
	TestEqual(TEXT("Capsule footprint detects a narrow platform"), Player->GetCurrentEnergy(), RescuedEnergy);
	Edge->Destroy();
	AActor* LandingFloor = TestWorld.Box(FVector(0, 0, Player->GetActorLocation().Z - 146), FVector(500, 500, 25));
	TestWorld.Advance(0.6f);
	TestTrue(TEXT("Character can still land after a long fall"), Player->GetCharacterMovement()->IsMovingOnGround());
	const float LandedEnergy = Player->GetCurrentEnergy();
	LandingFloor->Destroy();
	TestWorld.Fall();
	TestWorld.Advance(0.1f);
	TestEqual(TEXT("Landing resets the delay before another fall"), Player->GetCurrentEnergy(), LandedEnergy);
	TestWorld.Box(FVector(2000, 0, 0), FVector(100, 100, 100));
	AActor* NonBlocking = TestWorld.Box(FVector(0, 0, -10000), FVector(100, 100, 10));
	CastChecked<UBoxComponent>(NonBlocking->GetRootComponent())->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	TestWorld.Advance(1.5f);
	TestTrue(TEXT("Sideways structures and nonblocking geometry do not prevent death"), Player->IsEnergyDepleted());
	TestEqual(TEXT("Death uses the existing energy system"), Player->GetCurrentEnergy(), 0.0f);
	TestEqual(TEXT("Death disables player movement"), Player->GetCharacterMovement()->MovementMode, MOVE_None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerVoidFallFrameRateTest, "MyProject6.Gameplay.VoidFall.FrameRateIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerVoidFallFrameRateTest::RunTest(const FString& Parameters)
{
	for (const int32 FPS : { 30, 120 })
	{
		FVoidFallTestWorld TestWorld;
		if (!TestNotNull(TEXT("Player spawns"), TestWorld.Player)) { return false; }
		TestWorld.SetFloat(TEXT("VoidFallDelay"), 0.2f);
		TestWorld.SetFloat(TEXT("VoidFallEnergyDrainPerSecond"), 50.0f);
		TestWorld.Advance(0.8f, FPS);
		TestEqual(FString::Printf(TEXT("Same energy loss at %d FPS"), FPS), TestWorld.Player->GetCurrentEnergy(), 70.0f, 0.1f);
	}
	return true;
}

#endif
