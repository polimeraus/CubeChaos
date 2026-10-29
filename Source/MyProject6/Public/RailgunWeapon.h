#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RailgunVisualSettings.h"
#include "RailgunViewMotion.h"
#include "RailgunWeapon.generated.h"

class AMyProject6Character;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UAnimSequence;
class USoundBase;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UPointLightComponent;
class ASleepSignalDevice;

UCLASS()
class MYPROJECT6_API ARailgunWeapon : public AActor
{
    GENERATED_BODY()
public:
    ARailgunWeapon();
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable, Category="Railgun")
    void Equip(AMyProject6Character* Character);

    UFUNCTION(BlueprintCallable, Category="Railgun")
    void ActivateRailgun();
    UFUNCTION(BlueprintCallable, Category="Electric Tether") void ActivateElectricTether();

    UFUNCTION(BlueprintCallable, Category="Weapons")
    void ActivateDefaultWeapon();

    UFUNCTION(BlueprintCallable, Category="BFG")
    void ActivateBFG();
    UFUNCTION(BlueprintCallable, Category="BFG")
    bool FireBFG();

    UFUNCTION(BlueprintCallable, Category="Bomb") void ActivateBomb();
    UFUNCTION(BlueprintCallable, Category="Bomb") bool FireBomb();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb", meta=(ClampMin="0.1", Units="s")) float BombFuseSeconds = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb", meta=(ClampMin="0.8", Units="s")) float BombThrowInterval = .9f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bomb", meta=(ClampMin="100", Units="cm/s")) float BombThrowSpeed = 1250.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Bomb") int32 BombsThrown = 0;

    UFUNCTION(BlueprintCallable, Category="Sleep Signal") void ActivateSleepSignal();
    UFUNCTION(BlueprintCallable, Category="Sleep Signal") bool FireSleepSignal();
    /** Tune the saved device Blueprint for signal diameter, broadcast duration and wake delay. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Sleep Signal") TSubclassOf<ASleepSignalDevice> SleepDeviceClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sleep Signal", meta=(ClampMin="100", ClampMax="450", Units="cm/s")) float SleepThrowSpeed = 260.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Sleep Signal", meta=(ClampMin="1.1", Units="s")) float SleepThrowInterval = 1.4f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Sleep Signal") int32 SleepDevicesThrown = 0;

    /** Each press requests one instantaneous shot; both keys share the same cooldown. */
    UFUNCTION(BlueprintCallable, Category="Railgun")
    bool Fire();

    /** Also used by the character's rapid projectile fire, with a smaller impulse. */
    void AddFireMotion(float Strength = 1.f);
    void AddLandingMotion(float DownwardSpeed);

    UFUNCTION(BlueprintPure, Category="Railgun")
    FVector GetVisualMuzzleLocation() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Railgun")
    TObjectPtr<USkeletalMeshComponent> Arms;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Railgun")
    TObjectPtr<UStaticMeshComponent> WeaponMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Railgun")
    TObjectPtr<USceneComponent> Muzzle;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BFG")
    TObjectPtr<UPointLightComponent> BFGMuzzleLight;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0"))
    float Damage = 200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.1", Units="s"))
    float FireInterval = 1.5f;
    /** Minimum trail length on a miss, not a damage limit. Shots always cover the loaded world.
     * Keep the serialized Range name so existing Blueprint/instance values remain compatible. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(DisplayName="Miss Trail Length", ClampMin="100", Units="cm"))
    float Range = 20480.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.05", ClampMax="2", Units="s"))
    float TrailDuration = .65f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(ClampMin="0.05", Units="s"))
    float BFGFireInterval = .22f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(ClampMin="0"))
    float BFGDamage = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(ClampMin="0"))
    float BFGSplashDamage = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(ClampMin="1", Units="cm"))
    float BFGSplashRadius = 300.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Destruction", meta=(ClampMin="1"))
    float BFGSculptureRadiusMultiplier = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Destruction", meta=(ClampMin="1"))
    float BFGSculptureDamageMultiplier = 1.5f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="BFG")
    int32 BFGShotsFired = 0;

    /** Weapon and hands recoil; does not rotate the camera or change the aim ray. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Feedback", meta=(ClampMin="0", ClampMax="3"))
    float BFGRecoilStrength = 3.f;
    /** Short firing shake. Zero disables camera movement. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Feedback", meta=(ClampMin="0", ClampMax="3"))
    float BFGCameraShakeScale = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Feedback", meta=(ClampMin="0", Units="lm"))
    float BFGMuzzleFlashIntensity = 18000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Feedback", meta=(ClampMin="1", Units="cm"))
    float BFGMuzzleFlashRadius = 650.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Feedback", meta=(ClampMin="0.01", ClampMax="0.3", Units="s"))
    float BFGMuzzleFlashDuration = .12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Feedback")
    FLinearColor BFGMuzzleFlashColor = FLinearColor(.025f, 1.f, .005f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun|Visuals")
    FRailgunVisualSettings Visuals;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun|Motion", meta=(ClampMin="0", ClampMax="1.5"))
    float MotionAmount = 1.f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Railgun")
    FVector LastShotEnd = FVector::ZeroVector;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Railgun")
    int32 ShotsFired = 0;

private:
    UPROPERTY() TObjectPtr<UStaticMesh> DefaultModel;
    UPROPERTY() TObjectPtr<UStaticMesh> ArcModel;
    UPROPERTY() TObjectPtr<UAnimSequence> ArcHoldAnimation;
    UPROPERTY() TObjectPtr<AMyProject6Character> Wielder;
    UPROPERTY() TObjectPtr<UAnimSequence> HoldAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> BFGHoldAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> BombHoldAnimation;
    UPROPERTY() TObjectPtr<UStaticMesh> BombModel;
    UPROPERTY() TObjectPtr<UStaticMesh> SleepModel;
    UPROPERTY() TObjectPtr<UAnimSequence> SleepHoldAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> SleepThrowAnimation;
    UPROPERTY() TObjectPtr<USoundBase> BombThrowSound;
    UPROPERTY() TObjectPtr<USoundBase> FireSound;
    UPROPERTY() TObjectPtr<UMaterialInterface> ArmsMaterial;
    UPROPERTY() TObjectPtr<UStaticMesh> RailgunModel;
    UPROPERTY() TObjectPtr<UStaticMesh> BFGModel;
    UPROPERTY() TObjectPtr<USoundBase> BFGFireSound;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ChargeMaterial;
    double NextFireTime = 0;
    double NextBFGFireTime = 0;
    double NextBombFireTime = 0;
    double LastBombThrowTime = -10;
    double LastSleepThrowTime = -10;
    double NextSleepFireTime = 0;
    bool bSleepThrowPending = false;
    bool bSleepThrowAnimating = false;
    FVector SleepGripRest = FVector::ZeroVector;
    void ReleaseSleepDevice();
    float BFGMuzzleFlashRemaining = 0;
    FVector RestLocation = FVector::ZeroVector;
    FVector PreviousVelocity = FVector::ZeroVector;
    FRotator PreviousAim = FRotator::ZeroRotator;
    FRailgunViewMotion ViewMotion;
    void ApplyHit(const FHitResult& Hit, const FVector& Direction);
    void RestoreWeaponAttachment();
    void ApplyHoldPose(UAnimSequence* Animation);
};
