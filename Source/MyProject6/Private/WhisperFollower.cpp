#include "WhisperFollower.h"

#include "ChasingAIController.h"
#include "NPCHealthBarComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

AWhisperFollower::AWhisperFollower()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(true);
	AIControllerClass = AChasingAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 100.0f);
	HealthBar = CreateDefaultSubobject<UNPCHealthBarComponent>(TEXT("HealthBar"));
	HealthBar->SetupAttachment(GetCapsuleComponent());
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
}

void AWhisperFollower::BeginPlay()
{
	Super::BeginPlay();
	MaxHealth = FMath::Max(1.0f, MaxHealth);
	CurrentHealth = MaxHealth;
	HealthBar->UpdateHealth(CurrentHealth, MaxHealth);
	if (LocomotionBlendSpace)
	{
		GetMesh()->PlayAnimation(LocomotionBlendSpace, true);
	}
	// Sample movement after the movement component has updated this frame.
	AddTickPrerequisiteComponent(GetCharacterMovement());
	GetMesh()->AddTickPrerequisiteActor(this);
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
		SetCanBeDamaged(false);
		if (GetController()) { GetController()->StopMovement(); }
		DetachFromControllerPendingDestroy();
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
		SetActorEnableCollision(false);
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
	}
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
	if (UAnimSingleNodeInstance* Animation = GetMesh()->GetSingleNodeInstance())
	{
		Animation->SetBlendSpacePosition(FVector(GetVelocity().Size2D(), 0.0f, 0.0f));
	}
}
