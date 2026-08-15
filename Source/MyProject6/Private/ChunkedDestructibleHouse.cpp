// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleHouse.h"

#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
	constexpr float MinimumHouseDimension = 1.0f;
	constexpr float HouseSpacingTolerance = 0.01f;
	constexpr double HousePolygonTolerance = 0.01;

	double CrossHouse2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	double HousePolygonArea(const TArray<FVector2D>& Polygon)
	{
		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const FVector2D& A = Polygon[Index];
			const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];
			TwiceArea += A.X * B.Y - B.X * A.Y;
		}
		return FMath::Abs(TwiceArea) * 0.5;
	}
}

AChunkedDestructibleHouse::AChunkedDestructibleHouse()
{
	FloorPattern = EChunkedFloorPattern::Solid;
	ChunkSideLength = 24;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		DebrisCubeMesh = CubeFinder.Object;
	}
}

void AChunkedDestructibleHouse::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	OnCellDestroyed.RemoveDynamic(this, &AChunkedDestructibleHouse::HandleHouseCellDestroyed);
	OnCellDestroyed.AddDynamic(this, &AChunkedDestructibleHouse::HandleHouseCellDestroyed);
}

#if WITH_EDITOR
bool AChunkedDestructibleHouse::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		const FName PropertyName = InProperty->GetFName();
		if (PropertyName == FName(TEXT("AreaSize")) ||
			PropertyName == FName(TEXT("CellDimensions")) ||
			PropertyName == FName(TEXT("CellSpacing")) ||
			PropertyName == FName(TEXT("FillRule")))
		{
			return false;
		}
	}
	return Super::CanEditChange(InProperty);
}
#endif

void AChunkedDestructibleHouse::BeginPlay()
{
	Super::BeginPlay();
	OnCellDestroyed.RemoveDynamic(this, &AChunkedDestructibleHouse::HandleHouseCellDestroyed);
	OnCellDestroyed.AddDynamic(this, &AChunkedDestructibleHouse::HandleHouseCellDestroyed);
}

void AChunkedDestructibleHouse::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearDebris();
	Super::EndPlay(EndPlayReason);
}

void AChunkedDestructibleHouse::RebuildHouse(bool bPreserveCellState)
{
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleHouse::ConfigureHouse(
	FIntVector NewWallBlockCount,
	int32 NewRoofHeightBlocks,
	int32 NewRoofOverhangBlocks,
	FVector NewBlockDimensions,
	FVector NewBlockSpacing,
	bool bNewHollowInterior,
	bool bPreserveCellState)
{
	WallBlockCount = FIntVector(
		FMath::Max(NewWallBlockCount.X, 3),
		FMath::Max(NewWallBlockCount.Y, 3),
		FMath::Max(NewWallBlockCount.Z, 2));
	RoofHeightBlocks = FMath::Clamp(NewRoofHeightBlocks, 1, 128);
	RoofOverhangBlocks = FMath::Clamp(NewRoofOverhangBlocks, 0, 16);
	BlockDimensions = FVector(
		FMath::Max(FMath::Abs(NewBlockDimensions.X), MinimumHouseDimension),
		FMath::Max(FMath::Abs(NewBlockDimensions.Y), MinimumHouseDimension),
		FMath::Max(FMath::Abs(NewBlockDimensions.Z), MinimumHouseDimension));
	BlockSpacing = FVector(
		FMath::Max(NewBlockSpacing.X, 0.0),
		FMath::Max(NewBlockSpacing.Y, 0.0),
		FMath::Max(NewBlockSpacing.Z, 0.0));
	bHollowInterior = bNewHollowInterior;
	GenerateFloor(bPreserveCellState);
}

int32 AChunkedDestructibleHouse::GetChimneyBaseZ() const
{
	return WallBlockCount.Z + FMath::Max(RoofHeightBlocks / 2, 1);
}

bool AChunkedDestructibleHouse::CalculateLayout()
{
	WallBlockCount = FIntVector(
		FMath::Max(WallBlockCount.X, 3),
		FMath::Max(WallBlockCount.Y, 3),
		FMath::Max(WallBlockCount.Z, 2));
	RoofHeightBlocks = FMath::Clamp(RoofHeightBlocks, 1, 128);
	RoofOverhangBlocks = FMath::Clamp(RoofOverhangBlocks, 0, 16);
	BlockDimensions = FVector(
		FMath::Max(FMath::Abs(BlockDimensions.X), MinimumHouseDimension),
		FMath::Max(FMath::Abs(BlockDimensions.Y), MinimumHouseDimension),
		FMath::Max(FMath::Abs(BlockDimensions.Z), MinimumHouseDimension));
	BlockSpacing = FVector(
		FMath::Max(BlockSpacing.X, 0.0),
		FMath::Max(BlockSpacing.Y, 0.0),
		FMath::Max(BlockSpacing.Z, 0.0));

	GridWidthBlocks = WallBlockCount.X + RoofOverhangBlocks * 2;
	GridDepthBlocks = WallBlockCount.Y + RoofOverhangBlocks * 2;
	const int32 RoofTopZ = WallBlockCount.Z + RoofHeightBlocks;
	const int32 ChimneyTopZ = bAddChimney ? GetChimneyBaseZ() + FMath::Max(ChimneyHeightBlocks, 1) : 0;
	GridHeightBlocks = FMath::Max(RoofTopZ, ChimneyTopZ);

	const int64 EncodedWidth = static_cast<int64>(GridWidthBlocks) * GridDepthBlocks;
	const int64 TotalCells = EncodedWidth * GridHeightBlocks;
	if (TotalCells <= 0 || TotalCells > FMath::Max(MaxGeneratedCells, 1))
	{
		UE_LOG(LogTemp, Error,
			TEXT("%s requested %lld house cells, exceeding MaxGeneratedCells (%d)."),
			*GetName(), TotalCells, MaxGeneratedCells);
		return false;
	}

	CellCount = FIntPoint(static_cast<int32>(EncodedWidth), GridHeightBlocks);
	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	ChunkCount = FIntPoint(
		FMath::DivideAndRoundUp(CellCount.X, SafeChunkSide),
		FMath::DivideAndRoundUp(CellCount.Y, SafeChunkSide));

	EffectiveCellDimensions = BlockDimensions;
	CellDimensions = BlockDimensions;
	CellSpacing = FVector2D(BlockSpacing.X, BlockSpacing.Z);
	const double Width = BlockDimensions.X * GridWidthBlocks +
		BlockSpacing.X * FMath::Max(GridWidthBlocks - 1, 0);
	const double Depth = BlockDimensions.Y * GridDepthBlocks +
		BlockSpacing.Y * FMath::Max(GridDepthBlocks - 1, 0);
	const double Height = BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	HouseGridLocalMin = FVector(-Width * 0.5, -Depth * 0.5, 0.0);
	GridLocalMin = FVector2D(HouseGridLocalMin.X, 0.0);
	EffectiveGridSize = FVector2D(Width, Height);
	AreaSize = EffectiveGridSize;
	return true;
}

FChunkedDestructibleHouseMetrics AChunkedDestructibleHouse::GetHouseMetrics() const
{
	FChunkedDestructibleHouseMetrics Metrics;
	Metrics.WallBlockCount = WallBlockCount;
	Metrics.BackingGridBlockCount = FIntVector(GridWidthBlocks, GridDepthBlocks, GridHeightBlocks);
	Metrics.WorldDimensions = FVector(
		BlockDimensions.X * GridWidthBlocks + BlockSpacing.X * FMath::Max(GridWidthBlocks - 1, 0),
		BlockDimensions.Y * GridDepthBlocks + BlockSpacing.Y * FMath::Max(GridDepthBlocks - 1, 0),
		BlockDimensions.Z * GridHeightBlocks + BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0));
	Metrics.LogicalCellCapacity = CellCount.X * CellCount.Y;
	return Metrics;
}

