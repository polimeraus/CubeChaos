// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChasingNPC.h"
#include "NPCSpawnPoint.h"
#include "NPCHealthBarComponent.h"
#include "NPCHitReactionComponent.h"

#include "ChasingAIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MyProject6Character.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AChasingNPC::AChasingNPC()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetCanBeDamaged(true);
	HitReaction = CreateDefaultSubobject<UNPCHitReactionComponent>(TEXT("HitReaction"));
	// Ghoul's placed actor and future spawns share the same authored hit response.
	HitReaction->ForwardBendAngle = 65.f;
	HitReaction->StaggerDuration = .40f;

	AIControllerClass = AChasingAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
	HealthBar = CreateDefaultSubobject<UNPCHealthBarComponent>(TEXT("HealthBar"));
	HealthBar->SetupAttachment(GetCapsuleComponent());

	ContactSensor = CreateDefaultSubobject<USphereComponent>(TEXT("Contact Sensor"));
	ContactSensor->SetupAttachment(GetCapsuleComponent());
	ContactSensor->InitSphereRadius(ContactInteractionRadius);
	ContactSensor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContactSensor->SetCollisionObjectType(ECC_WorldDynamic);
	ContactSensor->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContactSensor->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ContactSensor->SetGenerateOverlapEvents(true);
	ContactSensor->SetCanEverAffectNavigation(false);

	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetGenerateOverlapEvents(false);
	GetMesh()->SetCanEverAffectNavigation(false);
	// Off-screen replacement waves should not keep evaluating and refreshing every skeleton.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	GetMesh()->bEnableUpdateRateOptimizations = true;
	GetMesh()->bUpdateOverlapsOnAnimationFinalize = false;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	Movement->MaxWalkSpeed = ChaseSpeed;
	Movement->MaxAcceleration = 4096.0f;
	Movement->BrakingDecelerationWalking = 2048.0f;
	Movement->bUseRVOAvoidance = true;
	Movement->AvoidanceConsiderationRadius = 500.0f;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> CharacterMesh(
		TEXT("/Game/NPC/Ghoul/Mesh/SK_Ghoul_UE5.SK_Ghoul_UE5"));
	if (CharacterMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(CharacterMesh.Object);
	}

	// Ghoul's UE5 skeleton is compatible with this locomotion blueprint (SetupGhoulChaser.py).
	// Keep the mesh on the class default so death replacements also use Ghoul.
	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimationBlueprint(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (AnimationBlueprint.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimationBlueprint.Class);
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> DefaultContactAttack(
		TEXT("/Game/NPC/Ghoul/Animations/Anim_Ghoul_Claw.Anim_Ghoul_Claw"));
	if (DefaultContactAttack.Succeeded())
	{
		ContactAttackAnimation = DefaultContactAttack.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> DefaultDeathAnimation(
		TEXT("/Game/NPC/Ghoul/Animations/A_Ghoul_Death_FullFall.A_Ghoul_Death_FullFall"));
	if (DefaultDeathAnimation.Succeeded())
	{
		DeathAnimation = DefaultDeathAnimation.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> DefaultHitSound(
		TEXT("/Game/NPC/Audio/S_NPC_GhoulHit.S_NPC_GhoulHit"));
	if (DefaultHitSound.Succeeded())
	{
		HitSound = DefaultHitSound.Object;
	}

	// Preserve the separate contact and falling cues.
	static ConstructorHelpers::FObjectFinder<USoundBase> DefaultContactSound(
		TEXT("/Game/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02.FirstPersonTemplateWeaponFire02"));
	if (DefaultContactSound.Succeeded())
	{
		ContactDrainSound = DefaultContactSound.Object;
		FallingSound = DefaultContactSound.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FadeBody(
		TEXT("/Game/Materials/NPC/MI_NPCDeath_Slot0.MI_NPCDeath_Slot0"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FadeClothes(
		TEXT("/Game/Materials/NPC/MI_NPCDeath_Slot1.MI_NPCDeath_Slot1"));
	if (FadeBody.Succeeded() && FadeClothes.Succeeded())
	{
		DeathFadeMaterials = { FadeBody.Object, FadeClothes.Object };
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultContactGlow(
		TEXT("/Game/Materials/NPC/M_NPCContactGlow.M_NPCContactGlow"));
	if (DefaultContactGlow.Succeeded())
	{
		ContactGlowMaterial = DefaultContactGlow.Object;
	}
}

void AChasingNPC::BeginPlay()
{
	Super::BeginPlay();

	MaxHealth = FMath::Max(1.0f, MaxHealth);
	CurrentHealth = MaxHealth;
	HealthBar->UpdateHealth(CurrentHealth, MaxHealth);
	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.0f, ChaseSpeed);
	if (const ACharacter* PlayerCharacter = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
		UpdateChaseSpeedFromPlayer(PlayerCharacter);
	}
	ContactSensor->SetSphereRadius(FMath::Max(48.0f, ContactInteractionRadius));

	ContactSensor->OnComponentBeginOverlap.AddDynamic(this, &AChasingNPC::OnContactBegin);
	ContactSensor->OnComponentEndOverlap.AddDynamic(this, &AChasingNPC::OnContactEnd);
}

bool AChasingNPC::IsStaggering() const
{
	return HitReaction && HitReaction->IsStaggering();
}

bool AChasingNPC::ShouldPauseChase() const
{
	return IsDead() || bContactAttackActive || IsStaggering();
}

void AChasingNPC::UpdateChaseSpeedFromPlayer(const ACharacter* PlayerCharacter)
{
	if (!PlayerCharacter)
	{
		return;
	}

	const UCharacterMovementComponent* PlayerMovement = PlayerCharacter->GetCharacterMovement();
	UCharacterMovementComponent* NPCMovement = GetCharacterMovement();
	if (!PlayerMovement || !NPCMovement)
	{
		return;
	}

	const float SafeFraction = FMath::Clamp(PlayerSpeedFraction, 0.0f, 2.0f);
	NPCMovement->MaxWalkSpeed = FMath::Max(0.0f, PlayerMovement->MaxWalkSpeed * SafeFraction);
}

void AChasingNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ContactDrainTimerHandle);
	GetWorldTimerManager().ClearTimer(FallingSoundTimerHandle);
	GetWorldTimerManager().ClearTimer(DeathFadeTimerHandle);
	DeathMaterialInstances.Reset();
	ContactingPlayers.Reset();
	StopContactAttack();
	Super::EndPlay(EndPlayReason);
}

void AChasingNPC::OnMovementModeChanged(
	const EMovementMode PreviousMovementMode,
	const uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	GetWorldTimerManager().ClearTimer(FallingSoundTimerHandle);
	if (GetCharacterMovement()->IsFalling() && !bIsDead)
	{
		bFallingSoundPlayed = false;
		GetWorldTimerManager().SetTimer(
			FallingSoundTimerHandle,
			this,
			&AChasingNPC::PlayFallingSoundIfStillFalling,
			FMath::Max(0.01f, FallingSoundDelay),
			false);
	}
	else
	{
		bFallingSoundPlayed = false;
	}
}

void AChasingNPC::PlayFallingSoundIfStillFalling()
{
	if (bIsDead || bFallingSoundPlayed || !GetCharacterMovement()->IsFalling() || !FallingSound)
	{
		return;
	}

	bFallingSoundPlayed = true;
	UGameplayStatics::PlaySoundAtLocation(
		this,
		FallingSound,
		GetActorLocation(),
		FallingSoundVolume,
		FallingSoundPitch);
}

void AChasingNPC::FellOutOfWorld(const UDamageType& DamageType)
{
	if (!bIsDead)
	{
		// Do not create two more NPCs below the kill plane; that would recursively multiply.
		Die(false);
		return;
	}

	Super::FellOutOfWorld(DamageType);
}

void AChasingNPC::OnContactBegin(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	AMyProject6Character* PlayerCharacter = Cast<AMyProject6Character>(OtherActor);
	if (bIsDead || !PlayerCharacter || !PlayerCharacter->IsPlayerControlled() || PlayerCharacter->IsEnergyDepleted())
	{
		return;
	}

	ContactingPlayers.Add(PlayerCharacter);
	SetContactGlowActive(true);
	UpdateContactAttack(PlayerCharacter);
	ContactSoundCooldown = 0.0f;

	if (!GetWorldTimerManager().IsTimerActive(ContactDrainTimerHandle))
	{
		const float SafeInterval = FMath::Clamp(ContactDrainInterval, 0.02f, 0.5f);
		GetWorldTimerManager().SetTimer(
			ContactDrainTimerHandle,
			this,
			&AChasingNPC::ApplyContactEnergyDrain,
			SafeInterval,
			true,
			SafeInterval);
	}
}

void AChasingNPC::OnContactEnd(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	if (AMyProject6Character* PlayerCharacter = Cast<AMyProject6Character>(OtherActor))
	{
		ContactingPlayers.Remove(PlayerCharacter);
	}

	if (ContactingPlayers.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(ContactDrainTimerHandle);
		SetContactGlowActive(false);
		StopContactAttack();
		ContactSoundCooldown = 0.0f;
	}
}

void AChasingNPC::ApplyContactEnergyDrain()
{
	const float SafeInterval = FMath::Clamp(ContactDrainInterval, 0.02f, 0.5f);
	ContactSoundCooldown -= SafeInterval;
	bool bDrainedAnyEnergy = false;
	const AMyProject6Character* AttackTarget = nullptr;
	for (auto It = ContactingPlayers.CreateIterator(); It; ++It)
	{
		AMyProject6Character* PlayerCharacter = It->Get();
		if (!PlayerCharacter || PlayerCharacter->IsEnergyDepleted())
		{
			It.RemoveCurrent();
			continue;
		}

		PlayerCharacter->DrainEnergy(FMath::Max(0.0f, ContactEnergyDrainPerSecond) * SafeInterval);
		bDrainedAnyEnergy = true;
		if (PlayerCharacter->IsEnergyDepleted()) { It.RemoveCurrent(); }
		else if (!AttackTarget) { AttackTarget = PlayerCharacter; }
	}
	UpdateContactAttack(AttackTarget);

	if (bDrainedAnyEnergy && ContactDrainSound && ContactSoundCooldown <= 0.0f)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			ContactDrainSound,
			GetActorLocation(),
			ContactSoundVolume,
			ContactSoundPitch);
		ContactSoundCooldown = FMath::Max(0.1f, ContactSoundInterval);
	}

	if (ContactingPlayers.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(ContactDrainTimerHandle);
		SetContactGlowActive(false);
	}
}

void AChasingNPC::UpdateContactAttack(const AMyProject6Character* Target)
{
	if (!Target || bIsDead || IsStaggering())
	{
		StopContactAttack();
		return;
	}
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (!bContactAttackActive)
	{
		if (!ContactAttackAnimation || CharacterMesh->GetAnimationMode() != EAnimationMode::AnimationBlueprint)
		{
			return;
		}
		ContactLocomotionClass = CharacterMesh->GetAnimClass();
		bContactAttackActive = true;
		if (GetController()) { GetController()->StopMovement(); }
		GetCharacterMovement()->StopMovementImmediately();
		ConsumeMovementInputVector();
		CharacterMesh->PlayAnimation(ContactAttackAnimation, true);
		CharacterMesh->SetPlayRate(FMath::Clamp(ContactAttackPlayRate, 0.1f, 4.0f));
	}
	const FVector Direction = Target->GetActorLocation() - GetActorLocation();
	SetActorRotation(FRotator(0.0f, Direction.Rotation().Yaw, 0.0f));
}

void AChasingNPC::StopContactAttack()
{
	if (!bContactAttackActive) { return; }
	bContactAttackActive = false;
	if (!bIsDead)
	{
		GetMesh()->SetAnimInstanceClass(ContactLocomotionClass);
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	}
	ContactLocomotionClass = nullptr;
}

void AChasingNPC::SetContactGlowActive(const bool bActive)
{
	if (bContactGlowActive == bActive || !GetMesh())
	{
		return;
	}

	if (bActive)
	{
		if (!ContactGlowMaterial)
		{
			return;
		}

		OriginalMeshMaterials.Reset();
		const int32 MaterialCount = GetMesh()->GetNumMaterials();
		OriginalMeshMaterials.Reserve(MaterialCount);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			OriginalMeshMaterials.Add(GetMesh()->GetMaterial(MaterialIndex));
			GetMesh()->SetMaterial(MaterialIndex, ContactGlowMaterial);
		}
		bContactGlowActive = true;
		return;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < OriginalMeshMaterials.Num(); ++MaterialIndex)
	{
		GetMesh()->SetMaterial(MaterialIndex, OriginalMeshMaterials[MaterialIndex]);
	}
	OriginalMeshMaterials.Reset();
	bContactGlowActive = false;
}

