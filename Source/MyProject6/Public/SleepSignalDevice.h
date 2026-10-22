#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SleepSignalDevice.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UPointLightComponent;
class UAudioComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Short underhand deployable. Only a grounded device emits; it never applies damage. */
UCLASS(Blueprintable)
class MYPROJECT6_API ASleepSignalDevice : public AActor
{
    GENERATED_BODY()
public:
    ASleepSignalDevice();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sleep Signal") TObjectPtr<USphereComponent> Collision;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sleep Signal") TObjectPtr<UStaticMeshComponent> DeviceMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Sleep Signal") TObjectPtr<UProjectileMovementComponent> Movement;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sleep Signal", meta=(ClampMin="100", ClampMax="10000", Units="cm")) float SignalSphereDiameter = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sleep Signal", meta=(ClampMin="0.2", ClampMax="300", Units="s")) float SignalDuration = 12.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sleep Signal", meta=(ClampMin="0.3", ClampMax="5", Units="s")) float PulseInterval = 1.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sleep Signal", meta=(ClampMin="0.2", ClampMax="30", Units="s")) float WakeDelay = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sleep Signal") FLinearColor SignalColor = FLinearColor(.08f,.85f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sleep Signal") FVector LaunchVelocity = FVector(260,0,70);
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sleep Signal") bool bEmitting = false;
    UFUNCTION(BlueprintPure, Category="Sleep Signal") float GetSignalRemaining() const;
    UFUNCTION(BlueprintCallable, Category="Sleep Signal|Preview") void PreviewSignal(float Seconds);
private:
    UFUNCTION() void OnBounce(const FHitResult& Hit, const FVector& Velocity);
    void EmitSignal();
    void UpdateVisual(float Seconds, float Fade = 1.f);
    UPROPERTY() TObjectPtr<UMaterialInterface> SignalMaterial;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Shells;
    UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> ShellMaterials;
    UPROPERTY() TObjectPtr<UPointLightComponent> Light;
    UPROPERTY() TObjectPtr<UAudioComponent> Audio;
    float FlightAge = 0.f;
    float EmissionAge = 0.f;
    float ScanAccumulator = 0.f;
    bool bLanded = false;
};