bool AChunkedDestructibleHouse::DecodeCell(
	FIntPoint Cell,
	int32& OutX,
	int32& OutY,
	int32& OutZ) const
{
	if (Cell.X < 0 || Cell.Y < 0 || Cell.X >= CellCount.X || Cell.Y >= GridHeightBlocks || GridWidthBlocks <= 0)
	{
		return false;
	}
	OutX = Cell.X % GridWidthBlocks;
	OutY = Cell.X / GridWidthBlocks;
	OutZ = Cell.Y;
	return OutY >= 0 && OutY < GridDepthBlocks;
}

FIntPoint AChunkedDestructibleHouse::EncodeCell(int32 X, int32 Y, int32 Z) const
{
	if (X < 0 || Y < 0 || Z < 0 || X >= GridWidthBlocks || Y >= GridDepthBlocks || Z >= GridHeightBlocks)
	{
		return FIntPoint::NoneValue;
	}
	return FIntPoint(X + Y * GridWidthBlocks, Z);
}

FVector AChunkedDestructibleHouse::GetCellLocalCenter(FIntPoint Cell) const
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return FVector::ZeroVector;
	}
	return FVector(
		HouseGridLocalMin.X + BlockDimensions.X * 0.5 + X * (BlockDimensions.X + BlockSpacing.X),
		HouseGridLocalMin.Y + BlockDimensions.Y * 0.5 + Y * (BlockDimensions.Y + BlockSpacing.Y),
		BlockDimensions.Z * 0.5 + Z * (BlockDimensions.Z + BlockSpacing.Z));
}

bool AChunkedDestructibleHouse::WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const
{
	OutCell = FIntPoint::NoneValue;
	if (GridWidthBlocks <= 0 || GridDepthBlocks <= 0 || GridHeightBlocks <= 0)
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const double PitchX = BlockDimensions.X + BlockSpacing.X;
	const double PitchY = BlockDimensions.Y + BlockSpacing.Y;
	const double PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	int32 X = FMath::FloorToInt((Local.X - HouseGridLocalMin.X) / PitchX);
	int32 Y = FMath::FloorToInt((Local.Y - HouseGridLocalMin.Y) / PitchY);
	int32 Z = FMath::FloorToInt(Local.Z / PitchZ);
	X = X == GridWidthBlocks && FMath::IsNearlyEqual(Local.X, -HouseGridLocalMin.X, 0.1) ? X - 1 : X;
	Y = Y == GridDepthBlocks && FMath::IsNearlyEqual(Local.Y, -HouseGridLocalMin.Y, 0.1) ? Y - 1 : Y;
	const double GridTop = BlockDimensions.Z * GridHeightBlocks + BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	Z = Z == GridHeightBlocks && FMath::IsNearlyEqual(Local.Z, GridTop, 0.1) ? Z - 1 : Z;
	const FIntPoint Candidate = EncodeCell(X, Y, Z);
	if (Candidate == FIntPoint::NoneValue)
	{
		return false;
	}
	const FVector Center = GetCellLocalCenter(Candidate);
	const double Tolerance = 0.1;
	if (FMath::Abs(Local.X - Center.X) > BlockDimensions.X * 0.5 + Tolerance ||
		FMath::Abs(Local.Y - Center.Y) > BlockDimensions.Y * 0.5 + Tolerance ||
		FMath::Abs(Local.Z - Center.Z) > BlockDimensions.Z * 0.5 + Tolerance)
	{
		return false;
	}
	OutCell = Candidate;
	return true;
}

