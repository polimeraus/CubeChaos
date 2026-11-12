#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParametricPlatformCity.generated.h"

class AParametricPlatform;

/** Saved actor/Blueprint settings. Distances are local cm; roads are empty corridors. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FPlatformCityRules
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boundary", meta=(ClampMin="1000", ClampMax="100000", Units="cm")) float Width = 8000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boundary", meta=(ClampMin="1000", ClampMax="100000", Units="cm")) float Height = 5000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Main Roads", meta=(ClampMin="0", ClampMax="12")) int32 VerticalRoads = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Main Roads", meta=(ClampMin="0", ClampMax="12")) int32 HorizontalRoads = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Main Roads", meta=(ClampMin="160", Units="cm")) float MainRoadWidth = 600;
    /** Variation in spacing between the main roads. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Main Roads", meta=(ClampMin="0", ClampMax="1")) float DistrictVariation = .35f;
    /** Zero gives straight main roads; one gives the largest safe bends inside their lanes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Main Roads", meta=(ClampMin="0", ClampMax="1")) float RoadCurvature = .65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Side Roads", meta=(ClampMin="80", Units="cm")) float SideRoadWidth = 180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Side Roads", meta=(ClampMin="0", ClampMax="1")) float SideRoadBend = .65f;
    /** Chance of another subdivision branching from an existing side road. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Side Roads", meta=(ClampMin="0", ClampMax="1")) float BranchChance = .8f;
    /** Every plate must retain room for a disk of this diameter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Subdivision", meta=(ClampMin="200", Units="cm")) float MinPlateSize = 420;
    /** Approximate preferred plate area is TargetPlateSize squared. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Subdivision", meta=(ClampMin="200", Units="cm")) float TargetPlateSize = 850;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Subdivision", meta=(ClampMin="0", ClampMax="1")) float LargePlateChance = .3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platforms", meta=(ClampMin="10", ClampMax="500", Units="cm")) float Thickness = 80;
    /** Rounds into the plate, preserving road clearance. Limited locally if necessary. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platforms", meta=(ClampMin="0", Units="cm")) float CornerRadius = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platforms", meta=(ClampMin="2", ClampMax="24")) int32 CornerSegments = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Budget", meta=(ClampMin="1", ClampMax="256")) int32 MaxPlates = 128;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Budget", meta=(ClampMin="50", ClampMax="300", Units="cm")) float RoadSampleSpacing = 100;
    bool Validate(FString& Error) const;
};

UENUM(BlueprintType)
enum class EPlatformCityRoadKind : uint8 { VerticalMain, HorizontalMain, Side };

USTRUCT(BlueprintType)
struct FPlatformCityRoad
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") EPlatformCityRoadKind Kind = EPlatformCityRoadKind::Side;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") float Width = 0;
    /** Clipped centerline in generator coordinates. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FVector2D> Points;
    /** One reserved corridor defines both facing plate edges. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FVector2D> Outline;
};

USTRUCT(BlueprintType)
struct FPlatformCityPlot
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") FVector2D Center = FVector2D::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") FVector2D Size = FVector2D::ZeroVector;
    /** Exact road subtraction, relative to Center. Never a catalog shape. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FVector2D> SourceOutline;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FVector2D> Outline;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") int32 District = 0;
};

USTRUCT(BlueprintType)
struct FPlatformCityPlan
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FPlatformCityRoad> Roads;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FPlatformCityPlot> Plots;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") FString Signature;
};

namespace PlatformCity
{
    MYPROJECT6_API bool MakePlan(const FPlatformCityRules& Rules, int32 Seed, FPlatformCityPlan& Out, FString& Error);
}

/** Subtracts a shared road network from the city boundary and extrudes the remaining polygons. */
UCLASS(Blueprintable, meta=(DisplayName="Parametric Platform City"))
class MYPROJECT6_API AParametricPlatformCity : public AActor
{
    GENERATED_BODY()
public:
    AParametricPlatformCity();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Destroyed() override;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Layout", meta=(ShowOnlyInnerProperties)) FPlatformCityRules Rules;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") int32 Seed = 11022026;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") bool bGenerateOnBeginPlay = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") bool bRandomizeOnBeginPlay = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") bool bPreviewOnPlacement = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") bool bAutoRebuildInEditor = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Appearance") TSubclassOf<AParametricPlatform> PlatformClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Appearance") TArray<FLinearColor> DistrictColors;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") FPlatformCityRules AppliedRules;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") FPlatformCityPlan GeneratedPlan;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") FString LastError;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") FString GenerationReport;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") int32 GenerationSerial = 0;
    UFUNCTION(BlueprintCallable, Category="City") bool GenerateWithSeed(int32 InSeed);
    UFUNCTION(BlueprintCallable, Category="City") bool GenerateNewLayout();
    UFUNCTION(CallInEditor, Category="City|Actions", meta=(DisplayName="Generate New Layout")) void RandomizePreview();
    UFUNCTION(CallInEditor, Category="City|Actions", meta=(DisplayName="Rebuild Same Seed")) void RebuildPreview();
    UFUNCTION(CallInEditor, BlueprintCallable, Category="City|Actions") void ClearGenerated();
    UFUNCTION(BlueprintPure, Category="City") TArray<AParametricPlatform*> GetPlatforms() const;
    /** Samples a supported footprint. Caller manages content occupancy and base-pivot offsets. */
    UFUNCTION(BlueprintCallable, Category="City|Spawning")
    bool FindSpawnTransform(float FootprintRadius, float EdgeMargin, int32 SamplingSeed,
        FTransform& SurfaceTransform, AParametricPlatform*& Platform) const;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& Event) override;
#endif
private:
    UPROPERTY() TArray<TObjectPtr<AParametricPlatform>> Platforms;
    bool bGenerating = false;
    void DestroyPlatforms(TArray<TObjectPtr<AParametricPlatform>>& Actors);
};
