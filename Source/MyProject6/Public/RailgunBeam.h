#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RailgunVisualSettings.h"
#include "RailgunBeam.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Instant world-space core plus expanding, independently fading spiral particles. */
UCLASS()
class MYPROJECT6_API ARailgunBeam : public AActor
{
    GENERATED_BODY()
public:
    ARailgunBeam();
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable, Category="Railgun")
    void InitializeBeam(FVector Start, FVector End, float Duration = .4f);
    UFUNCTION(BlueprintCallable, Category="Railgun")
    void FaceCamera(FVector ViewLocation);
    UFUNCTION(BlueprintCallable, Category="Railgun")
    void SetImpact(FVector Location);
    /** Deterministic presentation time, also useful for editor previews. */
    UFUNCTION(BlueprintCallable, Category="Railgun")
    void SetPreviewAge(float Seconds);
    uint32 GetParticleTopologyBuildCount() const { return ParticleTopologyBuildCount; }
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun")
    FRailgunVisualSettings Visuals;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Railgun")
    TObjectPtr<UDynamicMeshComponent> Trail;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Railgun")
    TObjectPtr<UDynamicMeshComponent> Spiral;
private:
    UPROPERTY() TObjectPtr<UMaterialInterface> EnergyMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TrailMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> ParticleMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SpiralMaterial;
    FVector ShotDirection = FVector::ForwardVector;
    FVector LastViewLocation = FVector::ZeroVector;
    FVector ImpactLocation = FVector::ZeroVector;
    bool bHasImpact = false;
    float BeamLength = 0.f;
    float Age = 0.f;
    float Lifetime = .4f;
    bool bCachedSpiral = false;
    bool bCachedSmoothSpiral = false;
    uint32 ParticleTopologyBuildCount = 0;
    void UpdateParticles(FVector ViewLocation);
};