bool AChunkedDestructibleHouse::IsOpeningCell(int32 X, int32 Y, int32 Z) const
{
	const int32 WallMinX = RoofOverhangBlocks;
	const int32 WallMinY = RoofOverhangBlocks;
	const int32 WallMaxY = WallMinY + WallBlockCount.Y - 1;
	const int32 LocalX = X - WallMinX;
	const bool bFrontWall = Y == WallMinY;
	const bool bBackWall = Y == WallMaxY;
	if (!bFrontWall && !bBackWall)
	{
		return false;
	}

	if (bGenerateDoor && bFrontWall && Z < FMath::Min(DoorHeightBlocks, WallBlockCount.Z))
	{
		const int32 SafeDoorWidth = FMath::Clamp(DoorWidthBlocks, 1, WallBlockCount.X - 2);
		const int32 DoorCenter = (WallBlockCount.X - 1) / 2 + DoorHorizontalOffsetBlocks;
		const int32 DoorStart = FMath::Clamp(DoorCenter - (SafeDoorWidth - 1) / 2, 1, WallBlockCount.X - SafeDoorWidth - 1);
		if (LocalX >= DoorStart && LocalX < DoorStart + SafeDoorWidth)
		{
			return true;
		}
	}

	if (!bGenerateWindows || (bBackWall && !bWindowsOnBackWall))
	{
		return false;
	}
	const int32 SafeColumns = FMath::Clamp(WindowColumns, 1, FMath::Max(WallBlockCount.X - 2, 1));
	const int32 SafeWidth = FMath::Clamp(WindowWidthBlocks, 1, FMath::Max(WallBlockCount.X - 2, 1));
	for (int32 Row = 0; Row < FMath::Max(WindowRows, 1); ++Row)
	{
		const int32 RowBottom = WindowBottomBlocks + Row * (FMath::Max(WindowHeightBlocks, 1) + WindowVerticalSpacingBlocks);
		if (Z < RowBottom || Z >= RowBottom + WindowHeightBlocks || Z >= WallBlockCount.Z - 1)
		{
			continue;
		}
		for (int32 Column = 0; Column < SafeColumns; ++Column)
		{
			const float Alpha = SafeColumns > 1 ? static_cast<float>(Column) / (SafeColumns - 1) : 0.5f;
			const int32 Center = FMath::RoundToInt(FMath::Lerp(1.0f, static_cast<float>(WallBlockCount.X - 2), Alpha));
			const int32 Start = FMath::Clamp(Center - (SafeWidth - 1) / 2, 1, WallBlockCount.X - SafeWidth - 1);
			if (LocalX >= Start && LocalX < Start + SafeWidth)
			{
				return true;
			}
		}
	}
	return false;
}

bool AChunkedDestructibleHouse::IsWallCell(int32 X, int32 Y, int32 Z) const
{
	if (Z < 0 || Z >= WallBlockCount.Z)
	{
		return false;
	}
	const int32 MinX = RoofOverhangBlocks;
	const int32 MinY = RoofOverhangBlocks;
	const int32 MaxX = MinX + WallBlockCount.X - 1;
	const int32 MaxY = MinY + WallBlockCount.Y - 1;
	if (X < MinX || X > MaxX || Y < MinY || Y > MaxY || IsOpeningCell(X, Y, Z))
	{
		return false;
	}
	if (!bHollowInterior)
	{
		return true;
	}
	return X == MinX || X == MaxX || Y == MinY || Y == MaxY || Z == 0 || Z == WallBlockCount.Z - 1;
}

bool AChunkedDestructibleHouse::IsChimneyCell(int32 X, int32 Y, int32 Z) const
{
	if (!bAddChimney || Z < GetChimneyBaseZ() || Z >= GetChimneyBaseZ() + FMath::Max(ChimneyHeightBlocks, 1))
	{
		return false;
	}
	const int32 SafeSize = FMath::Clamp(ChimneySizeBlocks, 1, 4);
	const int32 InnerMinX = RoofOverhangBlocks;
	const int32 InnerMinY = RoofOverhangBlocks;
	const float NormalizedX = FMath::Clamp(static_cast<float>(ChimneyNormalizedOffset.X), -1.0f, 1.0f) * 0.5f + 0.5f;
	const float NormalizedY = FMath::Clamp(static_cast<float>(ChimneyNormalizedOffset.Y), -1.0f, 1.0f) * 0.5f + 0.5f;
	const int32 CenterX = InnerMinX + FMath::RoundToInt(NormalizedX * (WallBlockCount.X - 1));
	const int32 CenterY = InnerMinY + FMath::RoundToInt(NormalizedY * (WallBlockCount.Y - 1));
	const int32 StartX = FMath::Clamp(CenterX - (SafeSize - 1) / 2, 0, GridWidthBlocks - SafeSize);
	const int32 StartY = FMath::Clamp(CenterY - (SafeSize - 1) / 2, 0, GridDepthBlocks - SafeSize);
	return X >= StartX && X < StartX + SafeSize && Y >= StartY && Y < StartY + SafeSize;
}

void AChunkedDestructibleHouse::GetCellBoundsXZ(
	FIntPoint Cell,
	double& OutMinX,
	double& OutMinZ,
	double& OutMaxX,
	double& OutMaxZ) const
{
	const FVector Center = GetCellLocalCenter(Cell);
	OutMinX = Center.X - BlockDimensions.X * 0.5;
	OutMaxX = Center.X + BlockDimensions.X * 0.5;
	OutMinZ = Center.Z - BlockDimensions.Z * 0.5;
	OutMaxZ = Center.Z + BlockDimensions.Z * 0.5;
}

void AChunkedDestructibleHouse::GetRoofTriangle(
	FVector2D& OutLeft,
	FVector2D& OutRight,
	FVector2D& OutApex) const
{
	const double RoofBaseZ = WallBlockCount.Z * (BlockDimensions.Z + BlockSpacing.Z);
	const double RoofTopZ = RoofBaseZ + RoofHeightBlocks * (BlockDimensions.Z + BlockSpacing.Z) - BlockSpacing.Z;
	const double MinX = HouseGridLocalMin.X;
	const double MaxX = -HouseGridLocalMin.X;
	OutLeft = FVector2D(MinX, RoofBaseZ);
	OutRight = FVector2D(MaxX, RoofBaseZ);
	OutApex = FVector2D(0.0, RoofTopZ);
}