float AChasingNPC::TakeDamage(
	const float DamageAmount,
	const FDamageEvent& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{
	if (bIsDead || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	const float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	CurrentHealth = FMath::Max(0.0f, CurrentHealth - AppliedDamage);
	HealthBar->UpdateHealth(CurrentHealth, MaxHealth);
	if (HitSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitSound, GetActorLocation(),
			FMath::Max(0.0f, HitSoundVolume), FMath::Clamp(HitSoundPitch, 0.1f, 4.0f));
	}
	if (CurrentHealth <= 0.0f)
	{
		Die(true, UNPCHitReactionComponent::ResolveImpactDirection(*this, DamageEvent, EventInstigator, DamageCauser));
	}
	else
	{
		HitReaction->Stagger(UNPCHitReactionComponent::ResolveImpactDirection(*this, DamageEvent, EventInstigator, DamageCauser));
		if (IsStaggering()) { StopContactAttack(); }
	}

	HitReaction->PlayDamageFlash();
	return AppliedDamage;
}

void AChasingNPC::Die(const bool bShouldSpawnReplacements, const FVector& ImpactDirection)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;
	CurrentHealth = 0.0f;
	HealthBar->UpdateHealth(CurrentHealth, MaxHealth);
	SetCanBeDamaged(false);
	SetLifeSpan(0.0f);
	GetWorldTimerManager().ClearTimer(ContactDrainTimerHandle);
	GetWorldTimerManager().ClearTimer(FallingSoundTimerHandle);
	ContactingPlayers.Reset();
	SetContactGlowActive(false);
	StopContactAttack();

	if (AController* ExistingController = GetController())
	{
		ExistingController->StopMovement();
	}
	DetachFromControllerPendingDestroy();

	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	ContactSensor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OnNPCDeath.Broadcast();
	if (IsActorBeingDestroyed())
	{
		return;
	}

	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			DeathSound,
			GetActorLocation(),
			DeathSoundVolume,
			DeathSoundPitch);
	}

	if (bShouldSpawnReplacements)
	{
		SpawnReplacements();
	}

	float AnimationDuration = 0.0f;
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (DeathAnimation && CharacterMesh && CharacterMesh->GetSkeletalMeshAsset())
	{
		// Corpses must finish the death pose even if they briefly leave the camera view.
		CharacterMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		CharacterMesh->bEnableUpdateRateOptimizations = false;
		CharacterMesh->bPauseAnims = false;
		CharacterMesh->PlayAnimation(DeathAnimation, false);
		const float PlayRate = FMath::Clamp(DeathAnimationPlayRate, 0.1f, 4.0f);
		// Compensate for the asset's rate scale so the exposed rate controls actual duration.
		const float AssetRate = DeathAnimation->RateScale;
		CharacterMesh->SetPlayRate(FMath::IsNearlyZero(AssetRate) ? PlayRate : PlayRate / AssetRate);
		AnimationDuration = DeathAnimation->GetPlayLength() / PlayRate;
	}
	HitReaction->StartDeathFlight(ImpactDirection);

	const float FadeDelay = AnimationDuration + FMath::Max(0.0f, DeathHoldDuration);
	if (FadeDelay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(DeathFadeTimerHandle, this,
			&AChasingNPC::BeginDeathFade, FadeDelay, false);
	}
	else
	{
		BeginDeathFade();
	}
}

