// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleRoundedBlob.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Math/RandomStream.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"

using namespace UE::Geometry;

namespace
{
	constexpr float MinimumBlobDimension = 1.0f;
	constexpr float BlobSpacingTolerance = 0.01f;
	constexpr double BlobPolygonTolerance = 0.01;
	constexpr int32 MinimumBlobLobes = 3;
	constexpr int32 MaximumBlobLobes = 16;
	constexpr int32 MinimumSamplesPerLobe = 4;
	constexpr int32 MaximumSamplesPerLobe = 32;

	double SignedPolygonArea(const TArray<FVector2D>& Polygon)
	{
		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2D& A = Polygon[Index];
			const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
			TwiceArea += A.X * B.Y - B.X * A.Y;
		}
		return TwiceArea * 0.5;
	}

	bool IsPointOnSegment(
		const FVector2D& Point,
		const FVector2D& Start,
		const FVector2D& End,
		double Tolerance)
	{
		const FVector2D Segment = End - Start;
		const double SegmentLengthSquared = Segment.SizeSquared();
		if (SegmentLengthSquared <= FMath::Square(Tolerance))
		{
			return Point.Equals(Start, Tolerance);
		}

		const double Alpha = FMath::Clamp(
			FVector2D::DotProduct(Point - Start, Segment) / SegmentLengthSquared,
			0.0,
			1.0);
		return (Point - (Start + Segment * Alpha)).SizeSquared() <= FMath::Square(Tolerance);
	}

	double GetBoundaryDistance(const FVector2D& Point, int32 BoundaryIndex, double BoundaryValue)
	{
		switch (BoundaryIndex)
		{
		case 0: return Point.X - BoundaryValue; // X >= MinX
		case 1: return BoundaryValue - Point.X; // X <= MaxX
		case 2: return Point.Y - BoundaryValue; // Y >= MinY
		default: return BoundaryValue - Point.Y; // Y <= MaxY
		}
	}

	void ClipPolygonAgainstBoundary(
		TArray<FVector2D>& Polygon,
		int32 BoundaryIndex,
		double BoundaryValue)
	{
		if (Polygon.IsEmpty())
		{
			return;
		}

		const TArray<FVector2D> Input = Polygon;
		Polygon.Reset();
		FVector2D Previous = Input.Last();
		double PreviousDistance = GetBoundaryDistance(Previous, BoundaryIndex, BoundaryValue);
		bool bPreviousInside = PreviousDistance >= -BlobPolygonTolerance;

		for (const FVector2D& Current : Input)
		{
			const double CurrentDistance = GetBoundaryDistance(Current, BoundaryIndex, BoundaryValue);
			const bool bCurrentInside = CurrentDistance >= -BlobPolygonTolerance;
			if (bCurrentInside != bPreviousInside)
			{
				const double Denominator = PreviousDistance - CurrentDistance;
				if (!FMath::IsNearlyZero(Denominator))
				{
					const double Alpha = FMath::Clamp(PreviousDistance / Denominator, 0.0, 1.0);
					Polygon.Add(Previous + (Current - Previous) * Alpha);
				}
			}
			if (bCurrentInside)
			{
				Polygon.Add(Current);
			}
			Previous = Current;
			PreviousDistance = CurrentDistance;
			bPreviousInside = bCurrentInside;
		}
	}

	bool ClipSegmentToRectangle(
		const FVector2D& InputStart,
		const FVector2D& InputEnd,
		double MinX,
		double MinY,
		double MaxX,
		double MaxY,
		FVector2D& OutStart,
		FVector2D& OutEnd)
	{
		OutStart = InputStart;
		OutEnd = InputEnd;
		const double BoundaryValues[4] = { MinX, MaxX, MinY, MaxY };
		for (int32 BoundaryIndex = 0; BoundaryIndex < 4; ++BoundaryIndex)
		{
			const double StartDistance = GetBoundaryDistance(
				OutStart, BoundaryIndex, BoundaryValues[BoundaryIndex]);
			const double EndDistance = GetBoundaryDistance(
				OutEnd, BoundaryIndex, BoundaryValues[BoundaryIndex]);
			if (StartDistance < -BlobPolygonTolerance && EndDistance < -BlobPolygonTolerance)
			{
				return false;
			}
			if ((StartDistance < 0.0) != (EndDistance < 0.0))
			{
				const double Denominator = StartDistance - EndDistance;
				if (!FMath::IsNearlyZero(Denominator))
				{
					const FVector2D Intersection = OutStart +
						(OutEnd - OutStart) * FMath::Clamp(StartDistance / Denominator, 0.0, 1.0);
					if (StartDistance < 0.0)
					{
						OutStart = Intersection;
					}
					else
					{
						OutEnd = Intersection;
					}
				}
			}
		}
		return true;
	}

	void RemoveDuplicatePolygonPoints(TArray<FVector2D>& Polygon)
	{
		for (int32 Index = Polygon.Num() - 1; Index >= 0 && Polygon.Num() > 1; --Index)
		{
			const int32 PreviousIndex = (Index - 1 + Polygon.Num()) % Polygon.Num();
			if (Polygon[Index].Equals(Polygon[PreviousIndex], BlobPolygonTolerance))
			{
				Polygon.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
	}

}

AChunkedDestructibleRoundedBlob::AChunkedDestructibleRoundedBlob()
{
	AreaSize = BlobSize;
	FloorPattern = EChunkedFloorPattern::Checkerboard;
	// A shot rebuilds whole chunks synchronously. Ten keeps the affected working set small while
	// preserving the exact same logical cells, outline, materials and collision behavior.
	ChunkSideLength = 10;
}

#if WITH_EDITOR
bool AChunkedDestructibleRoundedBlob::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty && InProperty->GetFName() == FName(TEXT("AreaSize")))
	{
		return false;
	}
	return Super::CanEditChange(InProperty);
}
#endif