bool AChunkedDestructibleHouse::GetCellPolygonXZ(FIntPoint Cell, TArray<FVector2D>& OutPolygon) const
{
	OutPolygon.Reset();
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return false;
	}

	double MinX;
	double MinZ;
	double MaxX;
	double MaxZ;
	GetCellBoundsXZ(Cell, MinX, MinZ, MaxX, MaxZ);
	OutPolygon = {
		FVector2D(MinX, MinZ), FVector2D(MaxX, MinZ),
		FVector2D(MaxX, MaxZ), FVector2D(MinX, MaxZ)
	};
	if (IsChimneyCell(X, Y, Z) || IsWallCell(X, Y, Z))
	{
		return true;
	}
	if (Z < WallBlockCount.Z || Z >= WallBlockCount.Z + RoofHeightBlocks)
	{
		OutPolygon.Reset();
		return false;
	}

	FVector2D Triangle[3];
	GetRoofTriangle(Triangle[0], Triangle[1], Triangle[2]);
	for (int32 EdgeIndex = 0; EdgeIndex < 3 && !OutPolygon.IsEmpty(); ++EdgeIndex)
	{
		const FVector2D ClipStart = Triangle[EdgeIndex];
		const FVector2D ClipEdge = Triangle[(EdgeIndex + 1) % 3] - ClipStart;
		const TArray<FVector2D> Input = OutPolygon;
		OutPolygon.Reset();
		FVector2D Previous = Input.Last();
		double PreviousDistance = CrossHouse2D(ClipEdge, Previous - ClipStart);
		bool bPreviousInside = PreviousDistance >= -HousePolygonTolerance;
		for (const FVector2D& Current : Input)
		{
			const double CurrentDistance = CrossHouse2D(ClipEdge, Current - ClipStart);
			const bool bCurrentInside = CurrentDistance >= -HousePolygonTolerance;
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
		if (OutPolygon[Index].Equals(OutPolygon[PreviousIndex], HousePolygonTolerance))
		{
			OutPolygon.RemoveAt(Index, 1, EAllowShrinking::No);
		}
	}
	const double MinimumArea = FMath::Max(BlockDimensions.X * BlockDimensions.Z * 0.00001, 0.001);
	return OutPolygon.Num() >= 3 && HousePolygonArea(OutPolygon) >= MinimumArea;
}

bool AChunkedDestructibleHouse::IsCellPolygonFullRectangle(
	FIntPoint Cell,
	const TArray<FVector2D>& Polygon) const
{
	return Polygon.Num() == 4 && FMath::IsNearlyEqual(
		HousePolygonArea(Polygon),
		BlockDimensions.X * BlockDimensions.Z,
		0.1);
}

bool AChunkedDestructibleHouse::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	TArray<FVector2D> Polygon;
	return GetCellPolygonXZ(Cell, Polygon);
}

int32 AChunkedDestructibleHouse::EvaluatePatternMaterial(FIntPoint Cell) const
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return 0;
	}
	const int32 MaxIndex = FMath::Max(FloorMaterials.Num() - 1, 0);
	if (FloorPattern == EChunkedFloorPattern::HypnoticSpiral)
	{
		return FMath::Clamp(PrimaryMaterialIndex, 0, MaxIndex);
	}
	if (IsChimneyCell(X, Y, Z))
	{
		return FMath::Clamp(ChimneyMaterialIndex, 0, MaxIndex);
	}
	if (Z >= WallBlockCount.Z)
	{
		return FMath::Clamp(RoofMaterialIndex, 0, MaxIndex);
	}
	if (Z == 0)
	{
		return FMath::Clamp(FoundationMaterialIndex, 0, MaxIndex);
	}
	return FMath::Clamp(WallMaterialIndex, 0, MaxIndex);
}

bool AChunkedDestructibleHouse::CellExists3D(int32 X, int32 Y, int32 Z) const
{
	const FIntPoint Cell = EncodeCell(X, Y, Z);
	return Cell != FIntPoint::NoneValue && Cells.IsValidIndex(ToCellIndex(Cell)) && Cells[ToCellIndex(Cell)].bExists;
}

void AChunkedDestructibleHouse::AppendPolygonFaceAtY(
	FDynamicMesh3& Mesh,
	const TArray<FVector2D>& Polygon,
	double Y,
	bool bFrontFace,
	int32 MaterialIndex) const
{
	if (Polygon.Num() < 3)
	{
		return;
	}
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	const FVector3f Normal = bFrontFace ? -FVector3f::UnitY() : FVector3f::UnitY();
	for (int32 Index = 1; Index + 1 < Polygon.Num(); ++Index)
	{
		const FVector2D& PointA = Polygon[0];
		const FVector2D& PointB = bFrontFace ? Polygon[Index] : Polygon[Index + 1];
		const FVector2D& PointC = bFrontFace ? Polygon[Index + 1] : Polygon[Index];
		AppendMeshTriangle(
			Mesh,
			FVector3d(PointA.X, Y, PointA.Y),
			FVector3d(PointB.X, Y, PointB.Y),
			FVector3d(PointC.X, Y, PointC.Y),
			Normal,
			MaterialIndex,
			FVector2f(PointA.X / SafeUVScale, PointA.Y / SafeUVScale),
			FVector2f(PointB.X / SafeUVScale, PointB.Y / SafeUVScale),
			FVector2f(PointC.X / SafeUVScale, PointC.Y / SafeUVScale));
	}
}