void AChasingNPC::BeginDeathFade()
{
	// A low platform can leave the corpse airborne longer than its death animation.
	if (HitReaction->IsDeathInFlight())
	{
		GetWorldTimerManager().SetTimer(DeathFadeTimerHandle, this,
			&AChasingNPC::BeginDeathFade, 0.1f, false);
		return;
	}
	USkeletalMeshComponent* CharacterMesh = GetMesh();
	if (CharacterMesh)
	{
		// Hold the final pose, including when the mesh was off-screen during death.
		if (DeathAnimation && CharacterMesh->GetSingleNodeInstance())
		{
			CharacterMesh->SetPosition(DeathAnimation->GetPlayLength(), false);
			CharacterMesh->GetSingleNodeInstance()->SetPlaying(false);
			CharacterMesh->TickAnimation(0.0f, false);
			CharacterMesh->RefreshBoneTransforms();
		}
		CharacterMesh->bPauseAnims = true;
		for (int32 Index = 0; Index < CharacterMesh->GetNumMaterials(); ++Index)
		{
			UMaterialInterface* Source = DeathFadeMaterials.IsValidIndex(Index) && DeathFadeMaterials[Index]
				? DeathFadeMaterials[Index].Get() : CharacterMesh->GetMaterial(Index);
			if (UMaterialInstanceDynamic* Material = CharacterMesh->CreateDynamicMaterialInstance(Index, Source))
			{
				Material->SetScalarParameterValue(DeathOpacityParameter, 1.0f);
				DeathMaterialInstances.Add(Material);
			}
		}
	}
	ActiveDeathFadeDuration = FMath::Max(0.1f, DeathFadeDuration);
	DeathFadeStartTime = GetWorld()->GetTimeSeconds();
	SetActorTickEnabled(true);
}

