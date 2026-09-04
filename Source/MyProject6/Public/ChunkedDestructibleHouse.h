// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleHouse.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

USTRUCT(BlueprintType)
struct FChunkedDestructibleHouseMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "House")
	FIntVector WallBlockCount = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "House")
	FIntVector BackingGridBlockCount = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "House")
	FVector WorldDimensions = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "House")
	int32 LogicalCellCapacity = 0;
};

/**
 * Parametric, chunked and destructible stylized town house.
 *
 * The body is assembled from real 3D block cells. Its gabled roof is clipped against a triangular
 * X/Z profile, producing wedge/polygonal prisms along both slopes instead of a stair-step outline.
 * It reuses the floor grid's material, damage, explosion, flash, sound and chunk lifecycle.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API AChunkedDestructibleHouse : public AChunkedDestructibleFloor
{
	GENERATED_BODY()
	friend class FDestructibleStructureTest;

public:
	AChunkedDestructibleHouse();
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	/** Rebuilds the house from its current settings. */
	UFUNCTION(BlueprintCallable, Category = "Destructible House|Generation")
	void RebuildHouse(bool bPreserveCellState = false);

	/** Updates the major dimensions and rebuilds the complete structure. */
	UFUNCTION(BlueprintCallable, Category = "Destructible House|Generation")
	void ConfigureHouse(
		FIntVector NewWallBlockCount,
		int32 NewRoofHeightBlocks,
		int32 NewRoofOverhangBlocks,
		FVector NewBlockDimensions,
		FVector NewBlockSpacing,
		bool bNewHollowInterior = true,
		bool bPreserveCellState = false);

	UFUNCTION(BlueprintPure, Category = "Destructible House|Generation")
	FChunkedDestructibleHouseMetrics GetHouseMetrics() const;

	UFUNCTION(BlueprintPure, Category = "Destructible House|Debris")
	int32 GetActiveDebrisPieceCount() const { return ActiveDebrisComponents.Num(); }

	/** Runs the optional foundation-connectivity collapse check immediately. */
	UFUNCTION(BlueprintCallable, Category = "Destructible House|Collapse")
	void EvaluateStructuralIntegrity();

	virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const override;

protected:
	virtual void BeginPlay() override;
	virtual void ClearAdditionalRuntimeEffects() override;

	/** X=width, Y=depth, Z=wall height, in whole blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Shape", meta = (ClampMin = "3"))
	FIntVector WallBlockCount = FIntVector(12, 10, 8);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Shape", meta = (ClampMin = "1", ClampMax = "128"))
	int32 RoofHeightBlocks = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Shape", meta = (ClampMin = "0", ClampMax = "16"))
	int32 RoofOverhangBlocks = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector BlockDimensions = FVector(100.0, 100.0, 100.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Shape", meta = (ClampMin = "0.0", Units = "cm"))
	FVector BlockSpacing = FVector::ZeroVector;

	/** When true only walls, floor and ceiling are generated instead of a solid body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Shape")
	bool bHollowInterior = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings")
	bool bGenerateDoor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings", meta = (ClampMin = "1"))
	int32 DoorWidthBlocks = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings", meta = (ClampMin = "1"))
	int32 DoorHeightBlocks = 3;

	/** Door offset from the center in whole blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings")
	int32 DoorHorizontalOffsetBlocks = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings")
	bool bGenerateWindows = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WindowColumns = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WindowRows = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings", meta = (ClampMin = "1"))
	int32 WindowWidthBlocks = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings", meta = (ClampMin = "1"))
	int32 WindowHeightBlocks = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings", meta = (ClampMin = "1"))
	int32 WindowBottomBlocks = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings", meta = (ClampMin = "0"))
	int32 WindowVerticalSpacingBlocks = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Openings")
	bool bWindowsOnBackWall = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Chimney")
	bool bAddChimney = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Chimney", meta = (ClampMin = "1", ClampMax = "8"))
	int32 ChimneyHeightBlocks = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Chimney", meta = (ClampMin = "1", ClampMax = "4"))
	int32 ChimneySizeBlocks = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Chimney", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	FVector2D ChimneyNormalizedOffset = FVector2D(0.45, 0.2);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Materials", meta = (ClampMin = "0"))
	int32 WallMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Materials", meta = (ClampMin = "0"))
	int32 RoofMaterialIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Materials", meta = (ClampMin = "0"))
	int32 FoundationMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Materials", meta = (ClampMin = "0"))
	int32 ChimneyMaterialIndex = 1;

	/** Unsupported face-connected sections are destroyed after losing every path to Z=0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Collapse")
	bool bCollapseUnsupportedSections = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Collapse", meta = (ClampMin = "0.0", Units = "s"))
	FVector2D CollapseDelayRange = FVector2D(0.0, 0.35);

	/** Rebuilds a small physical 3D region instead of a long flattened X/Y slice after a hit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Performance")
	bool bUseSpatialRuntimeChunks = true;

	/** Side length of each physical runtime rebuild region, measured in house cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Performance", meta = (ClampMin = "4", ClampMax = "16"))
	int32 SpatialChunkSideLength = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Debris")
	bool bScatterDestroyedPieces = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Debris", meta = (ClampMin = "1", ClampMax = "8"))
	int32 DebrisPiecesPerCell = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Debris", meta = (ClampMin = "0.1", Units = "s"))
	float DebrisLifetime = 3.0f;

	/** Debris is removed when this physical phase ends, so it can never freeze in mid-air. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible House|Debris", meta = (ClampMin = "0.05", Units = "s"))
	float DebrisPhysicsDuration = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Debris", meta = (ClampMin = "0.0"))
	float DebrisImpulse = 650.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Debris", meta = (ClampMin = "0.0"))
	float DebrisUpwardImpulse = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Debris", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	FVector2D DebrisScaleRange = FVector2D(0.28, 0.48);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Debris", meta = (ClampMin = "0", ClampMax = "1024"))
	int32 MaxActiveDebrisPieces = 128;

	/** Runtime safety budget applied even when an older placed actor saved a larger active limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Destructible House|Debris", meta = (ClampMin = "0", ClampMax = "1024"))
	int32 MaxRuntimeDebrisPieces = 48;

	/** Uses one short-lived Chaos fragment per destroyed cell and a smaller active debris budget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible House|Performance")
	bool bUseLightweightDebris = true;

	virtual bool CalculateLayout() override;
	virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
	virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& OutMesh) const override;
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const override;
	virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
	virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
	virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& OutCell) const override;
	virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
	virtual int32 SelectCellsInRadius(
		FVector WorldLocation,
		float Radius,
		EChunkedFloorBlastShape Shape,
		TFunctionRef<void(int32, float)> CellFunction) override;
	virtual void MarkCellChunksDirty(FIntPoint Cell) override;

	/** Extension points used by block-built house variants with custom facade openings. */
	virtual bool IsOpeningCell(int32 X, int32 Y, int32 Z) const;
	virtual bool IsWallCell(int32 X, int32 Y, int32 Z) const;
	virtual bool GetCellPolygonXZ(FIntPoint Cell, TArray<FVector2D>& OutPolygon) const;

	bool DecodeCell(FIntPoint Cell, int32& OutX, int32& OutY, int32& OutZ) const;
	FIntPoint EncodeCell(int32 X, int32 Y, int32 Z) const;
	bool IsChimneyCell(int32 X, int32 Y, int32 Z) const;
	void GetCellBoundsXZ(FIntPoint Cell, double& OutMinX, double& OutMinZ, double& OutMaxX, double& OutMaxZ) const;
	void GetRoofTriangle(FVector2D& OutLeft, FVector2D& OutRight, FVector2D& OutApex) const;

