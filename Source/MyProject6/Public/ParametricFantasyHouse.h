#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParametricFantasyHouse.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EFantasyRoofShape : uint8 { SweptSpire, Dome, Onion, Cone };

UENUM(BlueprintType)
enum class EFantasyApexShape : uint8 { None, Star, Orb, Spear };

UENUM(BlueprintType)
enum class EFantasyOpeningShape : uint8 { RoundArch, PointedArch, Rectangle };

UENUM(BlueprintType)
enum class EFantasyHousePreset : uint8 { WizardHouse, MoonObservatory, FloatingPalace };

USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyBodySettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body", meta=(ClampMin="120", ClampMax="2000", Units="cm"))
    float Radius = 280;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body", meta=(ClampMin="1", ClampMax="6"))
    int32 Floors = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body", meta=(ClampMin="180", ClampMax="600", Units="cm"))
    float FloorHeight = 250;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body", meta=(ClampMin="0.65", ClampMax="1.4"))
    float TopRadiusScale = 1.04f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body", meta=(ClampMin="8", ClampMax="60", Units="cm"))
    float WallThickness = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Body")
    bool bFloorBands = true;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyRoofSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof")
    EFantasyRoofShape Shape = EFantasyRoofShape::SweptSpire;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof", meta=(ClampMin="50", ClampMax="2000", Units="cm"))
    float Height = 460;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof", meta=(ClampMin="0", ClampMax="250", Units="cm"))
    float Overhang = 55;
    /** Controls the concave spire or fullness of the dome / onion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof", meta=(ClampMin="0.3", ClampMax="3"))
    float Curvature = 1.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof", meta=(ClampMin="4", ClampMax="16"))
    int32 ProfileSteps = 8;
    /** Offset of the roof tip in actor-local XY. Does not move the eaves. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof")
    FVector2D TipOffset = FVector2D(28, 0);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof")
    bool bRibs = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Roof", meta=(ClampMin="3", ClampMax="12", EditCondition="bRibs"))
    int32 RibCount = 8;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyApexSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apex")
    EFantasyApexShape Shape = EFantasyApexShape::Star;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apex", meta=(ClampMin="0", ClampMax="400", Units="cm"))
    float StemHeight = 55;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apex", meta=(ClampMin="8", ClampMax="160", Units="cm"))
    float Size = 36;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apex", meta=(ClampMin="4", ClampMax="8", EditCondition="Shape == EFantasyApexShape::Star"))
    int32 StarPoints = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Apex", meta=(Units="deg"))
    float Yaw = -90;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyWindowSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windows")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windows")
    EFantasyOpeningShape Shape = EFantasyOpeningShape::PointedArch;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windows", meta=(ClampMin="0", ClampMax="16"))
    int32 PerFloor = 8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windows", meta=(ClampMin="20", ClampMax="300", Units="cm"))
    float Width = 65;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windows", meta=(ClampMin="30", ClampMax="400", Units="cm"))
    float Height = 125;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windows", meta=(ClampMin="10", ClampMax="250", Units="cm"))
    float SillHeight = 75;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Windows", meta=(Units="deg"))
    float AngleOffset = -90;
};

/** Interior staircase made only of treads, built into the same mesh as the house. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyStairSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs")
    bool bEnabled = true;
    /** Ends at the highest storey; a single-storey house gets a stair to its loft. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs", meta=(EditCondition="bEnabled"))
    bool bAutoHeight = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs", meta=(ClampMin="40", ClampMax="3000", Units="cm", EditCondition="bEnabled && !bAutoHeight"))
    float Height = 500;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs", meta=(ClampMin="5", ClampMax="300", Units="cm", EditCondition="bEnabled"))
    float InnerRadius = 30;
    /** Clamped to keep the complete staircase inside the narrowest wall radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs", meta=(ClampMin="30", ClampMax="600", Units="cm", EditCondition="bEnabled"))
    float Width = 130;
    /** Actual rise is rounded down so the last tread ends exactly at the requested height. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs", meta=(ClampMin="8", ClampMax="30", Units="cm", EditCondition="bEnabled"))
    float MaxStepHeight = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs", meta=(ClampMin="12", ClampMax="48", EditCondition="bEnabled"))
    int32 StepsPerTurn = 24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs", meta=(Units="deg", EditCondition="bEnabled"))
    float StartAngle = -90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Stairs", meta=(EditCondition="bEnabled"))
    bool bClockwise = false;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyDoorSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door")
    bool bEnabled = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door")
    EFantasyOpeningShape Shape = EFantasyOpeningShape::RoundArch;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(ClampMin="45", ClampMax="250", Units="cm"))
    float Width = 115;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(ClampMin="80", ClampMax="400", Units="cm"))
    float Height = 195;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(Units="deg"))
    float Angle = -90;
    /** Removes the door leaf and leaves a passable opening. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door")
    bool bOpen = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Door", meta=(ClampMin="0", ClampMax="8"))
    int32 Steps = 3;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyBalconySettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Balcony")
    bool bEnabled = true;
    /** Zero-based storey. Clamped to the highest existing storey. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Balcony", meta=(ClampMin="0", ClampMax="5"))
    int32 Floor = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Balcony", meta=(ClampMin="30", ClampMax="360", Units="deg"))
    float ArcDegrees = 160;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Balcony", meta=(Units="deg"))
    float CenterAngle = -90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Balcony", meta=(ClampMin="30", ClampMax="250", Units="cm"))
    float Depth = 90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Balcony", meta=(ClampMin="35", ClampMax="150", Units="cm"))
    float RailHeight = 85;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Balcony", meta=(ClampMin="3", ClampMax="40"))
    int32 Posts = 16;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyTurretSettings
{
    GENERATED_BODY()
    /** All turrets are geometry inside this actor, never child actors. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turrets", meta=(ClampMin="0", ClampMax="5"))
    int32 Count = 2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turrets", meta=(ClampMin="0.2", ClampMax="0.65"))
    float RadiusScale = .4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turrets", meta=(ClampMin="0.5", ClampMax="1.8"))
    float HeightScale = .9f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turrets", meta=(ClampMin="0", ClampMax="0.5"))
    float HeightVariation = .23f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turrets", meta=(ClampMin="0.65", ClampMax="1.2"))
    float DistanceScale = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turrets", meta=(Units="deg"))
    float StartAngle = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turrets", meta=(ClampMin="0", ClampMax="360", Units="deg"))
    float SpreadAngle = 165;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Turrets")
    EFantasyRoofShape RoofShape = EFantasyRoofShape::Onion;
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FFantasyPalette
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette")
    FLinearColor Walls = FLinearColor(.36f, .24f, .51f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette")
    FLinearColor Roof = FLinearColor(.065f, .10f, .25f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette")
    FLinearColor Trim = FLinearColor(.65f, .40f, .14f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette")
    FLinearColor Stairs = FLinearColor(.23f, .13f, .065f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette")
    FLinearColor Door = FLinearColor(.085f, .035f, .06f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette")
    FLinearColor Apex = FLinearColor(1.f, .12f, .24f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Palette", meta=(ClampMin="0", ClampMax="20"))
    float ApexGlow = 1.5f;
};

/** Low-poly Geometry Script building; construction and runtime spawning use the same generator. */
UCLASS(Blueprintable, meta=(DisplayName="Parametric Fantasy House"))
class MYPROJECT6_API AParametricFantasyHouse : public AActor
{
    GENERATED_BODY()
public:
    AParametricFantasyHouse();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fantasy House")
    TObjectPtr<UDynamicMeshComponent> HouseMesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|01 Layout")
    FFantasyBodySettings Body;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|02 Roof")
    FFantasyRoofSettings Roof;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|03 Apex")
    FFantasyApexSettings Apex;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|04 Windows")
    FFantasyWindowSettings Windows;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|05 Door")
    FFantasyDoorSettings Door;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|06 Balcony")
    FFantasyBalconySettings Balcony;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|07 Turrets")
    FFantasyTurretSettings Turrets;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|Interior Stairs")
    FFantasyStairSettings Stairs;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|08 Foundation")
    bool bFloatingBase = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|08 Foundation", meta=(ClampMin="50", ClampMax="1000", Units="cm", EditCondition="bFloatingBase"))
    float FloatingBaseDepth = 230;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|09 Appearance")
    FFantasyPalette Palette;
    /** Optional overrides: wall, roof, trim, unused legacy window slot, door, apex, stairs (slots 0..6). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|09 Appearance")
    TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|09 Appearance")
    TObjectPtr<UMaterialInterface> BaseMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|10 Generation", meta=(ClampMin="8", ClampMax="32"))
    int32 RadialSegments = 16;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|10 Generation")
    bool bFlatShading = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|10 Generation")
    bool bEnableCollision = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|10 Generation")
    int32 Seed = 42;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Fantasy House|Presets")
    EFantasyHousePreset Preset = EFantasyHousePreset::WizardHouse;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Fantasy House|Statistics")
    int32 TriangleCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Fantasy House|Statistics")
    int32 StairStepCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Fantasy House|Statistics", meta=(Units="cm"))
    float GeneratedStairHeight = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Fantasy House|Statistics")
    float LastBuildMilliseconds = 0;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Fantasy House")
    void RebuildHouse();
    /** Resets geometry and palette settings to the selected preset; keeps material overrides. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Fantasy House|Presets")
    void ApplyPreset();
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Fantasy House|Presets")
    void NewVariation();
private:
    void UpdateMaterials();
    bool bRebuilding = false;
};
