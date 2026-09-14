#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "NPCHitReactionComponent.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "MyProject6Character.h"
#include "BallProjectile.h"
#include "CannonballProjectile.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
	struct FHitReactionWorld
	{
		UWorld* World;
		FHitReactionWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->SetGameInstance(NewObject<UGameInstance>(GEngine));
			World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
		}
		~FHitReactionWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
		void Box(FVector Location, FVector Extent)
		{
			AActor* Actor = World->SpawnActor<AActor>();
			UBoxComponent* Shape = NewObject<UBoxComponent>(Actor);
			Actor->SetRootComponent(Shape);
			Shape->SetBoxExtent(Extent);
			Shape->SetCollisionProfileName(TEXT("BlockAll"));
			Shape->RegisterComponent();
			Actor->SetActorLocation(Location);
		}
		void Advance(float Seconds, int32 FPS = 60)
		{
			const int32 Frames = FMath::Max(1, FMath::CeilToInt(Seconds * FPS));
			for (int32 Frame = 0; Frame < Frames; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All, Seconds / Frames); }
		}
		ACharacter* Spawn(UClass* Class, FVector Position = FVector(0, 0, 102))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACharacter* NPC = World->SpawnActor<ACharacter>(Class, Position, FRotator::ZeroRotator, Params);
			if (AChasingNPC* Chaser = Cast<AChasingNPC>(NPC))
			{
				FindFProperty<FBoolProperty>(Chaser->GetClass(), TEXT("bSpawnReplacementsOnDeath"))->SetPropertyValue_InContainer(Chaser, false);
			}
			NPC->GetCharacterMovement()->bRunPhysicsWithNoController = true;
			Advance(0.15f);
			return NPC;
		}
		float Hit(ACharacter* NPC, float Damage, FVector Direction)
		{
			FHitResult Hit(NPC, NPC->GetCapsuleComponent(), NPC->GetActorLocation(), -Direction);
			return UGameplayStatics::ApplyPointDamage(NPC, Damage, Direction, Hit, nullptr, nullptr, nullptr);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCHitReactionTest, "MyProject6.NPC.DirectionalHitReaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCHitReactionTest::RunTest(const FString& Parameters)
{
	const TArray<UClass*> Classes = {
		AChasingNPC::StaticClass(),
		LoadClass<AChasingNPC>(nullptr, TEXT("/Game/Characters/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C")),
		LoadClass<AWhisperFollower>(nullptr, TEXT("/Game/Characters/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"))
	};
	for (UClass* Class : Classes)
	{
		if (!TestNotNull(TEXT("Gameplay NPC class loads"), Class)) { return false; }
		for (int32 FPS : {30, 120})
		{
			FHitReactionWorld Scene;
			ACharacter* NPC = Scene.Spawn(Class);
			UNPCHitReactionComponent* Reaction = NPC->FindComponentByClass<UNPCHitReactionComponent>();
			if (!TestNotNull(TEXT("Actual NPC has a hit reaction component"), Reaction)) { return false; }
			const FVector Start = NPC->GetActorLocation();
			const FQuat RestRotation = NPC->GetMesh()->GetRelativeRotation().Quaternion();
			const FVector ShotDirection = FVector(1, 1, 0).GetSafeNormal();
			TestFalse(TEXT("Idle reaction does not tick"), Reaction->IsComponentTickEnabled());
			Scene.Hit(NPC, 25.0f, ShotDirection);
			TestTrue(TEXT("Hit immediately starts a stagger"), Reaction->IsStaggering());
			TestTrue(TEXT("AI pauses during the stagger"), Cast<AChasingNPC>(NPC)
				? CastChecked<AChasingNPC>(NPC)->ShouldPauseChase() : CastChecked<AWhisperFollower>(NPC)->ShouldPauseFollow());
			Scene.Advance(0.12f, FPS);
			TestTrue(TEXT("The body visibly leans on impact"), !RestRotation.Equals(NPC->GetMesh()->GetRelativeRotation().Quaternion(), 0.01f));
			Scene.Advance(0.3f, FPS);
			const FVector Recoil = NPC->GetActorLocation() - Start;
			TestTrue(TEXT("A light hit steps back 15-30 cm along the shot"),
				FVector::DotProduct(Recoil, ShotDirection) > 15 && Recoil.Size2D() < 30);
			TestTrue(TEXT("Mesh returns to its authored rotation"), RestRotation.Equals(NPC->GetMesh()->GetRelativeRotation().Quaternion(), 0.001f));
			TestFalse(TEXT("The stagger ends"), Reaction->IsStaggering());
			TestFalse(TEXT("The reaction stops ticking after recovery"), Reaction->IsComponentTickEnabled());

			// A second hit interrupts recoil, and the lethal hit's direction wins.
			Scene.Hit(NPC, 1.0f, -ShotDirection);
			Scene.Advance(0.08f, FPS);
			const FVector DeathStart = NPC->GetActorLocation();
			Scene.Hit(NPC, 1000.0f, FVector(0, -1, 0));
			TestTrue(TEXT("Lethal hit starts flight"), Reaction->IsDeathInFlight());
			TestFalse(TEXT("Lethal hit cancels the light stagger"), Reaction->IsStaggering());
			TestNull(TEXT("Dead NPC has no AI controller"), NPC->GetController());
			TestEqual(TEXT("Corpse ignores the player"), NPC->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
			TestEqual(TEXT("Corpse ignores projectiles"), NPC->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_GameTraceChannel1), ECR_Ignore);
			TestEqual(TEXT("Corpse still collides with the floor"), NPC->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Block);
			const FVector PendingVelocity = NPC->GetCharacterMovement()->PendingLaunchVelocity;
			TestEqual(TEXT("Further shots cannot relaunch a corpse"), Scene.Hit(NPC, 1000, FVector(1, 0, 0)), 0.0f);
			TestEqual(TEXT("Corpse keeps its original launch"), NPC->GetCharacterMovement()->PendingLaunchVelocity, PendingVelocity);
			Scene.Advance(0.3f, FPS);
			TestTrue(TEXT("Death visibly rises off the floor"), NPC->GetActorLocation().Z > DeathStart.Z + 35.0);
			TestTrue(TEXT("Death travels along the lethal projectile"), NPC->GetActorLocation().Y < DeathStart.Y - 100.0);
			Scene.Advance(1.0f, FPS);
			const FVector Displacement = NPC->GetActorLocation() - DeathStart;
			AddInfo(FString::Printf(TEXT("%s at %d FPS: death travel %.1f cm, landing Z %.1f"), *Class->GetName(), FPS, Displacement.Size2D(), NPC->GetActorLocation().Z));
			TestTrue(TEXT("Corpse lands 2.5-4.5 metres from the hit"), Displacement.Size2D() > 250 && Displacement.Size2D() < 450);
			TestTrue(TEXT("Corpse lands at floor height"), FMath::Abs(Displacement.Z) < 5.0);
			TestFalse(TEXT("Flight ends on landing"), Reaction->IsDeathInFlight());
			TestTrue(TEXT("Corpse stops moving after landing"), NPC->GetCharacterMovement()->MovementMode == MOVE_None);
			UAnimSingleNodeInstance* Animation = NPC->GetMesh()->GetSingleNodeInstance();
			TestNotNull(TEXT("Existing death animation remains active"), Animation);
			Scene.Advance(8.0f, FPS);
			TestTrue(TEXT("The corpse is eventually cleaned up"), NPC->IsActorBeingDestroyed());
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCHitObstructionTest, "MyProject6.NPC.HitReactionObstructionsAndProjectiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCHitObstructionTest::RunTest(const FString& Parameters)
{
	FHitReactionWorld Scene;
	ACharacter* NPC = Scene.Spawn(AChasingNPC::StaticClass());
	Scene.Box(FVector(75, 0, 200), FVector(10, 200, 200));
	for (int32 Hit = 0; Hit < 5; ++Hit)
	{
		Scene.Hit(NPC, 1, FVector(1, 0, 0));
		Scene.Advance(0.06f);
	}
	Scene.Advance(0.3f);
	TestTrue(TEXT("Repeated recoil cannot push through a wall"), NPC->GetActorLocation().X < 25.0);
	Scene.Hit(NPC, 1000, FVector(1, 0, 0));
	Scene.Advance(1.5f);
	TestTrue(TEXT("Death flight cannot pass through a wall"), NPC->GetActorLocation().X < 25.0);
	TestFalse(TEXT("Obstructed flight still lands"), NPC->FindComponentByClass<UNPCHitReactionComponent>()->IsDeathInFlight());

	for (UClass* ProjectileClass : {ABallProjectile::StaticClass(), ACannonballProjectile::StaticClass()})
	{
		ACharacter* Target = Scene.Spawn(AChasingNPC::StaticClass(), FVector(0, 1000, 102));
		Scene.World->SpawnActor<ABallProjectile>(ProjectileClass, FVector(-300, 1000, 100), FRotator::ZeroRotator);
		Scene.Advance(0.15f);
		TestTrue(TEXT("Actual yellow/red projectile starts directional recoil"), Target->FindComponentByClass<UNPCHitReactionComponent>()->IsStaggering());
		TestTrue(TEXT("Actual projectile pushes forward along its trajectory"), Target->GetActorLocation().X > 0);
		Target->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerMovementBoostTest, "MyProject6.Gameplay.FasterRunAndJump",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerMovementBoostTest::RunTest(const FString& Parameters)
{
	FHitReactionWorld Scene;
	UClass* Class = LoadClass<AMyProject6Character>(nullptr,
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Actual player Blueprint loads"), Class)) { return false; }
	ACharacter* Player = Scene.Spawn(Class);
	APlayerController* Controller = Scene.World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);
	UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
	TestEqual(TEXT("Player run speed increases from 600 to 900"), Movement->MaxWalkSpeed, 900.0f);
	TestEqual(TEXT("Player jump impulse increases from 420 to 600"), Movement->JumpZVelocity, 600.0f);
	const double GroundZ = Player->GetActorLocation().Z;
	Player->Jump();
	Scene.Advance(0.45f);
	AddInfo(FString::Printf(TEXT("Player jump: ground Z %.1f, current Z %.1f, velocity %s, movement %d"),
		GroundZ, Player->GetActorLocation().Z, *Movement->Velocity.ToString(), int32(Movement->MovementMode)));
	TestTrue(TEXT("Actual jump rises over 1.5 metres"), Player->GetActorLocation().Z > GroundZ + 150);
	return true;
}
#endif
