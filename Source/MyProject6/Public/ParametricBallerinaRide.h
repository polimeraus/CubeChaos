#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParametricBallerinaRide.generated.h"

class UChildActorComponent;
class UDynamicMeshComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** One entry addresses one zero-based skirt slice and the bulbs on its leading seam. */
USTRUCT(BlueprintType)
struct FBallerinaSliceOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice", meta=(ClampMin="0"))
    int32 SliceIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
    TObjectPtr<UMaterialInterface> Material = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
    bool bOverrideColor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice", meta=(EditCondition="bOverrideColor"))
    FLinearColor Color = FLinearColor(1,.76f,.08f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
    bool bShowLights = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice")
    bool bOverrideLightColor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slice", meta=(EditCondition="bOverrideLightColor"))
    FLinearColor LightColor = FLinearColor(.05f,.7f,1);
};

/** Waist at origin, +X forward. Replace with any Actor/Blueprint class on the ride. */
UCLASS(Blueprintable)
class MYPROJECT6_API ABallerinaRideFigure : public AActor
{
    GENERATED_BODY()
public:
    ABallerinaRideFigure();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ballerina")
    TObjectPtr<UStaticMeshComponent> Mesh;
};

/** Floor at origin, +X outward. Default seat is 165 x 156 x 132 cm. */
UCLASS(Blueprintable)
class MYPROJECT6_API ABallerinaRideSeat : public AActor
{
    GENERATED_BODY()
public:
    ABallerinaRideSeat();
    virtual void OnConstruction(const FTransform& Transform) override;
    UFUNCTION(BlueprintCallable, Category="Ballerina")
    void SetSeatColor(FLinearColor Color);
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ballerina")
    TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina")
    FLinearColor PaintColor = FLinearColor(.025f,.42f,.48f);
};

/** Segmented fairground ballerina: a rigid spinning platform with a travelling high edge. */
UCLASS(Blueprintable, meta=(DisplayName="Parametric Ballerina Ride"))
class MYPROJECT6_API AParametricBallerinaRide : public AActor
{
    GENERATED_BODY()
public:
    AParametricBallerinaRide();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override;

