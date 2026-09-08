// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleTriangle.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"

using namespace UE::Geometry;

namespace
{
	constexpr float MinimumTriangleDimension = 1.0f;
	constexpr float TriangleSpacingTolerance = 0.01f;
	constexpr double PolygonTolerance = 0.01;

	double Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

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

}

AChunkedDestructibleTriangle::AChunkedDestructibleTriangle()
{
	AreaSize = FVector2D(BaseLength, TriangleHeight);
	FloorPattern = EChunkedFloorPattern::Checkerboard;
	// Exact edge cells and pattern overlays are immutable between regenerations. Smaller chunks
	// keep a single hit from rebuilding a large part of the triangle without changing its shape.
	ChunkSideLength = 10;
}

#if WITH_EDITOR
bool AChunkedDestructibleTriangle::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty && InProperty->GetFName() == FName(TEXT("AreaSize")))
	{
		return false;
	}
	return Super::CanEditChange(InProperty);
}
#endif

void AChunkedDestructibleTriangle::ConfigureTriangle(
	float NewBaseLength,
	float NewTriangleHeight,
	float NewApexOffset,
	FVector NewCellDimensions,
	FVector2D NewCellSpacing,
	bool bPreserveCellState)
{
	BaseLength = FMath::Max(FMath::Abs(NewBaseLength), MinimumTriangleDimension);
	TriangleHeight = FMath::Max(FMath::Abs(NewTriangleHeight), MinimumTriangleDimension);
	ApexOffset = FMath::Clamp(NewApexOffset, -1.0f, 1.0f);
	CellDimensions = FVector(
		FMath::Max(FMath::Abs(NewCellDimensions.X), MinimumTriangleDimension),
		FMath::Max(FMath::Abs(NewCellDimensions.Y), MinimumTriangleDimension),
		FMath::Max(FMath::Abs(NewCellDimensions.Z), MinimumTriangleDimension));
	CellSpacing = FVector2D(
		FMath::Max(NewCellSpacing.X, 0.0),
		FMath::Max(NewCellSpacing.Y, 0.0));
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleTriangle::SetTriangleShape(
	float NewBaseLength,
	float NewTriangleHeight,
	float NewApexOffset,
	bool bPreserveCellState)
{
	BaseLength = FMath::Max(FMath::Abs(NewBaseLength), MinimumTriangleDimension);
	TriangleHeight = FMath::Max(FMath::Abs(NewTriangleHeight), MinimumTriangleDimension);
	ApexOffset = FMath::Clamp(NewApexOffset, -1.0f, 1.0f);
	GenerateFloor(bPreserveCellState);
}

bool AChunkedDestructibleTriangle::CalculateLayout()
{
	BaseLength = FMath::Max(FMath::Abs(BaseLength), MinimumTriangleDimension);
	TriangleHeight = FMath::Max(FMath::Abs(TriangleHeight), MinimumTriangleDimension);
	ApexOffset = FMath::Clamp(ApexOffset, -1.0f, 1.0f);
	AreaSize = FVector2D(BaseLength, TriangleHeight);
	if (!Super::CalculateLayout())
	{
		CellGeometryCache.Reset();
		return false;
	}
	RebuildCellGeometryCache();
	return true;
}

int32 AChunkedDestructibleTriangle::EvaluatePatternMaterial(FIntPoint Cell) const
{
	if (FloorPattern != EChunkedFloorPattern::HypnoticSpiral)
	{
		return Super::EvaluatePatternMaterial(Cell);
	}

	return FloorMaterials.IsEmpty()
		? 0
		: FMath::Clamp(PrimaryMaterialIndex, 0, FloorMaterials.Num() - 1);
}

void AChunkedDestructibleTriangle::GetTriangleVertices(
	FVector2D& OutBaseLeft,
	FVector2D& OutBaseRight,
	FVector2D& OutApex) const
{
	const double SafeBase = FMath::Max(FMath::Abs(BaseLength), MinimumTriangleDimension);
	const double SafeHeight = FMath::Max(FMath::Abs(TriangleHeight), MinimumTriangleDimension);
	const double HalfBase = SafeBase * 0.5;
	OutBaseLeft = FVector2D(-HalfBase, -SafeHeight * 0.5);
	OutBaseRight = FVector2D(HalfBase, -SafeHeight * 0.5);
	OutApex = FVector2D(FMath::Clamp(ApexOffset, -1.0f, 1.0f) * HalfBase, SafeHeight * 0.5);
}

FChunkedDestructibleTriangleMetrics AChunkedDestructibleTriangle::GetTriangleMetrics() const
{
	FVector2D BaseLeft;
	FVector2D BaseRight;
	FVector2D Apex;
	GetTriangleVertices(BaseLeft, BaseRight, Apex);

	FChunkedDestructibleTriangleMetrics Metrics;
	Metrics.BaseLength = static_cast<float>((BaseRight - BaseLeft).Size());
	Metrics.Height = FMath::Max(FMath::Abs(TriangleHeight), MinimumTriangleDimension);
	Metrics.LeftSideLength = static_cast<float>((Apex - BaseLeft).Size());
	Metrics.RightSideLength = static_cast<float>((Apex - BaseRight).Size());
	Metrics.LeftBaseAngle = FMath::RadiansToDegrees(FMath::Atan2(
		static_cast<float>(Apex.Y - BaseLeft.Y),
		static_cast<float>(Apex.X - BaseLeft.X)));
	Metrics.RightBaseAngle = FMath::RadiansToDegrees(FMath::Atan2(
		static_cast<float>(Apex.Y - BaseRight.Y),
		static_cast<float>(BaseRight.X - Apex.X)));
	Metrics.ApexAngle = FMath::Max(180.0f - Metrics.LeftBaseAngle - Metrics.RightBaseAngle, 0.0f);
	return Metrics;
}

bool AChunkedDestructibleTriangle::IsPointInsideTriangle(const FVector2D& Point, double Tolerance) const
{
	FVector2D Vertices[3];
	GetTriangleVertices(Vertices[0], Vertices[1], Vertices[2]);
	for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
	{
		const FVector2D& A = Vertices[EdgeIndex];
		const FVector2D& B = Vertices[(EdgeIndex + 1) % 3];
		if (Cross2D(B - A, Point - A) < -Tolerance)
		{
			return false;
		}
	}
	return true;
}

void AChunkedDestructibleTriangle::GetCellBounds(
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

bool AChunkedDestructibleTriangle::ComputeCellFullyInsideTriangle(FIntPoint Cell) const
{
	double MinX;
	double MinY;
	double MaxX;
	double MaxY;
	GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
	return IsPointInsideTriangle(FVector2D(MinX, MinY)) &&
		IsPointInsideTriangle(FVector2D(MaxX, MinY)) &&
		IsPointInsideTriangle(FVector2D(MaxX, MaxY)) &&
		IsPointInsideTriangle(FVector2D(MinX, MaxY));
}

bool AChunkedDestructibleTriangle::ComputeClippedCellPolygon(
	FIntPoint Cell,
	TArray<FVector2D>& OutPolygon) const
{
	OutPolygon.Reset();
	if (!IsValidCell(Cell))
	{
		return false;
	}

	double MinX;
	double MinY;
	double MaxX;
	double MaxY;
	GetCellBounds(Cell, MinX, MinY, MaxX, MaxY);
	OutPolygon = {
		FVector2D(MinX, MinY), FVector2D(MaxX, MinY),
		FVector2D(MaxX, MaxY), FVector2D(MinX, MaxY)
	};

	FVector2D TriangleVertices[3];
	GetTriangleVertices(TriangleVertices[0], TriangleVertices[1], TriangleVertices[2]);
	for (int32 EdgeIndex = 0; EdgeIndex < 3 && !OutPolygon.IsEmpty(); ++EdgeIndex)
	{
		const FVector2D ClipStart = TriangleVertices[EdgeIndex];
		const FVector2D ClipEnd = TriangleVertices[(EdgeIndex + 1) % 3];
		const FVector2D ClipEdge = ClipEnd - ClipStart;
		const TArray<FVector2D> InputPolygon = OutPolygon;
		OutPolygon.Reset();

		FVector2D Previous = InputPolygon.Last();
		double PreviousDistance = Cross2D(ClipEdge, Previous - ClipStart);
		bool bPreviousInside = PreviousDistance >= -PolygonTolerance;
		for (const FVector2D& Current : InputPolygon)
		{
			const double CurrentDistance = Cross2D(ClipEdge, Current - ClipStart);
			const bool bCurrentInside = CurrentDistance >= -PolygonTolerance;
			if (bCurrentInside != bPreviousInside)
			{
				const double Denominator = PreviousDistance - CurrentDistance;
				if (!FMath::IsNearlyZero(Denominator))
				{
					const double Alpha = FMath::Clamp(PreviousDistance / Denominator, 0.0, 1.0);
					OutPolygon.Add(Previous + (Current - Previous) * Alpha);
				}
			}
			if (bCurrentInside)
			{
				OutPolygon.Add(Current);
			}
			Previous = Current;
			PreviousDistance = CurrentDistance;
			bPreviousInside = bCurrentInside;
		}
	}

	for (int32 Index = OutPolygon.Num() - 1; Index >= 0 && OutPolygon.Num() > 1; --Index)
	{
		const int32 PreviousIndex = (Index - 1 + OutPolygon.Num()) % OutPolygon.Num();
		if (OutPolygon[Index].Equals(OutPolygon[PreviousIndex], PolygonTolerance))
		{
			OutPolygon.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}

	const double MinimumArea = FMath::Max(
		EffectiveCellDimensions.X * EffectiveCellDimensions.Y * 0.00001,
		0.001);
	return OutPolygon.Num() >= 3 && FMath::Abs(SignedPolygonArea(OutPolygon)) >= MinimumArea;
}

void AChunkedDestructibleTriangle::RebuildCellGeometryCache()
{
	CellGeometryCache.SetNum(CellCount.X * CellCount.Y);
	for (int32 Y = 0; Y < CellCount.Y; ++Y)
	{
		for (int32 X = 0; X < CellCount.X; ++X)
		{
			const FIntPoint Cell(X, Y);
			FTriangleCellGeometryCache& Cache = CellGeometryCache[ToCellIndex(Cell)];
			Cache.bBelongsToTriangle = ComputeClippedCellPolygon(Cell, Cache.Polygon);
			Cache.bFullyInside = Cache.bBelongsToTriangle && ComputeCellFullyInsideTriangle(Cell);
		}
	}
}

bool AChunkedDestructibleTriangle::IsCellFullyInsideTriangle(FIntPoint Cell) const
{
	const int32 CellIndex = IsValidCell(Cell) ? ToCellIndex(Cell) : INDEX_NONE;
	return CellGeometryCache.IsValidIndex(CellIndex)
		? CellGeometryCache[CellIndex].bFullyInside
		: ComputeCellFullyInsideTriangle(Cell);
}

bool AChunkedDestructibleTriangle::GetClippedCellPolygon(
	FIntPoint Cell,
	TArray<FVector2D>& OutPolygon) const
{
	const int32 CellIndex = IsValidCell(Cell) ? ToCellIndex(Cell) : INDEX_NONE;
	if (CellGeometryCache.IsValidIndex(CellIndex))
	{
		OutPolygon = CellGeometryCache[CellIndex].Polygon;
		return CellGeometryCache[CellIndex].bBelongsToTriangle;
	}
	return ComputeClippedCellPolygon(Cell, OutPolygon);
}

bool AChunkedDestructibleTriangle::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	TArray<FVector2D> Polygon;
	return GetClippedCellPolygon(Cell, Polygon);
}

void AChunkedDestructibleTriangle::AppendPolygonHorizontalFace(
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

void AChunkedDestructibleTriangle::AppendGreedyFullHorizontalFaces(
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
	const bool bCanMergeX = CellSpacing.X <= TriangleSpacingTolerance;
	const bool bCanMergeY = CellSpacing.Y <= TriangleSpacingTolerance;
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
		return Data.bExists && Data.MaterialIndex == MaterialIndex && IsCellFullyInsideTriangle(Cell);
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
			if (!StartData.bExists || !IsCellFullyInsideTriangle(StartCell))
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
						if (Visited[LocalIndex(TestX, TestY)] || !IsMergeCandidate(TestX, TestY, MaterialIndex))
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
			GetCellBounds(FIntPoint(X + MergeWidth - 1, Y + MergeHeight - 1), IgnoredMinX, IgnoredMinY, MaxX, MaxY);
			const double Z = bTopFace ? 0.0 : -EffectiveCellDimensions.Z;
			if (bTopFace)
			{
				AppendMeshQuad(Mesh,
					FVector3d(MinX, MinY, Z), FVector3d(MaxX, MinY, Z),
					FVector3d(MaxX, MaxY, Z), FVector3d(MinX, MaxY, Z),
					FVector3f::UnitZ(), MaterialIndex,
					static_cast<float>(MaxX - MinX), static_cast<float>(MaxY - MinY), UVWorldUnits);
			}
			else
			{
				AppendMeshQuad(Mesh,
					FVector3d(MinX, MaxY, Z), FVector3d(MaxX, MaxY, Z),
					FVector3d(MaxX, MinY, Z), FVector3d(MinX, MinY, Z),
					-FVector3f::UnitZ(), MaterialIndex,
					static_cast<float>(MaxX - MinX), static_cast<float>(MaxY - MinY), UVWorldUnits);
			}
		}
	}
}

void AChunkedDestructibleTriangle::BuildChunkMesh(int32 ChunkIndex, FDynamicMesh3& OutMesh) const
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
	FVector2D Outline[3];
	GetTriangleVertices(Outline[0], Outline[1], Outline[2]);
	auto IsOuterBoundary = [&Outline](const FVector2D& A, const FVector2D& B)
	{
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const FVector2D Edge = Outline[(Index + 1) % 3] - Outline[Index];
			const double Tolerance = PolygonTolerance * Edge.Size();
			if (FMath::Abs(Cross2D(Edge, A - Outline[Index])) <= Tolerance &&
				FMath::Abs(Cross2D(Edge, B - Outline[Index])) <= Tolerance)
			{
				return true;
			}
		}
		return false;
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
			TArray<FVector2D> Polygon;
			if (!GetClippedCellPolygon(Cell, Polygon))
			{
				continue;
			}

			if (!IsCellFullyInsideTriangle(Cell))
			{
				AppendPolygonHorizontalFace(OutMesh, Polygon, true, CellData.MaterialIndex);
				if (bGenerateBottomFaces)
				{
					AppendPolygonHorizontalFace(OutMesh, Polygon, false, CellData.MaterialIndex);
				}
			}

			double CellMinX;
			double CellMinY;
			double CellMaxX;
			double CellMaxY;
			GetCellBounds(Cell, CellMinX, CellMinY, CellMaxX, CellMaxY);
			for (int32 EdgeIndex = 0; EdgeIndex < Polygon.Num(); ++EdgeIndex)
			{
				const FVector2D& A = Polygon[EdgeIndex];
				const FVector2D& B = Polygon[(EdgeIndex + 1) % Polygon.Num()];
				const FVector2D Edge = B - A;
				const float EdgeLength = static_cast<float>(Edge.Size());
				if (EdgeLength <= KINDA_SMALL_NUMBER)
				{
					continue;
				}

				bool bSharedWithExistingCell = false;
				if (CellSpacing.X <= TriangleSpacingTolerance)
				{
					if (FMath::IsNearlyEqual(A.X, CellMinX, PolygonTolerance) &&
						FMath::IsNearlyEqual(B.X, CellMinX, PolygonTolerance))
					{
						bSharedWithExistingCell = CellExists(X - 1, Y);
					}
					else if (FMath::IsNearlyEqual(A.X, CellMaxX, PolygonTolerance) &&
						FMath::IsNearlyEqual(B.X, CellMaxX, PolygonTolerance))
					{
						bSharedWithExistingCell = CellExists(X + 1, Y);
					}
				}
				if (!bSharedWithExistingCell && CellSpacing.Y <= TriangleSpacingTolerance)
				{
					if (FMath::IsNearlyEqual(A.Y, CellMinY, PolygonTolerance) &&
						FMath::IsNearlyEqual(B.Y, CellMinY, PolygonTolerance))
					{
						bSharedWithExistingCell = CellExists(X, Y - 1);
					}
					else if (FMath::IsNearlyEqual(A.Y, CellMaxY, PolygonTolerance) &&
						FMath::IsNearlyEqual(B.Y, CellMaxY, PolygonTolerance))
					{
						bSharedWithExistingCell = CellExists(X, Y + 1);
					}
				}
				if (bSharedWithExistingCell)
				{
					continue;
				}

				const FVector2D Outward2D(Edge.Y, -Edge.X);
				const FVector3f Normal = FVector3f(
					static_cast<float>(Outward2D.X),
					static_cast<float>(Outward2D.Y),
					0.0f).GetSafeNormal();
				const double BottomZ = -EffectiveCellDimensions.Z;
				AppendFloorSideQuad(
					OutMesh,
					FVector3d(B.X, B.Y, BottomZ), FVector3d(B.X, B.Y, 0.0),
					FVector3d(A.X, A.Y, 0.0), FVector3d(A.X, A.Y, BottomZ),
					Normal, CellData.MaterialIndex, EdgeLength,
					static_cast<float>(EffectiveCellDimensions.Z),
					SideNeonMaterialInstance && SideNeon.Scope == EFloorSideNeonScope::OuterBoundary && IsOuterBoundary(A, B));
			}
		}
	}

	if (FloorPattern == EChunkedFloorPattern::HypnoticSpiral)
	{
		FVector2D BaseLeft;
		FVector2D BaseRight;
		FVector2D Apex;
		GetTriangleVertices(BaseLeft, BaseRight, Apex);
		const FVector2D PatternCenter = (BaseLeft + BaseRight + Apex) / 3.0;
		const double FurthestRadius = FMath::Max3(
			(BaseLeft - PatternCenter).Size(),
			(BaseRight - PatternCenter).Size(),
			(Apex - PatternCenter).Size());
		AppendHypnoticSpiralSurface2D(
			OutMesh,
			ChunkIndex,
			PatternCenter,
			FurthestRadius,
			[this](FIntPoint Cell, TArray<TArray<FVector2D>>& OutPolygons)
			{
				OutPolygons.Reset();
				if (!Cells[ToCellIndex(Cell)].bExists)
				{
					return false;
				}
				TArray<FVector2D> Polygon;
				if (!GetClippedCellPolygon(Cell, Polygon))
				{
					return false;
				}
				OutPolygons.Add(MoveTemp(Polygon));
				return true;
			},
			[this, &OutMesh](FIntPoint, const FVector2D& A, const FVector2D& B, const FVector2D& C, int32 MaterialIndex)
			{
				const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
				AppendMeshTriangle(
					OutMesh,
					FVector3d(A.X, A.Y, 0.20),
					FVector3d(B.X, B.Y, 0.20),
					FVector3d(C.X, C.Y, 0.20),
					FVector3f::UnitZ(),
					MaterialIndex,
					FVector2f(A.X / SafeUVScale, A.Y / SafeUVScale),
					FVector2f(B.X / SafeUVScale, B.Y / SafeUVScale),
					FVector2f(C.X / SafeUVScale, C.Y / SafeUVScale));
			});
	}
}

