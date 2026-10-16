#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParametricPickup.generated.h"

class UBoxComponent;
class UDynamicMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class APawn;
class AParametricPickup;
class USoundBase;
class USoundAttenuation;

UENUM(BlueprintType)
enum class EPickupWeapon : uint8
{
    Railgun,
    BFG UMETA(DisplayName="BFG 10K"),
    DefaultWeapon UMETA(DisplayName="Default Rifle")
};

UENUM(BlueprintType)
enum class EPickupMotion : uint8
{
    Stationary,
    Spin,
    Bob,
    SpinAndBob UMETA(DisplayName="Spin and Bob"),
    Orbit,
    OrbitAndSpin UMETA(DisplayName="Orbit and Spin")
};

UENUM(BlueprintType)
enum class EPickupSpinAxis : uint8 { X, Y, Z };

// Append new entries to preserve existing Blueprint and material values.
UENUM(BlueprintType)
enum class EPickupShape : uint8
{
    Cube,
    Sphere,
    RoundedCube UMETA(DisplayName="Rounded Cube"),
    Pyramid
};

UENUM(BlueprintType)
enum class EPickupMotif : uint8
{
    Star,
    QuestionMark UMETA(DisplayName="Question Mark"),
    Crescent,
    Cross,
    Rocket,
    Bomb,
    AerialBomb UMETA(DisplayName="Aerial Bomb"),
    RetroRocket UMETA(DisplayName="Retro Rocket"),
    RadiationBomb UMETA(DisplayName="Radiation Bomb"),
    BombCluster UMETA(DisplayName="Bomb Cluster")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FParametricPickupCollected, AParametricPickup*, Pickup, APawn*, Collector);

