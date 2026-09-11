#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "WhisperFollower.generated.h"

class UBlendSpace;

/** Placeable companion using the project's player-following AI and Whisper locomotion. */
UCLASS(Blueprintable)
class MYPROJECT6_API AWhisperFollower : public ACharacter
{
	GENERATED_BODY()

public:
	AWhisperFollower();
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	/** Rebuild the interpolation data after creating or editing samples from Python. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Whisper|Animation")
	void RebuildLocomotionBlendSpace();
#endif

protected:
	virtual void BeginPlay() override;

	/** Idle/walk/run blend space, driven by actual horizontal velocity in cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Whisper|Animation")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;
};
