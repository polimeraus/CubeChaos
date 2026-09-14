#include "NPCHitReactionComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

UNPCHitReactionComponent::UNPCHitReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

FVector UNPCHitReactionComponent::ResolveImpactDirection(const ACharacter& Victim,
	const FDamageEvent& Event, const AController* Instigator, const AActor* Causer)
{
	FVector Direction = FVector::ZeroVector;
	if (Event.IsOfType(FPointDamageEvent::ClassID))
	{
		Direction = static_cast<const FPointDamageEvent&>(Event).ShotDirection;
	}
	else if (Event.IsOfType(FRadialDamageEvent::ClassID))
	{
		Direction = Victim.GetActorLocation() - static_cast<const FRadialDamageEvent&>(Event).Origin;
	}
	if (Direction.IsNearlyZero() && Causer && Causer != &Victim)
	{
		Direction = Causer->GetVelocity();
		if (Direction.IsNearlyZero()) { Direction = Victim.GetActorLocation() - Causer->GetActorLocation(); }
	}
	if (Direction.IsNearlyZero() && Instigator && Instigator->GetPawn())
	{
		Direction = Victim.GetActorLocation() - Instigator->GetPawn()->GetActorLocation();
	}
	// Directionless environmental damage has no projectile knockback.
	return Direction.GetSafeNormal();
}

void UNPCHitReactionComponent::Stagger(const FVector& Direction)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || bDeathInFlight || Direction.IsNearlyZero()) { return; }
	if (!bStaggering) { RestMeshRotation = Character->GetMesh()->GetRelativeRotation().Quaternion(); }
	bStaggering = true;
	StaggerElapsed = 0.0f;
	StaggerDirection = Direction.GetSafeNormal2D();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const float FallingVelocity = Movement->IsFalling() ? Movement->Velocity.Z : 0.0f;
	if (Character->GetController()) { Character->GetController()->StopMovement(); }
	Movement->StopMovementImmediately();
	Movement->Velocity.Z = FallingVelocity;
	Character->ConsumeMovementInputVector();
	SetComponentTickEnabled(true);
}

void UNPCHitReactionComponent::ResetStagger()
{
	if (bStaggering)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			Character->GetMesh()->SetRelativeRotation(RestMeshRotation);
		}
	}
	bStaggering = false;
	SetComponentTickEnabled(false);
}

bool UNPCHitReactionComponent::StartDeathFlight(const FVector& Direction)
{
	ResetStagger();
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || Direction.IsNearlyZero()) { return false; }

	// Keep world collision for the arc and landing, but let players and shots pass the corpse.
	Character->SetActorEnableCollision(true);
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	Capsule->SetGenerateOverlapEvents(false);
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	Movement->bRunPhysicsWithNoController = true;
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = false;
	Movement->SetAvoidanceEnabled(false);
	Movement->AirControl = 0.0f;
	Movement->FallingLateralFriction = 0.0f;
	Movement->BrakingDecelerationFalling = 0.0f;
	Movement->StopMovementImmediately();
	Character->ConsumeMovementInputVector();
	Movement->SetMovementMode(MOVE_Falling);
	const FVector Velocity = Direction.GetSafeNormal2D() * FMath::Max(0.0f, DeathHorizontalSpeed)
		+ FVector::UpVector * FMath::Max(0.0f, DeathUpwardSpeed);
	Character->LaunchCharacter(Velocity, true, true);
	bDeathInFlight = true;
	AddTickPrerequisiteComponent(Movement);
	SetComponentTickEnabled(true);
	return true;
}

void UNPCHitReactionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) { SetComponentTickEnabled(false); return; }
	if (bDeathInFlight)
	{
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (Movement->IsMovingOnGround() && Movement->PendingLaunchVelocity.IsNearlyZero())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
			bDeathInFlight = false;
			SetComponentTickEnabled(false);
		}
		return;
	}
	if (!bStaggering) { SetComponentTickEnabled(false); return; }

	const float Duration = FMath::Max(0.05f, StaggerDuration);
	const float PreviousAlpha = FMath::Clamp(StaggerElapsed / Duration, 0.0f, 1.0f);
	StaggerElapsed += FMath::Max(0.0f, DeltaTime);
	const float Alpha = FMath::Clamp(StaggerElapsed / Duration, 0.0f, 1.0f);
	// Integrate an easing curve so the displacement is the same at different frame rates.
	const float DistanceFraction = FMath::Square(1.0f - PreviousAlpha) - FMath::Square(1.0f - Alpha);
	Character->AddActorWorldOffset(StaggerDirection * FMath::Max(0.0f, StaggerDistance) * DistanceFraction, true);
	const FVector LocalDirection = Character->GetActorQuat().UnrotateVector(StaggerDirection);
	const FVector LeanAxis = FVector::CrossProduct(FVector::UpVector, LocalDirection).GetSafeNormal();
	const float Lean = FMath::Sin(PI * Alpha) * FMath::DegreesToRadians(FMath::Clamp(StaggerLeanAngle, 0.0f, 20.0f));
	Character->GetMesh()->SetRelativeRotation(LeanAxis.IsNearlyZero()
		? RestMeshRotation : FQuat(LeanAxis, Lean) * RestMeshRotation);
	if (Alpha >= 1.0f) { ResetStagger(); }
}