void AChasingNPC::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bIsDead)
	{
		return;
	}
	const float Alpha = FMath::Clamp(
		static_cast<float>((GetWorld()->GetTimeSeconds() - DeathFadeStartTime) / ActiveDeathFadeDuration), 0.0f, 1.0f);
	const float Opacity = 1.0f - FMath::SmoothStep(0.0f, 1.0f, Alpha);
	for (UMaterialInstanceDynamic* Material : DeathMaterialInstances)
	{
		Material->SetScalarParameterValue(DeathOpacityParameter, Opacity);
	}
	if (Alpha >= 1.0f)
	{
		Destroy();
	}
}

void AChasingNPC::SpawnReplacements()
{
	UWorld* World = GetWorld();
	const int32 RequestedReplacementCount = FMath::Clamp(ReplacementCount, 0, 8);
	if (!bSpawnReplacementsOnDeath || !World || !HasAuthority() || GetNetMode() == NM_Client || RequestedReplacementCount == 0)
	{
		return;
	}

	ANPCSpawnPoint* SpawnPoint = ANPCSpawnPoint::FindNearestSpawnPoint(this, GetActorLocation());
	if (!SpawnPoint) { return; }

	int32 SafeReplacementCount = RequestedReplacementCount;
	const int32 SafePopulationLimit = FMath::Clamp(MaxActiveNPCCount, 0, 512);
	if (SafePopulationLimit > 0)
	{
		int32 LivingNPCCount = 0;
		for (TActorIterator<AChasingNPC> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsDead())
			{
				++LivingNPCCount;
			}
		}
		// Reserve queued reinforcements too: several deaths can occur before any spawn timer runs.
		for (TActorIterator<ANPCSpawnPoint> It(World); It; ++It)
		{
			if (!It->IsActorBeingDestroyed()) { LivingNPCCount += It->GetPendingReplacementNPCCount(); }
		}
		SafeReplacementCount = FMath::Min(
			RequestedReplacementCount,
			FMath::Max(SafePopulationLimit - LivingNPCCount, 0));
		if (SafeReplacementCount == 0)
		{
			return;
		}
	}

	SpawnPoint->SpawnReplacementNPCs(this, SafeReplacementCount);
}