void AChunkedDestructibleHouse::BuildChunkMesh(int32 ChunkIndex, FDynamicMesh3& OutMesh) const
{
	OutMesh.EnableVertexNormals(FVector3f::UnitZ());
	OutMesh.EnableAttributes();
	OutMesh.Attributes()->SetNumUVLayers(1);
	OutMesh.Attributes()->EnableMaterialID();

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinEncodedX = ChunkX * SafeChunkSide;
	const int32 MinZ = ChunkY * SafeChunkSide;
	const int32 MaxEncodedX = FMath::Min(MinEncodedX + SafeChunkSide, CellCount.X);
	const int32 MaxZ = FMath::Min(MinZ + SafeChunkSide, CellCount.Y);

	for (int32 Z = MinZ; Z < MaxZ; ++Z)
	{
		for (int32 EncodedX = MinEncodedX; EncodedX < MaxEncodedX; ++EncodedX)
		{
			const FIntPoint Cell(EncodedX, Z);
			const FFloorCellData& CellData = Cells[ToCellIndex(Cell)];
			if (!CellData.bExists)
			{
				continue;
			}
			int32 X;
			int32 Y;
			int32 DecodedZ;
			if (!DecodeCell(Cell, X, Y, DecodedZ))
			{
				continue;
			}
			TArray<FVector2D> Polygon;
			if (!GetCellPolygonXZ(Cell, Polygon))
			{
				continue;
			}

			const FVector Center = GetCellLocalCenter(Cell);
			const double FrontY = Center.Y - BlockDimensions.Y * 0.5;
			const double BackY = Center.Y + BlockDimensions.Y * 0.5;
			if (BlockSpacing.Y > HouseSpacingTolerance || !CellExists3D(X, Y - 1, DecodedZ))
			{
				AppendPolygonFaceAtY(OutMesh, Polygon, FrontY, true, CellData.MaterialIndex);
			}
			if (BlockSpacing.Y > HouseSpacingTolerance || !CellExists3D(X, Y + 1, DecodedZ))
			{
				AppendPolygonFaceAtY(OutMesh, Polygon, BackY, false, CellData.MaterialIndex);
			}

			double CellMinX;
			double CellMinZ;
			double CellMaxX;
			double CellMaxZ;
			GetCellBoundsXZ(Cell, CellMinX, CellMinZ, CellMaxX, CellMaxZ);
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

				bool bShared = false;
				if (BlockSpacing.X <= HouseSpacingTolerance)
				{
					if (FMath::IsNearlyEqual(A.X, CellMinX, HousePolygonTolerance) &&
						FMath::IsNearlyEqual(B.X, CellMinX, HousePolygonTolerance))
					{
						bShared = CellExists3D(X - 1, Y, DecodedZ);
					}
					else if (FMath::IsNearlyEqual(A.X, CellMaxX, HousePolygonTolerance) &&
						FMath::IsNearlyEqual(B.X, CellMaxX, HousePolygonTolerance))
					{
						bShared = CellExists3D(X + 1, Y, DecodedZ);
					}
				}
				if (!bShared && BlockSpacing.Z <= HouseSpacingTolerance)
				{
					if (FMath::IsNearlyEqual(A.Y, CellMinZ, HousePolygonTolerance) &&
						FMath::IsNearlyEqual(B.Y, CellMinZ, HousePolygonTolerance))
					{
						bShared = CellExists3D(X, Y, DecodedZ - 1);
					}
					else if (FMath::IsNearlyEqual(A.Y, CellMaxZ, HousePolygonTolerance) &&
						FMath::IsNearlyEqual(B.Y, CellMaxZ, HousePolygonTolerance))
					{
						bShared = CellExists3D(X, Y, DecodedZ + 1);
					}
				}
				if (bShared)
				{
					continue;
				}

				const FVector2D Outward(Edge.Y, -Edge.X);
				const FVector3f Normal = FVector3f(
					static_cast<float>(Outward.X), 0.0f, static_cast<float>(Outward.Y)).GetSafeNormal();
				AppendMeshQuad(
					OutMesh,
					FVector3d(A.X, FrontY, A.Y), FVector3d(A.X, BackY, A.Y),
					FVector3d(B.X, BackY, B.Y), FVector3d(B.X, FrontY, B.Y),
					Normal, CellData.MaterialIndex,
					static_cast<float>(BlockDimensions.Y), EdgeLength, UVWorldUnits);
			}
		}
	}

	if (FloorPattern == EChunkedFloorPattern::HypnoticSpiral)
	{
		const FVector2D PatternCenter(
			HouseGridLocalMin.X + EffectiveGridSize.X * 0.5,
			EffectiveGridSize.Y * 0.5);
		AppendHypnoticSpiralSurface2D(
			OutMesh,
			ChunkIndex,
			PatternCenter,
			(EffectiveGridSize * 0.5).Size(),
			[this](FIntPoint Cell, TArray<TArray<FVector2D>>& OutPolygons)
			{
				OutPolygons.Reset();
				if (!Cells[ToCellIndex(Cell)].bExists)
				{
					return false;
				}
				TArray<FVector2D> Polygon;
				if (!GetCellPolygonXZ(Cell, Polygon))
				{
					return false;
				}
				OutPolygons.Add(MoveTemp(Polygon));
				return true;
			},
			[this, &OutMesh](FIntPoint Cell, const FVector2D& A, const FVector2D& B, const FVector2D& C, int32 MaterialIndex)
			{
				int32 X;
				int32 Y;
				int32 Z;
				if (!DecodeCell(Cell, X, Y, Z))
				{
					return;
				}
				const FVector Center = GetCellLocalCenter(Cell);
				const double FrontY = Center.Y - BlockDimensions.Y * 0.5 - 0.20;
				const double BackY = Center.Y + BlockDimensions.Y * 0.5 + 0.20;
				const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
				if (BlockSpacing.Y > HouseSpacingTolerance || !CellExists3D(X, Y - 1, Z))
				{
					AppendMeshTriangle(
						OutMesh,
						FVector3d(A.X, FrontY, A.Y),
						FVector3d(B.X, FrontY, B.Y),
						FVector3d(C.X, FrontY, C.Y),
						-FVector3f::UnitY(),
						MaterialIndex,
						FVector2f(A.X / SafeUVScale, A.Y / SafeUVScale),
						FVector2f(B.X / SafeUVScale, B.Y / SafeUVScale),
						FVector2f(C.X / SafeUVScale, C.Y / SafeUVScale));
				}
				if (BlockSpacing.Y > HouseSpacingTolerance || !CellExists3D(X, Y + 1, Z))
				{
					AppendMeshTriangle(
						OutMesh,
						FVector3d(A.X, BackY, A.Y),
						FVector3d(C.X, BackY, C.Y),
						FVector3d(B.X, BackY, B.Y),
						FVector3f::UnitY(),
						MaterialIndex,
						FVector2f(A.X / SafeUVScale, A.Y / SafeUVScale),
						FVector2f(C.X / SafeUVScale, C.Y / SafeUVScale),
						FVector2f(B.X / SafeUVScale, B.Y / SafeUVScale));
				}
			});
	}
}

