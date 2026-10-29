// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DestructiblePlanarAnimation.h"
#include "MusicReactionListener.h"
#include "ChunkedDestructibleFloor.generated.h"

class UDynamicMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class USoundAttenuation;
class USoundBase;
struct FKAggregateGeom;

UENUM(BlueprintType)
enum class EMusicAnimationType : uint8
{
	TravelingColumns UMETA(DisplayName = "Traveling Columns"),
	Equalizer UMETA(DisplayName = "Equalizer")
};

UENUM(BlueprintType)
enum class EMusicEqualizerBaseEdge : uint8
{
	MinX UMETA(DisplayName = "Min X (Grow Along +X)"),
	MaxX UMETA(DisplayName = "Max X (Grow Along -X)"),
	MinY UMETA(DisplayName = "Min Y (Grow Along +Y)"),
	MaxY UMETA(DisplayName = "Max Y (Grow Along -Y)")
};

namespace UE::Geometry
{
	class FDynamicMesh3;
}

/** Defines how the requested area is converted into a whole number of cells. */
UENUM(BlueprintType)
enum class EChunkedFloorFillRule : uint8
{
	InsideArea UMETA(DisplayName = "Stay Inside Area"),
	CoverArea UMETA(DisplayName = "Cover Entire Area"),
	StretchCells UMETA(DisplayName = "Stretch Cells To Fit")
};

UENUM(BlueprintType)
enum class EFloorSideNeonMode : uint8
{
	Off UMETA(DisplayName = "Off"),
	FullSide UMETA(DisplayName = "Full Side"),
	Stripe UMETA(DisplayName = "Stripe"),
	FullSideWithStripe UMETA(DisplayName = "Full Side + Center Stripe")
};

UENUM(BlueprintType)
enum class EFloorSideNeonScope : uint8
{
	OuterBoundary UMETA(DisplayName = "Outer Boundary Only"),
	AllExposedSides UMETA(DisplayName = "All Exposed Sides (Including Damage)")
};

/** Neon drawn on the existing side triangles, with one shared material instance per floor. */
USTRUCT(BlueprintType)
struct FFloorSideNeonSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon")
	EFloorSideNeonMode Mode = EFloorSideNeonMode::Off;

	/** Outer boundary uses the original silhouette; damage and cell gaps do not become new perimeter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (EditCondition = "Mode != EFloorSideNeonMode::Off", EditConditionHides))
	EFloorSideNeonScope Scope = EFloorSideNeonScope::OuterBoundary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (EditCondition = "Mode != EFloorSideNeonMode::Off", EditConditionHides, HideAlphaChannel))
	FLinearColor Color = FLinearColor(0.0f, 0.5f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (EditCondition = "Mode != EFloorSideNeonMode::Off", EditConditionHides, ClampMin = "0.0", ClampMax = "100.0", UIMax = "20.0"))
	float Intensity = 8.0f;

	/** Thickness in the actor's local centimetres; actor scaling scales the stripe too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (DisplayName = "Center Stripe Width", EditCondition = "Mode == EFloorSideNeonMode::Stripe || Mode == EFloorSideNeonMode::FullSideWithStripe", EditConditionHides, ClampMin = "0.0", UIMax = "30.0", Units = "cm"))
	float StripeWidth = 4.0f;

	/** 0 = bottom edge, 0.5 = middle, 1 = top edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (DisplayName = "Center Stripe Position", EditCondition = "Mode == EFloorSideNeonMode::Stripe || Mode == EFloorSideNeonMode::FullSideWithStripe", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
	float StripePosition = 0.5f;

	/** Additional light from the thin stripe, layered over the full side glow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (DisplayName = "Center Stripe Color", EditCondition = "Mode == EFloorSideNeonMode::FullSideWithStripe", EditConditionHides, HideAlphaChannel))
	FLinearColor StripeColor = FLinearColor(0.55f, 0.85f, 1.0f);

	/** Emissive brightness of the neon core and its halo; zero leaves just the full side glow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (DisplayName = "Center Stripe Intensity", EditCondition = "Mode == EFloorSideNeonMode::FullSideWithStripe", EditConditionHides, ClampMin = "0.0", ClampMax = "1000.0", UIMax = "250.0"))
	float StripeIntensity = 64.0f;

	/** Soft light spread on each side of the core, in local centimetres. Zero disables the halo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (DisplayName = "Center Stripe Glow Width", EditCondition = "Mode == EFloorSideNeonMode::FullSideWithStripe", EditConditionHides, ClampMin = "0.0", UIMax = "50.0", Units = "cm"))
	float StripeGlowWidth = 12.0f;

	/** Halo brightness relative to the core. Zero keeps only the sharp center stripe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (DisplayName = "Center Stripe Glow Strength", EditCondition = "Mode == EFloorSideNeonMode::FullSideWithStripe", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
	float StripeGlowStrength = 0.15f;

	/** Unlit side-panel color outside the stripe. Top/bottom and unselected sides keep their materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Side Neon", meta = (EditCondition = "Mode == EFloorSideNeonMode::Stripe", EditConditionHides, HideAlphaChannel))
	FLinearColor BodyColor = FLinearColor(0.015f, 0.015f, 0.02f);
};

/** Built-in coordinate-driven material patterns. */
UENUM(BlueprintType)
enum class EChunkedFloorPattern : uint8
{
	Solid,
	Checkerboard,
	StripesX UMETA(DisplayName = "Vertical Stripes"),
	StripesY UMETA(DisplayName = "Horizontal Stripes"),
	DiagonalStripes,
	ConcentricRings,
	SeededRandom,
	HypnoticSpiral UMETA(DisplayName = "Spiral"),
	HeartMotif UMETA(DisplayName = "Heart"),
	StarMotif UMETA(DisplayName = "Star"),
	FlowerMotif UMETA(DisplayName = "Flower"),
	CirclesAndPaths UMETA(DisplayName = "Circles & Paths (Lobby)")
};

