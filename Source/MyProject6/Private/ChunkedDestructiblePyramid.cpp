// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructiblePyramid.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"

using namespace UE::Geometry;

namespace
{
	constexpr float MinimumPyramidDimension = 10.0f;
	constexpr int32 MaximumFaceRows = 512;
	constexpr int32 MaximumFaceColumns = 512;
	constexpr float GeometryTolerance = 0.1f;

	FVector AveragePoints(const TArray<FVector>& A, const TArray<FVector>& B)
	{
		FVector Sum = FVector::ZeroVector;
		int32 Count = 0;
		for (const FVector& Point : A)
		{
			Sum += Point;
			++Count;
		}
		for (const FVector& Point : B)
		{
			Sum += Point;
			++Count;
		}
		return Count > 0 ? Sum / static_cast<float>(Count) : FVector::ZeroVector;
	}
}

AChunkedDestructiblePyramid::AChunkedDestructiblePyramid()
{
	FloorPattern = EChunkedFloorPattern::Solid;
	ChunkSideLength = 96;
	MaxGeneratedCells = 100000;
	bGenerateBottomFaces = true;
	bUseDestructionRipple = true;
}

#if WITH_EDITOR
bool AChunkedDestructiblePyramid::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		const FName PropertyName = InProperty->GetFName();
		if (PropertyName == FName(TEXT("AreaSize")) ||
			PropertyName == FName(TEXT("CellDimensions")) ||
			PropertyName == FName(TEXT("CellSpacing")) ||
			PropertyName == FName(TEXT("FillRule")) ||
			PropertyName == FName(TEXT("FloorPattern")) ||
			PropertyName == FName(TEXT("SpiralSettings")) ||
			PropertyName == FName(TEXT("PrimaryMaterialIndex")) ||
			PropertyName == FName(TEXT("SecondaryMaterialIndex")) ||
			PropertyName == FName(TEXT("PatternCellScale")) ||
			PropertyName == FName(TEXT("PatternRandomSeed")))
		{
			return false;
		}
	}
	return Super::CanEditChange(InProperty);
}
#endif