void AChunkedDestructibleHouse::BuildChunkSimpleCollision(
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
	const int32 MinEncodedX = ChunkX * SafeChunkSide;
	const int32 MinZ = ChunkY * SafeChunkSide;
	const int32 MaxEncodedX = FMath::Min(MinEncodedX + SafeChunkSide, CellCount.X);
	const int32 MaxZ = FMath::Min(MinZ + SafeChunkSide, CellCount.Y);

	for (int32 Z = MinZ; Z < MaxZ; ++Z)
	{
		for (int32 EncodedX = MinEncodedX; EncodedX < MaxEncodedX; ++EncodedX)
		{
			const FIntPoint Cell(EncodedX, Z);
			if (!Cells[ToCellIndex(Cell)].bExists)
			{
				continue;
			}
			TArray<FVector2D> Polygon;
			if (!GetCellPolygonXZ(Cell, Polygon))
			{
				continue;
			}
			const FVector Center = GetCellLocalCenter(Cell);
			if (IsCellPolygonFullRectangle(Cell, Polygon))
			{
				FKBoxElem& Box = OutCollision.BoxElems.Emplace_GetRef(
					static_cast<float>(BlockDimensions.X),
					static_cast<float>(BlockDimensions.Y),
					static_cast<float>(BlockDimensions.Z));
				Box.Center = Center;
				continue;
			}

			const double FrontY = Center.Y - BlockDimensions.Y * 0.5;
			const double BackY = Center.Y + BlockDimensions.Y * 0.5;
			FKConvexElem& Convex = OutCollision.ConvexElems.Emplace_GetRef();
			Convex.VertexData.Reserve(Polygon.Num() * 2);
			for (const FVector2D& Point : Polygon)
			{
				Convex.VertexData.Add(FVector(Point.X, FrontY, Point.Y));
				Convex.VertexData.Add(FVector(Point.X, BackY, Point.Y));
			}
			Convex.UpdateElemBox();
		}
	}
}

FTransform AChunkedDestructibleHouse::GetCellFlashTransform(FIntPoint Cell) const
{
	FVector Center;
	FVector Size;
	GetPieceLocalBounds(Cell, Center, Size);
	return FTransform(
		FQuat::Identity,
		Center,
		Size / 100.0 * FMath::Max(FlashScale, 0.01f));
}

void AChunkedDestructibleHouse::GetPieceLocalBounds(
	FIntPoint Cell,
	FVector& OutCenter,
	FVector& OutSize) const
{
	TArray<FVector2D> Polygon;
	if (!GetCellPolygonXZ(Cell, Polygon))
	{
		OutCenter = GetCellLocalCenter(Cell);
		OutSize = BlockDimensions;
		return;
	}
	FBox2D Bounds(Polygon);
	const FVector CellCenter = GetCellLocalCenter(Cell);
	OutCenter = FVector(Bounds.GetCenter().X, CellCenter.Y, Bounds.GetCenter().Y);
	OutSize = FVector(Bounds.GetSize().X, BlockDimensions.Y, Bounds.GetSize().Y);
}

bool AChunkedDestructibleHouse::ResolveExistingCellFromHit(
	const FHitResult& Hit,
	FIntPoint& OutCell) const
{
	OutCell = FIntPoint::NoneValue;
	const FVector InteriorPoint = GetInteriorImpactPoint(Hit);
	FIntPoint Candidate;
	auto AcceptCandidate = [this, &OutCell](FIntPoint TestCell)
	{
		if (TestCell != FIntPoint::NoneValue && Cells.IsValidIndex(ToCellIndex(TestCell)) &&
			Cells[ToCellIndex(TestCell)].bExists)
		{
			OutCell = TestCell;
			return true;
		}
		return false;
	};
	if (WorldLocationToCell(InteriorPoint, Candidate) && AcceptCandidate(Candidate))
	{
		return true;
	}

	const FTransform ActorTransform = GetActorTransform();
	const FVector LocalInterior = ActorTransform.InverseTransformPosition(InteriorPoint);
	const float Probe = FMath::Min(
		FMath::Clamp(FMath::Min(BlockDimensions.X, BlockDimensions.Z) * 0.002f, 0.1f, 1.0f),
		FMath::Min(BlockDimensions.X, BlockDimensions.Z) * 0.1f);
	const FVector2D Offsets[] = {
		FVector2D(-Probe, 0.0), FVector2D(Probe, 0.0),
		FVector2D(0.0, -Probe), FVector2D(0.0, Probe),
		FVector2D(-Probe, -Probe), FVector2D(-Probe, Probe),
		FVector2D(Probe, -Probe), FVector2D(Probe, Probe)
	};
	for (const FVector2D& Offset : Offsets)
	{
		const FVector ProbeLocal = LocalInterior + FVector(Offset.X, 0.0, Offset.Y);
		if (WorldLocationToCell(ActorTransform.TransformPosition(ProbeLocal), Candidate) && AcceptCandidate(Candidate))
		{
			return true;
		}
	}
	return false;
}