private:
	int32 GridWidthBlocks = 1;
	int32 GridDepthBlocks = 1;
	int32 GridHeightBlocks = 1;
	FVector HouseGridLocalMin = FVector::ZeroVector;
	FIntVector SpatialChunkCount = FIntVector::ZeroValue;
	TArray<TArray<FVector2D>> CachedCellPolygons;
	TBitArray<> CachedCellPolygonValid;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> DebrisCubeMesh;

	TArray<TWeakObjectPtr<UStaticMeshComponent>> ActiveDebrisComponents;
	bool bIntegrityCheckQueued = false;
	FTimerHandle StructuralIntegrityTimerHandle;
	bool bEvaluatingIntegrity = false;

	bool CellExists3D(int32 X, int32 Y, int32 Z) const;
	bool UsesSpatialRuntimeChunks() const;
	int32 GetEffectiveSpatialChunkSideLength() const;
	int32 GetSpatialChunkIndex(int32 X, int32 Y, int32 Z) const;
	bool GetSpatialChunkBounds(
		int32 ChunkIndex,
		FIntVector& OutMinimum,
		FIntVector& OutMaximum) const;
	void GatherChunkCells(int32 ChunkIndex, TArray<FIntPoint>& OutCells) const;
	void RebuildCellPolygonCache();
	bool GetCachedCellPolygonXZ(FIntPoint Cell, TArray<FVector2D>& OutPolygon) const;
	int32 GetChimneyBaseZ() const;
	bool IsCellPolygonFullRectangle(FIntPoint Cell, const TArray<FVector2D>& Polygon) const;
	void AppendPolygonFaceAtY(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TArray<FVector2D>& Polygon,
		double Y,
		bool bFrontFace,
		int32 MaterialIndex) const;
	void GetPieceLocalBounds(FIntPoint Cell, FVector& OutCenter, FVector& OutSize) const;

	UFUNCTION()
	void HandleHouseCellDestroyed(FIntPoint Cell, FVector WorldLocation, bool bRadialDestruction);

	int32 GetEffectiveDebrisLimit() const;
	void SpawnDebrisForCell(FIntPoint Cell, FVector WorldLocation);
	void ScheduleStructuralIntegrityCheck();
	void ClearDebris();
};
