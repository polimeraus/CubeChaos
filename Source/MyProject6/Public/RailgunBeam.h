#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RailgunVisualSettings.h"
#include "RailgunBeam.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Soft camera-facing energy trail. Hit damage is resolved independently. */
UCLASS()
class MYPROJECT6_API ARailgunBeam : public AActor
{
    GENERATED_BODY()
public:
    ARailgunBeam();
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable, Category="Railgun")
    void InitializeBeam(FVector Start, FVector End, float Duration = .55f);
    UFUNCTION(BlueprintCallable, Category="Railgun")
    void FaceCamera(FVector ViewLocation);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun")
    FRailgunVisualSettings Visuals;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Railgun")
    TObjectPtr<UDynamicMeshComponent> Trail;
private:
    UPROPERTY() TObjectPtr<UMaterialInterface> EnergyMaterial;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TrailMaterial;
    FVector ShotDirection = FVector::ForwardVector;
    float Age = 0.f;
    float Lifetime = .55f;
};