void AChunkedDestructibleRoundedBlob::ConfigureRoundedBlob(
	FVector2D NewBlobSize,
	int32 NewLobeCount,
	float NewLobeStrength,
	float NewEdgeRoundness,
	int32 NewShapeSeed,
	int32 NewOutlineSamplesPerLobe,
	FVector NewCellDimensions,
	FVector2D NewCellSpacing,
	bool bPreserveCellState)
{
	BlobSize = FVector2D(
		FMath::Max(FMath::Abs(NewBlobSize.X), MinimumBlobDimension),
		FMath::Max(FMath::Abs(NewBlobSize.Y), MinimumBlobDimension));
	LobeCount = FMath::Clamp(NewLobeCount, MinimumBlobLobes, MaximumBlobLobes);
	LobeStrength = FMath::Clamp(NewLobeStrength, 0.0f, 0.45f);
	EdgeRoundness = FMath::Clamp(NewEdgeRoundness, 0.0f, 1.0f);
	ShapeSeed = NewShapeSeed;
	OutlineSamplesPerLobe = FMath::Clamp(
		NewOutlineSamplesPerLobe, MinimumSamplesPerLobe, MaximumSamplesPerLobe);
	CellDimensions = FVector(
		FMath::Max(FMath::Abs(NewCellDimensions.X), MinimumBlobDimension),
		FMath::Max(FMath::Abs(NewCellDimensions.Y), MinimumBlobDimension),
		FMath::Max(FMath::Abs(NewCellDimensions.Z), MinimumBlobDimension));
	CellSpacing = FVector2D(
		FMath::Max(NewCellSpacing.X, 0.0),
		FMath::Max(NewCellSpacing.Y, 0.0));
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleRoundedBlob::SetRoundedBlobShape(
	FVector2D NewBlobSize,
	int32 NewLobeCount,
	float NewLobeStrength,
	float NewEdgeRoundness,
	int32 NewShapeSeed,
	int32 NewOutlineSamplesPerLobe,
	bool bPreserveCellState)
{
	BlobSize = FVector2D(
		FMath::Max(FMath::Abs(NewBlobSize.X), MinimumBlobDimension),
		FMath::Max(FMath::Abs(NewBlobSize.Y), MinimumBlobDimension));
	LobeCount = FMath::Clamp(NewLobeCount, MinimumBlobLobes, MaximumBlobLobes);
	LobeStrength = FMath::Clamp(NewLobeStrength, 0.0f, 0.45f);
	EdgeRoundness = FMath::Clamp(NewEdgeRoundness, 0.0f, 1.0f);
	ShapeSeed = NewShapeSeed;
	OutlineSamplesPerLobe = FMath::Clamp(
		NewOutlineSamplesPerLobe, MinimumSamplesPerLobe, MaximumSamplesPerLobe);
	GenerateFloor(bPreserveCellState);
}

bool AChunkedDestructibleRoundedBlob::CalculateLayout()
{
	BlobSize = FVector2D(
		FMath::Max(FMath::Abs(BlobSize.X), MinimumBlobDimension),
		FMath::Max(FMath::Abs(BlobSize.Y), MinimumBlobDimension));
	LobeCount = FMath::Clamp(LobeCount, MinimumBlobLobes, MaximumBlobLobes);
	LobeStrength = FMath::Clamp(LobeStrength, 0.0f, 0.45f);
	EdgeRoundness = FMath::Clamp(EdgeRoundness, 0.0f, 1.0f);
	OutlineSamplesPerLobe = FMath::Clamp(
		OutlineSamplesPerLobe, MinimumSamplesPerLobe, MaximumSamplesPerLobe);
	AreaSize = BlobSize;
	RebuildOutlineCache();
	if (!Super::CalculateLayout())
	{
		CachedCellGeometry.Reset();
		CachedSpiralTrianglesByCell.Reset();
		CachedSpiralChunks.Reset();
		bCachedSpiralSettingsValid = false;
		return false;
	}
	RebuildCellGeometryCache();
	return true;
}

void AChunkedDestructibleRoundedBlob::RebuildOutlineCache()
{
	CachedOutline.Reset();
	CachedTriangles.Reset();
	CachedFanCenter = FVector2D::ZeroVector;

	const int32 SafeLobeCount = FMath::Clamp(LobeCount, MinimumBlobLobes, MaximumBlobLobes);
	const int32 AnchorCount = SafeLobeCount * 2;
	const int32 SampleCount = SafeLobeCount * FMath::Clamp(
		OutlineSamplesPerLobe, MinimumSamplesPerLobe, MaximumSamplesPerLobe);
	const double SafeStrength = FMath::Clamp(LobeStrength, 0.0f, 0.45f);
	const double SafeRoundness = FMath::Clamp(EdgeRoundness, 0.0f, 1.0f);

	FRandomStream Random(ShapeSeed);
	TArray<double> AnchorRadii;
	AnchorRadii.Reserve(AnchorCount);
	for (int32 AnchorIndex = 0; AnchorIndex < AnchorCount; ++AnchorIndex)
	{
		const double Variation = Random.FRand();
		const bool bOuterAnchor = (AnchorIndex % 2) == 0;
		const double Radius = bOuterAnchor
			? 1.0 + SafeStrength * (0.55 + 0.45 * Variation)
			: 1.0 - SafeStrength * (0.25 + 0.40 * Variation);
		AnchorRadii.Add(Radius);
	}

	auto GetWrappedRadius = [&AnchorRadii, AnchorCount](int32 Index)
	{
		return AnchorRadii[(Index % AnchorCount + AnchorCount) % AnchorCount];
	};

	CachedOutline.Reserve(SampleCount);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const double NormalizedPosition = static_cast<double>(SampleIndex) / SampleCount;
		const double AnchorPosition = NormalizedPosition * AnchorCount;
		const int32 AnchorIndex = FMath::FloorToInt(AnchorPosition);
		const double Alpha = AnchorPosition - AnchorIndex;
		const double Radius0 = GetWrappedRadius(AnchorIndex - 1);
		const double Radius1 = GetWrappedRadius(AnchorIndex);
		const double Radius2 = GetWrappedRadius(AnchorIndex + 1);
		const double Radius3 = GetWrappedRadius(AnchorIndex + 2);
		const double LinearRadius = FMath::Lerp(Radius1, Radius2, Alpha);
		const double AlphaSquared = Alpha * Alpha;
		const double AlphaCubed = AlphaSquared * Alpha;
		const double SplineRadius = 0.5 * (
			2.0 * Radius1 +
			(-Radius0 + Radius2) * Alpha +
			(2.0 * Radius0 - 5.0 * Radius1 + 4.0 * Radius2 - Radius3) * AlphaSquared +
			(-Radius0 + 3.0 * Radius1 - 3.0 * Radius2 + Radius3) * AlphaCubed);
		const double Radius = FMath::Clamp(
			FMath::Lerp(LinearRadius, SplineRadius, SafeRoundness), 0.45, 1.55);
		const double Angle = NormalizedPosition * UE_TWO_PI;
		CachedOutline.Add(FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius));
	}

	FBox2D RawBounds(EForceInit::ForceInit);
	for (const FVector2D& Point : CachedOutline)
	{
		RawBounds += Point;
	}
	const FVector2D RawSize = RawBounds.GetSize();
	const FVector2D RawCenter = RawBounds.GetCenter();
	const FVector2D Scale(
		BlobSize.X / FMath::Max(RawSize.X, static_cast<double>(KINDA_SMALL_NUMBER)),
		BlobSize.Y / FMath::Max(RawSize.Y, static_cast<double>(KINDA_SMALL_NUMBER)));
	for (FVector2D& Point : CachedOutline)
	{
		Point = (Point - RawCenter) * Scale;
	}
	CachedFanCenter = -RawCenter * Scale;

	if (SignedPolygonArea(CachedOutline) < 0.0)
	{
		Algo::Reverse(CachedOutline);
	}

	CachedTriangles.Reserve(CachedOutline.Num());
	for (int32 Index = 0; Index < CachedOutline.Num(); ++Index)
	{
		FBlobTriangle& Triangle = CachedTriangles.Emplace_GetRef();
		Triangle.A = CachedFanCenter;
		Triangle.B = CachedOutline[Index];
		Triangle.C = CachedOutline[(Index + 1) % CachedOutline.Num()];
		Triangle.Bounds = FBox2D(EForceInit::ForceInit);
		Triangle.Bounds += Triangle.A;
		Triangle.Bounds += Triangle.B;
		Triangle.Bounds += Triangle.C;
	}
}

