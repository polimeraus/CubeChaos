// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CannonballLauncherSubsystem.generated.h"

class APlayerController;

/** Adds one cannonball shot to every right-mouse press without modifying the character Blueprint. */
UCLASS()
class MYPROJECT6_API UCannonballLauncherSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	void FireCannonball(APlayerController& PlayerController);
};
