#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BFGProjectile.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USoundBase;
class UPointLightComponent;

/** Fast plasma: lethal direct NPC hits and cover-aware radial falloff. */
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Destruction", meta=(ClampMin="1")) float SculptureRadiusMultiplier = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Destruction", meta=(ClampMin="1")) float SculptureDamageMultiplier = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(Units="cm/s")) float Speed = 8000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Light", meta=(ClampMin="0", Units="lm")) float FlightLightIntensity = 3600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Light", meta=(ClampMin="1", Units="cm")) float FlightLightRadius = 420.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Light", meta=(ClampMin="0", Units="lm")) float ImpactLightIntensity = 11000.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BFG") TObjectPtr<UDynamicMeshComponent> Plasma;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BFG") TObjectPtr<UPointLightComponent> PlasmaLight;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="BFG") bool bExploded = false;

    void Explode(const FHitResult& Hit);
    /** Deterministic editor-only preview; does not apply damage or move a live projectile. */
    UFUNCTION(BlueprintCallable, Category="BFG|Preview")
    void PreviewEffect(float Seconds, bool bImpact, FVector ViewLocation);
    int32 GetVisualTopologyBuildCount() const { return VisualTopologyBuildCount; }
    int32 GetVisualUpdateCount() const { return VisualUpdateCount; }

private:
    UPROPERTY() TObjectPtr<UMaterialInterface> PlasmaMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY() TObjectPtr<USoundBase> ImpactSound;
    float Age = 0.f;
    float ImpactAge = 0.f;
    float VisualAccumulator = 0.f;
    bool bVisualImpact = false;
    int32 VisualTopologyBuildCount = 0;
    int32 VisualUpdateCount = 0;
    void DrawEffect(float Seconds, bool bImpact, FVector ViewLocation);
};
