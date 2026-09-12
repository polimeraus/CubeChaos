#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LaunchPad.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class ACharacter;

/** Square floor pad. Pivot is the bottom; keep the actor upright. */
UCLASS(Blueprintable)
class MYPROJECT6_API ALaunchPad : public AActor
{
    GENERATED_BODY()
public:
    ALaunchPad();
    virtual void OnConstruction(const FTransform& Transform) override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad") bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad", meta=(ClampMin="50", ClampMax="1000", Units="cm")) float Size = 160;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad", meta=(ClampMin="0", ClampMax="10000", Units="cm/s")) float LaunchUpSpeed = 1200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad", meta=(ClampMin="0", ClampMax="10000", Units="cm/s")) float ForwardSpeed = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad") bool bPreserveHorizontalVelocity = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad") bool bAffectPlayers = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad") bool bAffectNPCs = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad", meta=(ClampMin="0.1", ClampMax="10", Units="s")) float Cooldown = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad|Visual") FLinearColor Color = FLinearColor(0.02f, 0.75f, 1, 1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad|Visual", meta=(ClampMin="0", ClampMax="30")) float Brightness = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad|Visual", meta=(ClampMin="0", ClampMax="10")) float AnimationSpeed = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Launch Pad|Visual") TObjectPtr<UMaterialInterface> PadMaterial;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Launch Pad") void RefreshPad();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Launch Pad") TObjectPtr<UStaticMeshComponent> Base;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Launch Pad") TObjectPtr<UStaticMeshComponent> Symbol;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Launch Pad") TObjectPtr<UBoxComponent> Trigger;
private:
    UFUNCTION() void OnEnter(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex, bool bSweep, const FHitResult& Hit);
    UFUNCTION() void OnLeave(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent, int32 BodyIndex);
    void CheckCharacters();
    FTimerHandle CheckTimer;
    TMap<TWeakObjectPtr<ACharacter>, double> LastLaunch;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MaterialInstance;
};
