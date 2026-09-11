// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ChasingNPC.generated.h"

class USoundBase;
class USphereComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UAnimSequence;
class AMyProject6Character;
class ACharacter;
class UDamageType;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FChasingNPCDeathSignature);

/**
 * A lightweight enemy that continuously chases player zero.
 * It uses AChasingAIController for navigation, then plays a death pose and fades away.
 */
UCLASS(Blueprintable)
class MYPROJECT6_API AChasingNPC : public ACharacter
{
	GENERATED_BODY()

public:
	AChasingNPC();

	virtual float TakeDamage(
		float DamageAmount,
		const FDamageEvent& DamageEvent,
		AController* EventInstigator,
		AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "Chasing NPC|Health")
	float GetCurrentHealth() const { return CurrentHealth; }

	UFUNCTION(BlueprintPure, Category = "Chasing NPC|Health")
	bool IsDead() const { return bIsDead; }

	float GetChaseAcceptanceRadius() const { return ChaseAcceptanceRadius; }
	float GetRepathInterval() const { return RepathInterval; }
	virtual bool ShouldPauseChase() const { return IsDead(); }

	/** Updates movement so this NPC runs at PlayerSpeedFraction of the player's walk speed. */
	void UpdateChaseSpeedFromPlayer(const ACharacter* PlayerCharacter);

	/** Fired once when health reaches zero, before the death presentation begins. */
	UPROPERTY(BlueprintAssignable, Category = "Chasing NPC|Health")
	FChasingNPCDeathSignature OnNPCDeath;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;

	/** Detects actual capsule-range contact without changing the character's blocking collision. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chasing NPC|Contact")
	TObjectPtr<USphereComponent> ContactSensor;

	/** Matches the AI stopping distance so interaction starts as soon as the NPC arrives. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Contact", meta = (ClampMin = "48.0", Units = "cm"))
	float ContactInteractionRadius = 140.0f;

	/** Energy removed per second while this NPC touches the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Contact", meta = (ClampMin = "0.0"))
	float ContactEnergyDrainPerSecond = 20.0f;

	/** Small timer interval keeps energy drain smooth without adding another actor Tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Contact", meta = (ClampMin = "0.02", ClampMax = "0.5", Units = "s"))
	float ContactDrainInterval = 0.1f;

	/** Emissive material applied to all mesh slots while touching the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Contact")
	TObjectPtr<UMaterialInterface> ContactGlowMaterial;

	/** Short pulse repeated while this NPC is draining player energy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Contact Audio")
	TObjectPtr<USoundBase> ContactDrainSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Contact Audio", meta = (ClampMin = "0.1", Units = "s"))
	float ContactSoundInterval = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Contact Audio", meta = (ClampMin = "0.0"))
	float ContactSoundVolume = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Contact Audio", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float ContactSoundPitch = 1.8f;

	/** One default ball projectile deals 100 damage, so the default NPC dies in one hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chasing NPC|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Chasing NPC|Health")
	float CurrentHealth = 100.0f;

	/** Used only until a valid player is found. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Movement", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ChaseSpeed = 300.0f;

	/** Live multiplier of the player's MaxWalkSpeed; 0.5 means exactly half speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Movement", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float PlayerSpeedFraction = 0.5f;

	/** How close the NPC tries to get before considering the player caught. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Movement", meta = (ClampMin = "0.0", Units = "cm"))
	float ChaseAcceptanceRadius = 90.0f;

	/** Navigation path refresh rate. This avoids doing an expensive path request every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Movement", meta = (ClampMin = "0.05", Units = "s"))
	float RepathInterval = 0.25f;

	/** Played once for each successful hit, including the lethal hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Hit Audio")
	TObjectPtr<USoundBase> HitSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Hit Audio", meta = (ClampMin = "0.0"))
	float HitSoundVolume = 0.85f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Hit Audio", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float HitSoundPitch = 1.0f;

	/** Optional additional death sound. Empty by default to avoid doubling the lethal hit sound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death")
	TObjectPtr<USoundBase> DeathSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death", meta = (ClampMin = "0.0"))
	float DeathSoundVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float DeathSoundPitch = 0.75f;

	/** Non-looping sequence; its final pose remains visible during the hold and fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death")
	TObjectPtr<UAnimSequence> DeathAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float DeathAnimationPlayRate = 1.0f;

	/** Delay after the death animation has finished. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death", meta = (ClampMin = "0.0", Units = "s"))
	float DeathHoldDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death", meta = (ClampMin = "0.1", Units = "s"))
	float DeathFadeDuration = 2.0f;

	/** Fade-capable materials in mesh slot order; used only on the corpse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death", AdvancedDisplay)
	TArray<TObjectPtr<UMaterialInterface>> DeathFadeMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Death", AdvancedDisplay)
	FName DeathOpacityParameter = TEXT("NPCDeathOpacity");

	/** Played after a short continuous fall, before a possible FellOutOfWorld death. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Falling")
	TObjectPtr<USoundBase> FallingSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Falling", meta = (ClampMin = "0.0", Units = "s"))
	float FallingSoundDelay = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Falling", meta = (ClampMin = "0.0"))
	float FallingSoundVolume = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Falling", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float FallingSoundPitch = 1.15f;

	/** When enabled, killing this NPC creates new chasers at the death location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Replacement")
	bool bSpawnReplacementsOnDeath = true;

	/** The requested behavior is two replacements for every defeated NPC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Replacement", meta = (ClampMin = "0", ClampMax = "8"))
	int32 ReplacementCount = 2;

	/** Separates replacements so their capsules do not start inside each other. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Replacement", meta = (ClampMin = "100.0", Units = "cm"))
	float ReplacementSpawnRadius = 160.0f;

	/**
	 * Hard population budget for all chasing NPCs in this world. Zero removes the limit.
	 * This prevents the default two-for-one replacement rule from growing without bound.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chasing NPC|Replacement", meta = (ClampMin = "0", ClampMax = "512"))
	int32 MaxActiveNPCCount = 32;

private:
	UFUNCTION()
	void OnContactBegin(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void OnContactEnd(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex);

	void ApplyContactEnergyDrain();
	void SetContactGlowActive(bool bActive);
	void PlayFallingSoundIfStillFalling();
	void Die(bool bShouldSpawnReplacements = true);
	void BeginDeathFade();
	void SpawnReplacements();

	TSet<TWeakObjectPtr<AMyProject6Character>> ContactingPlayers;
	TArray<TObjectPtr<UMaterialInterface>> OriginalMeshMaterials;
	FTimerHandle ContactDrainTimerHandle;
	FTimerHandle FallingSoundTimerHandle;
	FTimerHandle DeathFadeTimerHandle;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DeathMaterialInstances;
	double DeathFadeStartTime = 0.0;
	float ActiveDeathFadeDuration = 2.0f;
	float ContactSoundCooldown = 0.0f;
	bool bIsDead = false;
	bool bContactGlowActive = false;
	bool bFallingSoundPlayed = false;
};