void AChunkedDestructibleRoundedBlob::RebuildCellGeometryCache()
{
	const int32 TotalCellCount = CellCount.X * CellCount.Y;
	CachedCellGeometry.Reset();
	CachedCellGeometry.SetNum(TotalCellCount);

	for (int32 Y = 0; Y < CellCount.Y; ++Y)
	{
		for (int32 X = 0; X < CellCount.X; ++X)
		{
			const FIntPoint Cell(X, Y);
			FBlobCellGeometryCache& CellCache = CachedCellGeometry[ToCellIndex(Cell)];
			CellCache.bBelongsToBlob = ComputeClippedCellFragments(Cell, CellCache.Fragments);
			CellCache.bFullyInside = CellCache.bBelongsToBlob && ComputeCellFullyInsideBlob(Cell);
			if (!CellCache.bBelongsToBlob || CellCache.bFullyInside)
			{
				continue;
			}

			double CellMinX;
			double CellMinY;
			double CellMaxX;
			double CellMaxY;
			GetCellBounds(Cell, CellMinX, CellMinY, CellMaxX, CellMaxY);
			for (int32 EdgeIndex = 0; EdgeIndex < CachedOutline.Num(); ++EdgeIndex)
			{
				FVector2D ClippedStart;
				FVector2D ClippedEnd;
				if (ClipSegmentToRectangle(
					CachedOutline[EdgeIndex],
					CachedOutline[(EdgeIndex + 1) % CachedOutline.Num()],
					CellMinX, CellMinY, CellMaxX, CellMaxY,
					ClippedStart, ClippedEnd))
				{
					FBlobOutlineSegment& Segment = CellCache.OutlineSegments.Emplace_GetRef();
					Segment.A = ClippedStart;
					Segment.B = ClippedEnd;
				}
			}
		}
	}

	CachedSpiralTrianglesByCell.Reset();
	CachedSpiralTrianglesByCell.SetNum(TotalCellCount);
	CachedSpiralChunks.Init(false, ChunkCount.X * ChunkCount.Y);
	bCachedSpiralSettingsValid = false;
}

void AChunkedDestructibleRoundedBlob::GetRoundedBlobOutline(TArray<FVector2D>& OutOutline) const
{
	OutOutline = CachedOutline;
}

FChunkedDestructibleRoundedBlobMetrics AChunkedDestructibleRoundedBlob::GetRoundedBlobMetrics() const
{
	FChunkedDestructibleRoundedBlobMetrics Metrics;
	Metrics.FootprintSize = BlobSize;
	Metrics.OutlinePointCount = CachedOutline.Num();
	Metrics.ApproximateArea = static_cast<float>(FMath::Abs(SignedPolygonArea(CachedOutline)));
	for (int32 Index = 0; Index < CachedOutline.Num(); ++Index)
	{
		Metrics.ApproximatePerimeter += static_cast<float>(
			(CachedOutline[(Index + 1) % CachedOutline.Num()] - CachedOutline[Index]).Size());
	}
	return Metrics;
}

bool AChunkedDestructibleRoundedBlob::IsPointInsideBlob(
	const FVector2D& Point,
	double Tolerance) const
{
	if (CachedOutline.Num() < 3)
	{
		return false;
	}

	bool bInside = false;
	for (int32 Index = 0; Index < CachedOutline.Num(); ++Index)
	{
		const FVector2D& A = CachedOutline[Index];
		const FVector2D& B = CachedOutline[(Index + 1) % CachedOutline.Num()];
		if (IsPointOnSegment(Point, A, B, Tolerance))
		{
			return true;
		}
		if ((A.Y > Point.Y) != (B.Y > Point.Y))
		{
			const double IntersectionX = A.X +
				(Point.Y - A.Y) * (B.X - A.X) / (B.Y - A.Y);
			if (Point.X < IntersectionX)
			{
				bInside = !bInside;
			}
		}
	}
	return bInside;
}