void AChunkedDestructiblePyramid::RebuildPyramid(bool bPreserveCellState)
{
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructiblePyramid::ConfigurePyramid(
	const FChunkedPyramidShapeSettings& NewShape,
	const FChunkedPyramidStepSettings& NewSteps,
	const FChunkedPyramidEntranceSettings& NewEntrance,
	const FChunkedPyramidInteriorFloorSettings& NewInteriorFloor,
	bool bPreserveCellState)
{
	Shape = NewShape;
	Steps = NewSteps;
	Entrance = NewEntrance;
	InteriorFloor = NewInteriorFloor;
	GenerateFloor(bPreserveCellState);
}

FChunkedDestructiblePyramidMetrics AChunkedDestructiblePyramid::GetPyramidMetrics() const
{
	FChunkedDestructiblePyramidMetrics Metrics;
	Metrics.PyramidBaseWidth = FMath::Max(Shape.BaseWidth, MinimumPyramidDimension);
	Metrics.OverallBaseWidth = CachedOverallBaseWidth > 0.0f
		? CachedOverallBaseWidth
		: Metrics.PyramidBaseWidth;
	Metrics.OverallHeight = CachedOverallHeight > 0.0f
		? CachedOverallHeight
		: FMath::Max(Shape.Height, MinimumPyramidDimension);
	Metrics.PyramidPieceCount = CachedPyramidPieceCount;
	Metrics.StepPieceCount = CachedStepPieceCount;
	Metrics.InteriorFloorPieceCount = CachedInteriorFloorPieceCount;
	Metrics.TotalPieceCount = PieceGeometry.Num();
	return Metrics;
}

bool AChunkedDestructiblePyramid::CalculateLayout()
{
	Shape.BaseWidth = FMath::Max(FMath::Abs(Shape.BaseWidth), 100.0f);
	Shape.Height = FMath::Max(FMath::Abs(Shape.Height), 100.0f);
	Shape.PieceSize = FMath::Max(FMath::Abs(Shape.PieceSize), MinimumPyramidDimension);
	Shape.ShellThickness = FMath::Clamp(
		FMath::Abs(Shape.ShellThickness),
		2.0f,
		Shape.PieceSize * 2.0f);
	Shape.PieceGap = FMath::Clamp(
		Shape.PieceGap,
		0.0f,
		Shape.PieceSize * 0.45f);

	Steps.StepCount = FMath::Clamp(Steps.StepCount, 1, 16);
	Steps.StepDepth = FMath::Max(FMath::Abs(Steps.StepDepth), MinimumPyramidDimension);
	Steps.StepHeight = FMath::Max(FMath::Abs(Steps.StepHeight), MinimumPyramidDimension);
	Entrance.Width = FMath::Clamp(
		FMath::Abs(Entrance.Width),
		MinimumPyramidDimension,
		Shape.BaseWidth * 0.8f);
	Entrance.Height = FMath::Clamp(
		FMath::Abs(Entrance.Height),
		MinimumPyramidDimension,
		Shape.Height * 0.85f);
	InteriorFloor.Thickness = FMath::Clamp(
		FMath::Abs(InteriorFloor.Thickness),
		2.0f,
		Shape.PieceSize * 2.0f);
	InteriorFloor.EdgeInset = FMath::Clamp(
		FMath::Abs(InteriorFloor.EdgeInset),
		0.0f,
		Shape.BaseWidth * 0.45f);

	if (!RebuildPieceGeometry() || PieceGeometry.IsEmpty())
	{
		PieceGeometry.Reset();
		UE_LOG(
			LogTemp,
			Error,
			TEXT("%s could not generate its pyramid pieces within MaxGeneratedCells (%d)."),
			*GetName(),
			FMath::Max(MaxGeneratedCells, 1));
		return false;
	}

	CellCount = FIntPoint(PieceGeometry.Num(), 1);
	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	ChunkCount = FIntPoint(FMath::DivideAndRoundUp(CellCount.X, SafeChunkSide), 1);
	EffectiveCellDimensions = FVector(
		Shape.PieceSize,
		Shape.PieceSize,
		FMath::Max(
			FMath::Max3(Shape.ShellThickness, Steps.StepHeight, MinimumPyramidDimension),
			InteriorFloor.Thickness));
	CellDimensions = EffectiveCellDimensions;
	CellSpacing = FVector2D(Shape.PieceGap);
	EffectiveGridSize = FVector2D(CachedOverallBaseWidth);
	GridLocalMin = -EffectiveGridSize * 0.5f;
	AreaSize = EffectiveGridSize;
	return true;
}

bool AChunkedDestructiblePyramid::RebuildPieceGeometry()
{
	PieceGeometry.Reset();
	CachedPyramidPieceCount = 0;
	CachedStepPieceCount = 0;
	CachedInteriorFloorPieceCount = 0;
	const int32 EffectiveStepCount = Steps.bGenerateSteps ? Steps.StepCount : 0;
	CachedBodyBaseZ = EffectiveStepCount * Steps.StepHeight;
	CachedOverallBaseWidth = Shape.BaseWidth +
		2.0f * static_cast<float>(EffectiveStepCount) * Steps.StepDepth;
	CachedOverallHeight = CachedBodyBaseZ + Shape.Height;

	if (!AddSteppedBasePieces())
	{
		return false;
	}
	CachedStepPieceCount = PieceGeometry.Num();
	if (!AddInteriorFloorPieces())
	{
		return false;
	}
	CachedInteriorFloorPieceCount = PieceGeometry.Num() - CachedStepPieceCount;
	if (!AddPyramidFacePieces())
	{
		return false;
	}
	CachedPyramidPieceCount = PieceGeometry.Num() -
		CachedStepPieceCount - CachedInteriorFloorPieceCount;
	return true;
}

bool AChunkedDestructiblePyramid::CanAddAnotherPiece() const
{
	return PieceGeometry.Num() < FMath::Max(MaxGeneratedCells, 1);
}

bool AChunkedDestructiblePyramid::AddSteppedBasePieces()
{
	if (!Steps.bGenerateSteps)
	{
		return true;
	}

	const float BodyHalfWidth = Shape.BaseWidth * 0.5f;
	for (int32 Layer = 0; Layer < Steps.StepCount; ++Layer)
	{
		const float OuterHalfWidth = BodyHalfWidth +
			static_cast<float>(Steps.StepCount - Layer) * Steps.StepDepth;
		const float InnerHalfWidth = BodyHalfWidth +
			static_cast<float>(Steps.StepCount - Layer - 1) * Steps.StepDepth;
		const float LayerWidth = OuterHalfWidth * 2.0f;
		const int32 CellsPerAxis = FMath::Clamp(
			FMath::CeilToInt(LayerWidth / Shape.PieceSize),
			1,
			MaximumFaceColumns);
		const float CellWidth = LayerWidth / static_cast<float>(CellsPerAxis);
		const float HorizontalSize = FMath::Max(CellWidth - Shape.PieceGap, 1.0f);
		const float VerticalSize = FMath::Max(Steps.StepHeight - Shape.PieceGap, 1.0f);
		const float CenterZ = (static_cast<float>(Layer) + 0.5f) * Steps.StepHeight;

		for (int32 Y = 0; Y < CellsPerAxis; ++Y)
		{
			const float CenterY = -OuterHalfWidth +
				(static_cast<float>(Y) + 0.5f) * CellWidth;
			for (int32 X = 0; X < CellsPerAxis; ++X)
			{
				const float CenterX = -OuterHalfWidth +
					(static_cast<float>(X) + 0.5f) * CellWidth;
				const bool bTouchesExposedRing =
					FMath::Max(FMath::Abs(CenterX), FMath::Abs(CenterY)) +
						CellWidth * 0.5f > InnerHalfWidth + GeometryTolerance;
				if (!bTouchesExposedRing)
				{
					continue;
				}

				if (!AddBoxPiece(
					EPyramidPieceKind::Step,
					FVector(CenterX, CenterY, CenterZ),
					FVector(HorizontalSize, HorizontalSize, VerticalSize)))
				{
					return false;
				}
			}
		}
	}
	return true;
}

bool AChunkedDestructiblePyramid::AddInteriorFloorPieces()
{
	if (!InteriorFloor.bGenerateFloor)
	{
		return true;
	}

	const float FloorWidth = FMath::Max(
		Shape.BaseWidth - InteriorFloor.EdgeInset * 2.0f,
		MinimumPyramidDimension);
	const int32 CellsPerAxis = FMath::Clamp(
		FMath::CeilToInt(FloorWidth / Shape.PieceSize),
		1,
		MaximumFaceColumns);
	const float CellWidth = FloorWidth / static_cast<float>(CellsPerAxis);
	const float HorizontalSize = FMath::Max(CellWidth - Shape.PieceGap, 1.0f);
	const float VerticalSize = FMath::Max(
		InteriorFloor.Thickness - Shape.PieceGap,
		1.0f);
	const float FloorMin = -FloorWidth * 0.5f;
	const float CenterZ = CachedBodyBaseZ - InteriorFloor.Thickness * 0.5f;

	for (int32 Y = 0; Y < CellsPerAxis; ++Y)
	{
		const float CenterY = FloorMin + (static_cast<float>(Y) + 0.5f) * CellWidth;
		for (int32 X = 0; X < CellsPerAxis; ++X)
		{
			const float CenterX = FloorMin + (static_cast<float>(X) + 0.5f) * CellWidth;
			if (!AddBoxPiece(
				EPyramidPieceKind::InteriorFloor,
				FVector(CenterX, CenterY, CenterZ),
				FVector(HorizontalSize, HorizontalSize, VerticalSize)))
			{
				return false;
			}
		}
	}
	return true;
}

bool AChunkedDestructiblePyramid::AddPyramidFacePieces()
{
	// A single solid cap owns the exact tip. Extruding four independent triangular face
	// pieces at the same apex makes their inner vertices diverge and visually fork.
	const float ApexHeight = FMath::Clamp(
		Shape.PieceSize,
		MinimumPyramidDimension,
		Shape.Height * 0.1f);
	const float ApexBaseZ = Shape.Height - ApexHeight;
	const int32 RowCount = FMath::Clamp(
		FMath::CeilToInt(ApexBaseZ / Shape.PieceSize),
		1,
		MaximumFaceRows);
	const float RowHeight = ApexBaseZ / static_cast<float>(RowCount);

	for (int32 FaceIndex = 0; FaceIndex < 4; ++FaceIndex)
	{
		const FVector OutwardNormal = GetPyramidFaceNormal(FaceIndex);
		for (int32 Row = 0; Row < RowCount; ++Row)
		{
			const float RowBottom = static_cast<float>(Row) * RowHeight;
			const float RowTop = static_cast<float>(Row + 1) * RowHeight;
			const float RowMiddle = (RowBottom + RowTop) * 0.5f;
			const float WidthAtMiddle = Shape.BaseWidth *
				(1.0f - RowMiddle / Shape.Height);
			const int32 ColumnCount = FMath::Clamp(
				FMath::CeilToInt(WidthAtMiddle / Shape.PieceSize),
				1,
				MaximumFaceColumns);

			for (int32 Column = 0; Column < ColumnCount; ++Column)
			{
				const float BaseAlpha0 = static_cast<float>(Column) /
					static_cast<float>(ColumnCount);
				const float BaseAlpha1 = static_cast<float>(Column + 1) /
					static_cast<float>(ColumnCount);
				const float SegmentWidth = FMath::Max(
					WidthAtMiddle / static_cast<float>(ColumnCount),
					1.0f);
				const float AlphaInset = FMath::Min(
					Shape.PieceGap / FMath::Max(WidthAtMiddle * 2.0f, 1.0f),
					(BaseAlpha1 - BaseAlpha0) * 0.45f);
				const float Alpha0 = BaseAlpha0 + AlphaInset;
				const float Alpha1 = BaseAlpha1 - AlphaInset;

				const float HalfWidthAtBottom = Shape.BaseWidth * 0.5f *
					(1.0f - RowBottom / Shape.Height);
				const float HalfWidthAtTop = Shape.BaseWidth * 0.5f *
					(1.0f - RowTop / Shape.Height);
				const float UnshrunkCoordinates[] = {
					FMath::Lerp(-HalfWidthAtBottom, HalfWidthAtBottom, BaseAlpha0),
					FMath::Lerp(-HalfWidthAtBottom, HalfWidthAtBottom, BaseAlpha1),
					FMath::Lerp(-HalfWidthAtTop, HalfWidthAtTop, BaseAlpha0),
					FMath::Lerp(-HalfWidthAtTop, HalfWidthAtTop, BaseAlpha1)
				};
				float SegmentMinimum = UnshrunkCoordinates[0];
				float SegmentMaximum = UnshrunkCoordinates[0];
				for (int32 CoordinateIndex = 1; CoordinateIndex < UE_ARRAY_COUNT(UnshrunkCoordinates); ++CoordinateIndex)
				{
					SegmentMinimum = FMath::Min(SegmentMinimum, UnshrunkCoordinates[CoordinateIndex]);
					SegmentMaximum = FMath::Max(SegmentMaximum, UnshrunkCoordinates[CoordinateIndex]);
				}
				const bool bEntrancePiece =
					Entrance.bGenerateEntrance &&
					FaceHasEntrance(FaceIndex) &&
					RowBottom < Entrance.Height &&
					SegmentMaximum > -Entrance.Width * 0.5f &&
					SegmentMinimum < Entrance.Width * 0.5f;
				if (bEntrancePiece)
				{
					continue;
				}

				const float VerticalInset = FMath::Min(
					Shape.PieceGap * 0.5f,
					RowHeight * 0.45f);
				const float PieceBottom = RowBottom + VerticalInset;
				const float PieceTop = RowTop - VerticalInset;
				const float BottomHalfWidth = Shape.BaseWidth * 0.5f *
					(1.0f - PieceBottom / Shape.Height);
				const float TopHalfWidth = Shape.BaseWidth * 0.5f *
					(1.0f - PieceTop / Shape.Height);
				const float BottomHorizontal0 = FMath::Lerp(
					-BottomHalfWidth, BottomHalfWidth, Alpha0);
				const float BottomHorizontal1 = FMath::Lerp(
					-BottomHalfWidth, BottomHalfWidth, Alpha1);
				const float TopHorizontal0 = FMath::Lerp(
					-TopHalfWidth, TopHalfWidth, Alpha0);
				const float TopHorizontal1 = FMath::Lerp(
					-TopHalfWidth, TopHalfWidth, Alpha1);

				TArray<FVector> OuterPolygon;
				OuterPolygon.Reserve(4);
				OuterPolygon.Add(GetPyramidSurfacePoint(
					FaceIndex, BottomHorizontal0, PieceBottom));
				OuterPolygon.Add(GetPyramidSurfacePoint(
					FaceIndex, BottomHorizontal1, PieceBottom));
				const FVector TopRight = GetPyramidSurfacePoint(
					FaceIndex, TopHorizontal1, PieceTop);
				const FVector TopLeft = GetPyramidSurfacePoint(
					FaceIndex, TopHorizontal0, PieceTop);
				if (TopRight.Equals(TopLeft, GeometryTolerance))
				{
					OuterPolygon.Add(TopRight);
				}
				else
				{
					OuterPolygon.Add(TopRight);
					OuterPolygon.Add(TopLeft);
				}

				if (!AddFacePiece(
					FaceIndex,
					Row,
					Column,
					OuterPolygon,
					OutwardNormal))
				{
					return false;
				}
			}
		}
	}
	return AddApexPiece(ApexBaseZ);
}

bool AChunkedDestructiblePyramid::AddApexPiece(float LocalBaseZ)
{
	if (!CanAddAnotherPiece())
	{
		return false;
	}

	const float SafeBaseZ = FMath::Clamp(LocalBaseZ, 0.0f, Shape.Height);
	const float HalfWidth = Shape.BaseWidth * 0.5f *
		(1.0f - SafeBaseZ / Shape.Height);
	const float WorldBaseZ = CachedBodyBaseZ + SafeBaseZ;
	const float WorldApexZ = CachedBodyBaseZ + Shape.Height;

	FPyramidPieceGeometry& Piece = PieceGeometry.Emplace_GetRef();
	Piece.Kind = EPyramidPieceKind::Apex;
	Piece.OuterPolygon.Reserve(5);
	Piece.OuterPolygon.Add(FVector(-HalfWidth, -HalfWidth, WorldBaseZ));
	Piece.OuterPolygon.Add(FVector(HalfWidth, -HalfWidth, WorldBaseZ));
	Piece.OuterPolygon.Add(FVector(HalfWidth, HalfWidth, WorldBaseZ));
	Piece.OuterPolygon.Add(FVector(-HalfWidth, HalfWidth, WorldBaseZ));
	Piece.OuterPolygon.Add(FVector(0.0f, 0.0f, WorldApexZ));
	Piece.Center = FVector(
		0.0f,
		0.0f,
		WorldBaseZ + (WorldApexZ - WorldBaseZ) * 0.25f);
	Piece.Bounds = FBox(ForceInit);
	for (const FVector& Point : Piece.OuterPolygon)
	{
		Piece.Bounds += Point;
	}
	return true;
}

bool AChunkedDestructiblePyramid::AddFacePiece(
	int32 FaceIndex,
	int32 RowIndex,
	int32 ColumnIndex,
	const TArray<FVector>& OuterPolygon,
	const FVector& OutwardNormal)
{
	if (OuterPolygon.Num() < 3 || !CanAddAnotherPiece())
	{
		return false;
	}

	FPyramidPieceGeometry& Piece = PieceGeometry.Emplace_GetRef();
	Piece.Kind = EPyramidPieceKind::Face;
	Piece.FaceIndex = FaceIndex;
	Piece.RowIndex = RowIndex;
	Piece.ColumnIndex = ColumnIndex;
	Piece.OuterPolygon = OuterPolygon;
	Piece.InnerPolygon.Reserve(OuterPolygon.Num());
	for (const FVector& Point : OuterPolygon)
	{
		Piece.InnerPolygon.Add(Point - OutwardNormal * Shape.ShellThickness);
	}
	Piece.Center = AveragePoints(Piece.OuterPolygon, Piece.InnerPolygon);
	Piece.Bounds = FBox(ForceInit);
	for (const FVector& Point : Piece.OuterPolygon)
	{
		Piece.Bounds += Point;
	}
	for (const FVector& Point : Piece.InnerPolygon)
	{
		Piece.Bounds += Point;
	}
	return true;
}

bool AChunkedDestructiblePyramid::AddBoxPiece(
	EPyramidPieceKind Kind,
	FVector Center,
	FVector Size)
{
	if (Kind == EPyramidPieceKind::Face || !CanAddAnotherPiece())
	{
		return false;
	}
	FPyramidPieceGeometry& Piece = PieceGeometry.Emplace_GetRef();
	Piece.Kind = Kind;
	Piece.BoxCenter = Center;
	Piece.BoxSize = Size.ComponentMax(FVector(1.0f));
	Piece.Center = Center;
	Piece.Bounds = FBox(
		Center - Piece.BoxSize * 0.5f,
		Center + Piece.BoxSize * 0.5f);
	return true;
}

bool AChunkedDestructiblePyramid::FaceHasEntrance(int32 FaceIndex) const
{
	switch (Entrance.Sides)
	{
	case EChunkedPyramidEntranceSides::AllSides:
		return true;
	case EChunkedPyramidEntranceSides::FrontAndBack:
		return FaceIndex == 0 || FaceIndex == 2;
	case EChunkedPyramidEntranceSides::FrontOnly:
	default:
		return FaceIndex == 0;
	}
}

FVector AChunkedDestructiblePyramid::GetPyramidSurfacePoint(
	int32 FaceIndex,
	float Horizontal,
	float LocalBodyZ) const
{
	const float HalfWidth = Shape.BaseWidth * 0.5f *
		(1.0f - FMath::Clamp(LocalBodyZ / Shape.Height, 0.0f, 1.0f));
	const FVector RadialAxes[] = {
		FVector(0.0f, -1.0f, 0.0f),
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(-1.0f, 0.0f, 0.0f)
	};
	const FVector HorizontalAxes[] = {
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(-1.0f, 0.0f, 0.0f),
		FVector(0.0f, -1.0f, 0.0f)
	};
	const int32 SafeFace = FMath::Clamp(FaceIndex, 0, 3);
	return RadialAxes[SafeFace] * HalfWidth +
		HorizontalAxes[SafeFace] * Horizontal +
		FVector::UpVector * (CachedBodyBaseZ + LocalBodyZ);
}

FVector AChunkedDestructiblePyramid::GetPyramidFaceNormal(int32 FaceIndex) const
{
	const FVector RadialAxes[] = {
		FVector(0.0f, -1.0f, 0.0f),
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(-1.0f, 0.0f, 0.0f)
	};
	const int32 SafeFace = FMath::Clamp(FaceIndex, 0, 3);
	const float VerticalSlope = Shape.BaseWidth * 0.5f /
		FMath::Max(Shape.Height, MinimumPyramidDimension);
	return (RadialAxes[SafeFace] + FVector::UpVector * VerticalSlope).GetSafeNormal();
}

bool AChunkedDestructiblePyramid::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	return Cell.Y == 0 && PieceGeometry.IsValidIndex(Cell.X);
}