void AChunkedDestructibleTriangle::BuildChunkSimpleCollision(
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
	const bool bCanMergeX = CellSpacing.X <= TriangleSpacingTolerance;
	const bool bCanMergeY = CellSpacing.Y <= TriangleSpacingTolerance;
	auto LocalIndex = [Width, MinCellX, MinCellY](int32 X, int32 Y)
	{
		return (X - MinCellX) + (Y - MinCellY) * Width;
	};
	auto IsFullExistingCell = [this](int32 X, int32 Y)
	{
		const FIntPoint Cell(X, Y);
		return IsValidCell(Cell) && Cells[ToCellIndex(Cell)].bExists && IsCellFullyInsideTriangle(Cell);
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
			GetCellBounds(FIntPoint(X + MergeWidth - 1, Y + MergeHeight - 1),
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

	// Exact clipped edge cells use convex prisms; full cells above were merged into cheap boxes.
	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const FIntPoint Cell(X, Y);
			if (!Cells[ToCellIndex(Cell)].bExists || IsCellFullyInsideTriangle(Cell))
			{
				continue;
			}
			TArray<FVector2D> Polygon;
			if (!GetClippedCellPolygon(Cell, Polygon))
			{
				continue;
			}
			FKConvexElem& Convex = OutCollision.ConvexElems.Emplace_GetRef();
			Convex.VertexData.Reserve(Polygon.Num() * 2);
			for (const FVector2D& Point : Polygon)
			{
				Convex.VertexData.Add(FVector(Point.X, Point.Y, 0.0));
				Convex.VertexData.Add(FVector(Point.X, Point.Y, -EffectiveCellDimensions.Z));
			}
			Convex.UpdateElemBox();
		}
	}
}

FTransform AChunkedDestructibleTriangle::GetCellFlashTransform(FIntPoint Cell) const
{
	TArray<FVector2D> Polygon;
	if (!GetClippedCellPolygon(Cell, Polygon))
	{
		return Super::GetCellFlashTransform(Cell);
	}
	FBox2D Bounds(Polygon);
	const FVector2D Size = Bounds.GetSize();
	const FVector2D Center = Bounds.GetCenter();
	const float SafeFlashScale = FMath::Max(FlashScale, 0.01f);
	return FTransform(
		FQuat::Identity,
		FVector(Center.X, Center.Y, -EffectiveCellDimensions.Z * 0.5),
		FVector(Size.X / 100.0, Size.Y / 100.0, EffectiveCellDimensions.Z / 100.0) * SafeFlashScale);
}
