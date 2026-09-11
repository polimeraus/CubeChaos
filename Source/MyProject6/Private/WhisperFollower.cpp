#include "WhisperFollower.h"

#include "ChasingAIController.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AWhisperFollower::AWhisperFollower()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
	AIControllerClass = AChasingAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	GetCapsuleComponent()->InitCapsuleSize(42.0f, 100.0f);
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
}

void AWhisperFollower::BeginPlay()
{
	Super::BeginPlay();
	if (LocomotionBlendSpace)
	{
		GetMesh()->PlayAnimation(LocomotionBlendSpace, true);
	}
	// Sample movement after the movement component has updated this frame.
	AddTickPrerequisiteComponent(GetCharacterMovement());
	GetMesh()->AddTickPrerequisiteActor(this);
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