/** Geometry and direction controls shown only while the Spiral floor pattern is selected. */
USTRUCT(BlueprintType)
struct FChunkedSpiralPatternSettings
{
	GENERATED_BODY()

	/** Number of complete rotations from the center to the furthest edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral", meta = (ClampMin = "1.0", ClampMax = "12.0"))
	float Turns = 4.5f;

	/** Stripe width relative to the distance between consecutive rotations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float BandFraction = 0.52f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral", meta = (Units = "cm"))
	FVector2D CenterOffset = FVector2D::ZeroVector;

	/** Minimum curve samples; an adaptive per-turn minimum also prevents visible corners. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral", meta = (ClampMin = "128", ClampMax = "4096"))
	int32 CurveResolution = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral")
	bool bClockwise = true;
};

/** Immutable 2D triangle used by the shared spiral geometry cache. */
struct FChunkedCachedSpiralTriangle
{
	FVector2D A = FVector2D::ZeroVector;
	FVector2D B = FVector2D::ZeroVector;
	FVector2D C = FVector2D::ZeroVector;
	FVector2D BoundsMin = FVector2D::ZeroVector;
	FVector2D BoundsMax = FVector2D::ZeroVector;
};

/** Footprint used when selecting cells around an explosion. */
UENUM(BlueprintType)
enum class EChunkedFloorBlastShape : uint8
{
	Circle,
	Square,
	Diamond,
	/** World-space sphere intersecting cell volumes, including vertical distance. */
	Sphere
};

/** Selects the reusable material animation rendered over a supported destructible structure. */
UENUM(BlueprintType)
enum class EConeStructureAnimationType : uint8
{
	None UMETA(DisplayName = "None"),
	BottomToTopGlowWave UMETA(DisplayName = "Bottom To Top Glow Wave"),
	BottomToTopSineRows UMETA(DisplayName = "Bottom To Top Sine Rows"),
	RisingSpiralGlow UMETA(DisplayName = "Rising Candy Cane Spiral Glow")
};

/** Read-only snapshot of one logical floor cell. */
USTRUCT(BlueprintType)
struct FChunkedFloorCellInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cell")
	bool bExists = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cell")
	bool bDestructible = true;

	UPROPERTY(BlueprintReadOnly, Category = "Cell")
	bool bPendingDestruction = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cell")
	int32 MaterialIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cell")
	float Health = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
	FChunkedFloorCellDestroyedSignature,
	FIntPoint, Cell,
	FVector, WorldLocation,
	bool, bRadialDestruction);

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FChunkedFloorRegeneratedSignature);

