// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructiblePyramid.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Math/RotationMatrix.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UDynamicMesh.h"

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

	template <typename DistanceFunctionType>
	TArray<FVector> ClipFacePolygonToHalfSpace(
		const TArray<FVector>& Input,
		DistanceFunctionType&& SignedDistance)
	{
		TArray<FVector> Output;
		if (Input.IsEmpty())
		{
			return Output;
		}

		Output.Reserve(Input.Num() + 2);
		FVector Previous = Input.Last();
		float PreviousDistance = SignedDistance(Previous);
		bool bPreviousInside = PreviousDistance >= -GeometryTolerance;
		for (const FVector& Current : Input)
		{
			const float CurrentDistance = SignedDistance(Current);
			const bool bCurrentInside = CurrentDistance >= -GeometryTolerance;
			if (bCurrentInside != bPreviousInside)
			{
				const float Denominator = PreviousDistance - CurrentDistance;
				if (!FMath::IsNearlyZero(Denominator))
				{
					const float Alpha = FMath::Clamp(
						PreviousDistance / Denominator, 0.0f, 1.0f);
					Output.Add(FMath::Lerp(Previous, Current, Alpha));
				}
			}
			if (bCurrentInside)
			{
				Output.Add(Current);
			}
			Previous = Current;
			PreviousDistance = CurrentDistance;
			bPreviousInside = bCurrentInside;
		}
		return Output;
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

void AChunkedDestructiblePyramid::ConfigureTopSlice(
	bool bEnabled,
	int32 NewLayerCount,
	float NewSeparationDistance,
	UMaterialInterface* NewMaterial,
	bool bPreserveCellState)
{
	bTopSlice = bEnabled;
	TopSliceLayerCount = FMath::Clamp(NewLayerCount, 1, MaximumFaceRows + 1);
	TopSliceSeparationDistance = FMath::Max(NewSeparationDistance, 0.0f);
	TopSliceMaterial = NewMaterial;
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructiblePyramid::SetTopSliceCount(
	EChunkedPyramidTopSliceCount NewSliceCount,
	bool bPreserveCellState)
{
	TopSliceCount = NewSliceCount;
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
	TopSliceLayerCount = FMath::Clamp(TopSliceLayerCount, 1, MaximumFaceRows + 1);
	TopSliceSeparationDistance = FMath::Max(TopSliceSeparationDistance, 0.0f);

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
	CachedOverallHeight = CachedBodyBaseZ + Shape.Height +
		static_cast<float>(GetEffectiveTopSliceCount()) * TopSliceSeparationDistance;

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
	const int32 TotalPyramidLayerCount = RowCount + 1;

	for (int32 FaceIndex = 0; FaceIndex < 4; ++FaceIndex)
	{
		const FVector OutwardNormal = GetPyramidFaceNormal(FaceIndex);
		for (int32 Row = 0; Row < RowCount; ++Row)
		{
			const int32 LayerFromTop = RowCount - Row;
			const int32 TopSliceOffsetMultiplier = GetTopSliceOffsetMultiplierForLayer(
				LayerFromTop, TotalPyramidLayerCount);
			const bool bTopSliceRow = TopSliceOffsetMultiplier > 0;
			const float TopSliceVerticalOffset =
				static_cast<float>(TopSliceOffsetMultiplier) * TopSliceSeparationDistance;
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
				const float AlphaInset = FMath::Min(
					Shape.PieceGap / FMath::Max(WidthAtMiddle * 2.0f, 1.0f),
					(BaseAlpha1 - BaseAlpha0) * 0.45f);
				const float Alpha0 = BaseAlpha0 + AlphaInset;
				const float Alpha1 = BaseAlpha1 - AlphaInset;

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
				if (!AddEntranceClippedFacePieces(
					FaceIndex,
					Row,
					Column,
					OuterPolygon,
					OutwardNormal,
					bTopSliceRow,
					TopSliceVerticalOffset))
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
	const int32 TopSliceOffsetMultiplier = GetTopSliceOffsetMultiplierForLayer(0, 1);
	const float TopSliceOffset =
		static_cast<float>(TopSliceOffsetMultiplier) * TopSliceSeparationDistance;
	const float WorldBaseZ = CachedBodyBaseZ + SafeBaseZ + TopSliceOffset;
	const float WorldApexZ = CachedBodyBaseZ + Shape.Height + TopSliceOffset;

	FPyramidPieceGeometry& Piece = PieceGeometry.Emplace_GetRef();
	Piece.Kind = EPyramidPieceKind::Apex;
	Piece.bBelongsToTopSlice = TopSliceOffsetMultiplier > 0;
	Piece.TopSliceVerticalOffset = TopSliceOffset;
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
	const FVector& OutwardNormal,
	bool bBelongsToTopSlice,
	float TopSliceVerticalOffset)
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
	Piece.bBelongsToTopSlice = bBelongsToTopSlice;
	Piece.TopSliceVerticalOffset = TopSliceVerticalOffset;
	Piece.OuterPolygon = OuterPolygon;
	Piece.InnerPolygon.Reserve(OuterPolygon.Num());
	for (const FVector& Point : OuterPolygon)
	{
		FVector InnerPoint = Point - OutwardNormal * Shape.ShellThickness;
		if (Entrance.bGenerateEntrance && FaceHasEntrance(FaceIndex))
		{
			const FVector HorizontalAxes[] = {
				FVector(1.0f, 0.0f, 0.0f),
				FVector(0.0f, 1.0f, 0.0f),
				FVector(-1.0f, 0.0f, 0.0f),
				FVector(0.0f, -1.0f, 0.0f)
			};
			const FVector& HorizontalAxis = HorizontalAxes[FMath::Clamp(FaceIndex, 0, 3)];
			const float DoorTopZ = CachedBodyBaseZ + Entrance.Height +
				TopSliceVerticalOffset;
			const float Horizontal = FVector::DotProduct(Point, HorizontalAxis);
			if (FMath::IsNearlyEqual(Point.Z, DoorTopZ, GeometryTolerance) &&
				FMath::Abs(Horizontal) <= Entrance.Width * 0.5f + GeometryTolerance)
			{
				// Keep the inner lintel edge at the same height as the outer edge. A regular
				// normal extrusion lowers it on a sloped face and makes the opening look notched.
				InnerPoint.Z = Point.Z;
			}
		}
		Piece.InnerPolygon.Add(InnerPoint);
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

bool AChunkedDestructiblePyramid::AddEntranceClippedFacePieces(
	int32 FaceIndex,
	int32 RowIndex,
	int32 ColumnIndex,
	const TArray<FVector>& OuterPolygon,
	const FVector& OutwardNormal,
	bool bBelongsToTopSlice,
	float TopSliceVerticalOffset)
{
	TArray<TArray<FVector>> VisiblePolygons;
	if (!Entrance.bGenerateEntrance || !FaceHasEntrance(FaceIndex))
	{
		VisiblePolygons.Add(OuterPolygon);
	}
	else
	{
		// Split the tile into non-overlapping regions outside the exact door rectangle. Keeping
		// the upper region separate from the two lower side regions produces straight jambs and
		// a horizontal lintel instead of removing every whole tile touched by the opening.
		const float DoorTopZ = CachedBodyBaseZ + Entrance.Height;
		const float DoorHalfWidth = Entrance.Width * 0.5f;
		const FVector HorizontalAxes[] = {
			FVector(1.0f, 0.0f, 0.0f),
			FVector(0.0f, 1.0f, 0.0f),
			FVector(-1.0f, 0.0f, 0.0f),
			FVector(0.0f, -1.0f, 0.0f)
		};
		const FVector& HorizontalAxis = HorizontalAxes[FMath::Clamp(FaceIndex, 0, 3)];

		TArray<FVector> AboveDoor = ClipFacePolygonToHalfSpace(
			OuterPolygon,
			[DoorTopZ](const FVector& Point)
			{
				return Point.Z - DoorTopZ;
			});
		if (AboveDoor.Num() >= 3)
		{
			VisiblePolygons.Add(MoveTemp(AboveDoor));
		}

		const TArray<FVector> BelowDoor = ClipFacePolygonToHalfSpace(
			OuterPolygon,
			[DoorTopZ](const FVector& Point)
			{
				return DoorTopZ - Point.Z;
			});
		TArray<FVector> LeftOfDoor = ClipFacePolygonToHalfSpace(
			BelowDoor,
			[DoorHalfWidth, HorizontalAxis](const FVector& Point)
			{
				return -DoorHalfWidth - FVector::DotProduct(Point, HorizontalAxis);
			});
		if (LeftOfDoor.Num() >= 3)
		{
			VisiblePolygons.Add(MoveTemp(LeftOfDoor));
		}
		TArray<FVector> RightOfDoor = ClipFacePolygonToHalfSpace(
			BelowDoor,
			[DoorHalfWidth, HorizontalAxis](const FVector& Point)
			{
				return FVector::DotProduct(Point, HorizontalAxis) - DoorHalfWidth;
			});
		if (RightOfDoor.Num() >= 3)
		{
			VisiblePolygons.Add(MoveTemp(RightOfDoor));
		}
	}

	const FVector TopSliceOffset = FVector::UpVector * TopSliceVerticalOffset;
	for (TArray<FVector>& VisiblePolygon : VisiblePolygons)
	{
		for (FVector& Point : VisiblePolygon)
		{
			Point += TopSliceOffset;
		}
		if (!AddFacePiece(
			FaceIndex,
			RowIndex,
			ColumnIndex,
			VisiblePolygon,
			OutwardNormal,
			bBelongsToTopSlice,
			TopSliceVerticalOffset))
		{
			return false;
		}
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

int32 AChunkedDestructiblePyramid::GetEffectiveTopSliceCount() const
{
	if (!bTopSlice)
	{
		return 0;
	}
	return TopSliceCount == EChunkedPyramidTopSliceCount::Two ? 2 : 1;
}

int32 AChunkedDestructiblePyramid::GetTopSliceOffsetMultiplierForLayer(
	int32 LayerFromTop,
	int32 TotalLayerCount) const
{
	const int32 SliceCount = GetEffectiveTopSliceCount();
	if (SliceCount <= 0 || LayerFromTop < 0 || TotalLayerCount <= 0)
	{
		return 0;
	}

	// Clamp the interval so every requested slice remains inside the pyramid. A layer above both
	// cuts crosses two gaps and receives two separation offsets; the middle group crosses one.
	const int32 MaximumLayersPerSlice = FMath::Max(TotalLayerCount / SliceCount, 1);
	const int32 LayersPerSlice = FMath::Clamp(
		TopSliceLayerCount, 1, MaximumLayersPerSlice);
	const int32 GroupFromTop = LayerFromTop / LayersPerSlice;
	return FMath::Clamp(SliceCount - GroupFromTop, 0, SliceCount);
}

int32 AChunkedDestructiblePyramid::GetTopSliceMaterialSlot() const
{
	// Slot zero is still present when the inherited palette is empty, so the dedicated material
	// must begin after whichever regular slots ConfigureChunkComponent creates.
	return FMath::Max(FloorMaterials.Num(), 1);
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
	const bool bUseTopSliceMaterial = Piece.bBelongsToTopSlice &&
		bTopSlice && TopSliceMaterial && MaterialIndex == GetTopSliceMaterialSlot();
	const int32 CutMaterialIndex = bUseTopSliceMaterial
		? MaterialIndex
		: FMath::Clamp(PyramidCutMaterialIndex, 0, MaximumMaterialIndex);
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
	const bool bUseTopSliceMaterial = Piece.bBelongsToTopSlice &&
		bTopSlice && TopSliceMaterial && MaterialIndex == GetTopSliceMaterialSlot();
	const int32 CutMaterialIndex = bUseTopSliceMaterial
		? MaterialIndex
		: FMath::Clamp(PyramidCutMaterialIndex, 0, MaximumMaterialIndex);
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
		if (bReactToMusic)
		{
			MusicPieceTriangles.Remove(CellIndex);
		}
		if (!Cells.IsValidIndex(CellIndex) || !Cells[CellIndex].bExists)
		{
			continue;
		}
		const int32 FirstTriangle = OutMesh.MaxTriangleID();
		const FPyramidPieceGeometry& Piece = PieceGeometry[CellIndex];
		const int32 PieceMaterialIndex = Piece.bBelongsToTopSlice &&
			bTopSlice && TopSliceMaterial
			? GetTopSliceMaterialSlot()
			: Cells[CellIndex].MaterialIndex;
		if (Piece.Kind == EPyramidPieceKind::Face)
		{
			AppendFacePieceMesh(OutMesh, Piece, PieceMaterialIndex);
		}
		else if (Piece.Kind == EPyramidPieceKind::Apex)
		{
			AppendApexPieceMesh(OutMesh, Piece, PieceMaterialIndex);
		}
		else
		{
			AppendBoxPieceMesh(OutMesh, Piece, PieceMaterialIndex);
		}
		if (bReactToMusic)
		{
			FMusicPieceTriangles& Mapping = MusicPieceTriangles.Add(CellIndex);
			Mapping.FirstTriangle = FirstTriangle;
			auto* MaterialIDs = OutMesh.Attributes()->GetMaterialID();
			const bool bHighlighted = MusicHighlightedCells.Contains(CellIndex) &&
				!PendingCellIndices.Contains(CellIndex);
			for (int32 TriangleID = FirstTriangle; TriangleID < OutMesh.MaxTriangleID(); ++TriangleID)
			{
				Mapping.OriginalMaterials.Add(MaterialIDs->GetValue(TriangleID));
				if (bHighlighted)
				{
					MaterialIDs->SetValue(TriangleID, GetMusicMaterialSlot());
				}
			}
		}
	}
}

void AChunkedDestructiblePyramid::ConfigureChunkComponent(
	UDynamicMeshComponent* Component) const
{
	Super::ConfigureChunkComponent(Component);
	if (!Component)
	{
		return;
	}

	if (bTopSlice && TopSliceMaterial)
	{
		const int32 TopSliceMaterialSlot = GetTopSliceMaterialSlot();
		Component->SetNumMaterials(TopSliceMaterialSlot + 1);
		Component->SetMaterial(TopSliceMaterialSlot, TopSliceMaterial);
	}
	if (bReactToMusic && MusicReactionMaterialInstance)
	{
		const int32 MusicSlot = GetMusicMaterialSlot();
		Component->SetNumMaterials(MusicSlot + 1);
		Component->SetMaterial(MusicSlot, MusicReactionMaterialInstance);
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

FTransform AChunkedDestructiblePyramid::GetMusicEqualizerTransform(FIntPoint Cell) const
{
	if (Cell.Y != 0 || !PieceGeometry.IsValidIndex(Cell.X))
	{
		return FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	}
	// Used only to place logical equalizer slots. The existing piece mesh renders the effect.
	return FTransform(PieceGeometry[Cell.X].Center);
}

int32 AChunkedDestructiblePyramid::GetMusicMaterialSlot() const
{
	return GetTopSliceMaterialSlot() + (bTopSlice && TopSliceMaterial ? 1 : 0);
}

void AChunkedDestructiblePyramid::RenderMusicEqualizerCells(TConstArrayView<int32> CellIndices)
{
	TSet<int32> NewHighlights;
	for (const int32 CellIndex : CellIndices)
	{
		if (Cells.IsValidIndex(CellIndex) && Cells[CellIndex].bExists &&
			!PendingCellIndices.Contains(CellIndex))
		{
			NewHighlights.Add(CellIndex);
		}
	}

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	TSet<int32> ChangedChunks;
	for (const int32 CellIndex : MusicHighlightedCells)
	{
		if (!NewHighlights.Contains(CellIndex))
		{
			ChangedChunks.Add(CellIndex / SafeChunkSide);
		}
	}
	for (const int32 CellIndex : NewHighlights)
	{
		const int32 ChunkIndex = CellIndex / SafeChunkSide;
		const bool bMaterialChanged = ChunkComponents.IsValidIndex(ChunkIndex) &&
			ChunkComponents[ChunkIndex] &&
			ChunkComponents[ChunkIndex]->GetMaterial(GetMusicMaterialSlot()) != MusicReactionMaterialInstance;
		if (!MusicHighlightedCells.Contains(CellIndex) || !MusicPieceTriangles.Contains(CellIndex) || bMaterialChanged)
		{
			ChangedChunks.Add(ChunkIndex);
		}
	}
	MusicHighlightedCells = MoveTemp(NewHighlights);

	for (const int32 ChunkIndex : ChangedChunks)
	{
		if (!ChunkComponents.IsValidIndex(ChunkIndex) || !ChunkComponents[ChunkIndex])
		{
			continue;
		}
		const int32 FirstCell = ChunkIndex * SafeChunkSide;
		const int32 LastCell = FMath::Min(FirstCell + SafeChunkSide, Cells.Num());
		bool bNeedsTriangleMapping = false;
		for (int32 CellIndex = FirstCell; CellIndex < LastCell; ++CellIndex)
		{
			if (MusicHighlightedCells.Contains(CellIndex) && !MusicPieceTriangles.Contains(CellIndex))
			{
				bNeedsTriangleMapping = true;
				break;
			}
		}
		if (bNeedsTriangleMapping)
		{
			// Music may have been enabled after this chunk was built without animation data.
			RebuildChunk(ChunkIndex);
			continue;
		}

		UDynamicMeshComponent* Component = ChunkComponents[ChunkIndex];
		FDynamicMesh3* Mesh = Component->GetMesh();
		if (!Mesh || !Mesh->HasAttributes() || !Mesh->Attributes()->HasMaterialID())
		{
			continue;
		}
		const int32 MusicSlot = GetMusicMaterialSlot();
		if (MusicReactionMaterialInstance)
		{
			Component->SetMaterial(MusicSlot, MusicReactionMaterialInstance);
		}
		auto* MaterialIDs = Mesh->Attributes()->GetMaterialID();
		bool bChanged = false;
		for (int32 CellIndex = FirstCell; CellIndex < LastCell; ++CellIndex)
		{
			const FMusicPieceTriangles* Piece = MusicPieceTriangles.Find(CellIndex);
			if (!Piece)
			{
				continue;
			}
			for (int32 Offset = 0; Offset < Piece->OriginalMaterials.Num(); ++Offset)
			{
				const int32 TriangleID = Piece->FirstTriangle + Offset;
				const int32 MaterialID = MusicHighlightedCells.Contains(CellIndex)
					? MusicSlot : Piece->OriginalMaterials[Offset];
				if (Mesh->IsTriangle(TriangleID) && MaterialIDs->GetValue(TriangleID) != MaterialID)
				{
					MaterialIDs->SetValue(TriangleID, MaterialID);
					bChanged = true;
				}
			}
		}
		if (bChanged)
		{
			// Material sections changed; positions, topology and collision stay intact.
			Component->NotifyMeshUpdated();
		}
	}
}

void AChunkedDestructiblePyramid::ClearMusicPieceMaterials()
{
	RenderMusicEqualizerCells(TConstArrayView<int32>());
	if (!MusicPieceTriangles.IsEmpty() || MusicReactionMaterialInstance)
	{
		const int32 MusicSlot = GetMusicMaterialSlot();
		for (UDynamicMeshComponent* Component : ChunkComponents)
		{
			if (Component && Component->GetNumMaterials() > MusicSlot)
			{
				Component->SetNumMaterials(MusicSlot);
			}
		}
		MusicPieceTriangles.Empty();
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
	// A cached sine-row mesh may still contain this piece. Force row geometry to refresh.
	for (int32& AssignedRow : PyramidAnimationRowAssignments)
	{
		AssignedRow = INDEX_NONE;
	}
}

bool AChunkedDestructiblePyramid::CreateFixedAnimationResources()
{
	if (PieceGeometry.IsEmpty())
	{
		return false;
	}

	switch (ConeAnimationType)
	{
	case EConeStructureAnimationType::BottomToTopGlowWave:
	case EConeStructureAnimationType::RisingSpiralGlow:
		CreatePyramidAnimationComponent();
		break;
	case EConeStructureAnimationType::BottomToTopSineRows:
		BuildPyramidAnimationRows();
		CreatePyramidSineRowAnimationComponents();
		break;
	case EConeStructureAnimationType::None:
	default:
		break;
	}
	return HasFixedAnimationResources();
}

bool AChunkedDestructiblePyramid::HasFixedAnimationResources() const
{
	return PyramidAnimationComponent || !PyramidAnimationRowComponents.IsEmpty();
}

void AChunkedDestructiblePyramid::DestroyFixedAnimationResources()
{
	if (PyramidAnimationComponent)
	{
		RemoveInstanceComponent(PyramidAnimationComponent);
		PyramidAnimationComponent->DestroyComponent();
		PyramidAnimationComponent = nullptr;
	}
	PyramidAnimationMaterialInstance = nullptr;

	for (UDynamicMeshComponent* RowComponent : PyramidAnimationRowComponents)
	{
		if (RowComponent)
		{
			RemoveInstanceComponent(RowComponent);
			RowComponent->DestroyComponent();
		}
	}
	PyramidAnimationRowComponents.Reset();
	PyramidAnimationRowMaterialInstances.Reset();
	PyramidAnimationCellsByRow.Reset();
	PyramidAnimationRowAssignments.Reset();
}

void AChunkedDestructiblePyramid::RenderFixedAnimation(
	TConstArrayView<float> AnimationAlphas)
{
	if (AnimationAlphas.IsEmpty())
	{
		if (PyramidAnimationComponent)
		{
			PyramidAnimationComponent->SetVisibility(false, true);
		}
		for (int32 ComponentIndex = 0;
			ComponentIndex < PyramidAnimationRowComponents.Num();
			++ComponentIndex)
		{
			if (PyramidAnimationRowComponents[ComponentIndex])
			{
				PyramidAnimationRowComponents[ComponentIndex]->SetVisibility(false, true);
			}
			if (PyramidAnimationRowMaterialInstances.IsValidIndex(ComponentIndex))
			{
				ApplyFixedAnimationMaterialParameters(
					PyramidAnimationRowMaterialInstances[ComponentIndex], 0.0f);
			}
		}
		return;
	}

	switch (ConeAnimationType)
	{
	case EConeStructureAnimationType::BottomToTopGlowWave:
		UpdatePyramidBottomToTopGlowWaves(AnimationAlphas);
		break;
	case EConeStructureAnimationType::BottomToTopSineRows:
		UpdatePyramidBottomToTopSineRows(AnimationAlphas);
		break;
	case EConeStructureAnimationType::RisingSpiralGlow:
		UpdatePyramidRisingSpiralGlow(AnimationAlphas);
		break;
	case EConeStructureAnimationType::None:
	default:
		break;
	}
}

void AChunkedDestructiblePyramid::CreatePyramidAnimationComponent()
{
	if (PyramidAnimationComponent || !GetRootComponent())
	{
		return;
	}

	const FName ComponentName = MakeUniqueObjectName(
		this,
		UDynamicMeshComponent::StaticClass(),
		TEXT("PyramidFixedAnimation"));
	PyramidAnimationComponent = NewObject<UDynamicMeshComponent>(
		this,
		ComponentName,
		RF_Transient);
	AddInstanceComponent(PyramidAnimationComponent);
	PyramidAnimationComponent->SetupAttachment(GetRootComponent());
	PyramidAnimationComponent->SetRelativeTransform(FTransform::Identity);
	PyramidAnimationComponent->SetMobility(EComponentMobility::Movable);
	PyramidAnimationComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PyramidAnimationComponent->SetGenerateOverlapEvents(false);
	PyramidAnimationComponent->SetCastShadow(false);
	PyramidAnimationComponent->SetCanEverAffectNavigation(false);
	PyramidAnimationComponent->SetTranslucentSortPriority(10);
	PyramidAnimationComponent->SetNumMaterials(1);
	PyramidAnimationComponent->RegisterComponent();
	PyramidAnimationComponent->SetDynamicMesh(NewObject<UDynamicMesh>(PyramidAnimationComponent));
	PyramidAnimationMaterialInstance = CreateFixedAnimationMaterialInstance();
	UMaterialInterface* AnimationMaterial = PyramidAnimationMaterialInstance
		? static_cast<UMaterialInterface*>(PyramidAnimationMaterialInstance.Get())
		: UMaterial::GetDefaultMaterial(MD_Surface);
	PyramidAnimationComponent->SetMaterial(0, AnimationMaterial);
	PyramidAnimationComponent->SetVisibility(false, true);
}

void AChunkedDestructiblePyramid::BuildPyramidAnimationRows()
{
	PyramidAnimationCellsByRow.Reset();
	const int32 StepRowCount = Steps.bGenerateSteps ? Steps.StepCount : 0;
	int32 MaximumFaceRow = INDEX_NONE;
	for (const FPyramidPieceGeometry& Piece : PieceGeometry)
	{
		if (Piece.Kind == EPyramidPieceKind::Face)
		{
			MaximumFaceRow = FMath::Max(MaximumFaceRow, Piece.RowIndex);
		}
	}

	const int32 FaceRowCount = MaximumFaceRow + 1;
	const int32 ApexRow = StepRowCount + FaceRowCount;
	PyramidAnimationCellsByRow.SetNum(FMath::Max(ApexRow + 1, 1));
	for (int32 CellIndex = 0; CellIndex < PieceGeometry.Num(); ++CellIndex)
	{
		const FPyramidPieceGeometry& Piece = PieceGeometry[CellIndex];
		int32 RowIndex = INDEX_NONE;
		switch (Piece.Kind)
		{
		case EPyramidPieceKind::Step:
			RowIndex = FMath::Clamp(
				FMath::FloorToInt(Piece.BoxCenter.Z / FMath::Max(Steps.StepHeight, 1.0f)),
				0,
				FMath::Max(StepRowCount - 1, 0));
			break;
		case EPyramidPieceKind::Face:
			RowIndex = StepRowCount + FMath::Max(Piece.RowIndex, 0);
			break;
		case EPyramidPieceKind::Apex:
			RowIndex = ApexRow;
			break;
		case EPyramidPieceKind::InteriorFloor:
		default:
			break;
		}

		if (PyramidAnimationCellsByRow.IsValidIndex(RowIndex))
		{
			PyramidAnimationCellsByRow[RowIndex].Add(CellIndex);
		}
	}
}

void AChunkedDestructiblePyramid::CreatePyramidSineRowAnimationComponents()
{
	if (!PyramidAnimationRowComponents.IsEmpty() || !GetRootComponent() ||
		PyramidAnimationCellsByRow.IsEmpty())
	{
		return;
	}

	const int32 ComponentCount = CalculateFixedSineComponentCount(
		PyramidAnimationCellsByRow.Num());
	PyramidAnimationRowComponents.Reserve(ComponentCount);
	PyramidAnimationRowMaterialInstances.Reserve(ComponentCount);
	PyramidAnimationRowAssignments.Init(INDEX_NONE, ComponentCount);
	for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
	{
		const FName BaseName(*FString::Printf(
			TEXT("PyramidSineRowAnimation_%d"),
			ComponentIndex));
		const FName ComponentName = MakeUniqueObjectName(
			this,
			UDynamicMeshComponent::StaticClass(),
			BaseName);
		UDynamicMeshComponent* RowComponent = NewObject<UDynamicMeshComponent>(
			this,
			ComponentName,
			RF_Transient);
		AddInstanceComponent(RowComponent);
		RowComponent->SetupAttachment(GetRootComponent());
		RowComponent->SetRelativeTransform(FTransform::Identity);
		RowComponent->SetMobility(EComponentMobility::Movable);
		RowComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RowComponent->SetGenerateOverlapEvents(false);
		RowComponent->SetCastShadow(false);
		RowComponent->SetCanEverAffectNavigation(false);
		RowComponent->SetTranslucentSortPriority(10 + ComponentIndex);
		RowComponent->SetNumMaterials(1);
		RowComponent->RegisterComponent();
		RowComponent->SetDynamicMesh(NewObject<UDynamicMesh>(RowComponent));
		UMaterialInstanceDynamic* RowMaterial = CreateFixedAnimationMaterialInstance();
		if (RowMaterial)
		{
			ApplyFixedAnimationMaterialParameters(RowMaterial, 0.0f);
			RowComponent->SetMaterial(0, RowMaterial);
		}
		else
		{
			RowComponent->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
		}
		RowComponent->SetVisibility(false, true);
		PyramidAnimationRowComponents.Add(RowComponent);
		PyramidAnimationRowMaterialInstances.Add(RowMaterial);
	}
}

void AChunkedDestructiblePyramid::AppendPyramidAnimationPieceSurface(
	FDynamicMesh3& Mesh,
	int32 CellIndex,
	float SurfaceOffset) const
{
	if (!PieceGeometry.IsValidIndex(CellIndex) || !Cells.IsValidIndex(CellIndex) ||
		!Cells[CellIndex].bExists || PendingCellIndices.Contains(CellIndex))
	{
		return;
	}

	const FPyramidPieceGeometry& Piece = PieceGeometry[CellIndex];
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	auto MakeUV = [SafeUVScale](const FVector& Point)
	{
		return FVector2f(
			static_cast<float>((Point.X + Point.Y) / SafeUVScale),
			static_cast<float>(Point.Z / SafeUVScale));
	};

	if (Piece.Kind == EPyramidPieceKind::Face && Piece.OuterPolygon.Num() >= 3)
	{
		const FVector Normal = GetPyramidFaceNormal(Piece.FaceIndex);
		const FVector Offset = Normal * SurfaceOffset;
		for (int32 PointIndex = 1; PointIndex + 1 < Piece.OuterPolygon.Num(); ++PointIndex)
		{
			const FVector A = Piece.OuterPolygon[0] + Offset;
			const FVector B = Piece.OuterPolygon[PointIndex] + Offset;
			const FVector C = Piece.OuterPolygon[PointIndex + 1] + Offset;
			AppendMeshTriangle(
				Mesh,
				FVector3d(A),
				FVector3d(B),
				FVector3d(C),
				FVector3f(Normal),
				0,
				MakeUV(A),
				MakeUV(B),
				MakeUV(C));
		}
		return;
	}

	if (Piece.Kind == EPyramidPieceKind::Apex && Piece.OuterPolygon.Num() == 5)
	{
		for (int32 FaceIndex = 0; FaceIndex < 4; ++FaceIndex)
		{
			const FVector Normal = GetPyramidFaceNormal(FaceIndex);
			const FVector Offset = Normal * SurfaceOffset;
			const FVector A = Piece.OuterPolygon[FaceIndex] + Offset;
			const FVector B = Piece.OuterPolygon[(FaceIndex + 1) % 4] + Offset;
			const FVector C = Piece.OuterPolygon[4] + Offset;
			AppendMeshTriangle(
				Mesh,
				FVector3d(A),
				FVector3d(B),
				FVector3d(C),
				FVector3f(Normal),
				0,
				MakeUV(A),
				MakeUV(B),
				MakeUV(C));
		}
		return;
	}

	if (Piece.Kind == EPyramidPieceKind::Step)
	{
		const FVector Extent = Piece.BoxSize * 0.5f;
		const double MinX = Piece.BoxCenter.X - Extent.X;
		const double MaxX = Piece.BoxCenter.X + Extent.X;
		const double MinY = Piece.BoxCenter.Y - Extent.Y;
		const double MaxY = Piece.BoxCenter.Y + Extent.Y;
		const double TopZ = Piece.BoxCenter.Z + Extent.Z + SurfaceOffset;
		AppendMeshQuad(
			Mesh,
			FVector3d(MinX, MinY, TopZ),
			FVector3d(MaxX, MinY, TopZ),
			FVector3d(MaxX, MaxY, TopZ),
			FVector3d(MinX, MaxY, TopZ),
			FVector3f::UnitZ(),
			0,
			Piece.BoxSize.X,
			Piece.BoxSize.Y,
			UVWorldUnits);
	}
}

void AChunkedDestructiblePyramid::UpdatePyramidBottomToTopGlowWaves(
	TConstArrayView<float> AnimationAlphas)
{
	if (!PyramidAnimationComponent || !PyramidAnimationComponent->GetDynamicMesh())
	{
		return;
	}

	float MinimumZ = TNumericLimits<float>::Max();
	float MaximumZ = TNumericLimits<float>::Lowest();
	for (const FPyramidPieceGeometry& Piece : PieceGeometry)
	{
		if (Piece.Kind != EPyramidPieceKind::InteriorFloor)
		{
			MinimumZ = FMath::Min(MinimumZ, Piece.Bounds.Min.Z);
			MaximumZ = FMath::Max(MaximumZ, Piece.Bounds.Max.Z);
		}
	}
	if (MinimumZ > MaximumZ)
	{
		return;
	}

	const float BandHeight = FMath::Max(
		GlowWaveBandHeightBlocks * Shape.PieceSize,
		1.0f);
	TBitArray<> SelectedPieces(false, PieceGeometry.Num());
	for (const float AnimationAlpha : AnimationAlphas)
	{
		const float BandTop = FMath::Lerp(
			MinimumZ,
			MaximumZ + BandHeight,
			FMath::Clamp(AnimationAlpha, 0.0f, 1.0f));
		const float BandBottom = BandTop - BandHeight;
		for (int32 CellIndex = 0; CellIndex < PieceGeometry.Num(); ++CellIndex)
		{
			const FPyramidPieceGeometry& Piece = PieceGeometry[CellIndex];
			if (Piece.Kind != EPyramidPieceKind::InteriorFloor &&
				Piece.Bounds.Max.Z >= BandBottom && Piece.Bounds.Min.Z <= BandTop)
			{
				SelectedPieces[CellIndex] = true;
			}
		}
	}

	FDynamicMesh3 AnimationMesh;
	AnimationMesh.EnableVertexNormals(FVector3f::UnitZ());
	AnimationMesh.EnableAttributes();
	AnimationMesh.Attributes()->SetNumUVLayers(1);
	AnimationMesh.Attributes()->EnableMaterialID();
	const float SurfaceOffset = FMath::Clamp(Shape.ShellThickness * 0.015f, 0.3f, 2.0f);
	for (TConstSetBitIterator It(SelectedPieces); It; ++It)
	{
		AppendPyramidAnimationPieceSurface(AnimationMesh, It.GetIndex(), SurfaceOffset);
	}

	const bool bVisible = AnimationMesh.TriangleCount() > 0;
	PyramidAnimationComponent->GetDynamicMesh()->SetMesh(MoveTemp(AnimationMesh));
	PyramidAnimationComponent->NotifyMeshUpdated();
	PyramidAnimationComponent->SetVisibility(bVisible, true);
	PyramidAnimationComponent->MarkRenderStateDirty();
}

void AChunkedDestructiblePyramid::UpdatePyramidBottomToTopSineRows(
	TConstArrayView<float> AnimationAlphas)
{
	const int32 RowCount = PyramidAnimationCellsByRow.Num();
	const int32 ComponentCount = PyramidAnimationRowComponents.Num();
	if (RowCount <= 0 || ComponentCount <= 0)
	{
		return;
	}

	TArray<float> RowIntensities;
	RowIntensities.Init(0.0f, RowCount);
	const int32 RowsPerWave = FMath::Clamp(
		SineRowsSimultaneousRowCount,
		2,
		FMath::Max(RowCount, 2));
	const float TravelSpan = FMath::Max(
		static_cast<float>(RowCount - 1 + RowsPerWave),
		1.0f);
	for (const float AnimationAlpha : AnimationAlphas)
	{
		const float WaveHead = FMath::Clamp(AnimationAlpha, 0.0f, 1.0f) * TravelSpan;
		const int32 LastRow = FMath::Min(FMath::FloorToInt(WaveHead), RowCount - 1);
		const int32 FirstRow = FMath::Max(LastRow - RowsPerWave + 1, 0);
		for (int32 RowIndex = FirstRow; RowIndex <= LastRow; ++RowIndex)
		{
			const float RowAge = WaveHead - RowIndex;
			if (RowAge < 0.0f || RowAge > RowsPerWave)
			{
				continue;
			}
			const float Intensity = FMath::Sin(
				UE_PI * FMath::Clamp(RowAge / RowsPerWave, 0.0f, 1.0f));
			RowIntensities[RowIndex] = 1.0f -
				(1.0f - RowIntensities[RowIndex]) * (1.0f - Intensity);
		}
	}

	TArray<bool> ActiveRows;
	ActiveRows.Init(false, RowCount);
	for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
	{
		ActiveRows[RowIndex] = RowIntensities[RowIndex] > KINDA_SMALL_NUMBER;
	}
	TArray<bool> UsedComponents;
	UsedComponents.Init(false, ComponentCount);
	for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
	{
		if (PyramidAnimationRowComponents[ComponentIndex])
		{
			PyramidAnimationRowComponents[ComponentIndex]->SetVisibility(false, true);
		}
	}

	const float SurfaceOffset = FMath::Clamp(Shape.ShellThickness * 0.015f, 0.3f, 2.0f);
	for (int32 RowIndex = 0; RowIndex < RowCount; ++RowIndex)
	{
		if (!ActiveRows[RowIndex])
		{
			continue;
		}

		int32 ComponentIndex = PyramidAnimationRowAssignments.Find(RowIndex);
		if (!PyramidAnimationRowComponents.IsValidIndex(ComponentIndex) ||
			!PyramidAnimationRowComponents[ComponentIndex] || UsedComponents[ComponentIndex])
		{
			ComponentIndex = INDEX_NONE;
		}
		if (ComponentIndex == INDEX_NONE)
		{
			for (int32 CandidateIndex = 0; CandidateIndex < ComponentCount; ++CandidateIndex)
			{
				const int32 AssignedRow = PyramidAnimationRowAssignments[CandidateIndex];
				if (!UsedComponents[CandidateIndex] &&
					(AssignedRow == INDEX_NONE || !ActiveRows.IsValidIndex(AssignedRow) ||
						!ActiveRows[AssignedRow]))
				{
					ComponentIndex = CandidateIndex;
					break;
				}
			}
		}
		if (!PyramidAnimationRowComponents.IsValidIndex(ComponentIndex) ||
			!PyramidAnimationRowComponents[ComponentIndex] ||
			!PyramidAnimationRowComponents[ComponentIndex]->GetDynamicMesh())
		{
			continue;
		}

		UsedComponents[ComponentIndex] = true;
		UDynamicMeshComponent* RowComponent = PyramidAnimationRowComponents[ComponentIndex];
		if (PyramidAnimationRowAssignments[ComponentIndex] != RowIndex)
		{
			FDynamicMesh3 RowMesh;
			RowMesh.EnableVertexNormals(FVector3f::UnitZ());
			RowMesh.EnableAttributes();
			RowMesh.Attributes()->SetNumUVLayers(1);
			RowMesh.Attributes()->EnableMaterialID();
			for (const int32 CellIndex : PyramidAnimationCellsByRow[RowIndex])
			{
				AppendPyramidAnimationPieceSurface(RowMesh, CellIndex, SurfaceOffset);
			}
			RowComponent->GetDynamicMesh()->SetMesh(MoveTemp(RowMesh));
			RowComponent->NotifyMeshUpdated();
			RowComponent->MarkRenderStateDirty();
			PyramidAnimationRowAssignments[ComponentIndex] = RowIndex;
		}
		if (PyramidAnimationRowMaterialInstances.IsValidIndex(ComponentIndex))
		{
			ApplyFixedAnimationMaterialParameters(
				PyramidAnimationRowMaterialInstances[ComponentIndex],
				RowIntensities[RowIndex]);
		}
		RowComponent->SetVisibility(true, true);
	}
}

void AChunkedDestructiblePyramid::UpdatePyramidRisingSpiralGlow(
	TConstArrayView<float> AnimationAlphas)
{
	if (!PyramidAnimationComponent || !PyramidAnimationComponent->GetDynamicMesh() ||
		AnimationAlphas.IsEmpty())
	{
		return;
	}

	const int32 StrandCount = FMath::Clamp(SpiralGlowStrandCount, 1, 8);
	const float Direction = bReverseSpiralGlowDirection ? -1.0f : 1.0f;
	const float DynamicPhase = -Direction *
		FMath::Clamp(AnimationAlphas[0], 0.0f, 1.0f) / StrandCount;
	const float StartPhase = SpiralGlowStartAngleDegrees / 360.0f;
	const float HalfStripeWidth = FMath::Min(
		SpiralGlowStripeWidthDegrees / 720.0f,
		0.49f / StrandCount);
	const FVector HorizontalAxes[] = {
		FVector(1.0f, 0.0f, 0.0f),
		FVector(0.0f, 1.0f, 0.0f),
		FVector(-1.0f, 0.0f, 0.0f),
		FVector(0.0f, -1.0f, 0.0f)
	};

	FDynamicMesh3 AnimationMesh;
	AnimationMesh.EnableVertexNormals(FVector3f::UnitZ());
	AnimationMesh.EnableAttributes();
	AnimationMesh.Attributes()->SetNumUVLayers(1);
	AnimationMesh.Attributes()->EnableMaterialID();
	for (int32 CellIndex = 0; CellIndex < PieceGeometry.Num(); ++CellIndex)
	{
		const FPyramidPieceGeometry& Piece = PieceGeometry[CellIndex];
		if (Piece.Kind != EPyramidPieceKind::Face &&
			Piece.Kind != EPyramidPieceKind::Apex)
		{
			continue;
		}

		const float LocalBodyZ = FMath::Clamp(
			Piece.Center.Z - CachedBodyBaseZ - Piece.TopSliceVerticalOffset,
			0.0f,
			Shape.Height);
		const float HeightAlpha = LocalBodyZ / FMath::Max(Shape.Height, 1.0f);
		bool bPieceInsideStripe = false;
		const int32 FirstFace = Piece.Kind == EPyramidPieceKind::Face ? Piece.FaceIndex : 0;
		const int32 LastFace = Piece.Kind == EPyramidPieceKind::Face ? Piece.FaceIndex : 3;
		for (int32 FaceIndex = FirstFace; FaceIndex <= LastFace; ++FaceIndex)
		{
			const float HalfWidth = Shape.BaseWidth * 0.5f * (1.0f - HeightAlpha);
			const float FaceAlpha = HalfWidth > KINDA_SMALL_NUMBER
				? FMath::Clamp(
					(FVector::DotProduct(Piece.Center, HorizontalAxes[FaceIndex]) + HalfWidth) /
						(2.0f * HalfWidth),
					0.0f,
					1.0f)
				: 0.5f;
			const float PerimeterPhase = (FaceIndex + FaceAlpha) * 0.25f;
			const float StripeCenter = StartPhase + DynamicPhase +
				Direction * SpiralGlowTurns * HeightAlpha;
			const float StrandSpace = (PerimeterPhase - StripeCenter) * StrandCount;
			const float DistanceToStrand =
				FMath::Abs(StrandSpace - FMath::RoundToFloat(StrandSpace)) / StrandCount;
			if (DistanceToStrand <= HalfStripeWidth)
			{
				bPieceInsideStripe = true;
				break;
			}
		}
		if (bPieceInsideStripe)
		{
			AppendPyramidAnimationPieceSurface(
				AnimationMesh,
				CellIndex,
				FMath::Clamp(SpiralGlowSurfaceOffset, 0.5f, 20.0f));
		}
	}

	const bool bVisible = AnimationMesh.TriangleCount() > 0;
	PyramidAnimationComponent->GetDynamicMesh()->SetMesh(MoveTemp(AnimationMesh));
	PyramidAnimationComponent->NotifyMeshUpdated();
	PyramidAnimationComponent->SetVisibility(bVisible, true);
	PyramidAnimationComponent->MarkRenderStateDirty();
}