void FNPCReplacementPresentation::Capture(const AChasingNPC& Source)
{
	HitSound = Source.HitSound;
	HitSoundVolume = Source.HitSoundVolume;
	HitSoundPitch = Source.HitSoundPitch;
	DeathSound = Source.DeathSound;
	DeathSoundVolume = Source.DeathSoundVolume;
	DeathSoundPitch = Source.DeathSoundPitch;
	DeathAnimation = Source.DeathAnimation;
	DeathAnimationPlayRate = Source.DeathAnimationPlayRate;
	DeathHoldDuration = Source.DeathHoldDuration;
	DeathFadeDuration = Source.DeathFadeDuration;
	DeathFadeMaterials = Source.DeathFadeMaterials;
	DeathOpacityParameter = Source.DeathOpacityParameter;
	StaggerDistance = Source.HitReaction->StaggerDistance;
	StaggerDuration = Source.HitReaction->StaggerDuration;
	ForwardBendAngle = Source.HitReaction->ForwardBendAngle;
	ForwardBendAttackTime = Source.HitReaction->ForwardBendAttackTime;
	ForwardBendAnimClass = Source.HitReaction->ForwardBendAnimClass;
	StaggerCompression = Source.HitReaction->StaggerCompression;
	bEnableHitFlash = Source.HitReaction->bEnableHitFlash;
	HitFlashColor = Source.HitReaction->HitFlashColor;
	HitFlashDuration = Source.HitReaction->HitFlashDuration;
	HitFlashIntensity = Source.HitReaction->HitFlashIntensity;
	HitFlashMaterial = Source.HitReaction->HitFlashMaterial;
	DeathHorizontalSpeed = Source.HitReaction->DeathHorizontalSpeed;
	DeathUpwardSpeed = Source.HitReaction->DeathUpwardSpeed;
}

