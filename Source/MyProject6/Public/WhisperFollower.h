#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "WhisperFollower.generated.h"

class UBlendSpace;
class UAnimSequence;
class UNPCHealthBarComponent;
class USphereComponent;
class UMaterialInterface;
class UNPCHitReactionComponent;

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

	bool ShouldPauseFollow() const;
	float GetFollowRepathInterval() const { return FollowRepathInterval; }

#if WITH_EDITOR
	/** Rebuild the interpolation data after creating or editing samples from Python. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Whisper|Animation")
	void RebuildLocomotionBlendSpace();
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Whisper|Contact")
	TObjectPtr<USphereComponent> ContactSensor;

	/** Matches the other NPCs' contact distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper|Contact", meta = (ClampMin = "48.0", Units = "cm"))
	float ContactInteractionRadius = 140.0f;

	/** Energy removed per second while touching the player, independently of the animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper|Contact", meta = (ClampMin = "0.0"))
	float ContactEnergyDrainPerSecond = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper|Contact")
	TObjectPtr<UMaterialInterface> ContactGlowMaterial;

	/** Repeats while touching the player; empty disables the claw animation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper|Contact")
	TObjectPtr<UAnimSequence> ContactAttackAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Whisper|Contact", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float ContactAttackPlayRate = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Whisper|Movement", meta = (ClampMin = "0.05", Units = "s"))
	float FollowRepathInterval = 0.25f;

	/** Idle/walk/run blend space, driven by actual horizontal velocity in cm/s. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Whisper|Animation")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whisper|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 150.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Whisper|Health")
	float CurrentHealth = 150.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Whisper|Health")
	TObjectPtr<UNPCHealthBarComponent> HealthBar;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Whisper|Hit Reaction")
	TObjectPtr<UNPCHitReactionComponent> HitReaction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whisper|Health")
	TObjectPtr<UAnimSequence> DeathAnimation;

	/** Optional recoil pose; restarts on each hit and lasts for the stagger. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Whisper|Hit Reaction")
	TObjectPtr<UAnimSequence> HitReactionAnimation;

private:
	void SetContactGlowActive(bool bActive);
	void StopContactInteraction();
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OriginalMeshMaterials;
	bool bContactGlowActive = false;
	bool bContactAttackActive = false;
	bool bHitReactionAnimationActive = false;
	bool bIsDead = false;
};
