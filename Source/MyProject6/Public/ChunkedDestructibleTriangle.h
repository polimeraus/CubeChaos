// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleTriangle.generated.h"

/** Derived measurements for the current triangle shape. Angles are expressed in degrees. */
USTRUCT(BlueprintType)
struct FChunkedDestructibleTriangleMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Triangle")
	float BaseLength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Triangle")
	float Height = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Triangle")
	float LeftSideLength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Triangle")
	float RightSideLength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Triangle")
	float LeftBaseAngle = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Triangle")
	float RightBaseAngle = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Triangle")
	float ApexAngle = 0.0f;
};

/**
 * A broad, chunked and destructible triangular platform.
 *
 * Full interior grid cells are rectangular prisms. Cells crossed by either diagonal edge are
 * clipped to the exact triangle and triangulated, so the outline is not a staircase made only
 * from cubes. It inherits the floor's material patterns, damage, explosions, flash, sound and
 * chunk-local rebuild behavior without creating one Actor or Component per cell.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API AChunkedDestructibleTriangle : public AChunkedDestructibleFloor
{
	GENERATED_BODY()

public:
	AChunkedDestructibleTriangle();

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	/** Reconfigures the triangle and its backing grid in one runtime-safe rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Triangle|Generation")
	void ConfigureTriangle(
		float NewBaseLength,
		float NewTriangleHeight,
		float NewApexOffset,
		FVector NewCellDimensions,
		FVector2D NewCellSpacing,
		bool bPreserveCellState = true);

	/** Changes only the triangle outline. Apex offset is normalized from -1 (left) to +1 (right). */
	UFUNCTION(BlueprintCallable, Category = "Destructible Triangle|Generation")
	void SetTriangleShape(
		float NewBaseLength,
		float NewTriangleHeight,
		float NewApexOffset,
		bool bPreserveCellState = true);

	UFUNCTION(BlueprintPure, Category = "Destructible Triangle|Generation")
	FChunkedDestructibleTriangleMetrics GetTriangleMetrics() const;

	/** Returns exact local-space outline points in counter-clockwise order. */
	UFUNCTION(BlueprintPure, Category = "Destructible Triangle|Generation")
	void GetTriangleVertices(FVector2D& OutBaseLeft, FVector2D& OutBaseRight, FVector2D& OutApex) const;

protected:
	/** Length of the horizontal base edge in local X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Triangle|Shape", meta = (ClampMin = "1.0", Units = "cm"))
	float BaseLength = 5000.0f;

	/** Perpendicular distance from the base to the apex in local Y. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Triangle|Shape", meta = (ClampMin = "1.0", Units = "cm"))
	float TriangleHeight = 4000.0f;

	/** Horizontal apex position: -1 is over the left base corner, 0 is centered, +1 is over the right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Triangle|Shape", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ApexOffset = 0.0f;

	virtual bool CalculateLayout() override;
	virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
	virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
	virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& OutMesh) const override;
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const override;
	virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;

private:
	bool IsPointInsideTriangle(const FVector2D& Point, double Tolerance = 0.01) const;
	bool IsCellFullyInsideTriangle(FIntPoint Cell) const;
	bool GetClippedCellPolygon(FIntPoint Cell, TArray<FVector2D>& OutPolygon) const;
	void GetCellBounds(FIntPoint Cell, double& OutMinX, double& OutMinY, double& OutMaxX, double& OutMaxY) const;
	void AppendGreedyFullHorizontalFaces(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 MinCellX,
		int32 MinCellY,
		int32 MaxCellX,
		int32 MaxCellY,
		bool bTopFace) const;
	void AppendPolygonHorizontalFace(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TArray<FVector2D>& Polygon,
		bool bTopFace,
		int32 MaterialIndex) const;
};
