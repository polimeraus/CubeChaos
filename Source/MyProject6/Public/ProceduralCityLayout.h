#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralCityLayout.generated.h"

UENUM(BlueprintType)
enum class ECityLayout : uint8 { Radial, Grid, Organic };
UENUM(BlueprintType)
enum class ECityLinkKind : uint8 { Road, Bridge, Ramp, Stairs };

/** Version 1 placement rules. Distances are centimetres, angles degrees. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FProceduralCityRules
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout") ECityLayout Layout = ECityLayout::Radial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(ClampMin="3",ClampMax="25")) int32 DistrictCount = 7;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(ClampMin="1600",Units="cm")) float MinDistrictRadius = 2400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(ClampMin="1600",Units="cm")) float MaxDistrictRadius = 3000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(ClampMin="600",Units="cm")) float DistrictGap = 1800;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(ClampMin="0",ClampMax="0.18")) float PositionJitter = .10f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layout", meta=(ClampMin="0",ClampMax="1")) float RoundPlatformChance = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Height", meta=(ClampMin="80",Units="cm")) float BaseElevation = 280;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Height", meta=(ClampMin="0",Units="cm")) float ElevationStep = 180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Height", meta=(ClampMin="1",ClampMax="5")) int32 ElevationLevels = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes", meta=(ClampMin="180",Units="cm")) float RoadWidth = 420;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes", meta=(ClampMin="0",ClampMax="1")) float ExtraConnectionChance = .45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes", meta=(ClampMin="0",ClampMax="0.2")) float Curvature = .12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes", meta=(ClampMin="1",ClampMax="20")) float MaxRampSlope = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes", meta=(ClampMin="0",ClampMax="1")) float StairChance = .45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes", meta=(ClampMin="8",ClampMax="30",Units="cm")) float MaxStepRise = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes", meta=(ClampMin="25",Units="cm")) float MinStepTread = 35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes", meta=(ClampMin="0",Units="cm")) float MaxStairHeight = 400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Routes") bool bGuardRails = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buildings", meta=(ClampMin="0",ClampMax="6")) int32 MinBuildingsPerDistrict = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buildings", meta=(ClampMin="0",ClampMax="6")) int32 MaxBuildingsPerDistrict = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buildings", meta=(ClampMin="80",Units="cm")) float BuildingClearance = 150;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buildings", meta=(ClampMin="80",Units="cm")) float PlatformEdgeSetback = 180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buildings", meta=(ClampMin="120",Units="cm")) float BuildingAccessWidth = 220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Buildings") bool bHubLandmark = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Budget", meta=(ClampMin="1",ClampMax="64")) int32 MaxTotalBuildings = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Budget", meta=(ClampMin="1000",ClampMax="300000")) int32 MaxEstimatedBuildingCells = 65000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Budget", meta=(ClampMin="20",ClampMax="1000")) int32 PlacementAttempts = 180;
    bool Validate(FString& Error) const;
};

/** Conservative footprint INCLUDING exterior stairs/overhangs. Offset recentres the pivot
 * and puts the lowest contact surface on Z=0. It never modifies the source class. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FCityBuildingCatalogEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog") FName Id;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog") TSubclassOf<AActor> ActorClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog", meta=(ClampMin="0")) float Weight = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog", meta=(ClampMin="20",Units="cm")) float FootprintRadius = 1000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog", meta=(Units="cm")) FVector PivotOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog", meta=(ClampMin="0.1",ClampMax="3")) float MinScale = .65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog", meta=(ClampMin="0.1",ClampMax="3")) float MaxScale = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog", meta=(Units="deg")) float FrontYaw = -90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog", meta=(ClampMin="1")) int32 EstimatedCells = 5000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Catalog") bool bAllowAsLandmark = true;
};

USTRUCT(BlueprintType)
struct FCityExclusionZone
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion") FVector2D Center = FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Exclusion", meta=(ClampMin="1",Units="cm")) float Radius = 1000;
};

USTRUCT(BlueprintType)
struct FCityDistrictPlan
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") FVector Center = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") float Radius = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") bool bRound = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") int32 PaletteIndex = 0;
    /** Unit directions from the centre toward the entrance ports. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FVector2D> Ports;
    double Inradius() const { return bRound ? Radius : Radius / UE_SQRT_2; }
    double BoundaryDistance(FVector2D Direction) const;
};

USTRUCT(BlueprintType)
struct FCityConnectionPlan
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") int32 From = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") int32 To = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") ECityLinkKind Kind = ECityLinkKind::Bridge;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FVector> Points;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") float Length = 0;
};

USTRUCT(BlueprintType)
struct FCityBuildingPlan
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") int32 District = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") int32 CatalogIndex = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") FVector Center = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") float Radius = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") float Scale = 1;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") float Yaw = 0;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FProceduralCityPlan
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") int32 Seed = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FCityDistrictPlan> Districts;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FCityConnectionPlan> Connections;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") TArray<FCityBuildingPlan> Buildings;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") int32 EstimatedCells = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") FString Signature;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") FString Notes;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") FVector EntryLocation = FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Plan") float FoundationRadius = 0;
};

/** Pure deterministic planner: no world mutation, no global RNG, bounded attempts. */
namespace ProceduralCity
{
    MYPROJECT6_API bool MakePlan(const FProceduralCityRules& Rules, int32 Seed,
        const TArray<FCityBuildingCatalogEntry>& Catalog, const TArray<FCityExclusionZone>& Exclusions,
        FProceduralCityPlan& Out, FString& Error);
    MYPROJECT6_API bool ValidatePlan(const FProceduralCityRules& Rules, const FProceduralCityPlan& Plan, FString& Error);
    MYPROJECT6_API double PointSegmentDistance(FVector2D P, FVector2D A, FVector2D B);
}
