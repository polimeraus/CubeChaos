#include "WhisperFollower.h"

#include "ChasingAIController.h"
#include "NPCHealthBarComponent.h"
#include "NPCHitReactionComponent.h"
#include "MyProject6Character.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AWhisperFollower::AWhisperFollower()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(true);
	HitReaction = CreateDefaultSubobject<UNPCHitReactionComponent>(TEXT("HitReaction"));
	AIControllerClass = AChasingAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 100.0f);
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
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -100.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetGenerateOverlapEvents(false);
	GetMesh()->SetCanEverAffectNavigation(false);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	bUseControllerRotationYaw = false;
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	Movement->MaxWalkSpeed = 450.0f;
	Movement->MaxAcceleration = 2048.0f;
	Movement->BrakingDecelerationWalking = 2048.0f;
	Movement->bUseRVOAvoidance = true;
	Movement->AvoidanceConsiderationRadius = 400.0f;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Death(
		TEXT("/Game/Characters/Whisper/Animations/Anim_Whisper_Death.Anim_Whisper_Death"));
	if (Death.Succeeded()) { DeathAnimation = Death.Object; }
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(
		TEXT("/Game/NPC/Materials/M_NPCContactGlow.M_NPCContactGlow"));
	if (Glow.Succeeded()) { ContactGlowMaterial = Glow.Object; }
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Attack(
		TEXT("/Game/Characters/Whisper/Animations/Anim_Whisper_Attack2.Anim_Whisper_Attack2"));
	if (Attack.Succeeded()) { ContactAttackAnimation = Attack.Object; }
}

void AWhisperFollower::BeginPlay()
{
	Super::BeginPlay();
	MaxHealth = FMath::Max(1.0f, MaxHealth);
	CurrentHealth = MaxHealth;
	HealthBar->UpdateHealth(CurrentHealth, MaxHealth);
	ContactSensor->SetSphereRadius(FMath::Max(48.0f, ContactInteractionRadius), true);
	if (LocomotionBlendSpace)
	{
		GetMesh()->PlayAnimation(LocomotionBlendSpace, true);
	}
	// Sample movement after the movement component has updated this frame.
	AddTickPrerequisiteComponent(GetCharacterMovement());
	GetMesh()->AddTickPrerequisiteActor(this);
}

bool AWhisperFollower::ShouldPauseFollow() const
{
	return bIsDead || bContactAttackActive || HitReaction->IsStaggering();
}

float AWhisperFollower::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (bIsDead || DamageAmount <= 0.0f) { return 0.0f; }
	const float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (AppliedDamage <= 0.0f) { return 0.0f; }
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - AppliedDamage);
	HealthBar->UpdateHealth(CurrentHealth, MaxHealth);
	if (CurrentHealth <= 0.0f)
	{
		bIsDead = true;
		bHitReactionAnimationActive = false;
		StopContactInteraction();
		SetCanBeDamaged(false);
		if (GetController()) { GetController()->StopMovement(); }
		DetachFromControllerPendingDestroy();
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
		SetActorEnableCollision(false);
		ContactSensor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SetActorTickEnabled(false);
		float AnimationDuration = 0.0f;
		if (DeathAnimation && GetMesh()->GetSkeletalMeshAsset())
		{
			GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			GetMesh()->bEnableUpdateRateOptimizations = false;
			GetMesh()->PlayAnimation(DeathAnimation, false);
			const float RateScale = DeathAnimation->RateScale;
			GetMesh()->SetPlayRate(FMath::IsNearlyZero(RateScale) ? 1.0f : 1.0f / RateScale);
			AnimationDuration = DeathAnimation->GetPlayLength();
		}
		SetLifeSpan(AnimationDuration + 3.0f);
		HitReaction->StartDeathFlight(UNPCHitReactionComponent::ResolveImpactDirection(*this, DamageEvent, EventInstigator, DamageCauser));
	}
	else
	{
		HitReaction->Stagger(UNPCHitReactionComponent::ResolveImpactDirection(*this, DamageEvent, EventInstigator, DamageCauser));
		if (HitReaction->IsStaggering())
		{
			StopContactInteraction();
			if (HitReactionAnimation)
			{
				bHitReactionAnimationActive = true;
				GetMesh()->PlayAnimation(HitReactionAnimation, false);
				// Include asset RateScale so the last pose coincides with recovery.
				GetMesh()->SetPlayRate(HitReactionAnimation->GetPlayLength()
					/ (FMath::Max(0.05f, HitReaction->StaggerDuration)
						* FMath::Max(0.01f, HitReactionAnimation->RateScale)));
			}
		}
	}
	HitReaction->PlayDamageFlash();
	return AppliedDamage;
}

