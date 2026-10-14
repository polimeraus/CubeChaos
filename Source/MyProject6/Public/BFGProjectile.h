#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BFGProjectile.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USoundBase;

/** Q3 BFG: fast straight plasma, a single direct hit and occluded radial falloff. */
UCLASS()
class MYPROJECT6_API ABFGProjectile : public AActor
{
    GENERATED_BODY()
public:
    ABFGProjectile();
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG") float DirectDamage = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG") float SplashDamage = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(Units="cm")) float SplashRadius = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(Units="cm/s")) float Speed = 5000.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BFG") TObjectPtr<UDynamicMeshComponent> Plasma;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="BFG") bool bExploded = false;

    void Explode(const FHitResult& Hit);
    /** Deterministic editor-only preview; does not apply damage or move a live projectile. */
    UFUNCTION(BlueprintCallable, Category="BFG|Preview")
    void PreviewEffect(float Seconds, bool bImpact, FVector ViewLocation);

private:
    UPROPERTY() TObjectPtr<UMaterialInterface> PlasmaMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<USoundBase> ImpactSound;
    float Age = 0.f;
    float ImpactAge = 0.f;
    void DrawEffect(float Seconds, bool bImpact, FVector ViewLocation);
};