FVector AChunkedDestructiblePyramid::GetCellLocalCenter(FIntPoint Cell) const
{
	return Cell.Y == 0 && PieceGeometry.IsValidIndex(Cell.X)
		? PieceGeometry[Cell.X].Center
		: FVector::ZeroVector;
}

int32 AChunkedDestructiblePyramid::EvaluatePatternMaterial(FIntPoint Cell) const
{
	if (Cell.Y != 0 || !PieceGeometry.IsValidIndex(Cell.X))
	{
		return 0;
	}
	const int32 MaximumMaterialIndex = FMath::Max(FloorMaterials.Num() - 1, 0);
	const FPyramidPieceGeometry& Piece = PieceGeometry[Cell.X];
	if (Piece.Kind == EPyramidPieceKind::Step)
	{
		return FMath::Clamp(StepMaterialIndex, 0, MaximumMaterialIndex);
	}
	if (Piece.Kind == EPyramidPieceKind::InteriorFloor)
	{
		return FMath::Clamp(InteriorFloorMaterialIndex, 0, MaximumMaterialIndex);
	}
	if (Piece.Kind == EPyramidPieceKind::Apex)
	{
		return FMath::Clamp(PyramidMaterialIndex, 0, MaximumMaterialIndex);
	}
	const bool bUseAccent = bAlternatePyramidPieces &&
		((Piece.FaceIndex + Piece.RowIndex + Piece.ColumnIndex) & 1) != 0;
	return FMath::Clamp(
		bUseAccent ? PyramidAccentMaterialIndex : PyramidMaterialIndex,
		0,
		MaximumMaterialIndex);
}

