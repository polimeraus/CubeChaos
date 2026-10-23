#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/DynamicMeshComponent.h"
#include "ElectricTetherWeapon.generated.h"
class AMyProject6Character;
class UNPCElectricTetherComponent;
class UAudioComponent;
class UPointLightComponent;
class USoundBase;

/** Render the rope after captive movement and the final player camera update. */
UCLASS()
class UElectricTetherFieldComponent : public UDynamicMeshComponent
{
    GENERATED_BODY()
public:
    UElectricTetherFieldComponent();
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
};

/** Hold primary fire to bind a living NPC. Mouse look drives its swept spring anchor. */
UCLASS(Blueprintable)
class MYPROJECT6_API AElectricTetherWeapon : public AActor
{
    GENERATED_BODY()
public:
    AElectricTetherWeapon();
    virtual void Tick(float Delta) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Equip(AMyProject6Character* Character);
    UFUNCTION(BlueprintCallable, Category="Electric Tether") void StartBeam(bool bTrackPhysicalButton=false);
    UFUNCTION(BlueprintCallable, Category="Electric Tether") void StopBeam(bool bThrow=true);
    UFUNCTION(BlueprintPure, Category="Electric Tether") bool HasTarget() const;
    UFUNCTION(BlueprintPure, Category="Electric Tether") float GetSwingPower() const;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Electric Tether", meta=(ClampMin="300", Units="cm")) float CaptureRange=1800.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Electric Tether") bool bFiring=false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Electric Tether") TObjectPtr<UNPCElectricTetherComponent> Captive;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Electric Tether") TObjectPtr<UDynamicMeshComponent> Field;
private:
    friend class UElectricTetherFieldComponent;
    void DrawField(float Delta);
    UPROPERTY() TObjectPtr<AMyProject6Character> Wielder;
    UPROPERTY() TObjectPtr<UAudioComponent> Hum;
    UPROPERTY() TObjectPtr<UPointLightComponent> Light;
    UPROPERTY() TObjectPtr<USoundBase> CatchSound;
    UPROPERTY() TObjectPtr<USoundBase> ReleaseSound;
    FVector BeamEnd=FVector::ZeroVector,AnchorOffset=FVector::ZeroVector;
    float HoldDistance=600.f,Age=0.f,OccludedTime=0.f,ReleaseFade=0.f;
    bool bBrokenUntilRelease=false,bTrackButton=false;
};
