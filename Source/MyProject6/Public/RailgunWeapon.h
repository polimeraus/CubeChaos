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

UCLASS()
class MYPROJECT6_API ARailgunWeapon : public AActor
{
    GENERATED_BODY()
public:
    ARailgunWeapon();
    virtual void Tick(float DeltaSeconds) override;
    UFUNCTION(BlueprintCallable, Category="Railgun")
    void Equip(AMyProject6Character* Character);

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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0"))
    float Damage = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.1", Units="s"))
    float FireInterval = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="100", Units="cm"))
    float Range = 20000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.05", ClampMax="2", Units="s"))
    float TrailDuration = .55f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun|Visuals")
    FRailgunVisualSettings Visuals;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun|Motion", meta=(ClampMin="0", ClampMax="1.5"))
    float MotionAmount = 1.f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Railgun")
    FVector LastShotEnd = FVector::ZeroVector;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Railgun")
    int32 ShotsFired = 0;

private:
    UPROPERTY() TObjectPtr<AMyProject6Character> Wielder;
    UPROPERTY() TObjectPtr<UAnimSequence> HoldAnimation;
    UPROPERTY() TObjectPtr<USoundBase> FireSound;
    UPROPERTY() TObjectPtr<UMaterialInterface> ArmsMaterial;
    double NextFireTime = 0;
    FVector RestLocation = FVector::ZeroVector;
    FVector PreviousVelocity = FVector::ZeroVector;
    FRotator PreviousAim = FRotator::ZeroRotator;
    FRailgunViewMotion ViewMotion;
    void ApplyHit(const FHitResult& Hit, const FVector& Direction);
};
