#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BombProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UDynamicMeshComponent;
class UPointLightComponent;
class UAudioComponent;
class USoundBase;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** A bouncing fuse bomb. Detonation affects a world-space sphere without contact or visibility requirements. */
UCLASS()
class MYPROJECT6_API ABombProjectile : public AActor
{
    GENERATED_BODY()
public:
    ABombProjectile();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bomb") TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bomb") TObjectPtr<UStaticMeshComponent> BombMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bomb") TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bomb") TObjectPtr<UDynamicMeshComponent> Effect;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bomb") TObjectPtr<UPointLightComponent> FuseLight;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Bomb") TObjectPtr<UAudioComponent> FuseAudio;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb", meta=(ClampMin="0.1", Units="s")) float FuseSeconds = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb", meta=(ClampMin="0")) float BlastDamage = 200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb", meta=(ClampMin="1", Units="cm")) float BlastRadius = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb|Damage", meta=(ClampMin="0")) float NPCDamageMultiplier = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb|Damage", meta=(ClampMin="0")) float PlayerEnergyDamage = 25.f;
    /** Pressure reaches beyond the damage zone and includes the throwing player. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb|Impulse", meta=(ClampMin="1", Units="cm")) float ImpulseRadius = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb|Impulse", meta=(ClampMin="0", Units="cm/s")) float ImpulseSpeed = 3250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb|Impulse", meta=(ClampMin="0", Units="cm/s")) float ImpulseLift = 1100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb") FVector LaunchVelocity = FVector(1250, 0, 220);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb|Destruction") float SculptureRadiusMultiplier = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb|Destruction") float SculptureDamageMultiplier = 1.5f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Bomb") bool bExploded = false;
    UFUNCTION(BlueprintPure, Category="Bomb") float GetFuseRemaining() const;
    UFUNCTION(BlueprintCallable, Category="Bomb") void Explode();
    UFUNCTION(BlueprintCallable, Category="Bomb|Preview") void PreviewEffect(float Seconds, bool bImpact, FVector ViewLocation);

private:
    UPROPERTY() TObjectPtr<UMaterialInterface> EffectMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;
    UPROPERTY() TObjectPtr<USoundBase> ExplosionSound;
    FTimerHandle FuseTimer;
    float Age = 0.f;
    float ImpactAge = 0.f;
    float VisualAccumulator = 0.f;
    bool bVisualImpact = false;
    void DrawEffect(float Seconds, bool bImpact, FVector ViewLocation);
    void ApplyBlastDamage(const FVector& Origin);
    void DamageStructures(const FVector& Origin);
    void ApplyBlastImpulse(const FVector& Origin);
};
