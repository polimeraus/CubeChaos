#include "NPCHitReactionComponent.h"

#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

UNPCHitReactionComponent::UNPCHitReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flash(TEXT("/Game/NPC/Materials/M_NPCHitFlash.M_NPCHitFlash"));
	HitFlashMaterial = Flash.Object;
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
	if (!bStaggering)
	{
		RestMeshRotation = Character->GetMesh()->GetRelativeRotation().Quaternion();
		RestMeshLocation = Character->GetMesh()->GetRelativeLocation();
		Tilt = TiltVelocity = FVector::ZeroVector;
		Compression = CompressionVelocity = 0.f;
	}
	bStaggering = true;
	StaggerElapsed = 0.0f;
	StaggerDirection = Direction.GetSafeNormal2D();
	const FVector LocalDirection = Character->GetActorQuat().UnrotateVector(StaggerDirection);
	const FVector LeanAxis = FVector::CrossProduct(FVector::UpVector, LocalDirection).GetSafeNormal();
	// Impulses accumulate without snapping the body back to rest on rapid repeated hits.
	TiltVelocity = (TiltVelocity + LeanAxis * FMath::Clamp(StaggerLeanAngle, 0.f, 20.f) * 40.f).GetClampedToMaxSize(600.f);
	CompressionVelocity = FMath::Max(CompressionVelocity - FMath::Clamp(StaggerCompression, 0.f, 6.f) * 40.f, -200.f);
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
			Character->GetMesh()->SetRelativeLocation(RestMeshLocation);
		}
	}
	bStaggering = false;
	RefreshTickEnabled();
}

void UNPCHitReactionComponent::RefreshTickEnabled()
{
	SetComponentTickEnabled(bStaggering || bDeathInFlight || FlashRemaining > 0.f);
}

void UNPCHitReactionComponent::PlayDamageFlash()
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !bEnableHitFlash || !HitFlashMaterial) return;
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!FlashInstance) FlashInstance = UMaterialInstanceDynamic::Create(HitFlashMaterial, this);
	if (!FlashInstance) return;
	if (Mesh->GetOverlayMaterial() != FlashInstance) PreviousOverlay = Mesh->GetOverlayMaterial();
	FlashInstance->SetVectorParameterValue(TEXT("HitFlashColor"), HitFlashColor);
	FlashInstance->SetScalarParameterValue(TEXT("HitFlashIntensity"), FMath::Clamp(HitFlashIntensity, 0.f, 10.f));
	FlashInstance->SetScalarParameterValue(TEXT("HitFlashAmount"), 1.f);
	Mesh->SetOverlayMaterial(FlashInstance);
	ActiveFlashDuration = FMath::Clamp(HitFlashDuration, .02f, .5f);
	FlashRemaining = ActiveFlashDuration;
	RefreshTickEnabled();
}

void UNPCHitReactionComponent::FinishFlash()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (FlashInstance && Character->GetMesh()->GetOverlayMaterial() == FlashInstance)
			Character->GetMesh()->SetOverlayMaterial(PreviousOverlay);
	}
	PreviousOverlay = nullptr;
	FlashRemaining = 0.f;
	RefreshTickEnabled();
}

void UNPCHitReactionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FinishFlash();
	ResetStagger();
	Super::EndPlay(EndPlayReason);
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
	if (FlashRemaining > 0.f)
	{
		FlashRemaining = FMath::Max(0.f, FlashRemaining - FMath::Max(0.f, DeltaTime));
		if (FlashRemaining <= 0.f || !bEnableHitFlash) FinishFlash();
		else if (FlashInstance)
		{
			// Brief bright impact, followed by a fast smooth fade.
			const float Amount = FMath::Clamp(FlashRemaining / (ActiveFlashDuration * .75f), 0.f, 1.f);
			FlashInstance->SetScalarParameterValue(TEXT("HitFlashAmount"), Amount * Amount * (3.f - 2.f * Amount));
		}
	}
	if (bDeathInFlight)
	{
		UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		if (Movement->IsMovingOnGround() && Movement->PendingLaunchVelocity.IsNearlyZero())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
			bDeathInFlight = false;
			RefreshTickEnabled();
		}
		return;
	}
	if (!bStaggering) { RefreshTickEnabled(); return; }

	const float Duration = FMath::Max(0.05f, StaggerDuration);
	const float PreviousAlpha = FMath::Clamp(StaggerElapsed / Duration, 0.0f, 1.0f);
	StaggerElapsed += FMath::Max(0.0f, DeltaTime);
	const float Alpha = FMath::Clamp(StaggerElapsed / Duration, 0.0f, 1.0f);
	// Integrate an easing curve so the displacement is the same at different frame rates.
	const float DistanceFraction = FMath::Pow(1.0f - PreviousAlpha, 3.f) - FMath::Pow(1.0f - Alpha, 3.f);
	Character->AddActorWorldOffset(StaggerDirection * FMath::Max(0.0f, StaggerDistance) * DistanceFraction, true);
	const float StepTime = FMath::Clamp(DeltaTime, 0.f, .1f);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(StepTime * 120.f));
	const float Step = StepTime / Steps;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		TiltVelocity += (-400.f * Tilt - 30.f * TiltVelocity) * Step;
		Tilt += TiltVelocity * Step;
		Tilt = Tilt.GetClampedToMaxSize(FMath::Clamp(StaggerLeanAngle, 0.f, 20.f) * 1.35f);
		CompressionVelocity += (-400.f * Compression - 30.f * CompressionVelocity) * Step;
		Compression = FMath::Clamp(Compression + CompressionVelocity * Step, -6.f, 1.f);
	}
	Character->GetMesh()->SetRelativeRotation(FQuat::MakeFromRotationVector(Tilt * (PI / 180.f)) * RestMeshRotation);
	Character->GetMesh()->SetRelativeLocation(RestMeshLocation + FVector(0, 0, Compression));
	if (Alpha >= 1.0f) { ResetStagger(); }
}