void AChunkedDestructibleRoundedBlob::GetCellBounds(
	FIntPoint Cell,
	double& OutMinX,
	double& OutMinY,
	double& OutMaxX,
	double& OutMaxY) const
{
	const FVector Center = GetCellLocalCenter(Cell);
	OutMinX = Center.X - EffectiveCellDimensions.X * 0.5;
	OutMaxX = Center.X + EffectiveCellDimensions.X * 0.5;
	OutMinY = Center.Y - EffectiveCellDimensions.Y * 0.5;
	OutMaxY = Center.Y + EffectiveCellDimensions.Y * 0.5;
}

bool AChunkedDestructibleRoundedBlob::DoesOutlineIntersectCell(FIntPoint Cell) const
{
	double MinX;
	double MinY;
	double MaxX;
	double MaxY;
	GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
	for (int32 Index = 0; Index < CachedOutline.Num(); ++Index)
	{
		FVector2D ClippedStart;
		FVector2D ClippedEnd;
		if (ClipSegmentToRectangle(
			CachedOutline[Index],
			CachedOutline[(Index + 1) % CachedOutline.Num()],
			MinX, MinY, MaxX, MaxY,
			ClippedStart, ClippedEnd))
		{
			return true;
		}
	}
	return false;
}

bool AChunkedDestructibleRoundedBlob::ComputeCellFullyInsideBlob(FIntPoint Cell) const
{
	double MinX;
	double MinY;
	double MaxX;
	double MaxY;
	GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
	return IsPointInsideBlob(FVector2D(MinX, MinY)) &&
		IsPointInsideBlob(FVector2D(MaxX, MinY)) &&
		IsPointInsideBlob(FVector2D(MaxX, MaxY)) &&
		IsPointInsideBlob(FVector2D(MinX, MaxY)) &&
		!DoesOutlineIntersectCell(Cell);
}

const AChunkedDestructibleRoundedBlob::FBlobCellGeometryCache*
AChunkedDestructibleRoundedBlob::FindCellGeometryCache(FIntPoint Cell) const
{
	if (!IsValidCell(Cell))
	{
		return nullptr;
	}
	const int32 CellIndex = ToCellIndex(Cell);
	return CachedCellGeometry.IsValidIndex(CellIndex)
		? &CachedCellGeometry[CellIndex]
		: nullptr;
}

bool AChunkedDestructibleRoundedBlob::IsCellFullyInsideBlob(FIntPoint Cell) const
{
	if (const FBlobCellGeometryCache* CellCache = FindCellGeometryCache(Cell))
	{
		return CellCache->bFullyInside;
	}
	return ComputeCellFullyInsideBlob(Cell);
}

bool AChunkedDestructibleRoundedBlob::ComputeClippedCellFragments(
	FIntPoint Cell,
	TArray<TArray<FVector2D>>& OutFragments) const
{
	OutFragments.Reset();
	if (!IsValidCell(Cell))
	{
		return false;
	}

	double MinX;
	double MinY;
	double MaxX;
	double MaxY;
	GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
	const double BoundaryValues[4] = { MinX, MaxX, MinY, MaxY };
	const double MinimumArea = FMath::Max(
		EffectiveCellDimensions.X * EffectiveCellDimensions.Y * 0.00001,
		0.001);

	for (const FBlobTriangle& Triangle : CachedTriangles)
	{
		if (Triangle.Bounds.Max.X < MinX - BlobPolygonTolerance ||
			Triangle.Bounds.Min.X > MaxX + BlobPolygonTolerance ||
			Triangle.Bounds.Max.Y < MinY - BlobPolygonTolerance ||
			Triangle.Bounds.Min.Y > MaxY + BlobPolygonTolerance)
		{
			continue;
		}

		TArray<FVector2D> Fragment = { Triangle.A, Triangle.B, Triangle.C };
		for (int32 BoundaryIndex = 0; BoundaryIndex < 4 && !Fragment.IsEmpty(); ++BoundaryIndex)
		{
			ClipPolygonAgainstBoundary(Fragment, BoundaryIndex, BoundaryValues[BoundaryIndex]);
		}
		RemoveDuplicatePolygonPoints(Fragment);
		if (Fragment.Num() >= 3 && FMath::Abs(SignedPolygonArea(Fragment)) >= MinimumArea)
		{
			OutFragments.Add(MoveTemp(Fragment));
		}
	}
	return !OutFragments.IsEmpty();
}

bool AChunkedDestructibleRoundedBlob::GetClippedCellFragments(
	FIntPoint Cell,
	TArray<TArray<FVector2D>>& OutFragments) const
{
	if (const FBlobCellGeometryCache* CellCache = FindCellGeometryCache(Cell))
	{
		OutFragments = CellCache->Fragments;
		return CellCache->bBelongsToBlob;
	}
	return ComputeClippedCellFragments(Cell, OutFragments);
}

bool AChunkedDestructibleRoundedBlob::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	if (const FBlobCellGeometryCache* CellCache = FindCellGeometryCache(Cell))
	{
		return CellCache->bBelongsToBlob;
	}
	TArray<TArray<FVector2D>> Fragments;
	return ComputeClippedCellFragments(Cell, Fragments);
}

void AChunkedDestructibleRoundedBlob::AppendPolygonHorizontalFace(
	FDynamicMesh3& Mesh,
	const TArray<FVector2D>& Polygon,
	bool bTopFace,
	int32 MaterialIndex) const
{
	if (Polygon.Num() < 3)
	{
		return;
	}

	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	const double Z = bTopFace ? 0.0 : -EffectiveCellDimensions.Z;
	const FVector3f Normal = bTopFace ? FVector3f::UnitZ() : -FVector3f::UnitZ();
	for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
	{
		const FVector2D& PointA = Polygon[0];
		const FVector2D& PointB = bTopFace ? Polygon[Index] : Polygon[Index + 1];
		const FVector2D& PointC = bTopFace ? Polygon[Index + 1] : Polygon[Index];
		AppendMeshTriangle(
			Mesh,
			FVector3d(PointA.X, PointA.Y, Z),
			FVector3d(PointB.X, PointB.Y, Z),
			FVector3d(PointC.X, PointC.Y, Z),
			Normal,
			MaterialIndex,
			FVector2f(PointA.X / SafeUVScale, PointA.Y / SafeUVScale),
			FVector2f(PointB.X / SafeUVScale, PointB.Y / SafeUVScale),
			FVector2f(PointC.X / SafeUVScale, PointC.Y / SafeUVScale));
	}
}