void AChunkedDestructiblePyramid::AppendFacePieceMesh(
	FDynamicMesh3& Mesh,
	const FPyramidPieceGeometry& Piece,
	int32 MaterialIndex) const
{
	const int32 VertexCount = Piece.OuterPolygon.Num();
	if (VertexCount < 3 || Piece.InnerPolygon.Num() != VertexCount)
	{
		return;
	}
	const FVector3f OuterNormal(GetPyramidFaceNormal(Piece.FaceIndex));
	const FVector3f InnerNormal = -OuterNormal;
	const int32 MaximumMaterialIndex = FMath::Max(FloorMaterials.Num() - 1, 0);
	const int32 CutMaterialIndex = FMath::Clamp(
		PyramidCutMaterialIndex,
		0,
		MaximumMaterialIndex);
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	auto MakeUV = [SafeUVScale](const FVector& Point)
	{
		return FVector2f(
			static_cast<float>((Point.X + Point.Y) / SafeUVScale),
			static_cast<float>(Point.Z / SafeUVScale));
	};

	for (int32 Index = 1; Index + 1 < VertexCount; ++Index)
	{
		AppendMeshTriangle(
			Mesh,
			FVector3d(Piece.OuterPolygon[0]),
			FVector3d(Piece.OuterPolygon[Index]),
			FVector3d(Piece.OuterPolygon[Index + 1]),
			OuterNormal,
			MaterialIndex,
			MakeUV(Piece.OuterPolygon[0]),
			MakeUV(Piece.OuterPolygon[Index]),
			MakeUV(Piece.OuterPolygon[Index + 1]));
		AppendMeshTriangle(
			Mesh,
			FVector3d(Piece.InnerPolygon[0]),
			FVector3d(Piece.InnerPolygon[Index + 1]),
			FVector3d(Piece.InnerPolygon[Index]),
			InnerNormal,
			CutMaterialIndex,
			MakeUV(Piece.InnerPolygon[0]),
			MakeUV(Piece.InnerPolygon[Index + 1]),
			MakeUV(Piece.InnerPolygon[Index]));
	}

	for (int32 EdgeIndex = 0; EdgeIndex < VertexCount; ++EdgeIndex)
	{
		const int32 NextIndex = (EdgeIndex + 1) % VertexCount;
		const FVector& OuterA = Piece.OuterPolygon[EdgeIndex];
		const FVector& OuterB = Piece.OuterPolygon[NextIndex];
		const FVector& InnerB = Piece.InnerPolygon[NextIndex];
		const FVector& InnerA = Piece.InnerPolygon[EdgeIndex];
		const FVector SideNormal = FVector::CrossProduct(
			InnerA - OuterA,
			InnerB - OuterA).GetSafeNormal();
		AppendMeshQuad(
			Mesh,
			FVector3d(OuterA),
			FVector3d(InnerA),
			FVector3d(InnerB),
			FVector3d(OuterB),
			FVector3f(SideNormal),
			CutMaterialIndex,
			static_cast<float>((OuterB - OuterA).Size()),
			Shape.ShellThickness,
			UVWorldUnits);
	}
}

