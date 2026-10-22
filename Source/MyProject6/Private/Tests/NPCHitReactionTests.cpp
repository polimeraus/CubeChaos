#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "NPCHitReactionComponent.h"
#include "NPCHitBendAnimInstance.h"
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
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
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
		LoadClass<AChasingNPC>(nullptr, TEXT("/Game/NPC/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C")),
		LoadClass<AWhisperFollower>(nullptr, TEXT("/Game/NPC/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"))
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
			TestTrue(TEXT("The waist visibly folds on impact"), Reaction->GetCurrentForwardBendAngle() > 35.f);
			TestTrue(TEXT("Forward bend post-process is installed"), Reaction->IsUsingForwardBend());
			TestTrue(TEXT("Impact does not tip the whole character backwards"), RestRotation.Equals(NPC->GetMesh()->GetRelativeRotation().Quaternion(), .001f));
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
	UMaterialInterface* ExistingOverlay = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/NPC/M_NPCContactGlow.M_NPCContactGlow"));
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
		TEXT("/Game/NPC/BlackMonster/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C"));
	if (!TestNotNull(TEXT("Black Monster Blueprint loads"), Class)) { return false; }
	for (int32 FPS : {30, 120})
	{
		FHitReactionWorld Scene;
		AWhisperFollower* NPC = CastChecked<AWhisperFollower>(Scene.Spawn(Class));
		USkeletalMeshComponent* Mesh = NPC->GetMesh();
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		UNPCHitReactionComponent* Reaction = NPC->FindComponentByClass<UNPCHitReactionComponent>();
		const FQuat RestRotation = Mesh->GetRelativeRotation().Quaternion();
		TestEqual(TEXT("New Black Monster instances inherit Whisper's strong fold"), Reaction->ForwardBendAngle, 70.f);
		TestEqual(TEXT("New Black Monster instances inherit the longer recovery"), Reaction->StaggerDuration, .42f);
		// Real shots exercise the collision -> damage -> animation/knockback route.
		for (int32 Shot = 0; Shot < 3; ++Shot)
		{
			const FVector Before = NPC->GetActorLocation();
			const float Health = NPC->GetCurrentHealth();
			Scene.World->SpawnActor<ABallProjectile>(Before-FVector(250,0,0), FRotator::ZeroRotator);
			Scene.Advance(.16f, FPS);
			TestTrue(TEXT("Every projectile starts a new stagger"), Reaction->IsStaggering());
			TestEqual(TEXT("Each yellow projectile damages the monster"), NPC->GetCurrentHealth(), Health-25.0f);
			TestTrue(TEXT("Each projectile folds the torso over locomotion"), Reaction->IsUsingForwardBend()
				&& Reaction->GetCurrentForwardBendAngle() > 0.f && Mesh->GetSingleNodeInstance()->GetCurrentAsset()->IsA<UBlendSpace>());
			Scene.Advance(.45f, FPS);
			const double Step = NPC->GetActorLocation().X-Before.X;
			TestTrue(TEXT("Each hit takes a short backward step"), Step > 26 && Step < 30);
			TestFalse(TEXT("Monster recovers after each nonfatal shot"), Reaction->IsStaggering());
			TestTrue(TEXT("Recovery restores locomotion"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->IsA<UBlendSpace>());
			TestTrue(TEXT("Recovery restores mesh orientation"), RestRotation.Equals(Mesh->GetRelativeRotation().Quaternion(), .001));
		}
		// A hit while already recoiling renews the bend without snapping upright.
		Scene.Hit(NPC, 1, FVector(1,0,0));
		Scene.Advance(.12f, FPS);
		const float FirstBend = Reaction->GetCurrentForwardBendAngle();
		Scene.Hit(NPC, 1, FVector(1,0,0));
		TestEqual(TEXT("Rapid repeated hits preserve the current fold"), Reaction->GetCurrentForwardBendAngle(), FirstBend);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FClownAggressiveApproachTest, "MyProject6.NPC.ClownAggressiveApproach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClownAggressiveApproachTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadClass<AWhisperFollower>(nullptr,
		TEXT("/Game/NPC/ClownMonster/BP_ClownMonster_NPC.BP_ClownMonster_NPC_C"));
	if (!TestNotNull(TEXT("Clown class loads"), Class)) return false;
	UClass* PlayerClass = LoadClass<AMyProject6Character>(nullptr,
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Playable character class loads"), PlayerClass)) return false;
	for (int32 FPS : {30, 60, 120})
	{
		FHitReactionWorld Scene;
		auto* Player = CastChecked<AMyProject6Character>(Scene.Spawn(PlayerClass, FVector(3500, 0, 102)));
		auto* Controller = Scene.World->SpawnActor<APlayerController>();
		Controller->SetAsLocalPlayerController();
		Controller->Possess(Player);
		Player->GetCharacterMovement()->DisableMovement();
		auto* NPC = CastChecked<AWhisperFollower>(Scene.Spawn(Class));
		auto* Mesh = NPC->GetMesh();
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		TestEqual(TEXT("Spawned Clown inherits faster pursuit"), NPC->GetCharacterMovement()->MaxWalkSpeed, 540.f);
		TestEqual(TEXT("Spawned Clown tracks the player every tenth of a second"), NPC->GetFollowRepathInterval(), .1f);
		bool bOnlyLocomotion = true;
		bool bKeepsFullSpeed = true;
		for (int32 Frame = 0; Frame < FPS * 2; ++Frame)
		{
			Scene.Advance(1.f / FPS, FPS);
			bOnlyLocomotion &= Mesh->GetSingleNodeInstance()->GetCurrentAsset()->IsA<UBlendSpace>();
			if (Frame > FPS / 2) bKeepsFullSpeed &= NPC->GetVelocity().Size2D() > 500.f;
		}
		TestTrue(TEXT("Distant pursuit never switches to an attack on any frame"), bOnlyLocomotion);
		TestTrue(TEXT("Distant pursuit does not repeatedly brake and accelerate"), bKeepsFullSpeed);
		TestEqual(TEXT("Distant pursuit does not damage the player"), Player->GetCurrentEnergy(), Player->GetMaxEnergy());
		NPC->GetCharacterMovement()->DisableMovement();
		const float ContactEdge = 140.f + Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
		Player->SetActorLocation(NPC->GetActorLocation() + FVector(ContactEdge + 30.f, 0, 0));
		const float Energy = Player->GetCurrentEnergy();
		Scene.Advance(1.f / FPS, FPS);
		auto* Animation = Mesh->GetSingleNodeInstance();
		TestTrue(TEXT("The former anticipation zone now plays only locomotion"), Animation->GetCurrentAsset()->IsA<UBlendSpace>());
		TestFalse(TEXT("Outside weapon range Clown remains free to follow"), NPC->ShouldPauseFollow());
		TestEqual(TEXT("No damage outside weapon range"), Player->GetCurrentEnergy(), Energy);
		Player->SetActorLocation(NPC->GetActorLocation() + FVector(ContactEdge - 10.f, 0, 0));
		Scene.Advance(1.f / FPS, FPS);
		TestEqual(TEXT("Swing starts on the first frame inside weapon range"), Animation->GetCurrentAsset()->GetFName(), FName(TEXT("anim_attack_1")));
		TestEqual(TEXT("The faster swing is active"), Mesh->GetPlayRate(), 1.8f);
		TestTrue(TEXT("Clown stops moving while attacking in range"), NPC->ShouldPauseFollow());
		const FVector Hand = Mesh->GetSocketLocation(TEXT("hand_r"));
		Scene.Advance(.2f, FPS);
		TestTrue(TEXT("Weapon hand visibly moves immediately after entering range"), FVector::Dist(Hand, Mesh->GetSocketLocation(TEXT("hand_r"))) > 10.f);
		TestTrue(TEXT("Weapon contact damages the player"), Player->GetCurrentEnergy() < Energy);
		for (int32 Repeat = 0; Repeat < 3; ++Repeat)
		{
			Player->SetActorLocation(NPC->GetActorLocation() + FVector(ContactEdge + 1.f, 0, 0));
			Scene.Advance(1.f / FPS, FPS);
			TestTrue(TEXT("Leaving by even one centimetre cancels the swing on the next frame"), Animation->GetCurrentAsset()->IsA<UBlendSpace>());
			TestEqual(TEXT("Walking restores normal animation rate"), Mesh->GetPlayRate(), 1.f);
			TestFalse(TEXT("Exiting range releases pursuit"), NPC->ShouldPauseFollow());
			Player->SetActorLocation(NPC->GetActorLocation() + FVector(ContactEdge - 10.f, 0, 0));
			Scene.Advance(1.f / FPS, FPS);
			TestEqual(TEXT("Re-entering range immediately restarts the weapon attack"), Animation->GetCurrentAsset()->GetFName(), FName(TEXT("anim_attack_1")));
		}
		Scene.Hit(NPC, 1.f, FVector::ForwardVector);
		TestTrue(TEXT("A projectile still interrupts the aggressive attack"), NPC->FindComponentByClass<UNPCHitReactionComponent>()->IsStaggering());
		Scene.Advance(.45f, FPS);
		TestEqual(TEXT("Clown promptly resumes swinging after recovery"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(), FName(TEXT("anim_attack_1")));
		Player->SetActorLocation(NPC->GetActorLocation() + FVector(0, 0, 500));
		Scene.Advance(1.f / FPS, FPS);
		TestTrue(TEXT("A target on another floor is outside weapon range"), Animation->GetCurrentAsset()->IsA<UBlendSpace>());
		Player->SetActorLocation(NPC->GetActorLocation() + FVector(1000, 0, 0));
		Scene.Advance(.05f, FPS);
		TestFalse(TEXT("Player escape releases the movement pause"), NPC->ShouldPauseFollow());
		TestTrue(TEXT("Escape restores locomotion"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->IsA<UBlendSpace>());
		const FVector BeforeChase = NPC->GetActorLocation();
		NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		Scene.Advance(.3f, FPS);
		AddInfo(FString::Printf(TEXT("Clown %d FPS: resumed %.1f cm, speed %.1f, movement mode %d"),
			FPS, NPC->GetActorLocation().X - BeforeChase.X, NPC->GetVelocity().Size2D(), int32(NPC->GetCharacterMovement()->MovementMode)));
		TestTrue(TEXT("Clown quickly resumes pursuit after escape"), NPC->GetActorLocation().X > BeforeChase.X + 60.f);
		Player->SetActorLocation(NPC->GetActorLocation() + FVector(ContactEdge - 10.f, 0, 0));
		Scene.Advance(1.f / FPS, FPS);
		Scene.Hit(NPC, 1000.f, FVector::ForwardVector);
		TestEqual(TEXT("Death replaces the weapon swing"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(), FName(TEXT("anim_death")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCForwardBendTest, "MyProject6.NPC.ForwardBendAllSkeletons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCForwardBendTest::RunTest(const FString& Parameters)
{
	struct FCase { const TCHAR* Class; const TCHAR* Head; const TCHAR* Foot; const TCHAR* Idle; };
	const FCase Cases[] = {
		{TEXT("/Script/MyProject6.ChasingNPC"), TEXT("head"), TEXT("foot_l"), TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle")},
		{TEXT("/Game/NPC/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C"), TEXT("head_01"), TEXT("foot_l"), TEXT("/Game/NPC/RadicalMike/Animations/Anim_ZMIKE_BusterIdle_R")},
		{TEXT("/Game/NPC/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"), TEXT("head"), TEXT("foot_l"), TEXT("/Game/NPC/Whisper/Animations/Anim_Whisper_Idle1")},
		{TEXT("/Game/NPC/BlackMonster/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C"), TEXT("Head"), TEXT("LeftFoot"), TEXT("/Game/NPC/BlackMonster/Animations/Anim_BlackMonster_Idle")},
		{TEXT("/Game/NPC/DemonImp/BP_DemonImp_NPC.BP_DemonImp_NPC_C"), TEXT("Character1_Head_048"), TEXT("Character1_LeftFoot_04"), TEXT("/Game/NPC/DemonImp/Animations/A_DemonImp_Idle")},
		{TEXT("/Game/NPC/DarkSorcerer/BP_DarkSorcerer_NPC.BP_DarkSorcerer_NPC_C"), TEXT("head"), TEXT("foot_l"), TEXT("/Game/NPC/DarkSorcerer/Animations/A_DS_MM_Idle")},
		{TEXT("/Game/NPC/ClownMonster/BP_ClownMonster_NPC.BP_ClownMonster_NPC_C"), TEXT("head"), TEXT("foot_l"), TEXT("/Game/NPC/ClownMonster/demo/animation/anim_idle_1")}
	};
	for (const FCase& Case : Cases)
	{
		UClass* Class = LoadClass<ACharacter>(nullptr, Case.Class);
		if (!TestNotNull(TEXT("NPC class loads"), Class)) return false;
		for (int32 FPS : {30, 60, 120})
		{
			FHitReactionWorld Scene;
			ACharacter* NPC = Scene.Spawn(Class);
			NPC->SetActorRotation(FRotator(0.f, 73.f, 0.f));
			USkeletalMeshComponent* Mesh = NPC->GetMesh();
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Mesh->bEnableUpdateRateOptimizations = false;
			// A paused real idle pose isolates bone deformation from locomotion time.
			UAnimationAsset* Idle = LoadObject<UAnimationAsset>(nullptr, Case.Idle);
			if (!TestNotNull(TEXT("NPC idle animation loads"), Idle)) return false;
			Mesh->PlayAnimation(Idle, true);
			Mesh->SetPosition(.3f, false);
			Mesh->SetPlayRate(0.f);
			Scene.Advance(.05f, FPS);
			auto ExportPose = [&](const TCHAR* Stage)
			{
				if (FPS != 60 || !FParse::Param(FCommandLine::Get(), TEXT("NPCBendPoseExport"))) return;
				const FString Directory = FPaths::ProjectSavedDir() / TEXT("NPCForwardBend/Poses");
				IFileManager::Get().MakeDirectory(*Directory, true);
				FString CSV = FString::Printf(TEXT("%s\n"), Case.Class);
				for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
				{
					const FName Name = Mesh->GetBoneName(Bone);
					const FTransform Pose = Mesh->GetSocketTransform(Name, RTS_Component);
					const FVector P = Pose.GetLocation(), S = Pose.GetScale3D();
					const FRotator R = Pose.Rotator();
					CSV += FString::Printf(TEXT("%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n"),
						*Name.ToString(), P.X, P.Y, P.Z, R.Pitch, R.Yaw, R.Roll, S.X, S.Y, S.Z);
				}
				FFileHelper::SaveStringToFile(CSV, *(Directory / (Class->GetName() + TEXT("_") + Stage + TEXT(".csv"))));
			};
			ExportPose(TEXT("Before"));
			auto* Reaction = NPC->FindComponentByClass<UNPCHitReactionComponent>();
			TestTrue(TEXT("Every skeleton receives the shared post-process"), Reaction->IsUsingForwardBend());
			const FTransform HeadBefore = Mesh->GetSocketTransform(Case.Head, RTS_Component);
			const FTransform FootBefore = Mesh->GetSocketTransform(Case.Foot, RTS_Component);
			const FQuat MeshBefore = Mesh->GetRelativeRotation().Quaternion();
			const float Height = FMath::Abs(HeadBefore.GetLocation().Z - FootBefore.GetLocation().Z);
			// A sideways projectile must still fold forwards, while pushing along the shot.
			const FVector ShotDirection = NPC->GetActorRightVector();
			const FVector Start = NPC->GetActorLocation();
			Scene.Hit(NPC, 1.f, ShotDirection);
			Scene.Advance(.09f, FPS);
			ExportPose(TEXT("Impact"));
			const FVector HeadShift = Mesh->GetComponentQuat().RotateVector(
				Mesh->GetSocketTransform(Case.Head, RTS_Component).GetLocation() - HeadBefore.GetLocation());
			const float Forward = FVector::DotProduct(HeadShift, NPC->GetActorForwardVector());
			AddInfo(FString::Printf(TEXT("%s %d FPS: fold %.1f deg, head forward %.1f, down %.1f, height %.1f"),
				*Class->GetName(), FPS, Reaction->GetCurrentForwardBendAngle(), Forward, -HeadShift.Z, Height));
			TestTrue(TEXT("Head moves clearly forward relative to the waist"), Forward > Height * .13f);
			TestTrue(TEXT("Head drops like a stomach punch"), HeadShift.Z < -Height * .03f);
			TestTrue(TEXT("Leg pose is not tilted with the torso"), FootBefore.Equals(Mesh->GetSocketTransform(Case.Foot, RTS_Component), .001f));
			TestTrue(TEXT("Mesh root is not rotated"), MeshBefore.Equals(Mesh->GetRelativeRotation().Quaternion(), .001f));
			Scene.Advance(.4f, FPS);
			ExportPose(TEXT("Recovered"));
			TestTrue(TEXT("The full incoming pose returns exactly"), HeadBefore.Equals(Mesh->GetSocketTransform(Case.Head, RTS_Component), .001f));
			TestTrue(TEXT("Every NPC retains 28 cm knockback"), FMath::IsNearlyEqual(
				FVector::DotProduct(NPC->GetActorLocation() - Start, ShotDirection), 28., 1.));
			Scene.Hit(NPC, 1.f, -NPC->GetActorForwardVector());
			Scene.Advance(.08f, FPS);
			const float PreviousBend = Reaction->GetCurrentForwardBendAngle();
			Scene.Hit(NPC, 1.f, ShotDirection);
			TestEqual(TEXT("Repeated impact keeps the current bend"), Reaction->GetCurrentForwardBendAngle(), PreviousBend);
			Scene.Advance(.06f, FPS);
			TestTrue(TEXT("Repeated impact stays within the configured angle"), Reaction->GetCurrentForwardBendAngle() <= Reaction->ForwardBendAngle + .001f);
			Scene.Hit(NPC, 10000.f, ShotDirection);
			TestEqual(TEXT("Death clears the bend before the death pose"), Reaction->GetCurrentForwardBendAngle(), 0.f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhoulFullFallTest, "MyProject6.NPC.GhoulStrongerBendAndFullFall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhoulFullFallTest::RunTest(const FString& Parameters)
{
	UAnimationAsset* Death = LoadObject<UAnimationAsset>(nullptr,
		TEXT("/Game/NPC/Ghoul/Animations/A_Ghoul_Death_FullFall"));
	if (!TestNotNull(TEXT("Complete Ghoul fall loads"), Death)) return false;
	for (int32 FPS : {30, 60, 120})
	{
		FHitReactionWorld Scene;
		ACharacter* NPC = Scene.Spawn(AChasingNPC::StaticClass());
		TestEqual(TEXT("New Ghoul instances inherit the complete fall"),
			FindFProperty<FObjectPropertyBase>(NPC->GetClass(), TEXT("DeathAnimation"))->GetObjectPropertyValue_InContainer(NPC), static_cast<UObject*>(Death));
		auto* Reaction = NPC->FindComponentByClass<UNPCHitReactionComponent>();
		TestEqual(TEXT("New Ghoul instances inherit the strong fold"), Reaction->ForwardBendAngle, 65.f);
		TestEqual(TEXT("New Ghoul instances inherit the authored recovery"), Reaction->StaggerDuration, .4f);
		USkeletalMeshComponent* Mesh = NPC->GetMesh();
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		Mesh->PlayAnimation(LoadObject<UAnimationAsset>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle")), true);
		Mesh->SetPosition(.3f, false);
		Mesh->SetPlayRate(0.f);
		Scene.Advance(.05f, FPS);
		auto Export = [&](const TCHAR* Stage)
		{
			if (FPS != 60 || !FParse::Param(FCommandLine::Get(), TEXT("NPCBendPoseExport"))) return;
			const FString Directory = FPaths::ProjectSavedDir() / TEXT("ClownGhoul/Poses");
			IFileManager::Get().MakeDirectory(*Directory, true);
			FString CSV = TEXT("/Script/MyProject6.ChasingNPC\n");
			for (int32 Bone = 0; Bone < Mesh->GetNumBones(); ++Bone)
			{
				const FName Name = Mesh->GetBoneName(Bone);
				const FTransform Pose = Mesh->GetSocketTransform(Name, RTS_Component);
				const FVector P = Pose.GetLocation(), S = Pose.GetScale3D();
				const FRotator R = Pose.Rotator();
				CSV += FString::Printf(TEXT("%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f\n"),
					*Name.ToString(), P.X, P.Y, P.Z, R.Pitch, R.Yaw, R.Roll, S.X, S.Y, S.Z);
			}
			FFileHelper::SaveStringToFile(CSV, *(Directory / (FString(TEXT("Ghoul_")) + Stage + TEXT(".csv"))));
		};
		Export(TEXT("Before"));
		const FVector HeadBefore = Mesh->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation();
		const FVector Start = NPC->GetActorLocation();
		Scene.Hit(NPC, 1.f, FVector::ForwardVector);
		Scene.Advance(.09f, FPS);
		Export(TEXT("Impact"));
		const FVector Shift = Mesh->GetComponentQuat().RotateVector(
			Mesh->GetSocketTransform(TEXT("head"), RTS_Component).GetLocation() - HeadBefore);
		AddInfo(FString::Printf(TEXT("Ghoul at %d FPS: bend %.1f, head forward %.1f, down %.1f"),
			FPS, Reaction->GetCurrentForwardBendAngle(), Shift.X, -Shift.Z));
		TestTrue(TEXT("Impact reaches the stronger 65 degree fold"), Reaction->GetCurrentForwardBendAngle() > 64.f);
		TestTrue(TEXT("Stronger fold lowers the head substantially"), Shift.Z < -30.f);
		TestTrue(TEXT("Stronger fold moves the head forward"), Shift.X > 40.f);
		Scene.Advance(.4f, FPS);
		TestTrue(TEXT("Stronger fold retains the short recoil"), FMath::IsNearlyEqual(NPC->GetActorLocation().X - Start.X, 28., 1.));
		Scene.Hit(NPC, 1.f, FVector::ForwardVector);
		Scene.Advance(.08f, FPS);
		Scene.Hit(NPC, 10000.f, FVector::ForwardVector);
		TestEqual(TEXT("Lethal hit clears a fold already in progress"), Reaction->GetCurrentForwardBendAngle(), 0.f);
		TestEqual(TEXT("Death switches to complete fall"), Mesh->GetSingleNodeInstance()->GetCurrentAsset(), Death);
		Scene.Advance(1.0f, FPS);
		Export(TEXT("Falling"));
		Scene.Advance(1.4f, FPS);
		Export(TEXT("Dead"));
		const FVector Head = Mesh->GetSocketLocation(TEXT("head"));
		const FVector Hips = Mesh->GetSocketLocation(TEXT("pelvis"));
		TestFalse(TEXT("Corpse has finished its backwards flight"), Reaction->IsDeathInFlight());
		TestTrue(TEXT("Head lies near the floor"), Head.Z > -5 && Head.Z < 40);
		TestTrue(TEXT("Pelvis lies near the floor"), Hips.Z > -5 && Hips.Z < 30);
		TestTrue(TEXT("Torso ends horizontally instead of crouching"), FMath::Abs(Head.Z-Hips.Z) < 15 && (Head-Hips).Size2D() > 50);
		Scene.Advance(3.1f, FPS);
		TestTrue(TEXT("Fade preserves the lying pose"), Mesh->GetSocketLocation(TEXT("head")).Equals(Head, 1.));
		Scene.Advance(3.f, FPS);
		TestTrue(TEXT("Ghoul corpse is cleaned up"), NPC->IsActorBeingDestroyed());
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCBlastRecoveryTest, "MyProject6.NPC.BlastTumbleRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCBlastRecoveryTest::RunTest(const FString&)
{
    for (int32 FPS : {30, 120})
    {
        FHitReactionWorld Scene;
        auto* NPC = Scene.Spawn(AChasingNPC::StaticClass());
        auto* Reaction = NPC->FindComponentByClass<UNPCHitReactionComponent>();
        const FTransform Rest = NPC->GetMesh()->GetRelativeTransform();
        const FVector Kick(950, 160, 650);
        NPC->LaunchCharacter(Kick, true, true);
        Reaction->StartBlastTumble(Kick);
        Scene.Advance(.3f, FPS);
        TestTrue(TEXT("Blast visibly rotates the body in flight"),
            Rest.GetRotation().AngularDistance(NPC->GetMesh()->GetRelativeRotation().Quaternion()) > .4f);
        TestTrue(TEXT("Capsule remains upright for swept world collision"),NPC->GetActorUpVector().Equals(FVector::UpVector,.001));
        TestTrue(TEXT("AI remains staggered during tumbling"),Reaction->IsStaggering());
        NPC->LaunchCharacter(Kick * .5f, false, false);
        Reaction->StartBlastTumble(Kick * .5f);
        Scene.Advance(3.5f, FPS);
        TestFalse(TEXT("Landing ends the blast reaction"),Reaction->IsBlastTumbling());
        TestFalse(TEXT("Living NPC can resume chasing after landing"),Reaction->IsStaggering());
        TestTrue(TEXT("Landing restores the exact mesh transform without accumulated drift"),
            Rest.Equals(NPC->GetMesh()->GetRelativeTransform(),.01));
        TestTrue(TEXT("Surviving NPC retains walking movement"),NPC->GetCharacterMovement()->IsMovingOnGround());
    }
    return true;
}
#endif
