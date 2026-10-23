#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCElectricTetherComponent.generated.h"

class ACharacter;
class AController;

UENUM(BlueprintType)
enum class EElectricCaptiveState : uint8 { Free, Captured, Thrown, Recovering };

/** Native crowd control inherited by placed NPCs, portal spawns and replacements. */
UCLASS(ClassGroup=(NPC), meta=(BlueprintSpawnableComponent))
class MYPROJECT6_API UNPCElectricTetherComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UNPCElectricTetherComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable, Category="Electric Tether") bool Capture(AActor* Source);
    UFUNCTION(BlueprintCallable, Category="Electric Tether") void SetHoldPoint(FVector Point) { HoldPoint=Point; }
    UFUNCTION(BlueprintCallable, Category="Electric Tether") void Release(bool bThrow=true);
    UFUNCTION(BlueprintPure, Category="Electric Tether") bool IsControlled() const { return State!=EElectricCaptiveState::Free; }
    UFUNCTION(BlueprintPure, Category="Electric Tether") bool IsCaptured() const { return State==EElectricCaptiveState::Captured; }
    UFUNCTION(BlueprintPure, Category="Electric Tether") FVector GetThrowVelocity() const;
    FVector FinishForDeath();
    static bool IsLivingNPC(const AActor* Actor);
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Electric Tether") EElectricCaptiveState State=EElectricCaptiveState::Free;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Electric Tether", meta=(ClampMin="0")) float ImpactDamageScale=.025f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Electric Tether", meta=(ClampMin="100")) float MinimumImpactSpeed=420.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Electric Tether", meta=(ClampMin="0.1")) float ImpactCooldown=.22f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Electric Tether") int32 ImpactCount=0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Electric Tether") FVector MotionVelocity=FVector::ZeroVector;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Electric Tether") float StruggleTime=0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Electric Tether") float PoseAlpha=0.f;
    float ImpactFlash=0.f;
    FQuat GetPoseBasis() const { return RestMesh.GetRotation().Inverse(); }
private:
    void Restore(bool bFalling);
    void Step(float Delta);
    void Impact(const FHitResult& Hit, float Speed);
    TWeakObjectPtr<AActor> Captor;
    TWeakObjectPtr<APawn> DamageOwner;
    FVector HoldPoint=FVector::ZeroVector, SmoothedVelocity=FVector::ZeroVector;
    FTransform RestMesh;
    FQuat Tumble=FQuat::Identity;
    uint8 RestMode=0, RestCustom=0, RestVisibility=0;
    bool bRestOrient=false,bRestDesired=false,bRestRunPhysics=false,bRestUpdateRate=false,bRestControllerYaw=false;
    float FlightAge=0.f,RecoveryAge=0.f;
    double NextImpactTime=0;
};
