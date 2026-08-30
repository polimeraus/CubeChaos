// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleHalloweenPumpkin4.h"
#include "ChunkedDestructibleHalloweenPumpkin5.generated.h"

/**
 * A hollow, smoothly tessellated destructible jack-o'-lantern based on the supplied photo.
 *
 * Logical destruction still happens on the inherited 3D cell grid. Unlike the earlier voxel
 * variants, the visible shell is extracted from a continuous pumpkin field inside every cell.
 * This fills the spaces between cells and gives the body, cavity, stem and carved edges smooth
 * transitions without placing a backing surface in any face opening. The face points along +X.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Destructible Halloween Pumpkin 5 (Reference Smooth Hollow)"))
class MYPROJECT6_API AChunkedDestructibleHalloweenPumpkin5 : public AChunkedDestructibleHalloweenPumpkin4
{
	GENERATED_BODY()

public:
	AChunkedDestructibleHalloweenPumpkin5();

	/** Configures the smooth hollow body and rebuilds all destructible surface cells. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 5|Generation")
	void ConfigureReferencePumpkin(
		FVector2D NewBodyRadiusBlocks,
		float NewBodyHalfHeightBlocks,
		float NewShellThicknessBlocks,
		float NewStemHeightBlocks,
		FVector NewBlockDimensions,
		int32 NewSurfaceSubdivisions,
		bool bPreserveCellState = false);

	/** Enables or removes the photo-matched through-cut face. No glow/backing plane is created. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 5|Face")
	void SetReferenceFaceEnabled(bool bEnabled, bool bPreserveCellState = false);

protected:
	/** Sub-cell samples used by the continuous surface extractor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 5|Surface", meta = (ClampMin = "1", ClampMax = "4"))
	int32 SurfaceSubdivisions = 2;

	/** Adds orange cut faces when a destructible cell is removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 5|Surface")
	bool bCapDestroyedCellEdges = true;

	/** Moves collision cooking off the game thread when damaged chunks are rebuilt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 5|Performance")
	bool bUseAsyncCollisionCooking = true;

	/** Side length of each true 3D runtime rebuild region, measured in logical cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 5|Performance", meta = (ClampMin = "4", ClampMax = "12"))
	int32 SpatialChunkSideLength = 8;

	virtual bool CalculateLayout() override;
	virtual bool IsFaceProjection(const FVector& LocalCenter) const override;
	virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
	virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& OutMesh) const override;
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const override;
	virtual void ConfigureChunkComponent(UDynamicMeshComponent* Component) const override;
	virtual void MarkCellChunksDirty(FIntPoint Cell) override;

private:
	struct FCachedSurfaceTriangle
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		FVector C = FVector::ZeroVector;
		FVector3f NormalA = FVector3f::UnitZ();
		FVector3f NormalB = FVector3f::UnitZ();
		FVector3f NormalC = FVector3f::UnitZ();
		int32 MaterialIndex = 0;
	};

	FIntVector SpatialChunkCount = FIntVector::ZeroValue;
	mutable TArray<TArray<FCachedSurfaceTriangle>> CachedCellSurfaceTriangles;
	mutable TBitArray<> CachedCellSurfaceBuilt;

	int32 GetEffectiveSpatialChunkSideLength() const;
	double EvaluateBodyField(const FVector& LocalPosition, float InsetBlocks) const;
	double EvaluateStemField(const FVector& LocalPosition) const;
	double EvaluateFaceProjectionField(const FVector& LocalPosition) const;
	double EvaluateSolidField(const FVector& LocalPosition) const;
	FVector3f EvaluateSurfaceNormal(const FVector& LocalPosition) const;
	int32 EvaluateSurfaceMaterial(const FVector& LocalPosition) const;

	void AppendSmoothTriangle(
		UE::Geometry::FDynamicMesh3& Mesh,
		FVector A,
		FVector B,
		FVector C,
		FVector3f NormalA,
		FVector3f NormalB,
		FVector3f NormalC,
		int32 MaterialIndex) const;
	void AppendMarchingTetrahedron(
		TArray<FCachedSurfaceTriangle>& OutTriangles,
		const FVector* Positions,
		const double* Fields,
		const int32* Tetrahedron) const;
	void BuildCellSurfaceCache(FIntPoint Cell, int32 X, int32 Y, int32 Z) const;
	void AppendDestroyedNeighborCaps(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 X,
		int32 Y,
		int32 Z,
		const FVector& CellCenter) const;
};