/**
 * Runtime-generated, chunked and individually destructible floor.
 *
 * Cells are lightweight data entries, never individual Actors or Components. Each chunk is rendered
 * and collided as one UDynamicMeshComponent. At zero spacing, hidden internal faces are omitted and
 * horizontal faces with the same material are greedily merged.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API AChunkedDestructibleFloor : public AActor, public IMusicReactionListener
{
	GENERATED_BODY()
	friend class FDestructibleMusicAnimationTest;
	friend class FDestructibleEqualizerTest;
	friend class FDestructiblePlanarAnimationTest;
	friend class UDestructiblePlanarAnimation;
	friend class FMusicReactionTimelineTest;
	friend class FDestructibleQueueTest;
	friend class FDestructibleCollisionTest;
	friend class FDestructibleStructureTest;
	friend class FFloorSideNeonTest;

public:
	AChunkedDestructibleFloor();

	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/** Recreates the logical grid and every chunk. Overlapping cell state can optionally survive a resize. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	virtual void GenerateFloor(bool bPreserveCellState = false);

	/** Changes area, cell size and spacing in one rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	virtual void ConfigureFloor(
		FVector2D NewAreaSize,
		FVector NewCellDimensions,
		FVector2D NewCellSpacing,
		bool bPreserveCellState = true);

	/** Runtime cell-size change. This rebuilds mesh and collision, so it should not be called every frame. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	virtual void SetCellDimensions(FVector NewCellDimensions, bool bPreserveCellState = true);

	/** Runtime gap change. This rebuilds mesh and collision, so it should not be called every frame. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	void SetCellSpacing(FVector2D NewCellSpacing, bool bPreserveCellState = true);

	/** Selects and immediately reapplies a built-in pattern. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	void SetFloorPattern(EChunkedFloorPattern NewPattern);

	/** Configures every built-in pattern parameter and reapplies it in one rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	void ConfigureMaterialPattern(
		EChunkedFloorPattern NewPattern,
		int32 NewPrimaryMaterialIndex,
		int32 NewSecondaryMaterialIndex,
		int32 NewPatternCellScale = 1,
		int32 NewRandomSeed = 1337);

	/** Re-evaluates the current pattern for every cell and rebuilds all chunks. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	virtual void ApplyMaterialPattern();

	/** Pushes the current material array to every chunk. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	void RefreshFloorMaterials();

	/** Mode on/off and scope changes rebuild chunks; appearance-only changes just update the material. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Side Neon")
	void SetSideNeonSettings(const FFloorSideNeonSettings& NewSettings);

	/** Changes the shared neon color and intensity without rebuilding geometry or collision. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Side Neon")
	void SetSideNeonColor(FLinearColor NewColor, float NewIntensity = 8.0f);

	UFUNCTION(BlueprintPure, Category = "Chunked Floor|Side Neon")
	FFloorSideNeonSettings GetSideNeonSettings() const { return SideNeon; }

	/** Replaces the runtime material palette and safely clamps every existing cell material index. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	void SetMaterialPalette(const TArray<UMaterialInterface*>& NewMaterials);

	/** Replaces the glow material and its scalar settings at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Effects")
	void ConfigureDestructionFlash(
		UMaterialInterface* NewFlashMaterial,
		FLinearColor NewFlashColor,
		float NewFlashIntensity,
		float NewFlashDuration);

	/** Called by the project music director for an onset or metronome beat. Only intact cells glow. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void TriggerMusicReaction(float NormalizedStrength, bool bIsOnset);
	virtual void ApplyMusicEvent(const struct FMusicReactionEvent& Event, double CurrentTime) override;
	virtual void ResetMusicPlayback() override;
	double GetMusicAnimationTime() const;

	/** Cheap shared query used by the music director; derived destructibles need no duplicate glue. */
	virtual bool IsMusicReactionEnabled() const override { return bReactToMusic && (bReactToOnsets || bReactToBPM || bReactToKeyPoints); }

	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetReactToMusic(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetMusicAnimationType(EMusicAnimationType Type);
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetMusicEqualizerBaseEdge(EMusicEqualizerBaseEdge Edge);
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetReactToOnsets(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetReactToBPM(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetReactToKeyPoints(bool bEnabled);

	/** Starts the selected reusable material animation on a structure that supports fixed animation. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Fixed")
	void PlayFixedAnimation();

	/** Stops the reusable material animation and releases its transient render resources. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Fixed")
	void StopFixedAnimation();

	/** Used by the Details customization to hide fixed settings on unsupported destructibles. */
	bool IsFixedAnimationSupported() const { return SupportsFixedAnimation(); }

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetPlanarAnimationType(EPlanarAnimationType Type);

	UFUNCTION(BlueprintPure, Category = "Animation")
	EConeStructureAnimationType GetFixedAnimationType() const;

	UFUNCTION(BlueprintPure, Category = "Animation")
	bool HasFixedAnimationSelected() const { return GetFixedAnimationType() != EConeStructureAnimationType::None; }
	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsFixedWaveAnimation() const { return GetFixedAnimationType() == EConeStructureAnimationType::BottomToTopGlowWave; }
	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsFixedSineAnimation() const { return GetFixedAnimationType() == EConeStructureAnimationType::BottomToTopSineRows; }
	UFUNCTION(BlueprintPure, Category = "Animation")
	bool IsFixedSpiralAnimation() const { return GetFixedAnimationType() == EConeStructureAnimationType::RisingSpiralGlow; }

	UFUNCTION(BlueprintCallable, Category = "Animation")
	void SetPlanarFlow(FDestructiblePlanarFlow Flow);

	/** Reflected so Details-panel EditCondition expressions can query the native override. */
	UFUNCTION(BlueprintPure, Category = "Animation")
	virtual bool UsesPlanarAnimation() const { return true; }


	/** Replaces the default tick and its most commonly tuned settings at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Sound")
	void ConfigureDestroyTickSound(
		USoundBase* NewSound,
		float NewVolume,
		FVector2D NewPitchRange);

	/** Damages the existing cell at a world-space hit point. Returns true when a cell accepted damage. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Damage")
	bool DamageCellAtWorldLocation(FVector WorldLocation, float DamageAmount = 1.0f);

	/**
	 * Damages the solid cell struck by a collision hit. Unlike a raw world point, this uses the
	 * impact normal to resolve points that lie exactly on a cell boundary, including exposed sides.
	 */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Damage")
	bool DamageCellFromHit(const FHitResult& Hit, float DamageAmount = 1.0f);

	/** Applies the floor's configured bomb behavior around a world-space impact. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Damage")
	int32 ApplyExplosionAtWorldLocation(FVector WorldLocation, float Radius, float DamageAmount);

	/** Applies the configured bomb behavior just inside the surface described by a collision hit. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Damage")
	int32 ApplyExplosionFromHit(const FHitResult& Hit, float Radius, float DamageAmount);

	/** Damages cells using explicit radial selection settings. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Damage")
	int32 DamageCellsInRadius(
		FVector WorldLocation,
		float Radius,
		float DamageAmount,
		EChunkedFloorBlastShape Shape,
		bool bUseDamageFalloff);

	/** Queues one cell for destruction, bypassing health but respecting its destructible flag. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Damage")
	bool DestroyCell(FIntPoint Cell);

	/** Queues every destructible cell inside a radial footprint, bypassing health. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Damage")
	int32 DestroyCellsInRadius(FVector WorldLocation, float Radius, EChunkedFloorBlastShape Shape);

	/** Destroys every cell around the inside of a collision hit, bypassing health. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Damage")
	int32 DestroyCellsFromHit(
		const FHitResult& Hit,
		float Radius,
		EChunkedFloorBlastShape Shape = EChunkedFloorBlastShape::Circle);

	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Cells")
	bool RestoreCell(FIntPoint Cell);

	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Cells")
	void RestoreAllCells();

	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Cells")
	bool SetCellDestructible(FIntPoint Cell, bool bNewDestructible);

	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Cells")
	void SetAllCellsDestructible(bool bNewDestructible);

	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	bool SetCellMaterial(FIntPoint Cell, int32 NewMaterialIndex);

	/** Converts a world point to a real cell. Points in spacing gaps deliberately return false. */
	UFUNCTION(BlueprintPure, Category = "Chunked Floor|Cells")
	virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const;

	UFUNCTION(BlueprintPure, Category = "Chunked Floor|Cells")
	bool GetCellInfo(FIntPoint Cell, FChunkedFloorCellInfo& OutInfo) const;

	UFUNCTION(BlueprintPure, Category = "Chunked Floor|Generation")
	FIntPoint GetCellCount() const { return CellCount; }

	UFUNCTION(BlueprintPure, Category = "Chunked Floor|Generation")
	FVector GetEffectiveCellDimensions() const { return EffectiveCellDimensions; }

	UFUNCTION(BlueprintPure, Category = "Chunked Floor|Generation")
	FVector2D GetEffectiveGridSize() const { return EffectiveGridSize; }

	/** Fired after a cell actually disappears (after an optional radial ripple delay). */
	UPROPERTY(BlueprintAssignable, Category = "Chunked Floor|Events")
	FChunkedFloorCellDestroyedSignature OnCellDestroyed;

	UPROPERTY(BlueprintAssignable, Category = "Chunked Floor|Events")
	FChunkedFloorRegeneratedSignature OnFloorRegenerated;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunked Floor|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> FloorRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chunked Floor|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInstancedStaticMeshComponent> FlashInstances;

	/** Allocated on demand for shapes that use the pooled equalizer renderer. */
	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Chunked Floor|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UInstancedStaticMeshComponent> MusicEqualizerInstances;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Generation", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D AreaSize = FVector2D(5000.0, 5000.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Generation", meta = (ClampMin = "1.0", Units = "cm"))
	FVector CellDimensions = FVector(100.0, 100.0, 50.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Generation", meta = (ClampMin = "0.0", Units = "cm"))
	FVector2D CellSpacing = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Generation")
	EChunkedFloorFillRule FillRule = EChunkedFloorFillRule::CoverArea;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Generation", meta = (ClampMin = "4", ClampMax = "128"))
	int32 ChunkSideLength = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Generation", meta = (ClampMin = "1", ClampMax = "2000000"))
	int32 MaxGeneratedCells = 250000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Geometry")
	bool bGenerateBottomFaces = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Geometry")
	bool bGenerateCollision = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Geometry")
	bool bCastFloorShadow = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Geometry", meta = (ClampMin = "1.0", Units = "cm"))
	float UVWorldUnits = 100.0f;

	/** Static emissive sides: no added lights, meshes, components, timers or actor ticks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chunked Floor|Side Neon", meta = (EditCondition = "UsesPlanarAnimation()", EditConditionHides, ShowOnlyInnerProperties))
	FFloorSideNeonSettings SideNeon;

	/** Serialized soft reference is cookable and only loads the asset when neon is enabled. */
	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> SideNeonMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/Materials/Structures/M_FloorSideNeon.M_FloorSideNeon")));

	UPROPERTY(Transient, DuplicateTransient)
	TObjectPtr<UMaterialInstanceDynamic> SideNeonMaterialInstance;

	/** Material index 0 and 1 are used by built-in two-tone patterns by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials")
	TArray<TObjectPtr<UMaterialInterface>> FloorMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials")
	EChunkedFloorPattern FloorPattern = EChunkedFloorPattern::Solid;

	/** Native expandable settings used as the conditional Spiral child group in Details. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DisplayName = "Spiral", EditCondition = "FloorPattern == EChunkedFloorPattern::HypnoticSpiral", EditConditionHides, DisplayAfter = "FloorPattern", DisplayPriority = "0", FullyExpand = "true"))
	FChunkedSpiralPatternSettings SpiralSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (ClampMin = "0"))
	int32 PrimaryMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (ClampMin = "0"))
	int32 SecondaryMaterialIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (ClampMin = "1"))
	int32 PatternCellScale = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials")
	int32 PatternRandomSeed = 1337;

	/** Number of complete rotations from the center to the furthest edge. */
	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Spiral settings."))
	float SpiralTurns = 4.5f;

	/** Stripe width relative to the distance between consecutive rotations. */
	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Spiral settings."))
	float SpiralBandFraction = 0.52f;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Spiral settings."))
	float SpiralRotationDegrees = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Spiral settings."))
	FVector2D SpiralCenterOffset = FVector2D::ZeroVector;

	/** Minimum curve samples; an adaptive per-turn minimum is also enforced to avoid visible corners. */
	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Spiral settings."))
	int32 SpiralCurveResolution = 1024;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Spiral settings."))
	bool bSpiralClockwise = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Damage", meta = (ClampMin = "0.01"))
	float DefaultCellHealth = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Damage")
	bool bDestructibleByDefault = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Explosion")
	EChunkedFloorBlastShape DefaultBlastShape = EChunkedFloorBlastShape::Circle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Explosion")
	bool bUseRadialDamageFalloff = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Explosion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinimumRadialDamageFraction = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Explosion")
	bool bUseDestructionRipple = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Explosion", meta = (ClampMin = "1.0", Units = "cm/s"))
	float DestructionRippleSpeed = 3000.0f;

	/** Default project glow material is selected in the constructor and can be replaced per floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Effects")
	TObjectPtr<UMaterialInterface> FlashMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Effects", meta = (HideAlphaChannel))
	FLinearColor FlashColor = FLinearColor(1.0f, 0.2f, 0.02f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Effects", meta = (ClampMin = "0.0"))
	float FlashIntensity = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Effects", meta = (ClampMin = "0.0", Units = "s"))
	float FlashDuration = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Effects", meta = (ClampMin = "0.01"))
	float FlashScale = 1.03f;

	/**
	 * Shared fixed-animation settings. A few C++ names retain their original Cone prefix so existing
	 * serialized cone actors keep their configured values after the settings moved into this base.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed", meta = (DisplayName = "Animation Type"))
	EConeStructureAnimationType ConeAnimationType = EConeStructureAnimationType::BottomToTopGlowWave;

	/** Separate selection preserves previously idle horizontal actors in existing maps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPlanarAnimationType, Category = "Animation|Fixed", meta = (DisplayName = "Animation Type"))
	EPlanarAnimationType PlanarAnimationType = EPlanarAnimationType::None;

	/** Shared by fixed waves and music reactions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetPlanarFlow, Category = "Animation|Flow")
	FDestructiblePlanarFlow PlanarFlow;

	/** Surface material with the shared planar-mask parameter contract. Loaded only on activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Flow")
	TSoftObjectPtr<UMaterialInterface> PlanarAnimationMaterial = TSoftObjectPtr<UMaterialInterface>(
		FSoftObjectPath(TEXT("/Game/Materials/Structures/M_PlanarAnimation.M_PlanarAnimation")));


	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed", meta = (DisplayName = "Auto Play Fixed Animation", EditCondition = "HasFixedAnimationSelected"))
	bool bAutoPlayConeAnimation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed", meta = (DisplayName = "Loop Fixed Animation", EditCondition = "HasFixedAnimationSelected"))
	bool bLoopConeAnimation = true;

	/** Allows a new wave to start before earlier waves have finished. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed", meta = (DisplayName = "Allow Overlapping Waves", EditCondition = "HasFixedAnimationSelected && !IsFixedSpiralAnimation && bLoopConeAnimation"))
	bool bAllowOverlappingConeWaves = true;

	/** Time between wave starts. Set this below the animation duration to overlap multiple waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed", meta = (DisplayName = "Wave Start Interval", EditCondition = "HasFixedAnimationSelected && !IsFixedSpiralAnimation && bLoopConeAnimation && bAllowOverlappingConeWaves", EditConditionHides, ClampMin = "0.05", Units = "s"))
	float ConeWaveStartInterval = 1.0f;

	/** Safety and performance cap for simultaneously active waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Fixed", meta = (DisplayName = "Maximum Concurrent Waves", EditCondition = "HasFixedAnimationSelected && !IsFixedSpiralAnimation && bLoopConeAnimation && bAllowOverlappingConeWaves", EditConditionHides, ClampMin = "1", ClampMax = "32"))
	int32 MaxConcurrentConeWaves = 8;

	/** Time taken by the glow band to travel completely from the base to the apex. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Bottom To Top Glow Wave", meta = (DisplayName = "Wave Duration", EditCondition = "IsFixedWaveAnimation", EditConditionHides, ClampMin = "0.05", Units = "s"))
	float GlowWaveDuration = 2.5f;

	/** Vertical height of the glowing band, measured using the structure's layer pitch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Bottom To Top Glow Wave", meta = (DisplayName = "Wave Band Height (Blocks)", EditCondition = "IsFixedWaveAnimation", EditConditionHides, ClampMin = "0.05", ClampMax = "64.0"))
	float GlowWaveBandHeightBlocks = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Bottom To Top Glow Wave", meta = (DisplayName = "Wave Loop Delay", EditCondition = "IsFixedWaveAnimation && bLoopConeAnimation", EditConditionHides, ClampMin = "0.0", Units = "s"))
	float GlowWaveLoopDelay = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Spiral Duration", EditCondition = "IsFixedSpiralAnimation", EditConditionHides, ClampMin = "0.05", Units = "s"))
	float SpiralGlowDuration = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Spiral Turns", EditCondition = "IsFixedSpiralAnimation", EditConditionHides, ClampMin = "0.1", ClampMax = "32.0"))
	float SpiralGlowTurns = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Stripe Width (Degrees)", EditCondition = "IsFixedSpiralAnimation", EditConditionHides, ClampMin = "1.0", ClampMax = "350.0", Units = "deg"))
	float SpiralGlowStripeWidthDegrees = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Spiral Strands Per Wave", EditCondition = "IsFixedSpiralAnimation", EditConditionHides, ClampMin = "1", ClampMax = "8"))
	int32 SpiralGlowStrandCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Start Angle", EditCondition = "IsFixedSpiralAnimation", EditConditionHides, ClampMin = "-360.0", ClampMax = "360.0", Units = "deg"))
	float SpiralGlowStartAngleDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Reverse Spiral Direction", EditCondition = "IsFixedSpiralAnimation", EditConditionHides))
	bool bReverseSpiralGlowDirection = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Surface Offset", EditCondition = "IsFixedSpiralAnimation", EditConditionHides, ClampMin = "0.5", ClampMax = "20.0", Units = "cm"))
	float SpiralGlowSurfaceOffset = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Use Crisp Opaque Material", EditCondition = "IsFixedSpiralAnimation", EditConditionHides))
	bool bUseCrispOpaqueSpiralMaterial = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Crisp Opaque Material", EditCondition = "IsFixedSpiralAnimation && bUseCrispOpaqueSpiralMaterial", EditConditionHides))
	TObjectPtr<UMaterialInterface> CrispSpiralMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Rising Candy Cane Spiral Glow", meta = (DisplayName = "Spiral Loop Delay", EditCondition = "IsFixedSpiralAnimation && bLoopConeAnimation", EditConditionHides, ClampMin = "0.0", Units = "s"))
	float SpiralGlowLoopDelay = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Glow Material", meta = (DisplayName = "Animation Glow Material", EditCondition = "HasFixedAnimationSelected", EditConditionHides))
	TObjectPtr<UMaterialInterface> GlowWaveMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Glow Material", meta = (DisplayName = "Animation Glow Color", EditCondition = "HasFixedAnimationSelected", EditConditionHides, HideAlphaChannel))
	FLinearColor GlowWaveColor = FLinearColor(0.05f, 0.55f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Glow Material", meta = (DisplayName = "Peak Glow Intensity", EditCondition = "HasFixedAnimationSelected", EditConditionHides, ClampMin = "0.0"))
	float GlowWaveIntensity = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Fixed|Glow Material", meta = (DisplayName = "Color Parameter Name", EditCondition = "HasFixedAnimationSelected", EditConditionHides))
	FName GlowWaveColorParameterName = TEXT("WaveColor");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Fixed|Glow Material", meta = (DisplayName = "Intensity Parameter Name", EditCondition = "HasFixedAnimationSelected", EditConditionHides))
	FName GlowWaveIntensityParameterName = TEXT("WaveIntensity");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Bottom To Top Sine Rows", meta = (DisplayName = "Sine Rows Duration", EditCondition = "IsFixedSineAnimation", EditConditionHides, ClampMin = "0.05", Units = "s"))
	float SineRowsDuration = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Bottom To Top Sine Rows", meta = (DisplayName = "Simultaneous Glowing Rows", EditCondition = "IsFixedSineAnimation", EditConditionHides, ClampMin = "2", ClampMax = "32"))
	int32 SineRowsSimultaneousRowCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Fixed|Bottom To Top Sine Rows", meta = (DisplayName = "Sine Rows Loop Delay", EditCondition = "IsFixedSineAnimation && bLoopConeAnimation", EditConditionHides, ClampMin = "0.0", Units = "s"))
	float SineRowsLoopDelay = 0.35f;

	/** Enables the shared onset/BPM animation. Disabled by default for every destructible actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetReactToMusic, Category = "Animation|Music", meta = (DisplayName = "React To Music"))
	bool bReactToMusic = false;

	/** Equalizer fills stationary columns from the base, with a fast rise and a slower fall. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMusicAnimationType, Category = "Animation|Music", meta = (DisplayName = "Music Animation Type", EditCondition = "bReactToMusic"))
	EMusicAnimationType MusicAnimationType = EMusicAnimationType::TravelingColumns;

	/** Base edge for planar equalizers. Volume shapes always fill from bottom to top in local Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetMusicEqualizerBaseEdge, Category = "Animation|Music", meta = (DisplayName = "Equalizer Base Edge", EditCondition = "bReactToMusic && MusicAnimationType == EMusicAnimationType::Equalizer", EditConditionHides))
	EMusicEqualizerBaseEdge MusicEqualizerBaseEdge = EMusicEqualizerBaseEdge::MinX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetReactToOnsets, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic"))
	bool bReactToOnsets = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetReactToBPM, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic"))
	bool bReactToBPM = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetReactToKeyPoints, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic"))
	bool bReactToKeyPoints = true;

	/** Key points with a target group only affect actors in that group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic"))
	FName MusicReactionGroup;

	/** Onsets weaker than this normalized Audio Synesthesia value are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic && bReactToOnsets", ClampMin = "0.0", ClampMax = "1.0"))
	float MusicOnsetThreshold = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (DisplayName = "Weak Onset Column Count", EditCondition = "bReactToMusic && bReactToOnsets && MusicAnimationType == EMusicAnimationType::TravelingColumns", EditConditionHides, ClampMin = "1", ClampMax = "32"))
	int32 WeakOnsetCellCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (DisplayName = "Strong Onset Column Count", EditCondition = "bReactToMusic && bReactToOnsets && MusicAnimationType == EMusicAnimationType::TravelingColumns", EditConditionHides, ClampMin = "1", ClampMax = "32"))
	int32 StrongOnsetCellCount = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (DisplayName = "BPM Beat Column Count", EditCondition = "bReactToMusic && bReactToBPM && MusicAnimationType == EMusicAnimationType::TravelingColumns", EditConditionHides, ClampMin = "1", ClampMax = "16"))
	int32 BPMBeatCellCount = 2;

	/** Optional dedicated material. When empty, the actor's destruction flash material is reused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic"))
	TObjectPtr<UMaterialInterface> MusicReactionMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic", HideAlphaChannel))
	FLinearColor MusicReactionColor = FLinearColor(0.03f, 0.45f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic", ClampMin = "0.0"))
	float WeakMusicGlowIntensity = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic", ClampMin = "0.0"))
	float StrongMusicGlowIntensity = 42.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (DisplayName = "Music Reaction Duration", EditCondition = "bReactToMusic", ClampMin = "0.15", ClampMax = "3.0", Units = "s"))
	float MusicReactionDuration = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (DisplayName = "Equalizer Column Count", EditCondition = "bReactToMusic", ClampMin = "4", ClampMax = "32"))
	int32 MusicEqualizerColumnCount = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (DisplayName = "Equalizer Row Count", EditCondition = "bReactToMusic", ClampMin = "8", ClampMax = "48"))
	int32 MusicEqualizerRowCount = 24;

	/** Number of glowing cells following the head of each upward-moving column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animation|Music", meta = (DisplayName = "Column Tail Length", EditCondition = "bReactToMusic && MusicAnimationType == EMusicAnimationType::TravelingColumns", EditConditionHides, ClampMin = "1", ClampMax = "8"))
	int32 MusicEqualizerTailLength = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Music", meta = (DisplayName = "Equalizer Block Scale", EditCondition = "bReactToMusic", ClampMin = "0.2", ClampMax = "1.2"))
	float MusicReactionSurfaceScale = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Music", meta = (DisplayName = "Maximum Active Columns", EditCondition = "bReactToMusic && MusicAnimationType == EMusicAnimationType::TravelingColumns", EditConditionHides, ClampMin = "4", ClampMax = "48"))
	int32 MaxSimultaneousMusicPulses = 24;

	/** Visual updates are intentionally capped below the game frame rate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Music", meta = (DisplayName = "Equalizer Update Rate", EditCondition = "bReactToMusic", ClampMin = "10.0", ClampMax = "60.0", Units = "Hz"))
	float MusicEqualizerUpdateRate = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Music", meta = (DisplayName = "Maximum Equalizer Instances", EditCondition = "bReactToMusic && MusicAnimationType == EMusicAnimationType::TravelingColumns", EditConditionHides, ClampMin = "16", ClampMax = "256"))
	int32 MaxMusicEqualizerInstances = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Animation|Music", meta = (EditCondition = "bReactToMusic"))
	int32 MusicReactionRandomSeed = 8675309;

	/** A short engine ping is selected by default; replace this with any USoundBase asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Sound")
	TObjectPtr<USoundBase> DestroyTickSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Sound")
	bool bPlayDestroyTickSound = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Sound", meta = (ClampMin = "0.0"))
	float DestroySoundVolume = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Sound", meta = (ClampMin = "0.01"))
	FVector2D DestroySoundPitchRange = FVector2D(0.92, 1.08);

	/** Zero disables rate limiting. A cap prevents hundreds of overlapping voices during a bomb blast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Sound", meta = (ClampMin = "0"))
	int32 MaxDestroySoundsPerSecond = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Sound")
	TObjectPtr<USoundAttenuation> DestroySoundAttenuation;

protected:
	/** Lightweight logical cell shared by grid-derived structures; cells are never individual Actors. */
	struct FFloorCellData
	{
		/** True when this coordinate belongs to the generated footprint, even if damage removed it. */
		bool bBelongsToGeneratedShape = true;
		bool bExists = true;
		bool bDestructible = true;
		float Health = 25.0f;
		int32 MaterialIndex = 0;
	};

	struct FQueuedCellDestruction
	{
		int32 CellIndex = INDEX_NONE;
		double ExecuteTime = 0.0;
		bool bRadial = false;

		bool operator<(const FQueuedCellDestruction& Other) const
		{
			return ExecuteTime < Other.ExecuteTime ||
				(ExecuteTime == Other.ExecuteTime && CellIndex < Other.CellIndex);
		}
	};

	struct FActiveFlash
	{
		FTransform Transform = FTransform::Identity;
		double ExpireTime = 0.0;
	};

	struct FMusicEqualizerSlot
	{
		int32 CellIndex = INDEX_NONE;
		FTransform Transform = FTransform::Identity;
	};

	struct FActiveMusicEqualizerColumn
	{
		int32 ColumnIndex = INDEX_NONE;
		double StartTime = 0.0;
		float Strength = 0.0f;
		float Duration = 0.72f;
		float PeakHeight = 1.0f;
		float StartHeight = 0.0f;
		int32 Priority = 0;
		float GetEqualizerHeight(double CurrentTime) const;
	};
	double LastMusicEventTime = -1.0e30;
	int32 LastMusicEventPriority = -1;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> ChunkComponents;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlashMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MusicReactionMaterialInstance;

	TArray<FFloorCellData> Cells;
	TArray<FQueuedCellDestruction> QueuedDestructions;
	TSet<int32> PendingCellIndices;
	TSet<int32> DirtyChunkIndices;
	TArray<FActiveFlash> ActiveFlashes;
	TArray<FMusicEqualizerSlot> MusicEqualizerSlots;
	// Real-piece renderers fill every piece in a bin, including lower pieces sharing a row.
	TArray<TArray<int32>> MusicEqualizerSlotCells;
	TArray<FActiveMusicEqualizerColumn> ActiveMusicEqualizerColumns;
	TArray<FTransform> MusicEqualizerTransformBuffer;
	TArray<int32> MusicEqualizerUsedCellBuffer;
	TArray<double> RecentDestroySoundTimes;
	FTimerHandle FlashCleanupTimerHandle;
	FRandomStream MusicReactionRandomStream;
	double NextMusicEqualizerUpdateTime = 0.0;
	int32 CachedMusicEqualizerColumnCount = 0;
	int32 CachedMusicEqualizerRowCount = 0;

	FIntPoint CellCount = FIntPoint::ZeroValue;
	FIntPoint ChunkCount = FIntPoint::ZeroValue;
	FVector EffectiveCellDimensions = FVector(100.0, 100.0, 50.0);
	FVector2D EffectiveGridSize = FVector2D::ZeroVector;
	FVector2D GridLocalMin = FVector2D::ZeroVector;

	// Spiral sampling and polygon clipping depend only on the generated layout and pattern
	// settings. Cache those immutable triangles while still evaluating bExists every rebuild.
	mutable TArray<FChunkedCachedSpiralTriangle> CachedSpiralSourceTriangles;
	mutable TArray<TArray<FChunkedCachedSpiralTriangle>> CachedSpiralCellTriangles;
	mutable TBitArray<> CachedSpiralCellBuilt;
	mutable bool bCachedSpiralGeometryKeyValid = false;
	mutable FVector2D CachedSpiralPatternCenter = FVector2D::ZeroVector;
	mutable double CachedSpiralFurthestRadius = 0.0;
	mutable float CachedSpiralTurns = 0.0f;
	mutable float CachedSpiralBandFraction = 0.0f;
	mutable float CachedSpiralRotationDegrees = 0.0f;
	mutable FVector2D CachedSpiralCenterOffset = FVector2D::ZeroVector;
	mutable int32 CachedSpiralCurveResolution = 0;
	mutable bool bCachedSpiralClockwise = false;

	/** Shape subclasses can derive their rectangular backing grid from their own parameters. */
	virtual bool CalculateLayout();

	/** Controls which logical cells belong to a generated shape. Rectangular floors accept all cells. */
	virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const { return true; }

	void ClearRuntimeEffects();
	void ClearChunkComponents();
	void CreateChunkComponents();
	virtual UClass* GetChunkComponentClass() const;
	void RebuildAllChunks();
	void RebuildChunk(int32 ChunkIndex);
	void ResetSharedGeometryCaches();
	virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& OutMesh) const;
	/** Adds the shared smooth spiral to an arbitrary 2D cell surface. */
	void AppendHypnoticSpiralSurface2D(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 ChunkIndex,
		FVector2D PatternCenter,
		double FurthestRadius,
		TFunctionRef<bool(FIntPoint, TArray<TArray<FVector2D>>&)> GetCellPolygons,
		TFunctionRef<void(FIntPoint, const FVector2D&, const FVector2D&, const FVector2D&, int32)> AppendSurfaceTriangle) const;
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const;
	/** Clip sampled spiral geometry to a cell, or to a subclass's continuous solid surface. */
	virtual void ClipSpiralSurface(
		const TArray<TArray<FVector2D>>& CellPolygons,
		TArray<FChunkedCachedSpiralTriangle>& OutTriangles) const;
	void RefreshSideNeonMaterial();
	/** A -> B must run from the bottom to the top of a vertical side. */
	void AppendFloorSideQuad(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FVector3d& A, const FVector3d& B, const FVector3d& C, const FVector3d& D,
		const FVector3f& Normal, int32 MaterialIndex,
		float ULength, float VLength, bool bOuterBoundary) const;
	/** Shared low-level face builder used by rectangular and custom-shaped grid structures. */
	static void AppendMeshQuad(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3d& D,
		const FVector3f& Normal,
		int32 MaterialIndex,
		float ULength,
		float VLength,
		float UVWorldUnits);
	/** Shared triangle-face builder; corners are counter-clockwise when viewed along Normal. */
	static void AppendMeshTriangle(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3f& Normal,
		int32 MaterialIndex,
		const FVector2f& UVA,
		const FVector2f& UVB,
		const FVector2f& UVC);
	void AppendGreedyHorizontalFaces(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 MinCellX,
		int32 MinCellY,
		int32 MaxCellX,
		int32 MaxCellY,
		bool bTopFace) const;
	virtual void ConfigureChunkComponent(UDynamicMeshComponent* Component) const;
	void RefreshFlashMaterial();
	virtual void ClearAdditionalRuntimeEffects() {}

	/** Fixed-animation controller hooks. Geometry remains polymorphic and lives in each shape. */
	virtual bool SupportsFixedAnimation() const { return UsesPlanarAnimation(); }
	virtual bool CreateFixedAnimationResources();
	virtual bool HasFixedAnimationResources() const;
	virtual void DestroyFixedAnimationResources();
	virtual void RenderFixedAnimation(TConstArrayView<float> AnimationAlphas);
	UMaterialInstanceDynamic* CreateFixedAnimationMaterialInstance();
	void ApplyFixedAnimationMaterialParameters(
		UMaterialInstanceDynamic* MaterialInstance,
		float NormalizedIntensity) const;
	int32 CalculateFixedSineComponentCount(int32 AvailableRowCount) const;
	virtual FTransform GetMusicEqualizerTransform(FIntPoint Cell) const;
	virtual bool UsesMusicPieceMaterials() const { return false; }
	virtual float GetTravelingColumnsSpeed() const { return 1.0f; }
	virtual void RenderMusicEqualizerCells(TConstArrayView<int32> CellIndices);
	virtual void ClearMusicPieceMaterials();
	void BuildMusicEqualizerLayout();
	int32 GetMusicEqualizerPoolSize() const;
	FVector2D GetMusicEqualizerCoordinates(FVector Position) const;
	void InitializeMusicEqualizerPool();
	void RefreshMusicReactionMaterial();
	void UpdateMusicEqualizer(double CurrentTime);
	void ClearMusicEqualizer();
	bool HasActiveMusicEqualizer() const { return !ActiveMusicEqualizerColumns.IsEmpty(); }

	int32 ToCellIndex(FIntPoint Cell) const;
	FIntPoint FromCellIndex(int32 CellIndex) const;
	bool IsValidCell(FIntPoint Cell) const;
	virtual FVector GetCellLocalCenter(FIntPoint Cell) const;
	virtual FVector GetCellWorldCenter(FIntPoint Cell) const;
	virtual FTransform GetCellFlashTransform(FIntPoint Cell) const;
	FVector GetInteriorImpactPoint(const FHitResult& Hit) const;
	virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& OutCell) const;
	virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const;
	float MeasureBlastDistance(FVector2D Delta, EChunkedFloorBlastShape Shape) const;
	virtual int32 SelectCellsInRadius(
		FVector WorldLocation,
		float Radius,
		EChunkedFloorBlastShape Shape,
		TFunctionRef<void(int32, float)> CellFunction);

	bool QueueCellDestruction(int32 CellIndex, float Delay, bool bRadial);
	/** Optional actor-wide gate shared by hit, explosion, direct and queued cell destruction. */
	virtual bool CanDestroyCell(FIntPoint Cell) const { return true; }
	void CancelQueuedDestruction(int32 CellIndex);
	bool ApplyDamageToCell(FIntPoint Cell, float DamageAmount);
	void ProcessQueuedDestructions(double CurrentTime);
	virtual void FinalizeDestructionBatch(double CurrentTime) {}
	virtual void MarkCellChunksDirty(FIntPoint Cell);
	void FlushDirtyChunks();
	virtual void SpawnCellFlash(FIntPoint Cell, double CurrentTime);
	void UpdateFlashes();
	void RebuildFlashInstances();
	void PlayDestroySound(FVector WorldLocation, double CurrentTime);
	void SyncSpiralSettingsFromLegacyFields();
	void SyncLegacyFieldsFromSpiralSettings();
	void UpdateFixedAnimation();
	void ClampFixedAnimationSettings();

private:
	bool EnsurePlanarAnimationResources();
	void ReleasePlanarAnimationIfUnused();
	void UpdatePlanarMusic(double CurrentTime);
	FVector2D GetPlanarAnimationCoordinates(FVector LocalPosition) const;
	UPROPERTY(Transient) TObjectPtr<UDestructiblePlanarAnimation> PlanarAnimation;
	TArray<uint8> PlanarMusicMask;
	FTimerHandle FixedAnimationTimerHandle;
	double FixedAnimationStartTime = 0.0;
	bool bFixedAnimationPlaying = false;
};
