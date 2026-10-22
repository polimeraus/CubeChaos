#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCSleepComponent.generated.h"

class UTextRenderComponent;
class UCapsuleComponent;

/** Authored support poses, in standing pelvis-height units above the floor. */
struct FNPCSleepPose
{
    FVector Hip = FVector(0,0,1);
    float Roll = 0, Pitch = 0, Curl = 0, Nod = 0;
    FVector Targets[4] = {FVector(.08,-.45,.85),FVector(.08,.45,.85),FVector(0,-.16,.10),FVector(0,.16,.10)};
    FVector Poles[4] = {FVector(-.3,-.8,.6),FVector(-.3,.8,.6),FVector(.8,-.2,.4),FVector(.8,.2,.4)};
};

UENUM(BlueprintType)
enum class ENPCSleepState : uint8 { Awake, Drowsy, FallingAsleep, Sleeping, Waking };

/** Non-damaging crowd control shared by every native NPC and its Blueprint descendants. */
UCLASS(ClassGroup=(NPC), meta=(BlueprintSpawnableComponent))
class MYPROJECT6_API UNPCSleepComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UNPCSleepComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    /** Refresh a short lease. Multiple emitters cannot wake each other's targets. */
    void ReceiveSignal(AActor* Source, float HoldSeconds);
    void CancelSleep();
    /** Death retains the current ground pose while the normal corpse lifecycle takes over. */
    bool FinishForDeath();
    FTransform ApplyBlastRotation(const FTransform& Base, const FQuat& Rotation) const;
    UFUNCTION(BlueprintPure, Category="NPC|Sleep") bool IsIncapacitated() const { return State != ENPCSleepState::Awake; }
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="NPC|Sleep") ENPCSleepState State = ENPCSleepState::Awake;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Sleep", meta=(ClampMin="0.1", Units="s")) float DrowsyDuration = .85f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Sleep", meta=(ClampMin="0.1", Units="s")) float FallDuration = .9f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NPC|Sleep", meta=(ClampMin="0.1", Units="s")) float WakeDuration = 2.9f;
    // Read on the game thread by the shared post-process animation proxy.
    float PoseAlpha = 0.f;
    float Curl = 0.f;
    float Nod = 0.f;
    float Breath = 0.f;
    FVector PoseBendAxis = FVector::RightVector;
    FVector PoseForwardAxis = FVector::ForwardVector;
    FVector LimbTargets[4];
    FVector LimbPoles[4];
    FQuat GroundRotation = FQuat::Identity;
    FVector GroundPoint = FVector::ZeroVector, GroundUp = FVector::UpVector;
    bool HasPose() const { return IsIncapacitated() || bDeathPose; }
private:
    bool IsLivingNPC() const;
    void StartSleep();
    void Animate(float Delta);
    TMap<TWeakObjectPtr<AActor>, double> Sources;
    FTransform RestMesh;
    uint8 RestMovementMode = 1;
    uint8 RestCustomMode = 0;
    uint8 RestVisibilityTick = 0;
    bool bRestUpdateRate = false;
    float Elapsed = 0.f;
    float Age = 0.f;
    FNPCSleepPose Pose, FallStart;
    float FallStartAlpha = 1.f;
    FVector RefHip = FVector::ZeroVector;
    float StandingHeight = 90.f;
    float SideSupportLift = 0.f;
    float CapsuleHalf = 96.f;
    bool bRestOrient = false, bRestDesiredRotation = false, bRestRunPhysics = false;
    bool bDeathPose = false;
    ECollisionResponse RestProjectileResponse = ECR_Block, RestVisibilityResponse = ECR_Block;
    void SetBodyCollision(bool bEnabled);
    void BuildBodyCollision();
    UPROPERTY(Transient) TArray<TObjectPtr<UCapsuleComponent>> BodyCollision;
    UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> SleepIndicator;
};
