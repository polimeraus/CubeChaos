// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "SpiralPlatformGlow.h"
#include "ZigguratTraversal.h"
#include "ChunkedDestructibleConeStructure.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;
class ACharacter;

/** Selects the secondary structure wrapped around the cone. */
UENUM(BlueprintType)
enum class EConeStructurePlatformStyle : uint8
{
	CircularRings UMETA(DisplayName = "Circular Rings"),
	RisingSpiral UMETA(DisplayName = "Rising Spiral")
};

/** Selects how projectile hits remove parts of a rising spiral platform. */
UENUM(BlueprintType)
enum class EConeStructureSpiralDestructionMode : uint8
{
	ExistingCellDestruction UMETA(DisplayName = "Existing Cell Destruction"),
	OneSegmentPerHit UMETA(DisplayName = "One Rectangle Slice Per Hit"),
	OneTrianglePerHit UMETA(DisplayName = "One Triangle Slice Per Hit")
};

/** Selects the non-destructible ornament placed above the cone tip. */
UENUM(BlueprintType)
enum class EConeStructureApexStyle : uint8
{
	CurrentApex UMETA(DisplayName = "Current Cone Apex"),
	CandySpire UMETA(DisplayName = "Candy Spire"),
	Star UMETA(DisplayName = "Star"),
	Moon UMETA(DisplayName = "Moon")
};

/** Balances destruction-flash fidelity against projectile-hit cost. */
UENUM(BlueprintType)
enum class EConeStructureDestructionFlashStyle : uint8
{
	None UMETA(DisplayName = "None (Fastest)"),
	FastCompact UMETA(DisplayName = "Fast Compact Flash"),
	DetailedSurface UMETA(DisplayName = "Detailed Curved Surface")
};

/** Read-only measurements for the generated hollow cone and its circular platforms. */
USTRUCT(BlueprintType)
struct FChunkedDestructibleConeStructureMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cone Structure")
	FIntVector BackingGridBlockCount = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Cone Structure")
	FVector WorldDimensions = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Cone Structure")
	int32 GeneratedBlockCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cone Structure")
	float BottomInteriorDiameter = 0.0f;
};

