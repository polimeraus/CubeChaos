// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChasingAIController.h"

#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "AI/NavigationSystemBase.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"

AChasingAIController::AChasingAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	// Navigation normally drives the pawn without an AIController tick. Enable ticking only
	// while the straight-line, no-NavMesh fallback actually needs per-frame movement input.
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Direct movement fallback must feed input every frame; intermittent input caused crawling.
	PrimaryActorTick.TickInterval = 0.0f;
	bAttachToPawn = true;
}

void AChasingAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	RefreshChasePath();

	const AChasingNPC* NPC = Cast<AChasingNPC>(InPawn);
	const AWhisperFollower* Follower = Cast<AWhisperFollower>(InPawn);
	const float RefreshInterval = FMath::Max(0.05f, NPC ? NPC->GetRepathInterval()
		: (Follower ? Follower->GetFollowRepathInterval() : 0.25f));
	GetWorldTimerManager().SetTimer(
		RepathTimerHandle,
		this,
		&AChasingAIController::RefreshChasePath,
		RefreshInterval,
		true,
		RefreshInterval);
}

void AChasingAIController::OnUnPossess()
{
	GetWorldTimerManager().ClearTimer(RepathTimerHandle);
	PlayerTarget.Reset();
	bUseDirectMovementFallback = false;
	SetActorTickEnabled(false);

	Super::OnUnPossess();
}

void AChasingAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RepathTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void AChasingAIController::RefreshChasePath()
{
	APawn* ControlledPawn = GetPawn();
	APawn* PlayerPawn = PlayerTarget.Get();
	if (!PlayerPawn)
	{
		PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	}
	if (!ControlledPawn || !PlayerPawn || ControlledPawn == PlayerPawn)
	{
		StopMovement();
		PlayerTarget.Reset();
		bUseDirectMovementFallback = false;
		SetActorTickEnabled(false);
		return;
	}

	PlayerTarget = PlayerPawn;
	SetFocus(PlayerPawn);

	AChasingNPC* NPC = Cast<AChasingNPC>(ControlledPawn);
	const AWhisperFollower* Follower = Cast<AWhisperFollower>(ControlledPawn);
	if ((NPC && NPC->ShouldPauseChase()) || (Follower && Follower->ShouldPauseFollow()))
	{
		// Aborting an already idle path can erase external launch velocity during sleep/knockback.
		if (GetMoveStatus() != EPathFollowingStatus::Idle) StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		bUseDirectMovementFallback = false;
		SetActorTickEnabled(false);
		return;
	}
	if (NPC)
	{
		NPC->UpdateChaseSpeedFromPlayer(Cast<ACharacter>(PlayerPawn));
	}
	// MoveToActor aborts/zeros movement on a failed path request. Without navigation
	// data, repeated requests prevented fast-repath followers from accelerating.
	// Keep direct input running and retry navigation as soon as data becomes available.
	const UNavigationSystemBase* Navigation = GetWorld()->GetNavigationSystem();
	if (!Navigation || !Navigation->GetMainNavData())
	{
		bUseDirectMovementFallback = true;
		SetActorTickEnabled(true);
		return;
	}
	const float AcceptanceRadius = NPC ? NPC->GetChaseAcceptanceRadius() : 90.0f;
	ACharacter* ControlledCharacter = Cast<ACharacter>(ControlledPawn);
	UCharacterMovementComponent* Movement = ControlledCharacter ? ControlledCharacter->GetCharacterMovement() : nullptr;
	const FVector PreviousVelocity = Movement ? Movement->Velocity : FVector::ZeroVector;
	const EPathFollowingRequestResult::Type RequestResult = MoveToActor(
		PlayerPawn,
		AcceptanceRadius,
		true,
		true,
		false,
		nullptr,
		true);

	bUseDirectMovementFallback = RequestResult == EPathFollowingRequestResult::Failed;
	if (bUseDirectMovementFallback && Movement)
	{
		// A NavMesh can exist without a usable path at this position. The failed
		// request stops movement internally; preserve pursuit momentum when handing
		// back to direct input so every repath does not restart idle/walk blending.
		Movement->Velocity = PreviousVelocity;
		Movement->UpdateComponentVelocity();
	}
	SetActorTickEnabled(bUseDirectMovementFallback);
}

void AChasingAIController::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bUseDirectMovementFallback || !PlayerTarget.IsValid())
	{
		return;
	}

	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	const AChasingNPC* NPC = Cast<AChasingNPC>(ControlledPawn);
	const AWhisperFollower* Follower = Cast<AWhisperFollower>(ControlledPawn);
	if ((NPC && NPC->ShouldPauseChase()) || (Follower && Follower->ShouldPauseFollow()))
	{
		return;
	}
	const float AcceptanceRadius = NPC ? NPC->GetChaseAcceptanceRadius() : 90.0f;
	FVector ToPlayer = PlayerTarget->GetActorLocation() - ControlledPawn->GetActorLocation();
	ToPlayer.Z = 0.0f;

	if (ToPlayer.SizeSquared() > FMath::Square(AcceptanceRadius))
	{
		ControlledPawn->AddMovementInput(ToPlayer.GetSafeNormal(), 1.0f, true);
	}
}
