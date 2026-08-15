// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ChasingAIController.generated.h"

/** Navigation-based player pursuit with a straight-line fallback when no NavMesh is available. */
UCLASS()
class MYPROJECT6_API AChasingAIController : public AAIController
{
	GENERATED_BODY()

public:
	AChasingAIController();

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RefreshChasePath();

	TWeakObjectPtr<APawn> PlayerTarget;
	FTimerHandle RepathTimerHandle;
	bool bUseDirectMovementFallback = false;
};
