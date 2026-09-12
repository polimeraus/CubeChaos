#include "NPCSpawnPointTestObserver.h"
#include "NPCSpawnPoint.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

ANPCSpawnPointLargeTestCharacter::ANPCSpawnPointLargeTestCharacter()
{
	GetCapsuleComponent()->InitCapsuleSize(70.0f, 150.0f);
	GetCapsuleComponent()->SetRelativeScale3D(FVector(2.0f));
}

void UNPCSpawnPointTestObserver::OnSpawned(ANPCSpawnPoint* Point, int32 RequestId, ACharacter* NPC)
{
	++SpawnEvents;
	LaunchVelocities.Add(NPC->GetCharacterMovement()->PendingLaunchVelocity);
	SpawnLocations.Add(NPC->GetActorLocation());
	SpawnRequestIds.Add(RequestId);
	SpawnTimes.Add(Point->GetWorld()->GetTimeSeconds());
	if (bCancelOnNextSpawn)
	{
		bCancelOnNextSpawn = false;
		Point->CancelPendingSpawns();
	}
}

void UNPCSpawnPointTestObserver::OnFinished(ANPCSpawnPoint* Point, int32 RequestId, int32 Requested, int32 Spawned, bool bCancelled)
{
	Results.Add({ RequestId, Requested, Spawned, bCancelled });
	if (bQueueOnNextFinish)
	{
		bQueueOnNextFinish = false;
		Point->SpawnNPCs(ACharacter::StaticClass(), 1);
	}
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "ChasingNPC.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FSpawnTestWorld
	{
		UWorld* World;
		FSpawnTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->SetGameInstance(NewObject<UGameInstance>(GEngine));
			World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}
		~FSpawnTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		void Advance(float Duration) const
		{
			const int32 Frames = FMath::Max(1, FMath::CeilToInt(Duration * 60.0f));
			for (int32 Index = 0; Index < Frames; ++Index)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, Duration / Frames);
			}
		}
		AActor* Box(FVector Location, FVector Extent) const
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
		ANPCSpawnPoint* Point(FName Id, FVector Location = FVector::ZeroVector) const
		{
			auto* Point = World->SpawnActor<ANPCSpawnPoint>(Location, FRotator::ZeroRotator);
			Point->SpawnPointId = Id;
			Point->DefaultNPCClass = ACharacter::StaticClass();
			Point->SpawnSettings.bEnsureAIController = false;
			return Point;
		}
	};

	void Observe(ANPCSpawnPoint* Point, UNPCSpawnPointTestObserver* Observer)
	{
		Point->OnNPCSpawned.AddDynamic(Observer, &UNPCSpawnPointTestObserver::OnSpawned);
		Point->OnSpawnRequestFinished.AddDynamic(Observer, &UNPCSpawnPointTestObserver::OnFinished);
	}

	bool CapsulesSeparated(const TArray<ACharacter*>& NPCs, float Gap)
	{
		for (int32 I = 0; I < NPCs.Num(); ++I)
		{
			for (int32 J = I + 1; J < NPCs.Num(); ++J)
			{
				const auto* A = NPCs[I]->GetCapsuleComponent();
				const auto* B = NPCs[J]->GetCapsuleComponent();
				if (FMath::Abs(A->GetComponentLocation().Z - B->GetComponentLocation().Z) < A->GetScaledCapsuleHalfHeight() + B->GetScaledCapsuleHalfHeight()
					&& FVector::Dist2D(A->GetComponentLocation(), B->GetComponentLocation()) + 0.1f < A->GetScaledCapsuleRadius() + B->GetScaledCapsuleRadius() + Gap)
				{
					return false;
				}
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCSpawnScatterTest, "MyProject6.NPC.SpawnPoint.ScatterAndRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCSpawnScatterTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	TestWorld.Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
	// Occupy the center with a wall taller than the ground trace's starting height.
	TestWorld.Box(FVector(0, 0, 300), FVector(100, 100, 300));
	ANPCSpawnPoint* First = TestWorld.Point(TEXT("Spawn_01"));
	ANPCSpawnPoint* Second = TestWorld.Point(TEXT("Spawn_02"), FVector(2000, 0, 0));
	TStrongObjectPtr<UNPCSpawnPointTestObserver> Observer(NewObject<UNPCSpawnPointTestObserver>());
	Observe(First, Observer.Get());
	TestEqual(TEXT("Unique point lookup"), ANPCSpawnPoint::FindSpawnPoint(TestWorld.World, TEXT("Spawn_01")), First);
	const int32 RequestId = ANPCSpawnPoint::SpawnNPCsAtPoint(TestWorld.World, TEXT("Spawn_01"), ACharacter::StaticClass(), 10);
	TestTrue(TEXT("Batch accepted"), RequestId > 0);
	TestEqual(TEXT("Only explicit commands spawn NPCs"), Second->GetSpawnedNPCs().Num(), 0);
	TestEqual(TEXT("Request begins asynchronously"), First->GetSpawnedNPCs().Num(), 0);
	TestWorld.Advance(1.0f / 60.0f);
	const TArray<ACharacter*> NPCs = First->GetSpawnedNPCs();
	TestEqual(TEXT("All ten spawn together within the default frame budget"), NPCs.Num(), 10);
	TestTrue(TEXT("All capsules have the requested gap"), CapsulesSeparated(NPCs, First->SpawnSettings.CapsuleGap));
	for (const ACharacter* NPC : NPCs)
	{
		TestTrue(TEXT("Spawn stays within the chosen disk"), NPC->GetActorLocation().Size2D() <= 600.1f);
		TestTrue(TEXT("Origin ID is available as an actor tag"), NPC->ActorHasTag(TEXT("Spawn_01")));
		TestTrue(TEXT("Capsules are placed above the floor"), NPC->GetActorLocation().Z >= NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}
	TestEqual(TEXT("Completion fires once"), Observer->Results.Num(), 1);
	if (Observer->Results.Num() == 1)
	{
		TestEqual(TEXT("Completion preserves request ID"), Observer->Results[0].Id, RequestId);
		TestEqual(TEXT("Completion reports all ten"), Observer->Results[0].Spawned, 10);
	}
	// A second point in the same region must also respect characters from the first point.
	Second->SetActorLocation(FVector::ZeroVector);
	Second->SpawnNPCs(ACharacter::StaticClass(), 10);
	TestWorld.Advance(1.0f / 60.0f);
	TArray<ACharacter*> Combined = NPCs;
	Combined.Append(Second->GetSpawnedNPCs());
	TestEqual(TEXT("Nearby independent point also emits ten"), Combined.Num(), 20);
	TestTrue(TEXT("Collision separation works across points"), CapsulesSeparated(Combined, First->SpawnSettings.CapsuleGap));
	Second->SetActorLocation(FVector(2000, 0, 0));
	Second->SpawnSettings.bEnsureAIController = true;
	TestTrue(TEXT("Console alias command executes"), IConsoleManager::Get().ProcessUserConsoleInput(TEXT("npc.Spawn Spawn_02 2 Ghoul"), *GLog, TestWorld.World));
	TestWorld.Advance(1.0f / 60.0f);
	int32 Chasers = 0;
	for (ACharacter* NPC : Second->GetSpawnedNPCs())
	{
		if (Cast<AChasingNPC>(NPC))
		{
			++Chasers;
			TestNotNull(TEXT("Spawned enemy has its AI controller"), NPC->GetController());
		}
	}
	TestEqual(TEXT("Console spawns exactly two of the chosen NPC type"), Chasers, 2);
	ANPCSpawnPoint* LargePoint = TestWorld.Point(TEXT("Large"), FVector(3500, 0, 0));
	LargePoint->SpawnSettings.CapsuleGap = 150.0f;
	LargePoint->SpawnNPCs(ANPCSpawnPointLargeTestCharacter::StaticClass(), 2);
	TestWorld.Advance(1.0f / 60.0f);
	TestEqual(TEXT("Scaled large characters and gaps larger than a normal half-height are supported"), LargePoint->GetSpawnedNPCs().Num(), 2);
	TestTrue(TEXT("Scaled capsules preserve the large requested gap"), CapsulesSeparated(LargePoint->GetSpawnedNPCs(), 150.0f));
	for (ACharacter* NPC : LargePoint->GetSpawnedNPCs())
	{
		TestTrue(TEXT("Large capsule is placed fully above ground"), NPC->GetActorLocation().Z >= NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCSpawnQueueTest, "MyProject6.NPC.SpawnPoint.QueueCancellationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCSpawnQueueTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	TestWorld.Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
	ANPCSpawnPoint* Point = TestWorld.Point(TEXT("Queue"));
	TStrongObjectPtr<UNPCSpawnPointTestObserver> Observer(NewObject<UNPCSpawnPointTestObserver>());
	Observe(Point, Observer.Get());
	Point->MaxPendingNPCs = 4;
	AddExpectedError(TEXT("rejected spawn request"), EAutomationExpectedErrorFlags::Contains, 3);
	TestEqual(TEXT("Zero count is rejected"), Point->SpawnNPCs(ACharacter::StaticClass(), 0), INDEX_NONE);
	TestEqual(TEXT("Null class is rejected"), Point->SpawnNPCs(nullptr, 1), INDEX_NONE);
	TestEqual(TEXT("Over-budget count is rejected in full"), Point->SpawnNPCs(ACharacter::StaticClass(), 5), INDEX_NONE);
	AddExpectedError(TEXT("unknown NPC type"), EAutomationExpectedErrorFlags::Contains, 1);
	TestEqual(TEXT("Unknown alias does not fall back to a different enemy"), Point->SpawnNPCType(TEXT("MissingType"), 1), INDEX_NONE);
	ANPCSpawnPoint* Duplicate = TestWorld.Point(TEXT("Queue"));
	AddExpectedError(TEXT("Duplicate spawn point ID"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNull(TEXT("Duplicate names never choose an arbitrary point"), ANPCSpawnPoint::FindSpawnPoint(TestWorld.World, TEXT("Queue")));
	Duplicate->Destroy();
	Point->MaxSpawnsPerFrame = 1;
	const int32 FirstId = Point->SpawnNPCs(ACharacter::StaticClass(), 3);
	Point->SpawnSettings.SpawnInterval = 1.0f; // Must not alter the queued request.
	TestWorld.Advance(1.0f / 60.0f);
	TestEqual(TEXT("Per-frame budget emits just one"), Point->GetSpawnedNPCs().Num(), 1);
	TestWorld.Advance(1.0f / 60.0f);
	TestEqual(TEXT("Queued settings are a snapshot"), Point->GetSpawnedNPCs().Num(), 2);
	Point->CancelPendingSpawns();
	TestEqual(TEXT("Cancellation removes outstanding work"), Point->GetPendingNPCCount(), 0);
	if (TestEqual(TEXT("One cancellation event"), Observer->Results.Num(), 1))
	{
		TestEqual(TEXT("Cancelled request ID"), Observer->Results[0].Id, FirstId);
		TestEqual(TEXT("Cancellation reports two already emitted"), Observer->Results[0].Spawned, 2);
		TestTrue(TEXT("Cancellation marked explicitly"), Observer->Results[0].bCancelled);
	}
	TestWorld.Advance(0.2f);
	TestEqual(TEXT("Cancellation preserves existing NPCs and emits no more"), Point->GetSpawnedNPCs().Num(), 2);
	// Cancel and enqueue from callbacks; neither callback may invalidate the in-flight queue access.
	Point->SpawnSettings.SpawnInterval = 0.0f;
	Point->MaxSpawnsPerFrame = 10;
	Observer->bCancelOnNextSpawn = true;
	Observer->bQueueOnNextFinish = true;
	Point->SpawnNPCs(ACharacter::StaticClass(), 3);
	TestWorld.Advance(0.1f);
	TestEqual(TEXT("Callback cancellation plus a new request creates exactly two more"), Point->GetSpawnedNPCs().Num(), 4);
	TestEqual(TEXT("Each request gets exactly one terminal event"), Observer->Results.Num(), 3);
	TestEqual(TEXT("Callback-generated request drains normally"), Point->GetPendingNPCCount(), 0);
	// Destroying the point also clears its timer without destroying existing characters.
	Point->SpawnNPCs(ACharacter::StaticClass(), 2);
	Point->Destroy();
	TestWorld.Advance(0.1f);
	TestEqual(TEXT("Destroyed point cannot emit later"), Observer->SpawnEvents, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCSpawnBlockedTest, "MyProject6.NPC.SpawnPoint.BlockedSpaceAndRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCSpawnBlockedTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	ANPCSpawnPoint* Point = TestWorld.Point(TEXT("Blocked"));
	Point->SpawnSettings.SpawnRadius = 0;
	Point->SpawnSettings.MaxBlockedAttempts = 3;
	Point->SpawnSettings.RetryInterval = 0.05f;
	TStrongObjectPtr<UNPCSpawnPointTestObserver> Observer(NewObject<UNPCSpawnPointTestObserver>());
	Observe(Point, Observer.Get());
	Point->SpawnNPCs(ACharacter::StaticClass(), 2);
	TestWorld.Advance(0.4f);
	TestEqual(TEXT("Missing ground never emits a falling batch"), Point->GetSpawnedNPCs().Num(), 0);
	TestEqual(TEXT("Blocked request stops retrying"), Point->GetPendingNPCCount(), 0);
	if (TestEqual(TEXT("Blocked result emitted once"), Observer->Results.Num(), 1))
	{
		TestEqual(TEXT("Blocked result reports zero of two"), Observer->Results[0].Spawned, 0);
		TestEqual(TEXT("Requested count is preserved"), Observer->Results[0].Requested, 2);
		TestFalse(TEXT("Blocked differs from cancellation"), Observer->Results[0].bCancelled);
	}
	TestWorld.Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
	AActor* Blocker = TestWorld.Box(FVector(0, 0, 300), FVector(200, 200, 300));
	Point->SpawnNPCs(ACharacter::StaticClass(), 2);
	TestWorld.Advance(1.0f / 60.0f);
	TestEqual(TEXT("World geometry blocks emission"), Point->GetSpawnedNPCs().Num(), 0);
	Blocker->Destroy();
	TestWorld.Advance(0.08f);
	TestEqual(TEXT("Retry emits when the blockage clears"), Point->GetSpawnedNPCs().Num(), 1);
	// Clear the first character's space while the second is waiting.
	if (!Point->GetSpawnedNPCs().IsEmpty()) { Point->GetSpawnedNPCs()[0]->SetActorLocation(FVector(1500, 0, 100)); }
	TestWorld.Advance(0.1f);
	TestEqual(TEXT("Remaining count succeeds after space becomes available"), Point->GetSpawnedNPCs().Num(), 2);
	// At a single occupied point, report a partial batch instead of stacking the second NPC.
	Point->SetActorLocation(FVector(2500, 0, 0));
	Point->SpawnNPCs(ACharacter::StaticClass(), 2);
	TestWorld.Advance(0.4f);
	TestEqual(TEXT("Only one additional NPC fits"), Point->GetSpawnedNPCs().Num(), 3);
	if (TestEqual(TEXT("All three requests finish"), Observer->Results.Num(), 3))
	{
		TestEqual(TEXT("Partial result reports one of two"), Observer->Results[2].Spawned, 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCSpawnLaunchTest, "MyProject6.NPC.SpawnPoint.LaunchAndMovingPoint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCSpawnLaunchTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	TestWorld.Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
	ANPCSpawnPoint* Point = TestWorld.Point(TEXT("Launch"));
	FNPCSpawnSettings Settings = Point->SpawnSettings;
	Settings.Mode = ENPCSpawnMode::Launch;
	Settings.SpawnRadius = 0.0f;
	Settings.LaunchInterval = 0.3f;
	Settings.LaunchUpSpeed = 1200.0f;
	Settings.LaunchOutwardSpeed = 0.0f;
	Settings.bEnsureAIController = true;
	Settings.RetryInterval = 0.05f;
	const int32 Id = Point->SpawnNPCsWithSettings(ACharacter::StaticClass(), 3, Settings);
	TestTrue(TEXT("Launch request accepted"), Id > 0);
	TestWorld.Advance(0.05f);
	TestEqual(TEXT("Launch emits only the first NPC initially"), Point->GetSpawnedNPCs().Num(), 1);
	if (!Point->GetSpawnedNPCs().IsEmpty())
	{
		TestTrue(TEXT("Character receives upward velocity"), Point->GetSpawnedNPCs()[0]->GetCharacterMovement()->Velocity.Z > 900.0f);
	}
	TestWorld.Advance(0.1f);
	TestEqual(TEXT("Launch honors minimum interval despite zero SpawnInterval"), Point->GetSpawnedNPCs().Num(), 1);
	TestWorld.Advance(0.22f);
	TestEqual(TEXT("Second NPC emerges after the first has cleared the point"), Point->GetSpawnedNPCs().Num(), 2);
	TestTrue(TEXT("Sequential launch does not overlap capsules"), CapsulesSeparated(Point->GetSpawnedNPCs(), 0.0f));
	Point->SetActorLocation(FVector(2000, 0, 0));
	TestWorld.Advance(0.4f);
	TestEqual(TEXT("Full launch request completes"), Point->GetSpawnedNPCs().Num(), 3);
	if (Point->GetSpawnedNPCs().Num() == 3)
	{
		TestEqual(TEXT("Later emissions follow the moved point"), Point->GetSpawnedNPCs()[2]->GetActorLocation().X, 2000.0, 1.0);
	}
	TestEqual(TEXT("Launch queue drained"), Point->GetPendingNPCCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCSpawnBallisticTest, "MyProject6.NPC.SpawnPoint.SpeedAngleAndBallisticFlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCSpawnBallisticTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	TestWorld.Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
	ANPCSpawnPoint* Point = TestWorld.Point(TEXT("Angled"));
	Point->SetActorRotation(FRotator(22.0f, 90.0f, -20.0f));
	FNPCSpawnSettings Settings = Point->SpawnSettings;
	Settings.Mode = ENPCSpawnMode::Launch;
	Settings.LaunchVelocityMode = ENPCLaunchVelocityMode::SpeedAndAngle;
	Settings.LaunchDirection = ENPCLaunchDirection::SpawnPointForward;
	Settings.SpawnRadius = 0.0f;
	Settings.LaunchSpeed = 1200.0f;
	Settings.LaunchElevationAngle = 60.0f;
	Settings.LaunchYawOffset = -30.0f;
	Settings.LaunchInterval = 0.35f;
	Settings.bEnsureAIController = true;
	Settings.bRandomYaw = true;
	TStrongObjectPtr<UNPCSpawnPointTestObserver> Observer(NewObject<UNPCSpawnPointTestObserver>());
	Observe(Point, Observer.Get());
	Point->SpawnNPCsWithSettings(ACharacter::StaticClass(), 2, Settings);
	TestWorld.Advance(1.0f / 60.0f);
	if (!TestEqual(TEXT("First directed emission"), Observer->LaunchVelocities.Num(), 1)) { return false; }
	const FVector Velocity = Observer->LaunchVelocities[0];
	TestEqual(TEXT("Yaw 90 plus offset -30 gives the requested X velocity"), Velocity.X, 300.0, 0.1);
	TestEqual(TEXT("Launch heading ignores character RandomYaw and point Pitch/Roll"), Velocity.Y, 519.615, 0.1);
	TestEqual(TEXT("Elevation 60 gives the requested vertical speed"), Velocity.Z, 1039.230, 0.1);
	TestEqual(TEXT("Launch strength is the full speed"), Velocity.Size(), 1200.0, 0.1);
	ACharacter* FirstNPC = Point->GetSpawnedNPCs()[0];
	const FVector Start = Observer->SpawnLocations[0];
	Point->SpawnSettings.LaunchSpeed = 100.0f;
	Point->SpawnSettings.LaunchElevationAngle = 5.0f;
	Point->SetActorRotation(FRotator(-40.0f, 180.0f, 30.0f));
	double Peak = Start.Z;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		TestWorld.Advance(1.0f / 60.0f);
		Peak = FMath::Max(Peak, FirstNPC->GetActorLocation().Z);
	}
	if (!TestEqual(TEXT("Both angled emissions complete"), Observer->LaunchVelocities.Num(), 2)) { return false; }
	const FVector Second = Observer->LaunchVelocities[1];
	TestEqual(TEXT("Next emission follows the newly rotated point"), Second.X, -519.615, 0.1);
	TestEqual(TEXT("Yaw offset is applied to the live point direction"), Second.Y, 300.0, 0.1);
	TestEqual(TEXT("Queued strength and elevation retain their snapshot"), Second.Z, Velocity.Z, 0.1);
	TestEqual(TEXT("The character follows gravity to the expected roughly 5.5 m apex"), Peak - Start.Z, 551.02, 25.0);
	TestTrue(TEXT("The angled launch travels horizontally"), FVector::Dist2D(Start, FirstNPC->GetActorLocation()) > 500.0);
	TestTrue(TEXT("The ballistic flight lands back on the floor"), FirstNPC->GetCharacterMovement()->IsMovingOnGround());
	// A 90 degree setting must be vertical regardless of point heading or horizontal offset.
	Point->SetActorLocation(FVector(3500, 0, 0));
	Settings.LaunchElevationAngle = 90.0f;
	Point->SpawnNPCsWithSettings(ACharacter::StaticClass(), 1, Settings);
	TestWorld.Advance(1.0f / 60.0f);
	if (!TestEqual(TEXT("Vertical angle request emits"), Observer->LaunchVelocities.Num(), 3)) { return false; }
	TestEqual(TEXT("90 degrees has no horizontal launch speed"), Observer->LaunchVelocities[2].Size2D(), 0.0, 0.001);
	TestEqual(TEXT("90 degrees puts all strength into height"), Observer->LaunchVelocities[2].Z, 1200.0, 0.1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCSpawnLaunchSpreadTest, "MyProject6.NPC.SpawnPoint.LaunchDirectionAndSpread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCSpawnLaunchSpreadTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	TestWorld.Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
	ANPCSpawnPoint* Point = TestWorld.Point(TEXT("Spread"));
	Point->SetActorRotation(FRotator(0.0f, 30.0f, 0.0f));
	FNPCSpawnSettings Settings = Point->SpawnSettings;
	Settings.Mode = ENPCSpawnMode::Launch;
	Settings.LaunchVelocityMode = ENPCLaunchVelocityMode::SpeedAndAngle;
	Settings.LaunchDirection = ENPCLaunchDirection::SpawnPointForward;
	Settings.SpawnRadius = 900.0f;
	Settings.LaunchInterval = 0.02f;
	Settings.LaunchSpeed = 800.0f;
	Settings.LaunchElevationAngle = 45.0f;
	Settings.LaunchYawOffset = 10.0f;
	Settings.LaunchYawSpread = 15.0f;
	Settings.bEnsureAIController = true;
	TStrongObjectPtr<UNPCSpawnPointTestObserver> Observer(NewObject<UNPCSpawnPointTestObserver>());
	Observe(Point, Observer.Get());
	Point->SpawnNPCsWithSettings(ACharacter::StaticClass(), 12, Settings);
	TestWorld.Advance(0.8f);
	if (!TestEqual(TEXT("A spread batch completes"), Observer->LaunchVelocities.Num(), 12)) { return false; }
	float MinYaw = 180.0f;
	float MaxYaw = -180.0f;
	for (const FVector& Velocity : Observer->LaunchVelocities)
	{
		const float Yaw = Velocity.Rotation().Yaw;
		MinYaw = FMath::Min(MinYaw, Yaw);
		MaxYaw = FMath::Max(MaxYaw, Yaw);
		TestTrue(TEXT("Spread stays within plus/minus 15 degrees of yaw 40"), Yaw >= 24.99f && Yaw <= 55.01f);
		TestEqual(TEXT("Spread preserves launch strength"), Velocity.Size(), 800.0, 0.1);
		TestEqual(TEXT("Spread preserves the elevation angle"), Velocity.Z, 565.685, 0.1);
	}
	TestTrue(TEXT("Each NPC samples its own spread angle"), MaxYaw - MinYaw > 0.1f);
	// Outward direction derives from the actual placement, with yaw offset applied afterward.
	Point->SetActorLocation(FVector(-3000, 0, 0));
	Observer->LaunchVelocities.Reset();
	Observer->SpawnLocations.Reset();
	Settings.LaunchDirection = ENPCLaunchDirection::Outward;
	Settings.LaunchYawSpread = 0.0f;
	Settings.LaunchYawOffset = 90.0f;
	Settings.InnerRadius = 400.0f;
	Settings.SpawnRadius = 400.0f;
	Point->SpawnNPCsWithSettings(ACharacter::StaticClass(), 4, Settings);
	TestWorld.Advance(0.4f);
	if (!TestEqual(TEXT("Outward batch completes"), Observer->LaunchVelocities.Num(), 4)) { return false; }
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector Offset = (Observer->SpawnLocations[Index] - Point->GetActorLocation()).GetSafeNormal2D();
		const FVector Heading = Observer->LaunchVelocities[Index].GetSafeNormal2D();
		TestEqual(TEXT("A 90 degree outward offset launches tangent to the spawn ring"), FVector::DotProduct(Offset, Heading), 0.0, 0.001);
		TestTrue(TEXT("Positive offset turns in the positive yaw direction"), FVector::CrossProduct(Offset, Heading).Z > 0.99);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCReplacementPriorityTest, "MyProject6.NPC.SpawnPoint.ReplacementPriorityAndNormalTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCReplacementPriorityTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	TestWorld.Box(FVector(0, 0, -50), FVector(10000, 10000, 50));
	ANPCSpawnPoint* Nearest = TestWorld.Point(NAME_None, FVector(1000, 0, 0));
	ANPCSpawnPoint* Farther = TestWorld.Point(NAME_None, FVector(-4000, 0, 0));
	TStrongObjectPtr<UNPCSpawnPointTestObserver> Observer(NewObject<UNPCSpawnPointTestObserver>());
	Observe(Nearest, Observer.Get());
	Nearest->SpawnSettings.SpawnInterval = 1.0f;
	const int32 NormalId = Nearest->SpawnNPCs(ACharacter::StaticClass(), 3);
	const int32 FollowingId = Nearest->SpawnNPCs(ACharacter::StaticClass(), 1);
	TestWorld.Advance(0.2f);
	if (!TestEqual(TEXT("Normal batch starts with one NPC"), Observer->SpawnTimes.Num(), 1)) { return false; }
	const double FirstNormalTime = Observer->SpawnTimes[0];
	AChasingNPC* Victim = TestWorld.World->SpawnActor<AChasingNPC>(FVector(4000, 0, 100), FRotator::ZeroRotator);
	TestEqual(TEXT("Distance lookup works without point IDs"), ANPCSpawnPoint::FindNearestSpawnPoint(Victim, Victim->GetActorLocation()), Nearest);
	UGameplayStatics::ApplyDamage(Victim, 25, nullptr, nullptr, nullptr);
	TestEqual(TEXT("Nonlethal hit does not create replacements"), Nearest->GetPendingNPCCount(), 3);
	UGameplayStatics::ApplyDamage(Victim, 75, nullptr, nullptr, nullptr);
	UGameplayStatics::ApplyDamage(Victim, 100, nullptr, nullptr, nullptr);
	TestEqual(TEXT("Exactly two extras join existing work"), Nearest->GetPendingNPCCount(), 5);
	TestWorld.Advance(0.1f);
	if (!TestEqual(TEXT("Both extras precede the next normal emission"), Observer->SpawnEvents, 3)) { return false; }
	const int32 ExtraId = Observer->SpawnRequestIds[1];
	TestTrue(TEXT("Extras have their own request"), ExtraId != NormalId && ExtraId != FollowingId);
	TestEqual(TEXT("Both extras belong to one request"), Observer->SpawnRequestIds[2], ExtraId);
	TestEqual(TEXT("Normal interval remains configured"), Nearest->SpawnSettings.SpawnInterval, 1.0f);
	TestEqual(TEXT("Farther point remains idle"), Farther->GetSpawnedNPCs().Num(), 0);
	for (int32 Index = 1; Index <= 2; ++Index)
	{
		TestTrue(TEXT("Extras originate within the nearest portal radius"), FVector::Dist2D(Observer->SpawnLocations[Index], Nearest->GetActorLocation()) <= 600.1);
		TestTrue(TEXT("Replacement retains the defeated class"), Nearest->GetSpawnedNPCs()[Index]->IsA<AChasingNPC>());
	}
	TestWorld.Advance(3.1f);
	if (!TestEqual(TEXT("All four normal NPCs plus two extras complete"), Observer->SpawnEvents, 6)) { return false; }
	TestEqual(TEXT("Original request resumes"), Observer->SpawnRequestIds[3], NormalId);
	TestEqual(TEXT("Original next due time was neither reset nor advanced"), Observer->SpawnTimes[3] - FirstNormalTime, 1.0, 0.04);
	TestEqual(TEXT("Original third NPC is retained"), Observer->SpawnRequestIds[4], NormalId);
	TestEqual(TEXT("Following normal request retains FIFO order"), Observer->SpawnRequestIds[5], FollowingId);
	TestEqual(TEXT("Every request completes once"), Observer->Results.Num(), 3);
	TestEqual(TEXT("Queue fully drains"), Nearest->GetPendingNPCCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCReplacementBlockedTest, "MyProject6.NPC.SpawnPoint.BlockedReplacementAndCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCReplacementBlockedTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	ANPCSpawnPoint* Point = TestWorld.Point(TEXT("BlockedExtras"));
	TStrongObjectPtr<UNPCSpawnPointTestObserver> Observer(NewObject<UNPCSpawnPointTestObserver>());
	Observe(Point, Observer.Get());
	FNPCSpawnSettings Normal = Point->SpawnSettings;
	Normal.bSnapToGround = false;
	Normal.SpawnInterval = 0.1f;
	Point->SpawnSettings.MaxBlockedAttempts = 3;
	Point->SpawnSettings.RetryInterval = 0.1f;
	Point->SpawnNPCsWithSettings(ACharacter::StaticClass(), 3, Normal);
	AChasingNPC* Source = TestWorld.World->SpawnActor<AChasingNPC>(FVector(4000, 0, 100), FRotator::ZeroRotator);
	Point->SpawnReplacementNPCs(Source, 2); // Missing ground blocks only the extra request.
	Source->Destroy(); // Queued presentation must survive destruction.
	TestWorld.Advance(0.5f);
	TestEqual(TEXT("Blocked extras cannot stall normal work"), Point->GetSpawnedNPCs().Num(), 3);
	TestEqual(TEXT("Both requests report a terminal result"), Observer->Results.Num(), 2);
	TestEqual(TEXT("No infinite retry remains"), Point->GetPendingNPCCount(), 0);
	Point->SpawnSettings.bSnapToGround = false;
	Point->SpawnSettings.Mode = ENPCSpawnMode::Launch;
	Point->SpawnSettings.LaunchInterval = 0.2f;
	Point->SpawnSettings.LaunchUpSpeed = 1500;
	Source = TestWorld.World->SpawnActor<AChasingNPC>(FVector(4000, 0, 100), FRotator::ZeroRotator);
	Point->SpawnNPCsWithSettings(ACharacter::StaticClass(), 3, Normal);
	Point->SpawnReplacementNPCs(Source, 2);
	Observer->bCancelOnNextSpawn = true;
	TestWorld.Advance(0.05f);
	TestEqual(TEXT("Callback cancellation removes normal and extra work"), Point->GetPendingNPCCount(), 0);
	TestEqual(TEXT("Priority extra emits before cancellation"), Observer->SpawnEvents, 4);
	TestTrue(TEXT("Extra follows the portal launch settings"), Observer->LaunchVelocities.Last().Z > 1400);
	TestWorld.Advance(0.5f);
	TestEqual(TEXT("Cancellation leaves no delayed spawns"), Observer->SpawnEvents, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCReplacementLimitsTest, "MyProject6.NPC.SpawnPoint.ReplacementAvailabilityAndPopulation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCReplacementLimitsTest::RunTest(const FString& Parameters)
{
	FSpawnTestWorld TestWorld;
	AChasingNPC* Orphan = TestWorld.World->SpawnActor<AChasingNPC>();
	TestNull(TEXT("No nearest point exists in an empty level"), ANPCSpawnPoint::FindNearestSpawnPoint(Orphan, Orphan->GetActorLocation()));
	UGameplayStatics::ApplyDamage(Orphan, 100, nullptr, nullptr, nullptr);
	TestWorld.Advance(0.1f);
	int32 Living = 0;
	for (TActorIterator<AChasingNPC> It(TestWorld.World); It; ++It) { if (!It->IsDead()) { ++Living; } }
	TestEqual(TEXT("No spawner means no random replacements at the death"), Living, 0);
	ANPCSpawnPoint* Point = TestWorld.Point(TEXT("Population"), FVector(2000, 0, 0));
	Point->SpawnSettings.bSnapToGround = false;
	AChasingNPC* First = TestWorld.World->SpawnActor<AChasingNPC>(FVector(4000, 0, 100), FRotator::ZeroRotator);
	AChasingNPC* Second = TestWorld.World->SpawnActor<AChasingNPC>(FVector(4500, 0, 100), FRotator::ZeroRotator);
	for (AChasingNPC* NPC : { First, Second })
	{
		FindFProperty<FIntProperty>(NPC->GetClass(), TEXT("MaxActiveNPCCount"))->SetPropertyValue_InContainer(NPC, 2);
		FindFProperty<FFloatProperty>(NPC->GetClass(), TEXT("HitSoundPitch"))->SetPropertyValue_InContainer(NPC, 1.3f);
	}
	UGameplayStatics::ApplyDamage(First, 100, nullptr, nullptr, nullptr);
	TestEqual(TEXT("Living population reserves its places"), Point->GetPendingNPCCount(), 1);
	UGameplayStatics::ApplyDamage(Second, 100, nullptr, nullptr, nullptr);
	TestEqual(TEXT("Simultaneous deaths include queued population"), Point->GetPendingNPCCount(), 2);
	First->Destroy();
	Second->Destroy();
	TestWorld.Advance(0.1f);
	TestEqual(TEXT("Reserved extras spawn within the cap"), Point->GetSpawnedNPCs().Num(), 2);
	for (ACharacter* NPC : Point->GetSpawnedNPCs())
	{
		TestEqual(TEXT("Presentation snapshot survives destruction of its source"),
			FindFProperty<FFloatProperty>(NPC->GetClass(), TEXT("HitSoundPitch"))->GetPropertyValue_InContainer(NPC), 1.3f);
	}
	Point->MaxSpawnsPerFrame = 2;
	Point->SpawnSettings.SpawnInterval = 1.0f;
	Point->SpawnNPCs(AChasingNPC::StaticClass(), 40);
	AChasingNPC* Third = TestWorld.World->SpawnActor<AChasingNPC>(FVector(5000, 0, 100), FRotator::ZeroRotator);
	UGameplayStatics::ApplyDamage(Third, 100, nullptr, nullptr, nullptr);
	TestEqual(TEXT("A large normal queue does not consume the replacement population budget"), Point->GetPendingReplacementNPCCount(), 2);
	TestWorld.Advance(1.0f / 60.0f);
	TestEqual(TEXT("Both extras fit the shared frame budget before normal work"), Point->GetSpawnedNPCs().Num(), 4);
	TestEqual(TEXT("All forty planned NPCs remain queued"), Point->GetPendingNPCCount(), 40);
	return true;
}

#endif
