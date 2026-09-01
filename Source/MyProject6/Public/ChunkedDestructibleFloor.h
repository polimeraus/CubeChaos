// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChunkedDestructibleFloor.generated.h"

class UDynamicMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class USoundAttenuation;
class USoundBase;
struct FKAggregateGeom;

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
	FlowerMotif UMETA(DisplayName = "Flower")
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
	Diamond
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
class MYPROJECT6_API AChunkedDestructibleFloor : public AActor
{
	GENERATED_BODY()

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
	void GenerateFloor(bool bPreserveCellState = false);

	/** Changes area, cell size and spacing in one rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	void ConfigureFloor(
		FVector2D NewAreaSize,
		FVector NewCellDimensions,
		FVector2D NewCellSpacing,
		bool bPreserveCellState = true);

	/** Runtime cell-size change. This rebuilds mesh and collision, so it should not be called every frame. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	void SetCellDimensions(FVector NewCellDimensions, bool bPreserveCellState = true);

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
	void ApplyMaterialPattern();

	/** Pushes the current material array to every chunk. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	void RefreshFloorMaterials();

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

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> ChunkComponents;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlashMaterialInstance;

	TArray<FFloorCellData> Cells;
	TArray<FQueuedCellDestruction> QueuedDestructions;
	TSet<int32> PendingCellIndices;
	TSet<int32> DirtyChunkIndices;
	TArray<FActiveFlash> ActiveFlashes;
	TArray<double> RecentDestroySoundTimes;
	FTimerHandle FlashCleanupTimerHandle;

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
	void ProcessQueuedDestructions(double CurrentTime);
	virtual void MarkCellChunksDirty(FIntPoint Cell);
	void FlushDirtyChunks();
	virtual void SpawnCellFlash(FIntPoint Cell, double CurrentTime);
	void UpdateFlashes();
	void RebuildFlashInstances();
	void PlayDestroySound(FVector WorldLocation, double CurrentTime);
	void SyncSpiralSettingsFromLegacyFields();
	void SyncLegacyFieldsFromSpiralSettings();
};
