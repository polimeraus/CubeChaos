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

    UFUNCTION(BlueprintCallable, Category="Weapons")
    void ActivateDefaultWeapon();

    UFUNCTION(BlueprintCallable, Category="BFG")
    void ActivateBFG();
    UFUNCTION(BlueprintCallable, Category="BFG")
    bool FireBFG();

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="100", Units="cm"))
    float Range = 20480.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.05", ClampMax="2", Units="s"))
    float TrailDuration = .65f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(ClampMin="0.05", Units="s"))
    float BFGFireInterval = .3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(ClampMin="0"))
    float BFGDamage = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(ClampMin="0"))
    float BFGSplashDamage = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG", meta=(ClampMin="1", Units="cm"))
    float BFGSplashRadius = 300.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="BFG")
    int32 BFGShotsFired = 0;

    /** Weapon and hands recoil; does not rotate the camera or change the aim ray. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BFG|Feedback", meta=(ClampMin="0", ClampMax="3"))
    float BFGRecoilStrength = 2.f;
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
    UPROPERTY() TObjectPtr<AMyProject6Character> Wielder;
    UPROPERTY() TObjectPtr<UAnimSequence> HoldAnimation;
    UPROPERTY() TObjectPtr<UAnimSequence> BFGHoldAnimation;
    UPROPERTY() TObjectPtr<USoundBase> FireSound;
    UPROPERTY() TObjectPtr<UMaterialInterface> ArmsMaterial;
    UPROPERTY() TObjectPtr<UStaticMesh> RailgunModel;
    UPROPERTY() TObjectPtr<UStaticMesh> BFGModel;
    UPROPERTY() TObjectPtr<USoundBase> BFGFireSound;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ChargeMaterial;
    double NextFireTime = 0;
    double NextBFGFireTime = 0;
    float BFGMuzzleFlashRemaining = 0;
    FVector RestLocation = FVector::ZeroVector;
    FVector PreviousVelocity = FVector::ZeroVector;
    FRotator PreviousAim = FRotator::ZeroRotator;
    FRailgunViewMotion ViewMotion;
    void ApplyHit(const FHitResult& Hit, const FVector& Direction);
};
