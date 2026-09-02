// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructiblePyramid.generated.h"

/** Selects which pyramid faces receive the same parametric entrance opening. */
UENUM(BlueprintType)
enum class EChunkedPyramidEntranceSides : uint8
{
	FrontOnly UMETA(DisplayName = "Front Only"),
	FrontAndBack UMETA(DisplayName = "Front And Back"),
	AllSides UMETA(DisplayName = "All Four Sides")
};

/** Main proportions and destructible surface-piece controls. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FChunkedPyramidShapeSettings
{
	GENERATED_BODY()

	/** Width of the square pyramid body where it meets the highest base step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
	float BaseWidth = 2400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "100.0", UIMin = "100.0", Units = "cm"))
	float Height = 2200.0f;

	/** Approximate width and vertical height of one independently destructible face piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "10.0", UIMin = "25.0", Units = "cm"))
	float PieceSize = 80.0f;

	/** Inward depth of every sloped face piece. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "2.0", UIMin = "5.0", Units = "cm"))
	float ShellThickness = 50.0f;

	/** Small reveal between neighbouring pieces, making the tiled construction readable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float PieceGap = 2.0f;
};

/** Square, stacked foundation rings that form the stair-like base. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FChunkedPyramidStepSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Steps")
	bool bGenerateSteps = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Steps", meta = (EditCondition = "bGenerateSteps", ClampMin = "1", ClampMax = "16"))
	int32 StepCount = 3;

	/** Horizontal tread depth added around every successively lower layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Steps", meta = (EditCondition = "bGenerateSteps", ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float StepDepth = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Steps", meta = (EditCondition = "bGenerateSteps", ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float StepHeight = 45.0f;
};

/** Rectangular opening cut from one or more pyramid faces. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FChunkedPyramidEntranceSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance")
	bool bGenerateEntrance = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance", meta = (EditCondition = "bGenerateEntrance"))
	EChunkedPyramidEntranceSides Sides = EChunkedPyramidEntranceSides::FrontOnly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance", meta = (EditCondition = "bGenerateEntrance", ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float Width = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entrance", meta = (EditCondition = "bGenerateEntrance", ClampMin = "10.0", UIMin = "10.0", Units = "cm"))
	float Height = 480.0f;
};

/** Destructible tiled floor sealing the inside of the pyramid at the body base. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FChunkedPyramidInteriorFloorSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior Floor")
	bool bGenerateFloor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior Floor", meta = (EditCondition = "bGenerateFloor", ClampMin = "2.0", UIMin = "5.0", Units = "cm"))
	float Thickness = 30.0f;

	/** Keeps the floor safely inside the sloped shell and exposes its thickness at entrances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interior Floor", meta = (EditCondition = "bGenerateFloor", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float EdgeInset = 30.0f;
};

/** Read-only measurements after settings have been clamped and tiled. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FChunkedDestructiblePyramidMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Pyramid")
	float PyramidBaseWidth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pyramid")
	float OverallBaseWidth = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pyramid")
	float OverallHeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Pyramid")
	int32 PyramidPieceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Pyramid")
	int32 StepPieceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Pyramid")
	int32 InteriorFloorPieceCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Pyramid")
	int32 TotalPieceCount = 0;
};

/**
 * A square pyramid assembled from small, independently destructible pieces.
 *
 * The four sloped faces use thin convex tiles instead of a voxel staircase. The base is made from
 * stacked square block rings. Damage, explosions, flash, sound and chunk-local rebuilding come
 * from AChunkedDestructibleFloor, so existing projectile behavior works without a special case.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Chunked Destructible Pyramid"))
class MYPROJECT6_API AChunkedDestructiblePyramid : public AChunkedDestructibleFloor
{
	GENERATED_BODY()

public:
	AChunkedDestructiblePyramid();

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Destructible Pyramid|Generation")
	void RebuildPyramid(bool bPreserveCellState = false);

	UFUNCTION(BlueprintCallable, Category = "Destructible Pyramid|Generation")
	void ConfigurePyramid(
		const FChunkedPyramidShapeSettings& NewShape,
		const FChunkedPyramidStepSettings& NewSteps,
		const FChunkedPyramidEntranceSettings& NewEntrance,
		const FChunkedPyramidInteriorFloorSettings& NewInteriorFloor,
		bool bPreserveCellState = false);

	UFUNCTION(BlueprintPure, Category = "Destructible Pyramid|Generation")
	FChunkedDestructiblePyramidMetrics GetPyramidMetrics() const;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Shape", meta = (ShowOnlyInnerProperties))
	FChunkedPyramidShapeSettings Shape;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Stepped Base", meta = (ShowOnlyInnerProperties))
	FChunkedPyramidStepSettings Steps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Entrance", meta = (ShowOnlyInnerProperties))
	FChunkedPyramidEntranceSettings Entrance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Interior Floor", meta = (ShowOnlyInnerProperties))
	FChunkedPyramidInteriorFloorSettings InteriorFloor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Materials", meta = (ClampMin = "0"))
	int32 PyramidMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Materials", meta = (ClampMin = "0"))
	int32 PyramidAccentMaterialIndex = 1;

	/** Material for the inward face and exposed edges that reveal each face piece's depth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Materials", meta = (ClampMin = "0"))
	int32 PyramidCutMaterialIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Materials", meta = (ClampMin = "0"))
	int32 StepMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Materials", meta = (ClampMin = "0"))
	int32 InteriorFloorMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Pyramid|Materials")
	bool bAlternatePyramidPieces = true;

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
		EChunkedFloorBlastShape ShapeType,
		TFunctionRef<void(int32, float)> CellFunction) override;
	virtual void MarkCellChunksDirty(FIntPoint Cell) override;

public:
	virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const override;

private:
	enum class EPyramidPieceKind : uint8
	{
		Face,
		Step,
		InteriorFloor,
		Apex
	};

	struct FPyramidPieceGeometry
	{
		EPyramidPieceKind Kind = EPyramidPieceKind::Face;
		int32 FaceIndex = INDEX_NONE;
		int32 RowIndex = INDEX_NONE;
		int32 ColumnIndex = INDEX_NONE;
		TArray<FVector> OuterPolygon;
		TArray<FVector> InnerPolygon;
		FVector BoxCenter = FVector::ZeroVector;
		FVector BoxSize = FVector::ZeroVector;
		FVector Center = FVector::ZeroVector;
		FBox Bounds = FBox(ForceInit);
	};

	TArray<FPyramidPieceGeometry> PieceGeometry;
	float CachedBodyBaseZ = 0.0f;
	float CachedOverallBaseWidth = 0.0f;
	float CachedOverallHeight = 0.0f;
	int32 CachedPyramidPieceCount = 0;
	int32 CachedStepPieceCount = 0;
	int32 CachedInteriorFloorPieceCount = 0;

	bool RebuildPieceGeometry();
	bool AddPyramidFacePieces();
	bool AddSteppedBasePieces();
	bool AddInteriorFloorPieces();
	bool AddApexPiece(float LocalBaseZ);
	bool AddFacePiece(
		int32 FaceIndex,
		int32 RowIndex,
		int32 ColumnIndex,
		const TArray<FVector>& OuterPolygon,
		const FVector& OutwardNormal);
	bool AddBoxPiece(EPyramidPieceKind Kind, FVector Center, FVector Size);
	bool CanAddAnotherPiece() const;
	bool FaceHasEntrance(int32 FaceIndex) const;
	FVector GetPyramidSurfacePoint(int32 FaceIndex, float Horizontal, float LocalBodyZ) const;
	FVector GetPyramidFaceNormal(int32 FaceIndex) const;
	bool FindCellAtLocalPoint(FVector LocalPoint, FIntPoint& OutCell, float ExtraTolerance) const;
	void AppendFacePieceMesh(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FPyramidPieceGeometry& Piece,
		int32 MaterialIndex) const;
	void AppendBoxPieceMesh(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FPyramidPieceGeometry& Piece,
		int32 MaterialIndex) const;
	void AppendApexPieceMesh(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FPyramidPieceGeometry& Piece,
		int32 MaterialIndex) const;
};