void AChunkedDestructibleRoundedBlob::AppendGreedyFullHorizontalFaces(
	FDynamicMesh3& Mesh,
	int32 MinCellX,
	int32 MinCellY,
	int32 MaxCellX,
	int32 MaxCellY,
	bool bTopFace) const
{
	const int32 Width = MaxCellX - MinCellX;
	const int32 Height = MaxCellY - MinCellY;
	TBitArray<> Visited(false, Width * Height);
	const bool bCanMergeX = CellSpacing.X <= BlobSpacingTolerance;
	const bool bCanMergeY = CellSpacing.Y <= BlobSpacingTolerance;
	auto LocalIndex = [Width, MinCellX, MinCellY](int32 X, int32 Y)
	{
		return (X - MinCellX) + (Y - MinCellY) * Width;
	};
	auto IsMergeCandidate = [this](int32 X, int32 Y, int32 MaterialIndex)
	{
		const FIntPoint Cell(X, Y);
		if (!IsValidCell(Cell))
		{
			return false;
		}
		const FFloorCellData& Data = Cells[ToCellIndex(Cell)];
		return Data.bExists && Data.MaterialIndex == MaterialIndex && IsCellFullyInsideBlob(Cell);
	};

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const int32 VisitedIndex = LocalIndex(X, Y);
			if (Visited[VisitedIndex])
			{
				continue;
			}
			const FIntPoint StartCell(X, Y);
			const FFloorCellData& StartData = Cells[ToCellIndex(StartCell)];
			if (!StartData.bExists || !IsCellFullyInsideBlob(StartCell))
			{
				Visited[VisitedIndex] = true;
				continue;
			}

			const int32 MaterialIndex = StartData.MaterialIndex;
			int32 MergeWidth = 1;
			if (bCanMergeX)
			{
				while (X + MergeWidth < MaxCellX &&
					!Visited[LocalIndex(X + MergeWidth, Y)] &&
					IsMergeCandidate(X + MergeWidth, Y, MaterialIndex))
				{
					++MergeWidth;
				}
			}

			int32 MergeHeight = 1;
			if (bCanMergeY)
			{
				bool bCanGrow = true;
				while (Y + MergeHeight < MaxCellY && bCanGrow)
				{
					for (int32 OffsetX = 0; OffsetX < MergeWidth; ++OffsetX)
					{
						const int32 TestX = X + OffsetX;
						const int32 TestY = Y + MergeHeight;
						if (Visited[LocalIndex(TestX, TestY)] ||
							!IsMergeCandidate(TestX, TestY, MaterialIndex))
						{
							bCanGrow = false;
							break;
						}
					}
					if (bCanGrow)
					{
						++MergeHeight;
					}
				}
			}

			for (int32 OffsetY = 0; OffsetY < MergeHeight; ++OffsetY)
			{
				for (int32 OffsetX = 0; OffsetX < MergeWidth; ++OffsetX)
				{
					Visited[LocalIndex(X + OffsetX, Y + OffsetY)] = true;
				}
			}

			double MinX;
			double MinY;
			double IgnoredMaxX;
			double IgnoredMaxY;
			GetCellBounds(StartCell, MinX, MinY, IgnoredMaxX, IgnoredMaxY);
			double IgnoredMinX;
			double IgnoredMinY;
			double MaxX;
			double MaxY;
			GetCellBounds(
				FIntPoint(X + MergeWidth - 1, Y + MergeHeight - 1),
				IgnoredMinX, IgnoredMinY, MaxX, MaxY);
			const double Z = bTopFace ? 0.0 : -EffectiveCellDimensions.Z;
			if (bTopFace)
			{
				AppendMeshQuad(
					Mesh,
					FVector3d(MinX, MinY, Z), FVector3d(MaxX, MinY, Z),
					FVector3d(MaxX, MaxY, Z), FVector3d(MinX, MaxY, Z),
					FVector3f::UnitZ(), MaterialIndex,
					static_cast<float>(MaxX - MinX), static_cast<float>(MaxY - MinY), UVWorldUnits);
			}
			else
			{
				AppendMeshQuad(
					Mesh,
					FVector3d(MinX, MaxY, Z), FVector3d(MaxX, MaxY, Z),
					FVector3d(MaxX, MinY, Z), FVector3d(MinX, MinY, Z),
					-FVector3f::UnitZ(), MaterialIndex,
					static_cast<float>(MaxX - MinX), static_cast<float>(MaxY - MinY), UVWorldUnits);
			}
		}
	}
}

