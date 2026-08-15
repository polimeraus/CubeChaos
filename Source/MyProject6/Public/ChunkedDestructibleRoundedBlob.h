// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleRoundedBlob.generated.h"

/** Derived measurements for the currently generated rounded blob outline. */
USTRUCT(BlueprintType)
struct FChunkedDestructibleRoundedBlobMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Rounded Blob")
	FVector2D FootprintSize = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Rounded Blob")
	float ApproximateArea = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Rounded Blob")
	float ApproximatePerimeter = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Rounded Blob")
	int32 OutlinePointCount = 0;
};

/**
 * A smooth, organic and individually destructible platform made from logical grid cells.
 *
 * The outline is a deterministic rounded radial spline. Interior cells retain the floor's cheap
 * merged geometry, while boundary cells are clipped to the smooth outline. Damage, explosions,
 * materials, effects and chunk-local rebuilds are inherited from AChunkedDestructibleFloor.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API AChunkedDestructibleRoundedBlob : public AChunkedDestructibleFloor
{
	GENERATED_BODY()

public:
	AChunkedDestructibleRoundedBlob();

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty *InProperty) const override;
#endif

	/** Reconfigures the complete blob and its backing grid in one runtime-safe rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Rounded Blob|Generation")
	void ConfigureRoundedBlob(
		FVector2D NewBlobSize,
		int32 NewLobeCount,
		float NewLobeStrength,
		float NewEdgeRoundness,
		int32 NewShapeSeed,
		int32 NewOutlineSamplesPerLobe,
		FVector NewCellDimensions,
		FVector2D NewCellSpacing,
		bool bPreserveCellState = true);

	/** Changes only the organic outline parameters and rebuilds the platform. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Rounded Blob|Generation")
	void SetRoundedBlobShape(
		FVector2D NewBlobSize,
		int32 NewLobeCount,
		float NewLobeStrength,
		float NewEdgeRoundness,
		int32 NewShapeSeed,
		int32 NewOutlineSamplesPerLobe,
		bool bPreserveCellState = true);

	UFUNCTION(BlueprintPure, Category = "Destructible Rounded Blob|Generation")
	FChunkedDestructibleRoundedBlobMetrics GetRoundedBlobMetrics() const;

	/** Returns the sampled local-space outline in counter-clockwise order. */
	UFUNCTION(BlueprintPure, Category = "Destructible Rounded Blob|Generation")
	void GetRoundedBlobOutline(TArray<FVector2D> &OutOutline) const;

protected:
	/** Exact X/Y extent of the generated outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Rounded Blob|Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D BlobSize = FVector2D(6000.0, 4500.0);

	/** Number of large rounded protrusions around the perimeter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Rounded Blob|Shape", meta = (ClampMin = "3", ClampMax = "16"))
	int32 LobeCount = 7;

	/** Amount of organic inward/outward variation. Zero produces an ellipse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Rounded Blob|Shape", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float LobeStrength = 0.24f;

	/** Blends the lobe profile from straight interpolation to a fully smooth spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Rounded Blob|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EdgeRoundness = 1.0f;

	/** Deterministically changes individual lobe sizes while keeping the same overall dimensions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Rounded Blob|Shape")
	int32 ShapeSeed = 1337;

	/** More samples give a smoother silhouette at the cost of extra boundary geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Rounded Blob|Shape", meta = (ClampMin = "4", ClampMax = "32"))
	int32 OutlineSamplesPerLobe = 10;

	virtual bool CalculateLayout() override;
	virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
	virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3 &OutMesh) const override;
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom &OutCollision) const override;
	virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;

private:
	struct FBlobTriangle
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		FVector2D C = FVector2D::ZeroVector;
		FBox2D Bounds = FBox2D(EForceInit::ForceInit);
	};

	TArray<FVector2D> CachedOutline;
	TArray<FBlobTriangle> CachedTriangles;
	FVector2D CachedFanCenter = FVector2D::ZeroVector;

	void RebuildOutlineCache();
	bool IsPointInsideBlob(const FVector2D &Point, double Tolerance = 0.01) const;
	bool DoesOutlineIntersectCell(FIntPoint Cell) const;
	bool IsCellFullyInsideBlob(FIntPoint Cell) const;
	bool GetClippedCellFragments(FIntPoint Cell, TArray<TArray<FVector2D>> &OutFragments) const;
	void GetCellBounds(FIntPoint Cell, double &OutMinX, double &OutMinY, double &OutMaxX, double &OutMaxY) const;
	void AppendGreedyFullHorizontalFaces(
		UE::Geometry::FDynamicMesh3 &Mesh,
		int32 MinCellX,
		int32 MinCellY,
		int32 MaxCellX,
		int32 MaxCellY,
		bool bTopFace) const;
	void AppendPolygonHorizontalFace(
		UE::Geometry::FDynamicMesh3 &Mesh,
		const TArray<FVector2D> &Polygon,
		bool bTopFace,
		int32 MaterialIndex) const;
};