int32 AChunkedDestructibleHouse::SelectCellsInRadius(
	FVector WorldLocation,
	float Radius,
	EChunkedFloorBlastShape Shape,
	TFunctionRef<void(int32, float)> CellFunction)
{
	if (Radius <= 0.0f || Cells.IsEmpty())
	{
		return 0;
	}
	const FVector LocalImpact = GetActorTransform().InverseTransformPosition(WorldLocation);
	const FVector Scale = GetActorScale3D().GetAbs();
	int32 SelectedCount = 0;
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		if (!Cells[CellIndex].bExists)
		{
			continue;
		}
		const FVector LocalDelta = GetCellLocalCenter(FromCellIndex(CellIndex)) - LocalImpact;
		const FVector ScaledDelta(LocalDelta.X * Scale.X, LocalDelta.Y * Scale.Y, LocalDelta.Z * Scale.Z);
		float Distance = 0.0f;
		switch (Shape)
		{
		case EChunkedFloorBlastShape::Square:
			Distance = FMath::Max3(FMath::Abs(ScaledDelta.X), FMath::Abs(ScaledDelta.Y), FMath::Abs(ScaledDelta.Z));
			break;
		case EChunkedFloorBlastShape::Diamond:
			Distance = FMath::Abs(ScaledDelta.X) + FMath::Abs(ScaledDelta.Y) + FMath::Abs(ScaledDelta.Z);
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

void AChunkedDestructibleHouse::MarkCellChunksDirty(FIntPoint Cell)
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return;
	}
	const FIntVector Offsets[] = {
		FIntVector(0, 0, 0), FIntVector(-1, 0, 0), FIntVector(1, 0, 0),
		FIntVector(0, -1, 0), FIntVector(0, 1, 0),
		FIntVector(0, 0, -1), FIntVector(0, 0, 1)
	};
	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	for (const FIntVector& Offset : Offsets)
	{
		const FIntPoint TestCell = EncodeCell(X + Offset.X, Y + Offset.Y, Z + Offset.Z);
		if (TestCell == FIntPoint::NoneValue)
		{
			continue;
		}
		const int32 ChunkX = TestCell.X / SafeChunkSide;
		const int32 ChunkY = TestCell.Y / SafeChunkSide;
		DirtyChunkIndices.Add(ChunkX + ChunkY * ChunkCount.X);
	}
}

void AChunkedDestructibleHouse::HandleHouseCellDestroyed(
	FIntPoint Cell,
	FVector WorldLocation,
	bool bRadialDestruction)
{
	if (bScatterDestroyedPieces)
	{
		SpawnDebrisForCell(Cell, WorldLocation);
	}
	if (bCollapseUnsupportedSections && !bEvaluatingIntegrity)
	{
		ScheduleStructuralIntegrityCheck();
	}
}

void AChunkedDestructibleHouse::SpawnDebrisForCell(FIntPoint Cell, FVector WorldLocation)
{
	if (!DebrisCubeMesh || MaxActiveDebrisPieces <= 0 || !GetWorld())
	{
		return;
	}
	ActiveDebrisComponents.RemoveAll(
		[](const TWeakObjectPtr<UStaticMeshComponent>& Component)
		{
			return !Component.IsValid();
		});
	if (ActiveDebrisComponents.Num() >= MaxActiveDebrisPieces)
	{
		return;
	}

	FVector LocalCenter;
	FVector LocalSize;
	GetPieceLocalBounds(Cell, LocalCenter, LocalSize);
	const int32 CellIndex = ToCellIndex(Cell);
	UMaterialInterface* DebrisMaterial = nullptr;
	if (Cells.IsValidIndex(CellIndex) && FloorMaterials.IsValidIndex(Cells[CellIndex].MaterialIndex))
	{
		DebrisMaterial = FloorMaterials[Cells[CellIndex].MaterialIndex];
	}
	const float MinScale = FMath::Clamp(
		static_cast<float>(FMath::Min(DebrisScaleRange.X, DebrisScaleRange.Y)), 0.05f, 1.0f);
	const float MaxScale = FMath::Clamp(
		static_cast<float>(FMath::Max(DebrisScaleRange.X, DebrisScaleRange.Y)), MinScale, 1.0f);
	const int32 SpawnCount = FMath::Min(
		FMath::Clamp(DebrisPiecesPerCell, 1, 8),
		MaxActiveDebrisPieces - ActiveDebrisComponents.Num());

	for (int32 PieceIndex = 0; PieceIndex < SpawnCount; ++PieceIndex)
	{
		const float FragmentScale = FMath::FRandRange(MinScale, MaxScale);
		const FVector FragmentSize = LocalSize * FragmentScale;
		const FVector LocalOffset(
			FMath::FRandRange(-LocalSize.X, LocalSize.X) * 0.2f,
			FMath::FRandRange(-LocalSize.Y, LocalSize.Y) * 0.2f,
			FMath::FRandRange(-LocalSize.Z, LocalSize.Z) * 0.2f);
		const FVector FragmentWorldLocation = GetActorTransform().TransformPosition(LocalCenter + LocalOffset);
		const FQuat FragmentWorldRotation = GetActorQuat() * FQuat(FRotator(
			FMath::FRandRange(-20.0f, 20.0f),
			FMath::FRandRange(-180.0f, 180.0f),
			FMath::FRandRange(-20.0f, 20.0f)));

		UStaticMeshComponent* Debris = NewObject<UStaticMeshComponent>(this);
		AddInstanceComponent(Debris);
		Debris->SetMobility(EComponentMobility::Movable);
		Debris->SetStaticMesh(DebrisCubeMesh);
		if (DebrisMaterial)
		{
			Debris->SetMaterial(0, DebrisMaterial);
		}
		Debris->SetCollisionProfileName(TEXT("PhysicsActor"));
		Debris->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Debris->SetWorldTransform(FTransform(
			FragmentWorldRotation,
			FragmentWorldLocation,
			FragmentSize / 100.0));
		Debris->RegisterComponent();
		Debris->SetSimulatePhysics(true);
		Debris->SetEnableGravity(true);
		Debris->SetLinearDamping(0.15f);
		Debris->SetAngularDamping(0.1f);

		FVector ScatterDirection = (FragmentWorldLocation - GetActorLocation()).GetSafeNormal();
		ScatterDirection += FVector(
			FMath::FRandRange(-0.45f, 0.45f),
			FMath::FRandRange(-0.45f, 0.45f),
			FMath::FRandRange(0.0f, 0.35f));
		ScatterDirection.Normalize();
		Debris->AddImpulse(
			ScatterDirection * DebrisImpulse + FVector::UpVector * DebrisUpwardImpulse,
			NAME_None,
			true);
		Debris->AddAngularImpulseInDegrees(FVector(
			FMath::FRandRange(-240.0f, 240.0f),
			FMath::FRandRange(-240.0f, 240.0f),
			FMath::FRandRange(-240.0f, 240.0f)),
			NAME_None,
			true);

		ActiveDebrisComponents.Add(Debris);
		const TWeakObjectPtr<UStaticMeshComponent> WeakDebris(Debris);
		FTimerDelegate CleanupDelegate = FTimerDelegate::CreateWeakLambda(
			this,
			[this, WeakDebris]()
			{
				if (UStaticMeshComponent* Component = WeakDebris.Get())
				{
					RemoveInstanceComponent(Component);
					Component->DestroyComponent();
				}
				ActiveDebrisComponents.Remove(WeakDebris);
			});
		FTimerHandle CleanupHandle;
		GetWorld()->GetTimerManager().SetTimer(
			CleanupHandle,
			MoveTemp(CleanupDelegate),
			FMath::Max(DebrisLifetime, 0.1f),
			false);
	}
}

