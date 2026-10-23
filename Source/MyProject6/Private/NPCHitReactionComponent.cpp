#include "NPCHitReactionComponent.h"
#include "NPCHitBendAnimInstance.h"
#include "NPCSleepComponent.h"
#include "NPCElectricTetherComponent.h"

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
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	ForwardBendAnimClass = TSoftClassPtr<UAnimInstance>(FSoftObjectPath(TEXT("/Game/NPC/Animations/ABP_NPCHitBend.ABP_NPCHitBend_C")));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flash(TEXT("/Game/Materials/NPC/M_NPCHitFlash.M_NPCHitFlash"));
	HitFlashMaterial = Flash.Object;
}

void UNPCHitReactionComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		AddTickPrerequisiteComponent(Character->GetCharacterMovement());
		Character->GetMesh()->AddTickPrerequisiteComponent(this);
		if (UClass* BendClass = ForwardBendAnimClass.LoadSynchronous())
		{
			Character->GetMesh()->SetOverridePostProcessAnimBP(BendClass);
		}
	}
}

bool UNPCHitReactionComponent::IsUsingForwardBend() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character && Cast<UNPCHitBendAnimInstance>(Character->GetMesh()->GetPostProcessInstance());
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

void UNPCHitReactionComponent::CancelForTether()
{
    ResetStagger();
    if(bBlastTumbling)if(auto* N=Cast<ACharacter>(GetOwner()))N->GetMesh()->SetRelativeTransform(BlastRestTransform);
    bBlastTumbling=false;RefreshTickEnabled();
}

void UNPCHitReactionComponent::Stagger(const FVector& Direction)
{
    if(auto* T=GetOwner()->FindComponentByClass<UNPCElectricTetherComponent>();T&&T->IsControlled())return;
	const auto* Sleep = GetOwner()->FindComponentByClass<UNPCSleepComponent>();
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || bDeathInFlight || Direction.IsNearlyZero()
		|| (bBlastTumbling && !(Sleep && Sleep->IsIncapacitated()))) { return; }
	if (!bStaggering)
	{
		RestMeshLocation = Character->GetMesh()->GetRelativeLocation();
	}
	// Renew the reaction from the current fold, without snapping upright on repeated hits.
	StartBendAngle = CurrentBendAngle;
	bStaggering = true;
	bSleepingStagger = Sleep && Sleep->IsIncapacitated();
	StaggerElapsed = 0.0f;
	StaggerDirection = Direction.GetSafeNormal2D();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const FVector FallingVelocity = Movement->IsFalling() ? Movement->Velocity : FVector::ZeroVector;
	const FVector PendingLaunch = Movement->PendingLaunchVelocity;
	if (Character->GetController()) { Character->GetController()->StopMovement(); }
	Movement->StopMovementImmediately();
	Movement->Velocity = FallingVelocity;
	Movement->PendingLaunchVelocity = PendingLaunch;
	Character->ConsumeMovementInputVector();
	SetComponentTickEnabled(true);
}

void UNPCHitReactionComponent::ResetStagger()
{
	if (bStaggering && !bSleepingStagger)
	{
		if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
		{
			Character->GetMesh()->SetRelativeLocation(RestMeshLocation);
		}
	}
	bStaggering = false;
	bSleepingStagger = false;
	CurrentBendAngle = StartBendAngle = 0.f;
	RefreshTickEnabled();
}

void UNPCHitReactionComponent::RefreshTickEnabled()
{
	SetComponentTickEnabled(bStaggering || bDeathInFlight || bBlastTumbling || FlashRemaining > 0.f);
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

void UNPCHitReactionComponent::StartBlastTumble(const FVector& Impulse)
{
    if(auto* T=GetOwner()->FindComponentByClass<UNPCElectricTetherComponent>();T&&T->IsControlled())return;
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character || !bEnableBlastTumble || Impulse.Size() < 180.f || BlastTumbleSpeed <= 0.f) return;
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Mesh || Mesh->IsAnySimulatingPhysics()) return;
	ResetStagger();
	if (!bBlastTumbling)
	{
		BlastRestTransform = Mesh->GetRelativeTransform();
		// The mesh origin is at the feet. Rotate around the hips, not around that origin.
		const FVector Hip = Mesh->GetBoneIndex(TEXT("pelvis")) != INDEX_NONE
			? Mesh->GetBoneLocation(TEXT("pelvis")) : Mesh->Bounds.Origin;
		BlastPivot = Mesh->GetComponentTransform().InverseTransformPosition(Hip);
		BlastRotation = FQuat::Identity;
	}
	const FVector Away = Impulse.GetSafeNormal2D(UE_SMALL_NUMBER, Character->GetActorForwardVector());
	const FVector WorldAxis = (FVector::CrossProduct(FVector::UpVector, Away) + Away * .24f).GetSafeNormal();
	BlastAxis = Character->GetActorTransform().InverseTransformVectorNoScale(WorldAxis).GetSafeNormal();
	ActiveBlastSpeed = FMath::DegreesToRadians(FMath::Clamp(BlastTumbleSpeed, 0.f, 720.f))
		* FMath::Clamp(Impulse.Size() / 1800.f, .4f, 1.35f);
	BlastElapsed = BlastRecovery = 0.f;
	bBlastTumbling = true;
	bBlastRecovering = false;
	if (Character->GetController()) Character->GetController()->StopMovement();
	Character->ConsumeMovementInputVector();
	RefreshTickEnabled();
}