    UFUNCTION(CallInEditor, BlueprintCallable, Category="Ballerina")
    void RebuildRide();
    UFUNCTION(BlueprintCallable, Category="Ballerina|Motion")
    void SetRideRunning(bool bRunning);
    UFUNCTION(CallInEditor, BlueprintCallable, Category="Ballerina|Motion")
    void ResetMotion();
    /** Deterministic pose; also useful for Sequencer, previews and tests. */
    UFUNCTION(BlueprintCallable, Category="Ballerina|Motion")
    void SetMotionTime(double Seconds);
    UFUNCTION(BlueprintPure, Category="Ballerina|Motion")
    FTransform EvaluatePlatformTransform(double Seconds) const;
    UFUNCTION(BlueprintPure, Category="Ballerina")
    int32 GetGeneratedSeatCount() const;
    UFUNCTION(BlueprintPure, Category="Ballerina")
    int32 GetGeneratedBulbCount() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ballerina|Components")
    TObjectPtr<USceneComponent> RideRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ballerina|Components")
    TObjectPtr<USceneComponent> PlatformPivot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ballerina|Components")
    TObjectPtr<UDynamicMeshComponent> Skirt;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ballerina|Components")
    TObjectPtr<UStaticMeshComponent> Deck;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ballerina|Components")
    TObjectPtr<UStaticMeshComponent> Base;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ballerina|Components")
    TObjectPtr<UChildActorComponent> Figure;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="150",ClampMax="5000",Units="cm"))
    float SkirtRadius = 600;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="10",Units="cm"))
    float HubRadius = 85;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="10",Units="cm"))
    float SkirtHeight = 225;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="25",Units="cm"))
    float DeckOverhang = 180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="2",Units="cm"))
    float DeckThickness = 22;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="0",Units="cm"))
    float PivotHeight = 185;
    /** Raises the whole assembly only if necessary to preserve the minimum edge clearance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions")
    bool bAutoGroundClearance = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="0",Units="cm"))
    float GroundClearance = 20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="6",ClampMax="96"))
    int32 SliceCount = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin="1",ClampMax="16"))
    int32 SubdivisionsPerSlice = 4;
    /** 1 is a cone, values above 1 give a fuller skirt near the waist. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Dimensions", meta=(ClampMin=".3",ClampMax="3"))
    float SkirtProfile = 1.15f;

    /** Empty entries use the supplied patterned material. Palette repeats around the skirt. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Materials")
    TArray<TObjectPtr<UMaterialInterface>> SliceMaterials;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Materials")
    TArray<FLinearColor> SliceColors;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Materials")
    TArray<FBallerinaSliceOverride> SliceOverrides;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Materials")
    bool bDecorativePattern = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Materials")
    TObjectPtr<UMaterialInterface> DeckMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Materials")
    TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Materials")
    FLinearColor DeckColor = FLinearColor(.24f,.015f,.19f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Figure")
    bool bShowFigure = true;
    /** Any Actor class is supported. Null selects the supplied figure. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Figure", meta=(EditCondition="bShowFigure"))
    TSubclassOf<AActor> FigureActorClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Figure", meta=(EditCondition="bShowFigure"))
    FTransform FigureOffset = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Seats")
    bool bShowSeats = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Seats", meta=(ClampMin="1",ClampMax="96",EditCondition="bShowSeats"))
    int32 SeatCount = 24;
    /** Any Actor class; local +X faces outward and origin rests on the deck. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Seats", meta=(EditCondition="bShowSeats"))
    TSubclassOf<AActor> SeatActorClass;
    /** Zero automatically centres the seats on the deck overhang. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Seats", meta=(ClampMin="0",Units="cm",EditCondition="bShowSeats"))
    float SeatRadius = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Seats", meta=(Units="deg",EditCondition="bShowSeats"))
    float SeatPhaseDegrees = 7.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Seats", meta=(EditCondition="bShowSeats"))
    FTransform SeatOffset = FTransform::Identity;
    /** Shrinks default seats tangentially if a large count would cause overlap. Custom actors use SeatOffset scale. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Seats")
    bool bAutoFitDefaultSeats = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Seats")
    TArray<FLinearColor> SeatColors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights")
    bool bShowSeamLights = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights")
    bool bShowRimLights = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights", meta=(ClampMin="1",ClampMax="80"))
    int32 BulbsPerSeam = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights", meta=(ClampMin="1",ClampMax="12",Units="cm"))
    float BulbRadius = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights")
    TArray<FLinearColor> LightColors;
    /** Optional custom bulb material. Built-in parent exposes Tint and Emission. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights")
    TObjectPtr<UMaterialInterface> BulbMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights", meta=(ClampMin="0",ClampMax="100"))
    float LightIntensity = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights")
    bool bChaseLights = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Lights", meta=(ClampMin="0",ClampMax="20",EditCondition="bChaseLights"))
    float LightChaseSpeed = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Motion")
    bool bAutoStart = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Motion", meta=(Units="deg/s"))
    float SpinSpeed = 24;
    /** High edge travels independently of the platform's axial spin. Signed degrees per second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Motion", meta=(Units="deg/s"))
    float WaveSpeed = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Motion", meta=(ClampMin="0",ClampMax="35",Units="deg"))
    float TiltAngle = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Motion", meta=(Units="deg"))
    float InitialSpinAngle = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Motion", meta=(Units="deg"))
    float InitialWaveAngle = 30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Motion")
    bool bPreviewMotionInEditor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ballerina|Collision")
    bool bEnableCollision = true;

private:
    UPROPERTY() TObjectPtr<UStaticMesh> SphereMesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> DefaultSurfaceMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> DefaultPatternMaterial;
    UPROPERTY() TObjectPtr<UMaterialInterface> DefaultBulbMaterial;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> LightMIDs;
    UPROPERTY(Transient) TArray<float> LightPhases;
    double MotionTime = 0;
    bool bRunning = true;
    float SafeRadius() const;
    float SurfaceHeight(float Radius) const;
    void UpdateLights();
};