/**
 * A hollow, enterable cone assembled from individually destructible cubic cells.
 *
 * Several annular platforms follow the cone taper and grow toward the bottom. The logical cells
 * reuse the chunked floor's damage, explosions, flash, sound, materials and partial rebuilds, while
 * custom 3D addressing keeps the interior and entrance physically open.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API AChunkedDestructibleConeStructure : public AChunkedDestructibleFloor
{
	GENERATED_BODY()
	friend class FDestructibleMusicAnimationTest;
	friend class FDestructibleConeMusicGroupsTest;
	friend class FDestructibleConeSpiralCutFacesTest;
	friend class FBombAirburstTest;
	friend class FSpiralPlatformGlowTest;
	friend class FDestructibleConeSurfaceTest;
	friend class FDestructibleConeSurfaceCoverageTest;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Destructible Cone Structure|Traversal", meta=(ShowOnlyInnerProperties))
    FZigguratTraversalSettings Traversal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Destructible Cone Structure|Traversal")
    TObjectPtr<UChildActorComponent> GroundLaunchPad;
    /** Minimum clear diameter through the interior and tip. The base remains solid. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Destructible Cone Structure|Traversal", meta=(Units="cm"))
    float EffectiveOpeningWidth=0;
    /** Low raised curb along the outside of the rising spiral. Destructible with its platform. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Destructible Cone Structure|Platforms|Outer Rim")
    bool bSpiralOuterRim=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Destructible Cone Structure|Platforms|Outer Rim", meta=(ClampMin="0",ClampMax="200",Units="cm",EditCondition="bSpiralOuterRim"))
    float SpiralRimHeight=30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Destructible Cone Structure|Platforms|Outer Rim", meta=(ClampMin="1",ClampMax="200",Units="cm",EditCondition="bSpiralOuterRim"))
    float SpiralRimWidth=18;
	/** Sequential triangle highlights on the spiral, independent of wall animations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Spiral Platform Glow", meta = (ShowOnlyInnerProperties))
	FSpiralPlatformGlowSettings SpiralPlatformGlow;

	UFUNCTION(BlueprintCallable, Category = "Animation|Spiral Platform Glow")
	void PlaySpiralPlatformGlow();

	UFUNCTION(BlueprintCallable, Category = "Animation|Spiral Platform Glow")
	void StopSpiralPlatformGlow();

	/** Applies settings at runtime and restarts the sequence. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Spiral Platform Glow")
	void SetSpiralPlatformGlowSettings(FSpiralPlatformGlowSettings NewSettings);

	virtual bool UsesPlanarAnimation() const override { return false; }

	AChunkedDestructibleConeStructure();
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	/** Rebuilds the cone from its current settings. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Generation")
	void RebuildConeStructure(bool bPreserveCellState = false);

	virtual int32 DamageCellsInRadius(FVector WorldLocation, float Radius, float DamageAmount,
		EChunkedFloorBlastShape Shape, bool bUseDamageFalloff) override;

	/** Updates the main cone dimensions and rebuilds all chunks. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Generation")
	void ConfigureConeStructure(
		int32 NewConeHeightBlocks,
		float NewBottomRadiusBlocks,
		float NewTopRadiusBlocks,
		float NewShellThicknessBlocks,
		FVector NewBlockDimensions,
		FVector NewBlockSpacing,
		bool bPreserveCellState = false);

	UFUNCTION(BlueprintPure, Category = "Destructible Cone Structure|Generation")
	FChunkedDestructibleConeStructureMetrics GetConeStructureMetrics() const;

	UFUNCTION(BlueprintPure, Category = "Destructible Cone Structure|Debris")
	int32 GetActiveDebrisPieceCount() const { return ActiveDebrisComponents.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetMusicGlowGroupSize(float NewSize);

	UFUNCTION(BlueprintPure, Category = "Animation|Music")
	float GetMusicGlowGroupSize() const { return MusicGlowGroupSize; }

	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetTravelingColumnsSpeed(float NewSpeed);

	UFUNCTION(BlueprintPure, Category = "Animation|Music")
	virtual float GetTravelingColumnsSpeed() const override;

	/** Sets how far the circular platform rings extend radially beyond the cone wall. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Platforms")
	void SetPlatformRingWidth(float NewRingWidthBlocks, bool bPreserveCellState = false);

	/** Switches between independent circular rings and one continuous rising spiral. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Platforms")
	void SetPlatformStyle(EConeStructurePlatformStyle NewStyle, bool bPreserveCellState = false);

	/** Changes the optional ornament above the tip without rebuilding destructible chunks. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Apex")
	void SetApexStyle(EConeStructureApexStyle NewStyle);

	UFUNCTION(BlueprintPure, Category = "Destructible Cone Structure|Platforms")
	float GetPlatformRingWidth() const { return PlatformOverhangBlocks; }

	/** Enables or disables the platform walk-speed multiplier at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Platforms|Movement")
	void SetPlatformSpeedBoost(bool bEnabled, float NewSpeedMultiplier = 2.0f);

	/** Starts the selected cone material animation from the beginning. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Fixed")
	void PlayConeAnimation();

	/** Stops the cone material animation and restores the unobstructed normal material. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Fixed")
	void StopConeAnimation();

	/** Configures the repeated arched openings and rebuilds the structure. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Doors")
	void ConfigureArchedDoors(
		int32 NewDoorCount,
		int32 NewDoorWidthBlocks,
		int32 NewDoorHeightBlocks,
		float NewDoorArchHeightBlocks,
		int32 NewFirstDoorBottomHeightBlocks,
		int32 NewDoorVerticalSpacingBlocks,
		float NewFirstDoorAngleDegrees,
		float NewDoorAngleStepDegrees,
		bool bPreserveCellState = false);

	/** Assigns the cone and circular-platform materials directly, without palette-index setup. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Materials")
	void SetStructureMaterials(UMaterialInterface* NewConeMaterial, UMaterialInterface* NewPlatformMaterial);

	/** Removes the intact cone cell described by a projectile hit, bypassing per-cell health. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Damage")
	bool DestroyConeCellFromHit(const FHitResult& Hit);

	/** Uses the same thickness-aware destruction as a normal cone hit for every piece in a radius. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Damage")
	int32 DestroyConeCellsFromHit(const FHitResult& Hit, float Radius);

	virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const override;

protected:
	/** Target square side length. 200 cm matches the default right-click destruction diameter.
	 * Traveling squares shrink near the tip to preserve their proportions. Equalizer groups whole cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMusicGlowGroupSize, Category = "Animation|Music", meta = (DisplayName = "Music Glow Group Size", EditCondition = "bReactToMusic", ClampMin = "1.0", ClampMax = "10000.0", UIMin = "50.0", UIMax = "1000.0", Units = "cm"))
	float MusicGlowGroupSize = 200.0f;

	/** Travel speed multiplier: 0.65 is slower than the original motion; 1 restores its speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetTravelingColumnsSpeed, Category = "Animation|Music", meta = (DisplayName = "Traveling Columns Speed", EditCondition = "bReactToMusic && MusicAnimationType == EMusicAnimationType::TravelingColumns", ClampMin = "0.1", ClampMax = "4.0", UIMin = "0.1", UIMax = "2.0"))
	float TravelingColumnsSpeed = 0.65f;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Lightweight ornament component; deliberately excluded from per-hit chunk rebuilds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Destructible Cone Structure|Components")
	TObjectPtr<UDynamicMeshComponent> ApexDecorationComponent;

	/** Overall height of the tapered shell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Cone", meta = (ClampMin = "4", ClampMax = "256"))
	int32 ConeHeightBlocks = 24;

	/** Outer radius at ground level, measured in blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Cone", meta = (ClampMin = "2.0", ClampMax = "128.0"))
	float BottomRadiusBlocks = 10.0f;

	/** Outer radius at the apex, measured in blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Cone", meta = (ClampMin = "0.0", ClampMax = "64.0"))
	float TopRadiusBlocks = 0.0f;

	/** Radial wall thickness. The middle remains empty and walkable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Cone", meta = (ClampMin = "0.5", ClampMax = "16.0"))
	float ShellThicknessBlocks = 1.0f;

	/** Fills the lowest block layer to close the cone with a destructible base. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Cone", meta = (DisplayName = "Generate Cone Base"))
	bool bGenerateConeBase = true;

	/** Dropdown: keep the existing point or add a decorative apex mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (DisplayName = "Apex Style"))
	EConeStructureApexStyle ApexStyle = EConeStructureApexStyle::CurrentApex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (EditCondition = "ApexStyle == EConeStructureApexStyle::CandySpire", EditConditionHides, ClampMin = "1.0", ClampMax = "12.0"))
	float CandySpireHeightBlocks = 4.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (EditCondition = "ApexStyle == EConeStructureApexStyle::CandySpire", EditConditionHides, ClampMin = "0.25", ClampMax = "6.0"))
	float CandySpireRadiusBlocks = 1.35f;

	/** Adds a narrow, upward-tapering spike at the exact centre of the Candy Spire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (DisplayName = "Generate Center Spike", EditCondition = "ApexStyle == EConeStructureApexStyle::CandySpire", EditConditionHides))
	bool bGenerateCandySpireCenterSpike = true;

	/** Height of the thin centre spike above the regular Candy Spire tip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (DisplayName = "Center Spike Height (Blocks)", EditCondition = "ApexStyle == EConeStructureApexStyle::CandySpire && bGenerateCandySpireCenterSpike", EditConditionHides, ClampMin = "0.1", ClampMax = "24.0"))
	float CandySpireCenterSpikeHeightBlocks = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (EditCondition = "ApexStyle == EConeStructureApexStyle::CandySpire", EditConditionHides))
	TObjectPtr<UMaterialInterface> CandySpireMaterial;

	/** Total height from the cone contact point to the highest part of the star. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (DisplayName = "Star Height (Blocks)", EditCondition = "ApexStyle == EConeStructureApexStyle::Star", EditConditionHides, ClampMin = "0.5", ClampMax = "24.0"))
	float StarHeightBlocks = 4.5f;

	/** Front-to-back depth of the faceted 3D star. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (DisplayName = "Star Thickness (Blocks)", EditCondition = "ApexStyle == EConeStructureApexStyle::Star", EditConditionHides, ClampMin = "0.1", ClampMax = "8.0"))
	float StarThicknessBlocks = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (EditCondition = "ApexStyle == EConeStructureApexStyle::Star", EditConditionHides))
	TObjectPtr<UMaterialInterface> StarMaterial;

	/** Total height of the crescent after its tips are tilted 45 degrees upward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (DisplayName = "Moon Height (Blocks)", EditCondition = "ApexStyle == EConeStructureApexStyle::Moon", EditConditionHides, ClampMin = "0.5", ClampMax = "24.0"))
	float MoonHeightBlocks = 4.5f;

	/** Front-to-back depth of the 3D crescent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (DisplayName = "Moon Thickness (Blocks)", EditCondition = "ApexStyle == EConeStructureApexStyle::Moon", EditConditionHides, ClampMin = "0.1", ClampMax = "8.0"))
	float MoonThicknessBlocks = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Apex", meta = (EditCondition = "ApexStyle == EConeStructureApexStyle::Moon", EditConditionHides))
	TObjectPtr<UMaterialInterface> MoonMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Blocks", meta = (ClampMin = "1.0", Units = "cm"))
	FVector BlockDimensions = FVector(100.0, 100.0, 100.0);

	/** Keeps destruction cells square in the horizontal plane by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Blocks", meta = (ClampMin = "1.0", ClampMax = "16.0"))
	float MaxHorizontalBlockAspectRatio = 1.0f;

	/** Forces X/Y to the same size so newly exposed destruction volumes are regular cubes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Blocks")
	bool bUseSquareHorizontalDestructionCells = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Blocks", meta = (ClampMin = "0.0", Units = "cm"))
	FVector BlockSpacing = FVector::ZeroVector;

	/** Number of facets around every true circular edge. Higher values look smoother and sharper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Geometry", meta = (ClampMin = "32", ClampMax = "512"))
	int32 CircularSegments = 192;

	/**
	 * Fills the visual portion of Block Spacing on the smooth cone skin. One closes seams between
	 * intact cells; zero deliberately preserves the complete block gap.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Geometry", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float SurfaceGapSealFraction = 1.0f;

	/** Runtime tessellation budget. Existing actors with very high segment counts are capped here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Geometry", meta = (ClampMin = "32", ClampMax = "512"))
	int32 MaxRuntimeCircularSegments = 128;

	/** Uses the smooth generated cone/ring mesh for collision instead of the backing voxel boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Geometry")
	bool bUseSmoothComplexCollision = false;

	/** Cooks changed cone collision off the game thread to prevent a hitch on every projectile hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Geometry")
	bool bUseAsyncConeCollisionCooking = true;

	/** Per-chunk safety cap; larger meshes automatically use reliable simple-cell collision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Geometry", meta = (ClampMin = "10000", ClampMax = "240000"))
	int32 MaxSmoothCollisionTrianglesPerChunk = 180000;

	/** True spatial chunk size. Smaller values reduce a hit hitch but create more components. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Performance", meta = (ClampMin = "4", ClampMax = "16"))
	int32 SpatialChunkSideLength = 8;

	/** Detailed Surface flashes the exact curved piece; Fast Compact is an optional cube fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Performance", meta = (DisplayName = "Destruction Flash Style"))
	EConeStructureDestructionFlashStyle DestructionFlashStyle =
		EConeStructureDestructionFlashStyle::DetailedSurface;

	/** Limits each hit to one short-lived Chaos fragment and a smaller active debris budget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Performance")
	bool bUseLightweightDebris = true;

	/** Hollow cones use compact shell cells, so they can safely exceed the floor's smaller default cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Generation", meta = (ClampMin = "250000", ClampMax = "2000000"))
	int32 MaxCompactConeCells = 1000000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms")
	bool bGeneratePlatforms = true;

	/** Chooses independent rings or a continuous, destructible helical ribbon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms", meta = (EditCondition = "bGeneratePlatforms"))
	EConeStructurePlatformStyle PlatformStyle = EConeStructurePlatformStyle::CircularRings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms", meta = (EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::CircularRings", EditConditionHides, ClampMin = "0", ClampMax = "32"))
	int32 PlatformCount = 4;

	/** Height of the lowest ring, or the spiral's starting bottom edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms", meta = (EditCondition = "bGeneratePlatforms", ClampMin = "0", ClampMax = "255"))
	int32 LowestPlatformHeightBlocks = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms", meta = (EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::CircularRings", EditConditionHides, ClampMin = "1", ClampMax = "128"))
	int32 PlatformVerticalSpacingBlocks = 5;

	/** Radial width outside the cone wall, shared by rings and the spiral. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms", meta = (DisplayName = "Outside Width (Blocks)", EditCondition = "bGeneratePlatforms", ClampMin = "0.0", ClampMax = "32.0", UIMin = "0.0", UIMax = "16.0"))
	float PlatformOverhangBlocks = 2.0f;

	/** Narrows the platform width as it rises; a rising spiral reaches zero width at its end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms", meta = (DisplayName = "Taper Platform Width To Zero", EditCondition = "bGeneratePlatforms"))
	bool bTaperPlatformsUpward = true;

	/** Multiplies the walk speed of any Character while it stands on a generated ring or spiral. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Movement", meta = (DisplayName = "Boost Character Walk Speed"))
	bool bBoostCharacterWalkSpeed = false;

	/** Applied to the Character's existing Max Walk Speed only while it is on a platform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Movement", meta = (DisplayName = "Walk Speed Multiplier", EditCondition = "bBoostCharacterWalkSpeed", ClampMin = "1.0", ClampMax = "20.0", UIMin = "1.0", UIMax = "10.0"))
	float CharacterWalkSpeedMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms", meta = (EditCondition = "bGeneratePlatforms", ClampMin = "1", ClampMax = "16"))
	int32 PlatformThicknessBlocks = 1;

	/** Height of the spiral's ending bottom edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Spiral", meta = (EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral", EditConditionHides, ClampMin = "0", ClampMax = "255"))
	int32 SpiralEndHeightBlocks = 20;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Spiral", meta = (DisplayName = "Spiral Turns", EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral", EditConditionHides, ClampMin = "0.25", ClampMax = "32.0"))
	float PlatformSpiralTurns = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Spiral", meta = (EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral", EditConditionHides, Units = "deg"))
	float SpiralStartAngleDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Spiral", meta = (DisplayName = "Spiral Clockwise", EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral", EditConditionHides))
	bool bPlatformSpiralClockwise = true;

	/** Samples per full turn. Higher values make the ramp and its collision smoother. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Spiral", meta = (EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral", EditConditionHides, ClampMin = "16", ClampMax = "256"))
	int32 SpiralSegmentsPerTurn = 64;

	/** Chooses backing-cell, rectangular-slice, or individual triangular-slice destruction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Spiral", meta = (DisplayName = "Spiral Destruction Mode", EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral", EditConditionHides))
	EConeStructureSpiralDestructionMode SpiralDestructionMode =
		EConeStructureSpiralDestructionMode::ExistingCellDestruction;

	/** Pushes the inner spiral edge into the cone so there is no visible connection seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Platforms|Spiral", meta = (EditCondition = "bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral", EditConditionHides, ClampMin = "0.0", ClampMax = "4.0"))
	float SpiralWallOverlapBlocks = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (DisplayName = "Generate Doors"))
	bool bGenerateEntrance = true;

	/** Total number of vertically repeated arched openings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (EditCondition = "bGenerateEntrance", ClampMin = "1", ClampMax = "32"))
	int32 DoorCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (DisplayName = "Door Width (Blocks)", EditCondition = "bGenerateEntrance", ClampMin = "1", ClampMax = "64"))
	int32 EntranceWidthBlocks = 3;

	/** Total height, including the arch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (DisplayName = "Door Height (Blocks)", EditCondition = "bGenerateEntrance", ClampMin = "1", ClampMax = "128"))
	int32 EntranceHeightBlocks = 4;

	/** Elliptical upper portion of each door. Zero produces a rectangular opening. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (EditCondition = "bGenerateEntrance", ClampMin = "0.0", ClampMax = "64.0"))
	float DoorArchHeightBlocks = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (EditCondition = "bGenerateEntrance", ClampMin = "0", ClampMax = "255"))
	int32 FirstDoorBottomHeightBlocks = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (EditCondition = "bGenerateEntrance", ClampMin = "1", ClampMax = "128"))
	int32 DoorVerticalSpacingBlocks = 6;

	/** Zero points toward local -Y. Rotate the Actor to aim the complete structure in the level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (DisplayName = "First Door Angle", EditCondition = "bGenerateEntrance", Units = "deg"))
	float EntranceAngleDegrees = 0.0f;

	/** Rotates each higher door around the cone relative to the previous one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Doors", meta = (EditCondition = "bGenerateEntrance", Units = "deg"))
	float DoorAngleStepDegrees = 0.0f;

	/** Direct material for the tapered cone shell. Falls back to the selected palette index when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Materials")
	TObjectPtr<UMaterialInterface> ConeMaterial;

	/** Direct material for every circular platform. Falls back to the selected palette index when empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Materials")
	TObjectPtr<UMaterialInterface> PlatformMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Materials", meta = (ClampMin = "0"))
	int32 ConeMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Materials", meta = (ClampMin = "0"))
	int32 PlatformMaterialIndex = 1;

	/** Spawns short-lived physical fragments whenever a cone cell is destroyed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Debris")
	bool bScatterDestroyedPieces = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "1", ClampMax = "8"))
	int32 DebrisPiecesPerCell = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "0.1", Units = "s"))
	float DebrisLifetime = 1.5f;

	/** Debris is removed when this physical phase ends, so it can never freeze in mid-air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "0.05", Units = "s"))
	float DebrisPhysicsDuration = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "0.0"))
	float DebrisImpulse = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "0.0"))
	float DebrisUpwardImpulse = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	FVector2D DebrisScaleRange = FVector2D(0.24, 0.42);

	/** Caps every fragment axis independently from unusually wide backing cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "2.0", ClampMax = "100.0", Units = "cm"))
	float MaxDebrisPieceSize = 32.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "0", ClampMax = "1024"))
	int32 MaxActiveDebrisPieces = 128;

	/** Final runtime safety budget, including actors saved with the older 128-piece default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Debris", meta = (ClampMin = "0", ClampMax = "1024"))
	int32 MaxRuntimeDebrisPieces = 48;

	virtual bool CalculateLayout() override;
	virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
	virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& OutMesh) const override;
	virtual FTransform GetMusicEqualizerTransform(FIntPoint Cell) const override;
	virtual bool UsesMusicPieceMaterials() const override { return true; }
	virtual void RenderMusicEqualizerCells(TConstArrayView<int32> CellIndices) override;
	virtual void ClearMusicPieceMaterials() override;
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const override;
	virtual void ConfigureChunkComponent(UDynamicMeshComponent* Component) const override;
	virtual void ClearAdditionalRuntimeEffects() override;
	virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
	virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
	virtual void SpawnCellFlash(FIntPoint Cell, double CurrentTime) override;
	virtual void FinalizeDestructionBatch(double CurrentTime) override;
	virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& OutCell) const override;
	virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
	virtual int32 SelectCellsInRadius(
		FVector WorldLocation,
		float Radius,
		EChunkedFloorBlastShape Shape,
		TFunctionRef<void(int32, float)> CellFunction) override;
	virtual void MarkCellChunksDirty(FIntPoint Cell) override;
	virtual bool SupportsFixedAnimation() const override { return true; }
	virtual bool CreateFixedAnimationResources() override;
	virtual bool HasFixedAnimationResources() const override;
	virtual void DestroyFixedAnimationResources() override;
	virtual void RenderFixedAnimation(TConstArrayView<float> AnimationAlphas) override;

private:
    friend class FStructureTraversalRimTest;
    float EffectiveTopRadiusBlocks=0;
    float GetSpiralRimHeight(float TopZ) const;
	static constexpr int32 MusicMaterialSlot = 2;
	TSet<int32> MusicHighlightedCells;
	mutable TMap<int32, TArray<int32>> MusicChunkOriginalMaterials;
	TArray<int32> MusicCellGroupIndices;
	TArray<TArray<int32>> MusicGlowGroups;
	float CachedMusicGlowGroupSize = 0.0f;
	EMusicAnimationType CachedMusicGroupAnimationType = EMusicAnimationType::TravelingColumns;
	struct FMusicSquareRow
	{
		double MinZ = 0, MaxZ = 0, Side = 0;
		int32 Columns = 0, FirstGroup = 0;
	};
	struct FMusicSquareTriangle
	{
		FVector3d A, B, C;
		int32 CellIndex = INDEX_NONE;
	};
	TArray<FMusicSquareRow> MusicSquareRows;
	TSet<int32> MusicHighlightedSquareGroups;
	mutable TMap<int32, TArray<int32>> MusicChunkShellTriangles;
	mutable TMap<int32, TMap<int32, TArray<FMusicSquareTriangle>>> MusicSquareChunkTriangles;
	mutable bool bMusicSquareMeshDirty = true;
	UPROPERTY(Transient)
	TObjectPtr<UDynamicMeshComponent> MusicSquareComponent;
	void BuildMusicSquareRows(float Size);
	void CacheMusicSquareChunk(int32 Chunk);
	void RenderMusicSurfaceSquares(const TSet<int32>& SelectedGroups);
	void EnsureMusicGlowGroups();
	FIntVector GetMusicGlowGroupKey(int32 CellIndex, float GroupSize) const;
	void SetMusicTriangleCells(UE::Geometry::FDynamicMesh3& Mesh, int32 FirstTriangle, FIntPoint Cell) const;
	void CacheMusicChunkMaterials(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& Mesh) const;
	void AddMusicAffectedChunks(int32 CellIndex, TSet<int32>& OutChunks) const;

	int32 GridDiameterBlocks = 1;
	int32 GridHeightBlocks = 1;
	FVector ConeGridLocalMin = FVector::ZeroVector;
	float CachedRadialBlockUnit = 100.0f;
	TArray<TArray<FIntPoint>> PhysicalCellsByLayer;
	TArray<TMap<int32, int32>> PhysicalCellSlotsByLayer;
	FIntVector SpatialChunkCount = FIntVector::ZeroValue;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DebrisCubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UDynamicMeshComponent> ConeAnimationComponent;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ConeAnimationMaterialInstance;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> ConeAnimationRowComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> ConeAnimationRowMaterialInstances;

	TArray<TWeakObjectPtr<UStaticMeshComponent>> ActiveDebrisComponents;
	TArray<TWeakObjectPtr<UDynamicMeshComponent>> ActiveSurfaceFlashComponents;
	TWeakObjectPtr<UDynamicMeshComponent> CurrentSurfaceFlashComponent;
	TSet<FIntPoint> CurrentSurfaceFlashCells;
	double CurrentSurfaceFlashTime = -TNumericLimits<double>::Max();
	mutable TSet<FIntPoint> SurfaceFlashCellFilter;
	FTimerHandle CharacterSpeedBoostTimerHandle;
	TMap<TWeakObjectPtr<ACharacter>, float> CharacterBaseWalkSpeeds;
	TMap<TWeakObjectPtr<ACharacter>, float> CharacterAppliedWalkSpeeds;
	TSet<int32> DestroyedSpiralSegmentIndices;
	TSet<int32> DestroyedSpiralTriangleSliceIndices;
	bool bSpiralCollisionReadyForAsyncUpdates = false;
	UPROPERTY(Transient) TObjectPtr<USpiralPlatformGlow> SpiralPlatformGlowRuntime;
	UFUNCTION() void RefreshSpiralPlatformGlowAfterGeneration();
	bool bConeAnimationGeometryDirty = false;
	TArray<int32> ConeAnimationRowAssignments;

	int32 GetSpatialChunkIndex(int32 X, int32 Y, int32 Z) const;
	bool GetSpatialChunkBounds(
		int32 ChunkIndex,
		FIntVector& OutMinimum,
		FIntVector& OutMaximum) const;
	void RebuildApexDecoration();

	bool DecodeCell(FIntPoint Cell, int32& OutX, int32& OutY, int32& OutZ) const;
	FIntPoint EncodeCell(int32 X, int32 Y, int32 Z) const;
	FVector GetPhysicalCellLocalCenter(int32 X, int32 Y, int32 Z) const;
	bool CellExists3D(int32 X, int32 Y, int32 Z) const;
	bool DoesExistingConeCellSupportSpiral(float SurfaceAngle, float LocalZ) const;
	bool IsConeShellCell(int32 X, int32 Y, int32 Z) const;
	bool IsConeBaseCell(int32 X, int32 Y, int32 Z) const;
	bool IsPlatformCell(int32 X, int32 Y, int32 Z) const;
	bool IsCircularPlatformCell(int32 X, int32 Y, int32 Z) const;
	bool IsSpiralPlatformCell(int32 X, int32 Y, int32 Z) const;
	bool IsEntranceCell(int32 X, int32 Y, int32 Z) const;
	float GetConeOuterRadiusAtHeight(float LocalZ) const;
	float GetConeInnerRadiusAtHeight(float LocalZ) const;
	float GetPlatformWidthScaleAtHeight(float LocalZ) const;
	float GetPlatformOuterRadiusAtHeight(float LocalZ) const;
	float GetSpiralThickness() const;
	float GetSpiralBottomHeight(bool bEnd) const;
	int32 GetSpiralSegmentCount() const;
	bool ResolveSpiralSegmentFromHit(
		const FHitResult& Hit,
		int32& OutSegmentIndex,
		int32* OutTriangleSliceIndex = nullptr) const;
	bool DestroySpiralSegmentFromHit(const FHitResult& Hit);
	int32 DestroySpiralSegmentsInRadiusFromHit(const FHitResult& Hit, float Radius);
	int32 DestroySpiralSegmentsInSphere(const FVector& WorldImpact, float Radius,
		int32 HitSegmentIndex = INDEX_NONE, int32 HitTriangleSliceIndex = INDEX_NONE);
	void AddSpiralCrossSectionCells(
		FIntPoint Cell,
		FVector WorldImpact,
		TSet<int32>& OutCellIndices) const;
	void RebuildSpiralSegmentChunks(int32 SegmentIndex);
	void RebuildSpiralSegmentChunks(const TSet<int32>& SegmentIndices);
	bool IsSpiralSegmentDestroyed(int32 SegmentIndex) const;
	bool IsSpiralTriangleSliceDestroyed(int32 SegmentIndex, int32 TriangleSliceIndex) const;
	void GetCellRadialRange(int32 X, int32 Y, float& OutMinimumRadius, float& OutMaximumRadius) const;
	void AppendClippedSurfaceTriangle(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 ChunkIndex,
		int32 LogicalZ,
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3f& Normal,
		int32 MaterialIndex) const;
	void AppendClippedSurfaceTriangleAcrossLayers(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 ChunkIndex,
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3f& Normal,
		int32 MaterialIndex) const;
	void AppendAnnularSurface(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 ChunkIndex,
		int32 LogicalZ,
		float LocalZ,
		float InnerRadius,
		float OuterRadius,
		bool bTopFace,
		int32 MaterialIndex) const;
	void AppendConeSurfaces(UE::Geometry::FDynamicMesh3& Mesh, int32 ChunkIndex) const;
	void AppendConeGlowBandSurfaces(
		UE::Geometry::FDynamicMesh3& Mesh,
		float BandBottomZ,
		float BandTopZ) const;
	void AppendConeSpiralGlowSurfaces(
		UE::Geometry::FDynamicMesh3& Mesh,
		float PhaseOffsetRadians) const;
	void AppendPlatformSurfaces(UE::Geometry::FDynamicMesh3& Mesh, int32 ChunkIndex) const;
	void AppendSpiralSurfaces(UE::Geometry::FDynamicMesh3& Mesh, int32 ChunkIndex, int32 OnlySlice = INDEX_NONE) const;
	bool IsPointInsideRenderedStructure(const FVector3d& LocalPoint) const;
	void AppendClippedDestructionCutFace(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3d& D,
		const FVector3f& Normal,
		int32 MaterialIndex) const;
	void AppendDestroyedCellCutFaces(
		UE::Geometry::FDynamicMesh3& Mesh,
		FIntPoint Cell) const;
	void AppendSurfaceFlashBoundaryFaces(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TSet<FIntPoint>& FlashCells) const;
	bool ShouldAppendPlanarCutFace(int32 X, int32 Y, int32 Z) const;
	bool IsCharacterStandingOnPlatform(const ACharacter* Character) const;
	void StartCharacterSpeedBoostMonitoring();
	void RefreshCharacterSpeedBoosts();
	void StopCharacterSpeedBoostMonitoring();
	void RestoreCharacterWalkSpeed(TWeakObjectPtr<ACharacter> Character);
	void CreateConeAnimationComponent();
	void CreateSineRowAnimationComponents();
	void UpdateBottomToTopGlowWaves(TConstArrayView<float> AnimationAlphas);
	void UpdateBottomToTopSineRows(TConstArrayView<float> AnimationAlphas);
	void UpdateRisingSpiralGlowWaves(TConstArrayView<float> AnimationAlphas);
	int32 GetEffectiveConeChunkSideLength() const;
	int32 GetEffectiveCircularSegments() const;
	int32 GetEffectiveDebrisLimit() const;
	bool CanUseSmoothCollisionForChunk(int32 ChunkIndex) const;
	UMaterialInterface* ResolveStructureMaterial(bool bPlatform) const;

	UFUNCTION()
	void HandleConeCellDestroyed(FIntPoint Cell, FVector WorldLocation, bool bRadialDestruction);

	void SpawnDebrisForCell(FIntPoint Cell);
	void SpawnDebrisAtLocalPoint(const FVector& LocalCenter, bool bPlatformCell);
	void ClearDebris();
};
