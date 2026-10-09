#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "NPCHitReactionComponent.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "MyProject6Character.h"
#include "NPCHealthBarComponent.h"
#include "BallProjectile.h"
#include "CannonballProjectile.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
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
#include "Materials/MaterialInstanceDynamic.h"
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
			const FVector RestLocation = NPC->GetMesh()->GetRelativeLocation();
			UMaterialInterface* OriginalSkin = NPC->GetMesh()->GetMaterial(0);
			const FVector ShotDirection = FVector(1, 1, 0).GetSafeNormal();
			TestFalse(TEXT("Idle reaction does not tick"), Reaction->IsComponentTickEnabled());
			Scene.Hit(NPC, 25.0f, ShotDirection);
			TestTrue(TEXT("Hit immediately starts a stagger"), Reaction->IsStaggering());
			TestNotNull(TEXT("Damage immediately adds the white flash overlay"), Cast<UMaterialInstanceDynamic>(NPC->GetMesh()->GetOverlayMaterial()));
			TestEqual(TEXT("Flash preserves the original skin"), NPC->GetMesh()->GetMaterial(0), OriginalSkin);
			TestTrue(TEXT("AI pauses during the stagger"), Cast<AChasingNPC>(NPC)
				? CastChecked<AChasingNPC>(NPC)->ShouldPauseChase() : CastChecked<AWhisperFollower>(NPC)->ShouldPauseFollow());
			Scene.Advance(0.12f, FPS);
			TestTrue(TEXT("The body visibly leans on impact"), !RestRotation.Equals(NPC->GetMesh()->GetRelativeRotation().Quaternion(), 0.01f));
			Scene.Advance(0.4f, FPS);
			const FVector Recoil = NPC->GetActorLocation() - Start;
			AddInfo(FString::Printf(TEXT("%s at %d FPS: recoil %.1f cm"), *Class->GetName(), FPS, Recoil.Size2D()));
			TestTrue(TEXT("Every NPC takes a restrained 28 cm backward step along the shot"),
				FVector::DotProduct(Recoil, ShotDirection) > 26 && Recoil.Size2D() < 30);
			TestTrue(TEXT("Mesh returns to its authored rotation"), RestRotation.Equals(NPC->GetMesh()->GetRelativeRotation().Quaternion(), 0.001f));
			TestFalse(TEXT("The stagger ends"), Reaction->IsStaggering());
			TestTrue(TEXT("Compression recovers the authored mesh height"), NPC->GetMesh()->GetRelativeLocation().Equals(RestLocation, .001));
			TestNull(TEXT("Flash is removed after the impact"), NPC->GetMesh()->GetOverlayMaterial());
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCDamageFlashTest, "MyProject6.NPC.DamageFlashAndRepeatedImpacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCDamageFlashTest::RunTest(const FString& Parameters)
{
	FHitReactionWorld Scene;
	ACharacter* NPC = Scene.Spawn(AChasingNPC::StaticClass());
	UNPCHitReactionComponent* Reaction = NPC->FindComponentByClass<UNPCHitReactionComponent>();
	USkeletalMeshComponent* Mesh = NPC->GetMesh();
	UMaterialInterface* Skin = Mesh->GetMaterial(0);
	UMaterialInterface* ExistingOverlay = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NPC/Materials/M_NPCContactGlow.M_NPCContactGlow"));
	Mesh->SetOverlayMaterial(ExistingOverlay);
	Reaction->HitFlashColor = FLinearColor(.1f, .3f, 1.f);
	Reaction->HitFlashIntensity = 1.5f;
	TestEqual(TEXT("Zero damage is rejected"), Scene.Hit(NPC, 0.f, FVector::ForwardVector), 0.f);
	TestEqual(TEXT("Rejected damage preserves the existing overlay"), Mesh->GetOverlayMaterial(), ExistingOverlay);
	UGameplayStatics::ApplyDamage(NPC, 1.f, nullptr, nullptr, nullptr);
	TestFalse(TEXT("Directionless damage does not invent a knockback direction"), Reaction->IsStaggering());
	auto* Flash = Cast<UMaterialInstanceDynamic>(Mesh->GetOverlayMaterial());
	if (!TestNotNull(TEXT("Directionless accepted damage still flashes"), Flash)) return false;
	TestTrue(TEXT("Editable flash color reaches the material"), Flash->K2_GetVectorParameterValue(TEXT("HitFlashColor")).Equals(Reaction->HitFlashColor));
	TestEqual(TEXT("Editable brightness reaches the material"), Flash->K2_GetScalarParameterValue(TEXT("HitFlashIntensity")), 1.5f);
	Scene.Advance(.07f);
	TestTrue(TEXT("Flash fades before it disappears"), Flash->K2_GetScalarParameterValue(TEXT("HitFlashAmount")) < 1.f);
	Scene.Hit(NPC, 1.f, FVector::ForwardVector);
	TestEqual(TEXT("Consecutive hits reuse the material instance"), Mesh->GetOverlayMaterial(), static_cast<UMaterialInterface*>(Flash));
	TestEqual(TEXT("New hit renews the flash immediately"), Flash->K2_GetScalarParameterValue(TEXT("HitFlashAmount")), 1.f);
	Scene.Advance(.06f);
	const FQuat BeforeRepeat = Mesh->GetRelativeRotation().Quaternion();
	Scene.Hit(NPC, 1.f, FVector::ForwardVector);
	TestTrue(TEXT("A repeated impact does not snap the torso to rest"), BeforeRepeat.Equals(Mesh->GetRelativeRotation().Quaternion(), .0001f));
	Scene.Advance(.5f);
	TestEqual(TEXT("Recovery restores an existing overlay exactly"), Mesh->GetOverlayMaterial(), ExistingOverlay);
	TestEqual(TEXT("Recovery preserves original skin material"), Mesh->GetMaterial(0), Skin);
	Reaction->bEnableHitFlash = false;
	Scene.Hit(NPC, 1.f, FVector::ForwardVector);
	TestTrue(TEXT("Flash can be disabled while stagger remains active"), Reaction->IsStaggering() && Mesh->GetOverlayMaterial() == ExistingOverlay);
	Reaction->bEnableHitFlash = true;
	Scene.Hit(NPC, 1000.f, FVector::ForwardVector);
	TestTrue(TEXT("Lethal damage flashes during the death reaction"), Reaction->IsDeathInFlight() && Mesh->GetOverlayMaterial() == Flash);
	Scene.Advance(.2f);
	TestEqual(TEXT("Death does not leave the white overlay stuck"), Mesh->GetOverlayMaterial(), ExistingOverlay);
	TestFalse(TEXT("Further damage to corpse cannot renew flash"), Scene.Hit(NPC, 1.f, FVector::ForwardVector) > 0.f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlackMonsterProjectileReactionTest, "MyProject6.NPC.BlackMonsterProjectileReactions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlackMonsterProjectileReactionTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadClass<AWhisperFollower>(nullptr,
		TEXT("/Game/Characters/black-monster/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C"));
	if (!TestNotNull(TEXT("Black Monster Blueprint loads"), Class)) { return false; }
	for (int32 FPS : {30, 120})
	{
		FHitReactionWorld Scene;
		AWhisperFollower* NPC = CastChecked<AWhisperFollower>(Scene.Spawn(Class));
		USkeletalMeshComponent* Mesh = NPC->GetMesh();
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		UNPCHitReactionComponent* Reaction = NPC->FindComponentByClass<UNPCHitReactionComponent>();
		const FQuat RestRotation = Mesh->GetRelativeRotation().Quaternion();
		// Real shots exercise the collision -> damage -> animation/knockback route.
		for (int32 Shot = 0; Shot < 3; ++Shot)
		{
			const FVector Before = NPC->GetActorLocation();
			const float Health = NPC->GetCurrentHealth();
			Scene.World->SpawnActor<ABallProjectile>(Before-FVector(250,0,0), FRotator::ZeroRotator);
			Scene.Advance(.16f, FPS);
			TestTrue(TEXT("Every projectile starts a new stagger"), Reaction->IsStaggering());
			TestEqual(TEXT("Each yellow projectile damages the monster"), NPC->GetCurrentHealth(), Health-25.0f);
			TestEqual(TEXT("Each projectile plays the recoil animation"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(), FName(TEXT("Anim_BlackMonster_Hit")));
			Scene.Advance(.45f, FPS);
			const double Step = NPC->GetActorLocation().X-Before.X;
			TestTrue(TEXT("Each hit takes a short backward step"), Step > 26 && Step < 30);
			TestFalse(TEXT("Monster recovers after each nonfatal shot"), Reaction->IsStaggering());
			TestTrue(TEXT("Recovery restores locomotion"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->IsA<UBlendSpace>());
			TestTrue(TEXT("Recovery restores mesh orientation"), RestRotation.Equals(Mesh->GetRelativeRotation().Quaternion(), .001));
		}
		// A hit while already recoiling restarts the animation instead of being ignored.
		Scene.Hit(NPC, 1, FVector(1,0,0));
		Scene.Advance(.12f, FPS);
		const float FirstPosition = Mesh->GetSingleNodeInstance()->GetCurrentTime();
		Scene.Hit(NPC, 1, FVector(1,0,0));
		TestTrue(TEXT("Rapid repeated hits restart recoil"), Mesh->GetSingleNodeInstance()->GetCurrentTime() < FirstPosition);
		Scene.Advance(.45f, FPS);
		// Finish with an actual red shot after reducing health to its lethal range.
		Scene.Hit(NPC, NPC->GetCurrentHealth()-40, FVector(1,0,0));
		Scene.Advance(.45f, FPS);
		const FVector DeathStart = NPC->GetActorLocation();
		Scene.World->SpawnActor<ACannonballProjectile>(DeathStart-FVector(250,0,0), FRotator::ZeroRotator);
		Scene.Advance(.2f, FPS);
		TestTrue(TEXT("Lethal projectile starts death flight"), NPC->IsDead() && Reaction->IsDeathInFlight());
		TestEqual(TEXT("Death plays the falling animation"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(), FName(TEXT("Anim_BlackMonster_Death")));
		TestFalse(TEXT("Death hides the green bar"), NPC->FindComponentByClass<UNPCHealthBarComponent>()->IsVisible());
		Scene.Advance(.2f, FPS);
		TestTrue(TEXT("Body rises during backward flight"), NPC->GetActorLocation().Z > DeathStart.Z + 35);
		Scene.Advance(1.1f, FPS);
		const double Travel = NPC->GetActorLocation().X-DeathStart.X;
		AddInfo(FString::Printf(TEXT("Black Monster at %d FPS: death flight %.1f cm"), FPS, Travel));
		TestTrue(TEXT("Lethal shot throws the monster three to five metres"), Travel > 300 && Travel < 500);
		TestFalse(TEXT("Death flight stops on landing"), Reaction->IsDeathInFlight());
		TestTrue(TEXT("Dead body lies down rather than continuing to attack"), Mesh->GetSocketLocation(TEXT("Head")).Z < DeathStart.Z);
		Scene.Advance(5, FPS);
		TestTrue(TEXT("Dead body is eventually removed"), NPC->IsActorBeingDestroyed());
	}
	return true;
}
#endif