void AChunkedDestructibleRoundedBlob::AppendCachedHypnoticSpiralSurface(
	int32 ChunkIndex,
	FDynamicMesh3& Mesh) const
{
	if (FloorPattern != EChunkedFloorPattern::HypnoticSpiral ||
		FloorMaterials.IsEmpty() || ChunkCount.X <= 0)
	{
		return;
	}

	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	const int32 StripeMaterialIndex = FMath::Clamp(
		SecondaryMaterialIndex, 0, FloorMaterials.Num() - 1);
	auto AppendTriangle = [this, &Mesh, SafeUVScale](
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		int32 MaterialIndex)
	{
		AppendMeshTriangle(
			Mesh,
			FVector3d(A.X, A.Y, 0.20),
			FVector3d(B.X, B.Y, 0.20),
			FVector3d(C.X, C.Y, 0.20),
			FVector3f::UnitZ(),
			MaterialIndex,
			FVector2f(A.X / SafeUVScale, A.Y / SafeUVScale),
			FVector2f(B.X / SafeUVScale, B.Y / SafeUVScale),
			FVector2f(C.X / SafeUVScale, C.Y / SafeUVScale));
	};

	const int32 TotalCellCount = CellCount.X * CellCount.Y;
	const int32 TotalChunkCount = ChunkCount.X * ChunkCount.Y;
	const bool bSpiralSettingsChanged =
		!bCachedSpiralSettingsValid ||
		CachedSpiralTurns != SpiralTurns ||
		CachedSpiralBandFraction != SpiralBandFraction ||
		CachedSpiralRotationDegrees != SpiralRotationDegrees ||
		CachedSpiralCenterOffset != SpiralCenterOffset ||
		CachedSpiralCurveResolution != SpiralCurveResolution ||
		bCachedSpiralClockwise != bSpiralClockwise;
	if (bSpiralSettingsChanged &&
		CachedSpiralTrianglesByCell.Num() == TotalCellCount &&
		CachedSpiralChunks.Num() == TotalChunkCount)
	{
		for (TArray<FBlobCachedSurfaceTriangle>& CellTriangles : CachedSpiralTrianglesByCell)
		{
			CellTriangles.Reset();
		}
		CachedSpiralChunks.Init(false, TotalChunkCount);
		CachedSpiralTurns = SpiralTurns;
		CachedSpiralBandFraction = SpiralBandFraction;
		CachedSpiralRotationDegrees = SpiralRotationDegrees;
		CachedSpiralCenterOffset = SpiralCenterOffset;
		CachedSpiralCurveResolution = SpiralCurveResolution;
		bCachedSpiralClockwise = bSpiralClockwise;
		bCachedSpiralSettingsValid = true;
	}
	const bool bCacheIsUsable =
		CachedCellGeometry.Num() == TotalCellCount &&
		CachedSpiralTrianglesByCell.Num() == TotalCellCount &&
		CachedSpiralChunks.Num() == TotalChunkCount &&
		ChunkIndex >= 0 && ChunkIndex < TotalChunkCount;
	if (!bCacheIsUsable)
	{
		AppendHypnoticSpiralSurface2D(
			Mesh,
			ChunkIndex,
			FVector2D::ZeroVector,
			(BlobSize * 0.5).Size(),
			[this](FIntPoint Cell, TArray<TArray<FVector2D>>& OutPolygons)
			{
				OutPolygons.Reset();
				return Cells.IsValidIndex(ToCellIndex(Cell)) &&
					Cells[ToCellIndex(Cell)].bExists &&
					GetClippedCellFragments(Cell, OutPolygons);
			},
			[&AppendTriangle](
				FIntPoint,
				const FVector2D& A,
				const FVector2D& B,
				const FVector2D& C,
				int32 MaterialIndex)
			{
				AppendTriangle(A, B, C, MaterialIndex);
			});
		return;
	}

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinCellX = ChunkX * SafeChunkSide;
	const int32 MinCellY = ChunkY * SafeChunkSide;
	const int32 MaxCellX = FMath::Min(MinCellX + SafeChunkSide, CellCount.X);
	const int32 MaxCellY = FMath::Min(MinCellY + SafeChunkSide, CellCount.Y);

	if (!CachedSpiralChunks[ChunkIndex])
	{
		for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
		{
			for (int32 X = MinCellX; X < MaxCellX; ++X)
			{
				CachedSpiralTrianglesByCell[ToCellIndex(FIntPoint(X, Y))].Reset();
			}
		}

		AppendHypnoticSpiralSurface2D(
			Mesh,
			ChunkIndex,
			FVector2D::ZeroVector,
			(BlobSize * 0.5).Size(),
			[this](FIntPoint Cell, TArray<TArray<FVector2D>>& OutPolygons)
			{
				OutPolygons.Reset();
				const FBlobCellGeometryCache* CellCache = FindCellGeometryCache(Cell);
				if (!CellCache || !CellCache->bBelongsToBlob)
				{
					return false;
				}
				OutPolygons = CellCache->Fragments;
				return !OutPolygons.IsEmpty();
			},
			[this, &AppendTriangle](
				FIntPoint Cell,
				const FVector2D& A,
				const FVector2D& B,
				const FVector2D& C,
				int32 MaterialIndex)
			{
				const int32 CellIndex = ToCellIndex(Cell);
				if (!CachedSpiralTrianglesByCell.IsValidIndex(CellIndex))
				{
					return;
				}
				FBlobCachedSurfaceTriangle& Triangle =
					CachedSpiralTrianglesByCell[CellIndex].Emplace_GetRef();
				Triangle.A = A;
				Triangle.B = B;
				Triangle.C = C;
				if (Cells.IsValidIndex(CellIndex) && Cells[CellIndex].bExists)
				{
					AppendTriangle(A, B, C, MaterialIndex);
				}
			});
		CachedSpiralChunks[ChunkIndex] = true;
		return;
	}

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const int32 CellIndex = ToCellIndex(FIntPoint(X, Y));
			if (!Cells[CellIndex].bExists)
			{
				continue;
			}
			for (const FBlobCachedSurfaceTriangle& Triangle : CachedSpiralTrianglesByCell[CellIndex])
			{
				AppendTriangle(Triangle.A, Triangle.B, Triangle.C, StripeMaterialIndex);
			}
		}
	}
}