void AChunkedDestructiblePyramid::AppendBoxPieceMesh(
	FDynamicMesh3& Mesh,
	const FPyramidPieceGeometry& Piece,
	int32 MaterialIndex) const
{
	const FVector Extent = Piece.BoxSize * 0.5f;
	const FVector& C = Piece.BoxCenter;
	const double MinX = C.X - Extent.X;
	const double MaxX = C.X + Extent.X;
	const double MinY = C.Y - Extent.Y;
	const double MaxY = C.Y + Extent.Y;
	const double MinZ = C.Z - Extent.Z;
	const double MaxZ = C.Z + Extent.Z;
	AppendMeshQuad(Mesh,
		FVector3d(MinX, MinY, MaxZ), FVector3d(MaxX, MinY, MaxZ),
		FVector3d(MaxX, MaxY, MaxZ), FVector3d(MinX, MaxY, MaxZ),
		FVector3f::UnitZ(), MaterialIndex, Piece.BoxSize.X, Piece.BoxSize.Y, UVWorldUnits);
	AppendMeshQuad(Mesh,
		FVector3d(MinX, MaxY, MinZ), FVector3d(MaxX, MaxY, MinZ),
		FVector3d(MaxX, MinY, MinZ), FVector3d(MinX, MinY, MinZ),
		-FVector3f::UnitZ(), MaterialIndex, Piece.BoxSize.X, Piece.BoxSize.Y, UVWorldUnits);
	AppendMeshQuad(Mesh,
		FVector3d(MinX, MinY, MinZ), FVector3d(MaxX, MinY, MinZ),
		FVector3d(MaxX, MinY, MaxZ), FVector3d(MinX, MinY, MaxZ),
		-FVector3f::UnitY(), MaterialIndex, Piece.BoxSize.X, Piece.BoxSize.Z, UVWorldUnits);
	AppendMeshQuad(Mesh,
		FVector3d(MaxX, MaxY, MinZ), FVector3d(MinX, MaxY, MinZ),
		FVector3d(MinX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MaxZ),
		FVector3f::UnitY(), MaterialIndex, Piece.BoxSize.X, Piece.BoxSize.Z, UVWorldUnits);
	AppendMeshQuad(Mesh,
		FVector3d(MaxX, MinY, MinZ), FVector3d(MaxX, MaxY, MinZ),
		FVector3d(MaxX, MaxY, MaxZ), FVector3d(MaxX, MinY, MaxZ),
		FVector3f::UnitX(), MaterialIndex, Piece.BoxSize.Y, Piece.BoxSize.Z, UVWorldUnits);
	AppendMeshQuad(Mesh,
		FVector3d(MinX, MaxY, MinZ), FVector3d(MinX, MinY, MinZ),
		FVector3d(MinX, MinY, MaxZ), FVector3d(MinX, MaxY, MaxZ),
		-FVector3f::UnitX(), MaterialIndex, Piece.BoxSize.Y, Piece.BoxSize.Z, UVWorldUnits);
}