void FNPCReplacementPresentation::Apply(AChasingNPC& Target) const
{
	Target.HitSound = HitSound;
	Target.HitSoundVolume = HitSoundVolume;
	Target.HitSoundPitch = HitSoundPitch;
	Target.DeathSound = DeathSound;
	Target.DeathSoundVolume = DeathSoundVolume;
	Target.DeathSoundPitch = DeathSoundPitch;
	Target.DeathAnimation = DeathAnimation;
	Target.DeathAnimationPlayRate = DeathAnimationPlayRate;
	Target.DeathHoldDuration = DeathHoldDuration;
	Target.DeathFadeDuration = DeathFadeDuration;
	Target.DeathFadeMaterials = DeathFadeMaterials;
	Target.DeathOpacityParameter = DeathOpacityParameter;
	Target.HitReaction->StaggerDistance = StaggerDistance;
	Target.HitReaction->StaggerDuration = StaggerDuration;
	Target.HitReaction->ForwardBendAngle = ForwardBendAngle;
	Target.HitReaction->ForwardBendAttackTime = ForwardBendAttackTime;
	Target.HitReaction->ForwardBendAnimClass = ForwardBendAnimClass;
	Target.HitReaction->StaggerCompression = StaggerCompression;
	Target.HitReaction->bEnableHitFlash = bEnableHitFlash;
	Target.HitReaction->HitFlashColor = HitFlashColor;
	Target.HitReaction->HitFlashDuration = HitFlashDuration;
	Target.HitReaction->HitFlashIntensity = HitFlashIntensity;
	Target.HitReaction->HitFlashMaterial = HitFlashMaterial;
	Target.HitReaction->DeathHorizontalSpeed = DeathHorizontalSpeed;
	Target.HitReaction->DeathUpwardSpeed = DeathUpwardSpeed;
}