void AChunkedDestructibleRoundedBlob::BuildChunkMesh(int32 ChunkIndex, FDynamicMesh3& OutMesh) const
{
	OutMesh.EnableVertexNormals(FVector3f::UnitZ());
	OutMesh.EnableAttributes();
	OutMesh.Attributes()->SetNumUVLayers(1);
	OutMesh.Attributes()->EnableMaterialID();

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinCellX = ChunkX * SafeChunkSide;
	const int32 MinCellY = ChunkY * SafeChunkSide;
	const int32 MaxCellX = FMath::Min(MinCellX + SafeChunkSide, CellCount.X);
	const int32 MaxCellY = FMath::Min(MinCellY + SafeChunkSide, CellCount.Y);

	AppendGreedyFullHorizontalFaces(OutMesh, MinCellX, MinCellY, MaxCellX, MaxCellY, true);
	if (bGenerateBottomFaces)
	{
		AppendGreedyFullHorizontalFaces(OutMesh, MinCellX, MinCellY, MaxCellX, MaxCellY, false);
	}

	auto CellExists = [this](int32 X, int32 Y)
	{
		const FIntPoint Cell(X, Y);
		return IsValidCell(Cell) && Cells[ToCellIndex(Cell)].bExists;
	};
	auto AppendWall = [this, &OutMesh](
		const FVector2D& A,
		const FVector2D& B,
		int32 MaterialIndex)
	{
		const FVector2D Edge = B - A;
		const float EdgeLength = static_cast<float>(Edge.Size());
		if (EdgeLength <= KINDA_SMALL_NUMBER)
		{
			return;
		}
		const FVector2D Outward2D(Edge.Y, -Edge.X);
		const FVector3f Normal = FVector3f(
			static_cast<float>(Outward2D.X),
			static_cast<float>(Outward2D.Y),
			0.0f).GetSafeNormal();
		const double BottomZ = -EffectiveCellDimensions.Z;
		AppendMeshQuad(
			OutMesh,
			FVector3d(B.X, B.Y, BottomZ), FVector3d(B.X, B.Y, 0.0),
			FVector3d(A.X, A.Y, 0.0), FVector3d(A.X, A.Y, BottomZ),
			Normal, MaterialIndex, EdgeLength,
			static_cast<float>(EffectiveCellDimensions.Z), UVWorldUnits);
	};

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const FIntPoint Cell(X, Y);
			const FFloorCellData& CellData = Cells[ToCellIndex(Cell)];
			if (!CellData.bExists)
			{
				continue;
			}

			const FBlobCellGeometryCache* CellCache = FindCellGeometryCache(Cell);
			const bool bFullCell = CellCache
				? CellCache->bFullyInside
				: IsCellFullyInsideBlob(Cell);
			double CellMinX;
			double CellMinY;
			double CellMaxX;
			double CellMaxY;
			GetCellBounds(Cell, CellMinX, CellMinY, CellMaxX, CellMaxY);

			TArray<TArray<FVector2D>> LocalFragments;
			const TArray<TArray<FVector2D>>* Fragments = nullptr;
			if (bFullCell)
			{
				LocalFragments.Add({
					FVector2D(CellMinX, CellMinY), FVector2D(CellMaxX, CellMinY),
					FVector2D(CellMaxX, CellMaxY), FVector2D(CellMinX, CellMaxY)
				});
				Fragments = &LocalFragments;
			}
			else if (CellCache && CellCache->bBelongsToBlob)
			{
				Fragments = &CellCache->Fragments;
			}
			else if (GetClippedCellFragments(Cell, LocalFragments))
			{
				Fragments = &LocalFragments;
			}
			if (!Fragments)
			{
				continue;
			}
			if (!bFullCell)
			{
				for (const TArray<FVector2D>& Fragment : *Fragments)
				{
					AppendPolygonHorizontalFace(OutMesh, Fragment, true, CellData.MaterialIndex);
					if (bGenerateBottomFaces)
					{
						AppendPolygonHorizontalFace(OutMesh, Fragment, false, CellData.MaterialIndex);
					}
				}
			}

			// Add walls only on exposed grid cuts. Fan-triangulation edges are deliberately ignored.
			for (const TArray<FVector2D>& Fragment : *Fragments)
			{
				for (int32 EdgeIndex = 0; EdgeIndex < Fragment.Num(); ++EdgeIndex)
				{
					const FVector2D& A = Fragment[EdgeIndex];
					const FVector2D& B = Fragment[(EdgeIndex + 1) % Fragment.Num()];
					bool bExposedGridEdge = false;
					if (FMath::IsNearlyEqual(A.X, CellMinX, BlobPolygonTolerance) &&
						FMath::IsNearlyEqual(B.X, CellMinX, BlobPolygonTolerance))
					{
						bExposedGridEdge = CellSpacing.X > BlobSpacingTolerance || !CellExists(X - 1, Y);
					}
					else if (FMath::IsNearlyEqual(A.X, CellMaxX, BlobPolygonTolerance) &&
						FMath::IsNearlyEqual(B.X, CellMaxX, BlobPolygonTolerance))
					{
						bExposedGridEdge = CellSpacing.X > BlobSpacingTolerance || !CellExists(X + 1, Y);
					}
					else if (FMath::IsNearlyEqual(A.Y, CellMinY, BlobPolygonTolerance) &&
						FMath::IsNearlyEqual(B.Y, CellMinY, BlobPolygonTolerance))
					{
						bExposedGridEdge = CellSpacing.Y > BlobSpacingTolerance || !CellExists(X, Y - 1);
					}
					else if (FMath::IsNearlyEqual(A.Y, CellMaxY, BlobPolygonTolerance) &&
						FMath::IsNearlyEqual(B.Y, CellMaxY, BlobPolygonTolerance))
					{
						bExposedGridEdge = CellSpacing.Y > BlobSpacingTolerance || !CellExists(X, Y + 1);
					}
					if (bExposedGridEdge)
					{
						AppendWall(A, B, CellData.MaterialIndex);
					}
				}
			}

			// The sampled spline segments form the actual rounded outside wall.
			if (!bFullCell && CellCache)
			{
				for (const FBlobOutlineSegment& Segment : CellCache->OutlineSegments)
				{
					AppendWall(Segment.A, Segment.B, CellData.MaterialIndex);
				}
			}
			else
			{
				for (int32 EdgeIndex = 0; !bFullCell && EdgeIndex < CachedOutline.Num(); ++EdgeIndex)
				{
					FVector2D ClippedStart;
					FVector2D ClippedEnd;
					if (ClipSegmentToRectangle(
						CachedOutline[EdgeIndex],
						CachedOutline[(EdgeIndex + 1) % CachedOutline.Num()],
						CellMinX, CellMinY, CellMaxX, CellMaxY,
						ClippedStart, ClippedEnd))
					{
						AppendWall(ClippedStart, ClippedEnd, CellData.MaterialIndex);
					}
				}
			}
		}
	}

	if (FloorPattern == EChunkedFloorPattern::HypnoticSpiral)
	{
		AppendCachedHypnoticSpiralSurface(ChunkIndex, OutMesh);
	}
}