#if WITH_EDITOR
void AWhisperFollower::RebuildLocomotionBlendSpace()
{
	if (LocomotionBlendSpace)
	{
		LocomotionBlendSpace->Modify();
		LocomotionBlendSpace->ValidateSampleData();
		LocomotionBlendSpace->ResampleData();
		LocomotionBlendSpace->MarkPackageDirty();
	}
}
#endif

void AWhisperFollower::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bIsDead) { return; }
	if (bHitReactionAnimationActive && !HitReaction->IsStaggering())
	{
		bHitReactionAnimationActive = false;
		if (LocomotionBlendSpace)
		{
			GetMesh()->PlayAnimation(LocomotionBlendSpace, true);
			GetMesh()->SetPlayRate(1.0f);
		}
	}
	AMyProject6Character* Player = Cast<AMyProject6Character>(UGameplayStatics::GetPlayerCharacter(this, 0));
	const bool bTouchingPlayer = Player && Player->IsPlayerControlled() && !Player->IsEnergyDepleted()
		&& ContactSensor->IsOverlappingActor(Player);
	if (bTouchingPlayer)
	{
		Player->DrainEnergy(FMath::Max(0.0f, ContactEnergyDrainPerSecond) * FMath::Max(0.0f, DeltaSeconds));
		if (Player->IsEnergyDepleted())
		{
			StopContactInteraction();
			return;
		}
		SetContactGlowActive(true);
		if (HitReaction->IsStaggering()) { return; }
		if (ContactAttackAnimation && !bContactAttackActive)
		{
			bContactAttackActive = true;
			if (GetController()) { GetController()->StopMovement(); }
			GetCharacterMovement()->StopMovementImmediately();
			ConsumeMovementInputVector();
			GetMesh()->PlayAnimation(ContactAttackAnimation, true);
			GetMesh()->SetPlayRate(FMath::Clamp(ContactAttackPlayRate, 0.1f, 4.0f));
		}
		if (bContactAttackActive)
		{
			const FVector Direction = Player->GetActorLocation() - GetActorLocation();
			SetActorRotation(FRotator(0.0f, Direction.Rotation().Yaw, 0.0f));
			return;
		}
	}
	else { StopContactInteraction(); }
	if (UAnimSingleNodeInstance* Animation = GetMesh()->GetSingleNodeInstance())
	{
		Animation->SetBlendSpacePosition(FVector(GetVelocity().Size2D(), 0.0f, 0.0f));
	}
}

void AWhisperFollower::SetContactGlowActive(bool bActive)
{
	if (bContactGlowActive == bActive) { return; }
	if (bActive)
	{
		if (!ContactGlowMaterial) { return; }
		for (int32 Index = 0; Index < GetMesh()->GetNumMaterials(); ++Index)
		{
			OriginalMeshMaterials.Add(GetMesh()->GetMaterial(Index));
			GetMesh()->SetMaterial(Index, ContactGlowMaterial);
		}
	}
	else
	{
		for (int32 Index = 0; Index < OriginalMeshMaterials.Num(); ++Index)
		{
			GetMesh()->SetMaterial(Index, OriginalMeshMaterials[Index]);
		}
		OriginalMeshMaterials.Reset();
	}
	bContactGlowActive = bActive;
}

void AWhisperFollower::StopContactInteraction()
{
	SetContactGlowActive(false);
	if (bContactAttackActive)
	{
		bContactAttackActive = false;
		if (!bIsDead && LocomotionBlendSpace)
		{
			GetMesh()->PlayAnimation(LocomotionBlendSpace, true);
			GetMesh()->SetPlayRate(1.0f);
		}
	}
}

void AWhisperFollower::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopContactInteraction();
	Super::EndPlay(EndPlayReason);
}