void AChunkedDestructiblePyramid::AppendApexPieceMesh(
	FDynamicMesh3& Mesh,
	const FPyramidPieceGeometry& Piece,
	int32 MaterialIndex) const
{
	if (Piece.OuterPolygon.Num() != 5)
	{
		return;
	}

	const int32 MaximumMaterialIndex = FMath::Max(FloorMaterials.Num() - 1, 0);
	const int32 CutMaterialIndex = FMath::Clamp(
		PyramidCutMaterialIndex,
		0,
		MaximumMaterialIndex);
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	auto MakeUV = [SafeUVScale](const FVector& Point)
	{
		return FVector2f(
			static_cast<float>((Point.X + Point.Y) / SafeUVScale),
			static_cast<float>(Point.Z / SafeUVScale));
	};

	const FVector& Apex = Piece.OuterPolygon[4];
	for (int32 FaceIndex = 0; FaceIndex < 4; ++FaceIndex)
	{
		const FVector& BaseA = Piece.OuterPolygon[FaceIndex];
		const FVector& BaseB = Piece.OuterPolygon[(FaceIndex + 1) % 4];
		AppendMeshTriangle(
			Mesh,
			FVector3d(BaseA),
			FVector3d(BaseB),
			FVector3d(Apex),
			FVector3f(GetPyramidFaceNormal(FaceIndex)),
			MaterialIndex,
			MakeUV(BaseA),
			MakeUV(BaseB),
			MakeUV(Apex));
	}

	const float BaseWidth = static_cast<float>(
		(Piece.OuterPolygon[1] - Piece.OuterPolygon[0]).Size());
	AppendMeshQuad(
		Mesh,
		FVector3d(Piece.OuterPolygon[0]),
		FVector3d(Piece.OuterPolygon[3]),
		FVector3d(Piece.OuterPolygon[2]),
		FVector3d(Piece.OuterPolygon[1]),
		-FVector3f::UnitZ(),
		CutMaterialIndex,
		BaseWidth,
		BaseWidth,
		UVWorldUnits);
}

