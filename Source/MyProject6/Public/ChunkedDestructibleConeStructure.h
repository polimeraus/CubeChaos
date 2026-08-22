// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleConeStructure.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** Selects the secondary structure wrapped around the cone. */
UENUM(BlueprintType)
enum class EConeStructurePlatformStyle : uint8
{
	CircularRings UMETA(DisplayName = "Circular Rings"),
	RisingSpiral UMETA(DisplayName = "Rising Spiral")
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

public:
	AChunkedDestructibleConeStructure();
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	/** Rebuilds the cone from its current settings. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Generation")
	void RebuildConeStructure(bool bPreserveCellState = false);

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

	/** Sets how far the circular platform rings extend radially beyond the cone wall. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Platforms")
	void SetPlatformRingWidth(float NewRingWidthBlocks, bool bPreserveCellState = false);

	/** Switches between independent circular rings and one continuous rising spiral. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Cone Structure|Platforms")
	void SetPlatformStyle(EConeStructurePlatformStyle NewStyle, bool bPreserveCellState = false);

	UFUNCTION(BlueprintPure, Category = "Destructible Cone Structure|Platforms")
	float GetPlatformRingWidth() const { return PlatformOverhangBlocks; }

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

	virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Cone Structure|Blocks", meta = (ClampMin = "1.0", Units = "cm"))
	FVector BlockDimensions = FVector(100.0, 100.0, 100.0);

	/** Prevents a single destructible cell from becoming an oversized horizontal slab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible Cone Structure|Blocks", meta = (ClampMin = "1.0", ClampMax = "16.0"))
	float MaxHorizontalBlockAspectRatio = 2.0f;

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

	/** Debris becomes visual-only after its initial bounce, avoiding long-running Chaos bodies. */
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
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const override;
	virtual void ConfigureChunkComponent(UDynamicMeshComponent* Component) const override;
	virtual void ClearAdditionalRuntimeEffects() override;
	virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
	virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
	virtual void SpawnCellFlash(FIntPoint Cell, double CurrentTime) override;
	virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& OutCell) const override;
	virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
	virtual int32 SelectCellsInRadius(
		FVector WorldLocation,
		float Radius,
		EChunkedFloorBlastShape Shape,
		TFunctionRef<void(int32, float)> CellFunction) override;
	virtual void MarkCellChunksDirty(FIntPoint Cell) override;

private:
	int32 GridDiameterBlocks = 1;
	int32 GridHeightBlocks = 1;
	FVector ConeGridLocalMin = FVector::ZeroVector;
	float CachedRadialBlockUnit = 100.0f;
	TArray<TArray<FIntPoint>> PhysicalCellsByLayer;
	TArray<TMap<int32, int32>> PhysicalCellSlotsByLayer;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DebrisCubeMesh;

	TArray<TWeakObjectPtr<UStaticMeshComponent>> ActiveDebrisComponents;
	TArray<TWeakObjectPtr<UDynamicMeshComponent>> ActiveSurfaceFlashComponents;
	TWeakObjectPtr<UDynamicMeshComponent> CurrentSurfaceFlashComponent;
	TSet<FIntPoint> CurrentSurfaceFlashCells;
	double CurrentSurfaceFlashTime = -TNumericLimits<double>::Max();
	mutable TSet<FIntPoint> SurfaceFlashCellFilter;

	bool DecodeCell(FIntPoint Cell, int32& OutX, int32& OutY, int32& OutZ) const;
	FIntPoint EncodeCell(int32 X, int32 Y, int32 Z) const;
	FVector GetPhysicalCellLocalCenter(int32 X, int32 Y, int32 Z) const;
	bool CellExists3D(int32 X, int32 Y, int32 Z) const;
	bool IsConeShellCell(int32 X, int32 Y, int32 Z) const;
	bool IsPlatformCell(int32 X, int32 Y, int32 Z) const;
	bool IsCircularPlatformCell(int32 X, int32 Y, int32 Z) const;
	bool IsSpiralPlatformCell(int32 X, int32 Y, int32 Z) const;
	bool IsEntranceCell(int32 X, int32 Y, int32 Z) const;
	float GetConeOuterRadiusAtHeight(float LocalZ) const;
	float GetConeInnerRadiusAtHeight(float LocalZ) const;
	float GetSpiralThickness() const;
	float GetSpiralBottomHeight(bool bEnd) const;
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
	void AppendPlatformSurfaces(UE::Geometry::FDynamicMesh3& Mesh, int32 ChunkIndex) const;
	void AppendSpiralSurfaces(UE::Geometry::FDynamicMesh3& Mesh, int32 ChunkIndex) const;
	bool IsPointInsideRenderedStructure(const FVector3d& LocalPoint) const;
	void AppendSampledDestructionCutFace(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3d& D,
		const FVector3f& Normal,
		int32 MaterialIndex) const;
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
	int32 GetEffectiveConeChunkSideLength() const;
	int32 GetEffectiveCircularSegments() const;
	int32 GetEffectiveDebrisLimit() const;
	bool CanUseSmoothCollisionForChunk(int32 ChunkIndex) const;
	UMaterialInterface* ResolveStructureMaterial(bool bPlatform) const;

	UFUNCTION()
	void HandleConeCellDestroyed(FIntPoint Cell, FVector WorldLocation, bool bRadialDestruction);

	void SpawnDebrisForCell(FIntPoint Cell);
	void ClearDebris();
};
