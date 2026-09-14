#pragma once

#include "CoreMinimal.h"
#include "ChasingNPC.h"
#include "RadicalMikeNPC.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class USceneComponent;

/** RadicalMike's supplied right-arm Buster, with timed ranged attacks while chasing. */
UCLASS(Blueprintable)
class MYPROJECT6_API ARadicalMikeNPC : public AChasingNPC
{
	GENERATED_BODY()
public:
	ARadicalMikeNPC();
	virtual bool ShouldPauseChase() const override { return Super::ShouldPauseChase() || bAttackInProgress; }
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RadicalMike|Weapon")
	TObjectPtr<UStaticMeshComponent> BusterWeapon;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RadicalMike|Weapon")
	TObjectPtr<USceneComponent> BusterMuzzle;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RadicalMike|Weapon")
	TObjectPtr<UPointLightComponent> MuzzleFlash;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RadicalMike|Combat")
	bool bEnableShooting = true;
	/** Seconds between attack starts; also controls the first shot's delay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RadicalMike|Combat", meta = (ClampMin = "0.8", Units = "s"))
	float FireInterval = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RadicalMike|Combat", meta = (ClampMin = "100.0", Units = "cm"))
	float FireRange = 2500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RadicalMike|Combat", meta = (ClampMin = "0.0"))
	float ShotEnergyDamage = 10.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RadicalMike|Combat", meta = (ClampMin = "100.0", Units = "cm/s"))
	float ProjectileSpeed = 2200.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RadicalMike|Animation")
	TObjectPtr<UAnimSequence> FireAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RadicalMike|Animation")
	TObjectPtr<UAnimSequence> ArmedIdleAnimation;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "RadicalMike|Animation")
	TObjectPtr<UAnimSequence> RunAnimation;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RadicalMike|Weapon")
	TObjectPtr<USoundBase> FireSound;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void TryStartAttack();
	void FireAtPlayer();
	void FinishAttack();
	void HideMuzzleFlash();
	void ScheduleAttack();
	bool CanFireAt(const ACharacter* Target, const FVector& Origin) const;
	void FaceTarget(const ACharacter* Target);
	UFUNCTION()
	void StopCombatOnDeath();
	FTimerHandle AttackTimer;
	FTimerHandle ShotTimer;
	FTimerHandle RecoverTimer;
	FTimerHandle FlashTimer;
	bool bAttackInProgress = false;
};