void AChunkedDestructiblePyramid::BuildChunkMesh(
	int32 ChunkIndex,
	FDynamicMesh3& OutMesh) const
{
	OutMesh.EnableVertexNormals(FVector3f::UnitZ());
	OutMesh.EnableAttributes();
	OutMesh.Attributes()->SetNumUVLayers(1);
	OutMesh.Attributes()->EnableMaterialID();

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 FirstCellIndex = ChunkIndex * SafeChunkSide;
	const int32 LastCellIndex = FMath::Min(FirstCellIndex + SafeChunkSide, PieceGeometry.Num());
	for (int32 CellIndex = FirstCellIndex; CellIndex < LastCellIndex; ++CellIndex)
	{
		if (!Cells.IsValidIndex(CellIndex) || !Cells[CellIndex].bExists)
		{
			continue;
		}
		const FPyramidPieceGeometry& Piece = PieceGeometry[CellIndex];
		if (Piece.Kind == EPyramidPieceKind::Face)
		{
			AppendFacePieceMesh(OutMesh, Piece, Cells[CellIndex].MaterialIndex);
		}
		else if (Piece.Kind == EPyramidPieceKind::Apex)
		{
			AppendApexPieceMesh(OutMesh, Piece, Cells[CellIndex].MaterialIndex);
		}
		else
		{
			AppendBoxPieceMesh(OutMesh, Piece, Cells[CellIndex].MaterialIndex);
		}
	}
}

void AChunkedDestructiblePyramid::BuildChunkSimpleCollision(
	int32 ChunkIndex,
	FKAggregateGeom& OutCollision) const
{
	OutCollision.EmptyElements();
	if (!bGenerateCollision)
	{
		return;
	}

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 FirstCellIndex = ChunkIndex * SafeChunkSide;
	const int32 LastCellIndex = FMath::Min(FirstCellIndex + SafeChunkSide, PieceGeometry.Num());
	for (int32 CellIndex = FirstCellIndex; CellIndex < LastCellIndex; ++CellIndex)
	{
		if (!Cells.IsValidIndex(CellIndex) || !Cells[CellIndex].bExists)
		{
			continue;
		}
		const FPyramidPieceGeometry& Piece = PieceGeometry[CellIndex];
		if (Piece.Kind == EPyramidPieceKind::Step ||
			Piece.Kind == EPyramidPieceKind::InteriorFloor)
		{
			FKBoxElem& Box = OutCollision.BoxElems.Emplace_GetRef(
				Piece.BoxSize.X,
				Piece.BoxSize.Y,
				Piece.BoxSize.Z);
			Box.Center = Piece.BoxCenter;
			continue;
		}

		FKConvexElem& Convex = OutCollision.ConvexElems.Emplace_GetRef();
		auto AddUniqueVertex = [&Convex](const FVector& Vertex)
		{
			for (const FVector& Existing : Convex.VertexData)
			{
				if (Existing.Equals(Vertex, GeometryTolerance))
				{
					return;
				}
			}
			Convex.VertexData.Add(Vertex);
		};
		for (const FVector& Vertex : Piece.OuterPolygon)
		{
			AddUniqueVertex(Vertex);
		}
		for (const FVector& Vertex : Piece.InnerPolygon)
		{
			AddUniqueVertex(Vertex);
		}
		if (Convex.VertexData.Num() >= 4)
		{
			Convex.UpdateElemBox();
		}
		else
		{
			OutCollision.ConvexElems.Pop(EAllowShrinking::No);
		}
	}
}

