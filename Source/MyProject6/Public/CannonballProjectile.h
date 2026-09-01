// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BallProjectile.h"
#include "CannonballProjectile.generated.h"

/** Large, gravity-driven projectile that removes chunked cells around its impact. */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API ACannonballProjectile : public ABallProjectile
{
	GENERATED_BODY()

public:
	ACannonballProjectile();

	UFUNCTION(BlueprintCallable, Category = "Cannonball|Destruction")
	void SetDestructionDiameter(float NewDiameter);

	UFUNCTION(BlueprintPure, Category = "Cannonball|Destruction")
	float GetDestructionDiameter() const { return DestructionDiameter; }

protected:
	virtual void BeginPlay() override;

	/** Diameter of the circular destruction area; 200 cm equals two metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cannonball|Destruction", meta = (ClampMin = "1.0", Units = "cm"))
	float DestructionDiameter = 200.0f;
};
