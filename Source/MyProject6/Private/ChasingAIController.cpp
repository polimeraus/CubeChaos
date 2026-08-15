// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChasingAIController.h"

#include "ChasingNPC.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"

AChasingAIController::AChasingAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	// Direct movement fallback must feed input every frame; intermittent input caused crawling.
	PrimaryActorTick.TickInterval = 0.0f;
	bAttachToPawn = true;
}

void AChasingAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	RefreshChasePath();

	const AChasingNPC* NPC = Cast<AChasingNPC>(InPawn);
	const float RefreshInterval = NPC ? FMath::Max(0.05f, NPC->GetRepathInterval()) : 0.25f;
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
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!ControlledPawn || !PlayerPawn || ControlledPawn == PlayerPawn)
	{
		StopMovement();
		PlayerTarget.Reset();
		bUseDirectMovementFallback = false;
		return;
	}

	PlayerTarget = PlayerPawn;
	SetFocus(PlayerPawn);

	AChasingNPC* NPC = Cast<AChasingNPC>(ControlledPawn);
	if (NPC)
	{
		NPC->UpdateChaseSpeedFromPlayer(Cast<ACharacter>(PlayerPawn));
	}
	const float AcceptanceRadius = NPC ? NPC->GetChaseAcceptanceRadius() : 90.0f;
	const EPathFollowingRequestResult::Type RequestResult = MoveToActor(
		PlayerPawn,
		AcceptanceRadius,
		true,
		true,
		false,
		nullptr,
		true);

	bUseDirectMovementFallback = RequestResult == EPathFollowingRequestResult::Failed;
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
	const float AcceptanceRadius = NPC ? NPC->GetChaseAcceptanceRadius() : 90.0f;
	FVector ToPlayer = PlayerTarget->GetActorLocation() - ControlledPawn->GetActorLocation();
	ToPlayer.Z = 0.0f;

	if (ToPlayer.SizeSquared() > FMath::Square(AcceptanceRadius))
	{
		ControlledPawn->AddMovementInput(ToPlayer.GetSafeNormal(), 1.0f, true);
	}
}
