#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "WhisperFollower.generated.h"

class UBlendSpace;
class UAnimSequence;
class UNPCHealthBarComponent;

/** Placeable companion using the project's player-following AI and Whisper locomotion. */
UCLASS(Blueprintable)
class MYPROJECT6_API AWhisperFollower : public ACharacter
{
	GENERATED_BODY()

public:
	AWhisperFollower();
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Whisper|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Whisper|Health")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintPure, Category = "Whisper|Health")
	bool IsDead() const { return bIsDead; }

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whisper|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 150.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Whisper|Health")
	float CurrentHealth = 150.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Whisper|Health")
	TObjectPtr<UNPCHealthBarComponent> HealthBar;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whisper|Health")
	TObjectPtr<UAnimSequence> DeathAnimation;

private:
	bool bIsDead = false;
};
