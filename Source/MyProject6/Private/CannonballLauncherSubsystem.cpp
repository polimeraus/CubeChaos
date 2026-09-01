// Copyright Epic Games, Inc. All Rights Reserved.

#include "CannonballLauncherSubsystem.h"

#include "CannonballProjectile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

void UCannonballLauncherSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (PlayerController && PlayerController->IsLocalController() &&
		PlayerController->WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		FireCannonball(*PlayerController);
	}
}

TStatId UCannonballLauncherSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCannonballLauncherSubsystem, STATGROUP_Tickables);
}

bool UCannonballLauncherSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UCannonballLauncherSubsystem::FireCannonball(APlayerController& PlayerController)
{
	UWorld* World = GetWorld();
	APawn* Pawn = PlayerController.GetPawn();
	if (!World || !Pawn)
	{
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	PlayerController.GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector LaunchDirection = ViewRotation.Vector().GetSafeNormal();
	if (LaunchDirection.IsNearlyZero())
	{
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Pawn;
	SpawnParameters.Instigator = Pawn;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const FVector SpawnLocation = ViewLocation + LaunchDirection * 75.0f;
	World->SpawnActor<ACannonballProjectile>(
		ACannonballProjectile::StaticClass(),
		SpawnLocation,
		LaunchDirection.Rotation(),
		SpawnParameters);
}