/** Procedural pickup with a separate dynamic material per actor. Pivot is the bottom centre. */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API AParametricPickup : public AActor
{
    GENERATED_BODY()

public:
    AParametricPickup();

    /** Weapon granted on collection. Existing pickups retain their railgun reward. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Collection")
    EPickupWeapon WeaponReward = EPickupWeapon::Railgun;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Shape")
    EPickupShape Shape = EPickupShape::Cube;

    /** Width/diameter and height in centimetres. The actor's scale can also stretch it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Shape", meta=(ClampMin="1", ClampMax="10000", Units="cm"))
    float Size = 60;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Shape", meta=(ClampMin="0", Units="cm", EditCondition="Shape == EPickupShape::RoundedCube"))
    float CornerRadius = 9;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Pickup|Shape", meta=(ClampMin="4", ClampMax="32", EditCondition="Shape == EPickupShape::Sphere || Shape == EPickupShape::RoundedCube"))
    int32 SurfaceSubdivisions = 16;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Icon")
    EPickupMotif Icon = EPickupMotif::Star;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Icon")
    FLinearColor IconColor = FLinearColor(0.03f, 0.75f, 1.f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Icon", meta=(ClampMin="0.1", ClampMax="1"))
    float IconScale = 0.72f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Icon", meta=(Units="deg"))
    float IconRotation = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Body")
    FLinearColor BodyColor = FLinearColor(0.025f, 0.04f, 0.065f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Body", meta=(ClampMin="0", ClampMax="1"))
    float Roughness = 0.32f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Body", meta=(ClampMin="0", ClampMax="1"))
    float Metallic = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Glow")
    bool bPulse = true;

    /** Complete bright/dim cycle, measured in seconds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Glow", meta=(ClampMin="0.05", ClampMax="3600", Units="s", EditCondition="bPulse"))
    float PulsePeriod = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Glow", meta=(ClampMin="0", ClampMax="100", EditCondition="bPulse"))
    float MinimumGlow = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Glow", meta=(ClampMin="0", ClampMax="100"))
    float MaximumGlow = 6;

    /** Offset in cycles; 0.5 makes two otherwise identical pickups alternate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Pickup|Glow", meta=(ClampMin="0", ClampMax="1", EditCondition="bPulse"))
    float PulsePhase = 0;

    /** Stationary preserves the placed pose; only animated pickups need an actor tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Motion")
    EPickupMotion Motion = EPickupMotion::Stationary;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Motion", meta=(EditCondition="Motion == EPickupMotion::Spin || Motion == EPickupMotion::SpinAndBob || Motion == EPickupMotion::OrbitAndSpin"))
    EPickupSpinAxis SpinAxis = EPickupSpinAxis::Z;

    /** Degrees per second about the mesh centre. Negative values reverse direction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Motion", meta=(ClampMin="-3600", ClampMax="3600", Units="deg/s", EditCondition="Motion == EPickupMotion::Spin || Motion == EPickupMotion::SpinAndBob || Motion == EPickupMotion::OrbitAndSpin"))
    float SpinSpeed = 90;

    /** Total bottom-to-top travel. A cosine curve slows smoothly at both ends. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Motion", meta=(ClampMin="0", ClampMax="10000", Units="cm", EditCondition="Motion == EPickupMotion::Bob || Motion == EPickupMotion::SpinAndBob"))
    float BobHeight = 30;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Motion", meta=(ClampMin="0.05", ClampMax="3600", Units="s", EditCondition="Motion == EPickupMotion::Bob || Motion == EPickupMotion::SpinAndBob"))
    float BobPeriod = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Motion", meta=(ClampMin="0", ClampMax="10000", Units="cm", EditCondition="Motion == EPickupMotion::Orbit || Motion == EPickupMotion::OrbitAndSpin"))
    float OrbitRadius = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Motion", meta=(ClampMin="0.05", ClampMax="3600", Units="s", EditCondition="Motion == EPickupMotion::Orbit || Motion == EPickupMotion::OrbitAndSpin"))
    float OrbitPeriod = 4;

    /** Additional local height for moving pickups; useful for horizontal-axis spinning. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Motion", meta=(ClampMin="0", ClampMax="10000", Units="cm", EditCondition="Motion != EPickupMotion::Stationary"))
    float HoverHeight = 0;

    /** Initial cycle offset for bobbing/orbiting; 0.5 offsets neighbours by half a cycle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Pickup|Motion", meta=(ClampMin="0", ClampMax="1", EditCondition="Motion != EPickupMotion::Stationary && Motion != EPickupMotion::Spin"))
    float MotionPhase = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Pickup|Motion")
    bool bPreviewMotionInEditor = true;

    /** Any Sound Wave, Sound Cue or other SoundBase. None disables collection audio. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Sound")
    TObjectPtr<USoundBase> CollectionSound;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Sound", meta=(ClampMin="0", ClampMax="4"))
    float SoundVolume = 0.65f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Sound", meta=(ClampMin="0.25", ClampMax="4"))
    float SoundPitch = 1;

    /** Optional spatial falloff override. None uses the sound asset's own settings. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Pickup|Sound")
    TObjectPtr<USoundAttenuation> SoundAttenuation;

    /** Assigned by BP_ParametricPickup. Custom parents must expose the same parameters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Pickup|Material")
    TObjectPtr<UMaterialInterface> SurfaceMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Collection")
    bool bAutoCollect = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Collection")
    bool bPlayerOnly = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pickup|Collection", meta=(ClampMin="0", ClampMax="500", Units="cm"))
    float CollectionPadding = 12;

    /** Fired once on the authority; bind a health/ammo/inventory reward here. */
    UPROPERTY(BlueprintAssignable, Category="Pickup|Collection")
    FParametricPickupCollected OnCollected;

    UFUNCTION(BlueprintImplementableEvent, Category="Pickup|Collection", meta=(DisplayName="On Pickup Collected"))
    void ReceiveCollected(APawn* Collector);

    /** Rebuilds shape and material after changing properties from Blueprint at runtime. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Pickup")
    void RefreshPickup();

    /** Updates only material parameters, without rebuilding geometry. */
    UFUNCTION(BlueprintCallable, Category="Pickup")
    void RefreshMaterial();

    /** Apply changed motion settings and restart their cycle without rebuilding the mesh. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Pickup|Motion")
    void RefreshMotion();

    /** Requires a valid pawn overlapping this pickup, and runs only on the authority. */
    UFUNCTION(BlueprintCallable, Category="Pickup|Collection")
    bool TryCollect(APawn* Collector);

    UFUNCTION(BlueprintPure, Category="Pickup|Collection")
    bool IsCollected() const { return bCollected; }

    UFUNCTION(BlueprintPure, Category="Pickup|Material")
    UMaterialInstanceDynamic* GetDynamicMaterial() const { return MaterialInstance; }

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pickup")
    TObjectPtr<UDynamicMeshComponent> Body;

    /** Centre pivot shared by the visible mesh and the collection volume. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pickup")
    TObjectPtr<USceneComponent> MotionPivot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pickup")
    TObjectPtr<UBoxComponent> CollectionTrigger;

private:
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> MaterialInstance;

    UPROPERTY(ReplicatedUsing=OnRep_Collected)
    bool bCollected = false;

    UFUNCTION()
    void OnRep_Collected();

    UFUNCTION()
    void OnOverlap(UPrimitiveComponent* Component, AActor* Other, UPrimitiveComponent* OtherComponent,
        int32 BodyIndex, bool bSweep, const FHitResult& Hit);

    void CheckInitialOverlaps();
    void ApplyMotion(double ElapsedSeconds);
    bool CanAnimate() const;
    double MotionElapsed = 0;

    /** A transient event, so joining after collection cannot replay an old sound. */
    UFUNCTION(NetMulticast, Reliable)
    void MulticastCollectionSound(USoundBase* Sound, FVector Location, float Volume, float Pitch, USoundAttenuation* Attenuation);
};