void UNPCHitReactionComponent::TickBlastTumble(float DeltaTime, ACharacter& Character)
{
	const float Dt = FMath::Max(0.f, DeltaTime);
	BlastElapsed += Dt;
	auto* Movement = Character.GetCharacterMovement();
	if (!bBlastRecovering && BlastElapsed > .08f && Movement->PendingLaunchVelocity.IsNearlyZero()
		&& (Movement->IsMovingOnGround() || Movement->MovementMode == MOVE_None))
	{
		bBlastRecovering = true;
		BlastLandingRotation = BlastRotation;
	}
	if (bBlastRecovering)
	{
		BlastRecovery = FMath::Min(1.f, BlastRecovery + Dt / .28f);
		const float Ease = BlastRecovery * BlastRecovery * (3.f - 2.f * BlastRecovery);
		BlastRotation = FQuat::Slerp(BlastLandingRotation, FQuat::Identity, Ease);
	}
	else
	{
		BlastRotation = (FQuat(BlastAxis, ActiveBlastSpeed * Dt) * BlastRotation).GetNormalized();
		Character.ConsumeMovementInputVector();
	}
	const FQuat Rotation = BlastRotation * BlastRestTransform.GetRotation();
	const FVector Pivot = BlastPivot * BlastRestTransform.GetScale3D();
	const FVector Location = BlastRestTransform.GetLocation() + BlastRestTransform.GetRotation().RotateVector(Pivot)
		- Rotation.RotateVector(Pivot);
	const auto* Sleep = Character.FindComponentByClass<UNPCSleepComponent>();
	if (!Sleep || !Sleep->IsIncapacitated()) Character.GetMesh()->SetRelativeLocationAndRotation(Location, Rotation);
	// Fold at the waist in flight while the underlying animation continues to move limbs.
	CurrentBendAngle = bBlastRecovering ? 24.f * (1.f - BlastRecovery) : 24.f;
	if (bBlastRecovering && BlastRecovery >= 1.f)
	{
		if (!Sleep || !Sleep->IsIncapacitated()) Character.GetMesh()->SetRelativeTransform(BlastRestTransform);
		bBlastTumbling = bBlastRecovering = false;
		CurrentBendAngle = 0.f;
		RefreshTickEnabled();
	}
}

void UNPCHitReactionComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) { SetComponentTickEnabled(false); return; }
	if (bBlastTumbling) TickBlastTumble(DeltaTime, *Character);
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
	const float PeakAngle = FMath::Clamp(ForwardBendAngle, 0.f, 70.f);
	const float Attack = FMath::Clamp(ForwardBendAttackTime, .01f, Duration * .3f);
	const float HoldEnd = Attack + Duration * .12f;
	if (StaggerElapsed < Attack)
	{
		const float T = FMath::Clamp(StaggerElapsed / Attack, 0.f, 1.f);
		CurrentBendAngle = FMath::Lerp(StartBendAngle, PeakAngle, 1.f - FMath::Square(1.f - T));
	}
	else
	{
		const float T = FMath::Clamp((StaggerElapsed - HoldEnd) / (Duration - HoldEnd), 0.f, 1.f);
		CurrentBendAngle = PeakAngle * (1.f - T * T * (3.f - 2.f * T));
	}
	const float Compression = -FMath::Clamp(StaggerCompression, 0.f, 6.f)
		* (PeakAngle > SMALL_NUMBER ? CurrentBendAngle / PeakAngle : 0.f);
	if (!bSleepingStagger) Character->GetMesh()->SetRelativeLocation(RestMeshLocation + FVector(0, 0, Compression));
	if (Alpha >= 1.0f) { ResetStagger(); }
}