FTransform AChunkedDestructiblePyramid::GetCellFlashTransform(FIntPoint Cell) const
{
	if (Cell.Y != 0 || !PieceGeometry.IsValidIndex(Cell.X))
	{
		return FTransform::Identity;
	}
	const FPyramidPieceGeometry& Piece = PieceGeometry[Cell.X];
	const bool bBoxPiece = Piece.Kind == EPyramidPieceKind::Step ||
		Piece.Kind == EPyramidPieceKind::InteriorFloor;
	const FVector Size = bBoxPiece
		? Piece.BoxSize
		: Piece.Bounds.GetSize().ComponentMax(FVector(1.0f));
	return FTransform(
		FQuat::Identity,
		Piece.Center,
		Size / 100.0f * FMath::Max(FlashScale, 0.01f));
}

bool AChunkedDestructiblePyramid::FindCellAtLocalPoint(
	FVector LocalPoint,
	FIntPoint& OutCell,
	float ExtraTolerance) const
{
	OutCell = FIntPoint::NoneValue;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 CellIndex = 0; CellIndex < PieceGeometry.Num(); ++CellIndex)
	{
		if (!Cells.IsValidIndex(CellIndex) || !Cells[CellIndex].bExists)
		{
			continue;
		}
		const FPyramidPieceGeometry& Piece = PieceGeometry[CellIndex];
		if (!Piece.Bounds.ExpandBy(ExtraTolerance).IsInsideOrOn(LocalPoint))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(LocalPoint, Piece.Center);
		if (DistanceSquared < BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			OutCell = FIntPoint(CellIndex, 0);
		}
	}
	return OutCell != FIntPoint::NoneValue;
}

bool AChunkedDestructiblePyramid::WorldLocationToCell(
	FVector WorldLocation,
	FIntPoint& OutCell) const
{
	return FindCellAtLocalPoint(
		GetActorTransform().InverseTransformPosition(WorldLocation),
		OutCell,
		1.0f);
}

bool AChunkedDestructiblePyramid::ResolveExistingCellFromHit(
	const FHitResult& Hit,
	FIntPoint& OutCell) const
{
	const FTransform ActorTransform = GetActorTransform();
	const FVector ImpactPoint = Hit.ImpactPoint.ContainsNaN()
		? Hit.Location
		: Hit.ImpactPoint;
	const float ProbeTolerance = FMath::Max(Shape.PieceGap + 2.0f, 2.0f);
	if (FindCellAtLocalPoint(
		ActorTransform.InverseTransformPosition(ImpactPoint),
		OutCell,
		ProbeTolerance))
	{
		return true;
	}
	return FindCellAtLocalPoint(
		ActorTransform.InverseTransformPosition(GetInteriorImpactPoint(Hit)),
		OutCell,
		ProbeTolerance + Shape.ShellThickness * 0.25f);
}

int32 AChunkedDestructiblePyramid::SelectCellsInRadius(
	FVector WorldLocation,
	float Radius,
	EChunkedFloorBlastShape ShapeType,
	TFunctionRef<void(int32, float)> CellFunction)
{
	if (Radius <= 0.0f || Cells.IsEmpty())
	{
		return 0;
	}
	const FVector LocalImpact = GetActorTransform().InverseTransformPosition(WorldLocation);
	const FVector ActorScale = GetActorScale3D().GetAbs();
	int32 SelectedCount = 0;
	for (int32 CellIndex = 0; CellIndex < PieceGeometry.Num(); ++CellIndex)
	{
		if (!Cells.IsValidIndex(CellIndex) || !Cells[CellIndex].bExists)
		{
			continue;
		}
		const FVector LocalDelta = PieceGeometry[CellIndex].Center - LocalImpact;
		const FVector ScaledDelta = LocalDelta * ActorScale;
		float Distance = 0.0f;
		switch (ShapeType)
		{
		case EChunkedFloorBlastShape::Square:
			Distance = FMath::Max3(
				FMath::Abs(ScaledDelta.X),
				FMath::Abs(ScaledDelta.Y),
				FMath::Abs(ScaledDelta.Z));
			break;
		case EChunkedFloorBlastShape::Diamond:
			Distance = FMath::Abs(ScaledDelta.X) +
				FMath::Abs(ScaledDelta.Y) +
				FMath::Abs(ScaledDelta.Z);
			break;
		case EChunkedFloorBlastShape::Circle:
		default:
			Distance = ScaledDelta.Size();
			break;
		}
		if (Distance <= Radius)
		{
			CellFunction(CellIndex, Distance);
			++SelectedCount;
		}
	}
	return SelectedCount;
}

void AChunkedDestructiblePyramid::MarkCellChunksDirty(FIntPoint Cell)
{
	if (Cell.Y != 0 || !PieceGeometry.IsValidIndex(Cell.X))
	{
		return;
	}
	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	DirtyChunkIndices.Add(Cell.X / SafeChunkSide);
}