void AChunkedDestructibleHouse::ScheduleStructuralIntegrityCheck()
{
	if (bIntegrityCheckQueued || !GetWorld())
	{
		return;
	}
	bIntegrityCheckQueued = true;
	GetWorld()->GetTimerManager().SetTimerForNextTick(
		FTimerDelegate::CreateWeakLambda(
			this,
			[this]()
			{
				bIntegrityCheckQueued = false;
				EvaluateStructuralIntegrity();
			}));
}

void AChunkedDestructibleHouse::EvaluateStructuralIntegrity()
{
	if (!bCollapseUnsupportedSections || Cells.IsEmpty() || bEvaluatingIntegrity)
	{
		return;
	}
	bEvaluatingIntegrity = true;
	TSet<int32> SupportedIndices;
	TArray<int32> OpenSet;
	OpenSet.Reserve(Cells.Num());
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		if (!Cells[CellIndex].bExists)
		{
			continue;
		}
		int32 X;
		int32 Y;
		int32 Z;
		if (DecodeCell(FromCellIndex(CellIndex), X, Y, Z) && Z == 0)
		{
			SupportedIndices.Add(CellIndex);
			OpenSet.Add(CellIndex);
		}
	}

	static const FIntVector NeighbourOffsets[] = {
		FIntVector(-1, 0, 0), FIntVector(1, 0, 0),
		FIntVector(0, -1, 0), FIntVector(0, 1, 0),
		FIntVector(0, 0, -1), FIntVector(0, 0, 1)
	};
	for (int32 OpenIndex = 0; OpenIndex < OpenSet.Num(); ++OpenIndex)
	{
		const int32 CellIndex = OpenSet[OpenIndex];
		int32 X;
		int32 Y;
		int32 Z;
		if (!DecodeCell(FromCellIndex(CellIndex), X, Y, Z))
		{
			continue;
		}
		for (const FIntVector& Offset : NeighbourOffsets)
		{
			const FIntPoint Neighbour = EncodeCell(X + Offset.X, Y + Offset.Y, Z + Offset.Z);
			if (Neighbour == FIntPoint::NoneValue)
			{
				continue;
			}
			const int32 NeighbourIndex = ToCellIndex(Neighbour);
			if (Cells[NeighbourIndex].bExists && !SupportedIndices.Contains(NeighbourIndex))
			{
				SupportedIndices.Add(NeighbourIndex);
				OpenSet.Add(NeighbourIndex);
			}
		}
	}

	const float MinDelay = FMath::Max(
		static_cast<float>(FMath::Min(CollapseDelayRange.X, CollapseDelayRange.Y)), 0.0f);
	const float MaxDelay = FMath::Max(
		static_cast<float>(FMath::Max(CollapseDelayRange.X, CollapseDelayRange.Y)), MinDelay);
	int32 QueuedCount = 0;
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		if (Cells[CellIndex].bExists && Cells[CellIndex].bDestructible && !SupportedIndices.Contains(CellIndex))
		{
			if (QueueCellDestruction(CellIndex, FMath::FRandRange(MinDelay, MaxDelay), true))
			{
				++QueuedCount;
			}
		}
	}
	if (QueuedCount > 0)
	{
		ProcessQueuedDestructions(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	}
	bEvaluatingIntegrity = false;
}

void AChunkedDestructibleHouse::ClearDebris()
{
	for (const TWeakObjectPtr<UStaticMeshComponent>& WeakDebris : ActiveDebrisComponents)
	{
		if (UStaticMeshComponent* Debris = WeakDebris.Get())
		{
			RemoveInstanceComponent(Debris);
			Debris->DestroyComponent();
		}
	}
	ActiveDebrisComponents.Reset();
}