void AChunkedDestructibleRoundedBlob::BuildChunkSimpleCollision(
	int32 ChunkIndex,
	FKAggregateGeom& OutCollision) const
{
	OutCollision.EmptyElements();
	if (!bGenerateCollision || ChunkCount.X <= 0)
	{
		return;
	}

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinCellX = ChunkX * SafeChunkSide;
	const int32 MinCellY = ChunkY * SafeChunkSide;
	const int32 MaxCellX = FMath::Min(MinCellX + SafeChunkSide, CellCount.X);
	const int32 MaxCellY = FMath::Min(MinCellY + SafeChunkSide, CellCount.Y);
	const int32 Width = MaxCellX - MinCellX;
	const int32 Height = MaxCellY - MinCellY;
	TBitArray<> Visited(false, Width * Height);
	const bool bCanMergeX = CellSpacing.X <= BlobSpacingTolerance;
	const bool bCanMergeY = CellSpacing.Y <= BlobSpacingTolerance;
	auto LocalIndex = [Width, MinCellX, MinCellY](int32 X, int32 Y)
	{
		return (X - MinCellX) + (Y - MinCellY) * Width;
	};
	auto IsFullExistingCell = [this](int32 X, int32 Y)
	{
		const FIntPoint Cell(X, Y);
		return IsValidCell(Cell) && Cells[ToCellIndex(Cell)].bExists && IsCellFullyInsideBlob(Cell);
	};

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const int32 StartIndex = LocalIndex(X, Y);
			if (Visited[StartIndex])
			{
				continue;
			}
			if (!IsFullExistingCell(X, Y))
			{
				Visited[StartIndex] = true;
				continue;
			}

			int32 MergeWidth = 1;
			if (bCanMergeX)
			{
				while (X + MergeWidth < MaxCellX &&
					!Visited[LocalIndex(X + MergeWidth, Y)] &&
					IsFullExistingCell(X + MergeWidth, Y))
				{
					++MergeWidth;
				}
			}
			int32 MergeHeight = 1;
			if (bCanMergeY)
			{
				bool bCanGrow = true;
				while (Y + MergeHeight < MaxCellY && bCanGrow)
				{
					for (int32 OffsetX = 0; OffsetX < MergeWidth; ++OffsetX)
					{
						const int32 TestX = X + OffsetX;
						const int32 TestY = Y + MergeHeight;
						if (Visited[LocalIndex(TestX, TestY)] || !IsFullExistingCell(TestX, TestY))
						{
							bCanGrow = false;
							break;
						}
					}
					if (bCanGrow)
					{
						++MergeHeight;
					}
				}
			}

			for (int32 OffsetY = 0; OffsetY < MergeHeight; ++OffsetY)
			{
				for (int32 OffsetX = 0; OffsetX < MergeWidth; ++OffsetX)
				{
					Visited[LocalIndex(X + OffsetX, Y + OffsetY)] = true;
				}
			}

			double BoxMinX;
			double BoxMinY;
			double IgnoredMaxX;
			double IgnoredMaxY;
			GetCellBounds(FIntPoint(X, Y), BoxMinX, BoxMinY, IgnoredMaxX, IgnoredMaxY);
			double IgnoredMinX;
			double IgnoredMinY;
			double BoxMaxX;
			double BoxMaxY;
			GetCellBounds(
				FIntPoint(X + MergeWidth - 1, Y + MergeHeight - 1),
				IgnoredMinX, IgnoredMinY, BoxMaxX, BoxMaxY);
			FKBoxElem& Box = OutCollision.BoxElems.Emplace_GetRef(
				static_cast<float>(BoxMaxX - BoxMinX),
				static_cast<float>(BoxMaxY - BoxMinY),
				static_cast<float>(EffectiveCellDimensions.Z));
			Box.Center = FVector(
				(BoxMinX + BoxMaxX) * 0.5,
				(BoxMinY + BoxMaxY) * 0.5,
				-EffectiveCellDimensions.Z * 0.5);
		}
	}

	// Only the rounded boundary cells require convex-prism decomposition.
	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!Cells[ToCellIndex(Cell)].bExists || IsCellFullyInsideBlob(Cell))
			{
				continue;
			}
			TArray<TArray<FVector2D>> LocalFragments;
			const FBlobCellGeometryCache* CellCache = FindCellGeometryCache(Cell);
			const TArray<TArray<FVector2D>>* Fragments =
				CellCache && CellCache->bBelongsToBlob ? &CellCache->Fragments : nullptr;
			if (!Fragments && GetClippedCellFragments(Cell, LocalFragments))
			{
				Fragments = &LocalFragments;
			}
			if (!Fragments)
			{
				continue;
			}
			for (const TArray<FVector2D>& Fragment : *Fragments)
			{
				FKConvexElem& Convex = OutCollision.ConvexElems.Emplace_GetRef();
				Convex.VertexData.Reserve(Fragment.Num() * 2);
				for (const FVector2D& Point : Fragment)
				{
					Convex.VertexData.Add(FVector(Point.X, Point.Y, 0.0));
					Convex.VertexData.Add(FVector(Point.X, Point.Y, -EffectiveCellDimensions.Z));
				}
				Convex.UpdateElemBox();
			}
		}
	}
}

FTransform AChunkedDestructibleRoundedBlob::GetCellFlashTransform(FIntPoint Cell) const
{
	if (IsCellFullyInsideBlob(Cell))
	{
		return Super::GetCellFlashTransform(Cell);
	}

	TArray<TArray<FVector2D>> LocalFragments;
	const FBlobCellGeometryCache* CellCache = FindCellGeometryCache(Cell);
	const TArray<TArray<FVector2D>>* Fragments =
		CellCache && CellCache->bBelongsToBlob ? &CellCache->Fragments : nullptr;
	if (!Fragments && GetClippedCellFragments(Cell, LocalFragments))
	{
		Fragments = &LocalFragments;
	}
	if (!Fragments)
	{
		return Super::GetCellFlashTransform(Cell);
	}

	FBox2D Bounds(EForceInit::ForceInit);
	for (const TArray<FVector2D>& Fragment : *Fragments)
	{
		for (const FVector2D& Point : Fragment)
		{
			Bounds += Point;
		}
	}
	const FVector2D Size = Bounds.GetSize();
	const FVector2D Center = Bounds.GetCenter();
	const float SafeFlashScale = FMath::Max(FlashScale, 0.01f);
	return FTransform(
		FQuat::Identity,
		FVector(Center.X, Center.Y, -EffectiveCellDimensions.Z * 0.5),
		FVector(Size.X / 100.0, Size.Y / 100.0, EffectiveCellDimensions.Z / 100.0) * SafeFlashScale);
}
