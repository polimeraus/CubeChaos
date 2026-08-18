// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleConeStructure.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "TimerManager.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
	constexpr float MinimumConeDimension = 1.0f;
	constexpr float ConeSpacingTolerance = 0.01f;
	constexpr double ConeSurfaceTolerance = 0.001;

	void ClipSurfacePolygonToPlane(
		TArray<FVector3d>& Polygon,
		int32 Axis,
		double Plane,
		bool bKeepGreater)
	{
		if (Polygon.IsEmpty())
		{
			return;
		}
		const TArray<FVector3d> Input = Polygon;
		Polygon.Reset();
		FVector3d Previous = Input.Last();
		double PreviousDistance = bKeepGreater
			? Previous[Axis] - Plane
			: Plane - Previous[Axis];
		bool bPreviousInside = PreviousDistance >= -ConeSurfaceTolerance;
		for (const FVector3d& Current : Input)
		{
			const double CurrentDistance = bKeepGreater
				? Current[Axis] - Plane
				: Plane - Current[Axis];
			const bool bCurrentInside = CurrentDistance >= -ConeSurfaceTolerance;
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

	void RemoveDuplicateSurfacePoints(TArray<FVector3d>& Polygon)
	{
		for (int32 Index = Polygon.Num() - 1; Index >= 0 && Polygon.Num() > 1; --Index)
		{
			const int32 PreviousIndex = (Index - 1 + Polygon.Num()) % Polygon.Num();
			if ((Polygon[Index] - Polygon[PreviousIndex]).SquaredLength() <=
				FMath::Square(ConeSurfaceTolerance))
			{
				Polygon.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
	}
}

AChunkedDestructibleConeStructure::AChunkedDestructibleConeStructure()
{
	FloorPattern = EChunkedFloorPattern::Solid;
	ChunkSideLength = 64;
	bDestructibleByDefault = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		DebrisCubeMesh = CubeFinder.Object;
	}
}

void AChunkedDestructibleConeStructure::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	OnCellDestroyed.RemoveDynamic(this, &AChunkedDestructibleConeStructure::HandleConeCellDestroyed);
	OnCellDestroyed.AddDynamic(this, &AChunkedDestructibleConeStructure::HandleConeCellDestroyed);
}

void AChunkedDestructibleConeStructure::BeginPlay()
{
	Super::BeginPlay();
	OnCellDestroyed.RemoveDynamic(this, &AChunkedDestructibleConeStructure::HandleConeCellDestroyed);
	OnCellDestroyed.AddDynamic(this, &AChunkedDestructibleConeStructure::HandleConeCellDestroyed);
}

void AChunkedDestructibleConeStructure::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearDebris();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
bool AChunkedDestructibleConeStructure::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		const FName PropertyName = InProperty->GetFName();
		if (PropertyName == FName(TEXT("AreaSize")) ||
			PropertyName == FName(TEXT("CellDimensions")) ||
			PropertyName == FName(TEXT("CellSpacing")) ||
			PropertyName == FName(TEXT("FillRule")) ||
			PropertyName == FName(TEXT("bGenerateBottomFaces")))
		{
			return false;
		}
	}
	return Super::CanEditChange(InProperty);
}
#endif

void AChunkedDestructibleConeStructure::RebuildConeStructure(bool bPreserveCellState)
{
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleConeStructure::SetStructureMaterials(
	UMaterialInterface* NewConeMaterial,
	UMaterialInterface* NewPlatformMaterial)
{
	ConeMaterial = NewConeMaterial;
	PlatformMaterial = NewPlatformMaterial;
	ApplyMaterialPattern();
}

bool AChunkedDestructibleConeStructure::DestroyConeCellFromHit(const FHitResult& Hit)
{
	FIntPoint Cell;
	return ResolveExistingCellFromHit(Hit, Cell) && DestroyCell(Cell);
}

void AChunkedDestructibleConeStructure::ConfigureConeStructure(
	int32 NewConeHeightBlocks,
	float NewBottomRadiusBlocks,
	float NewTopRadiusBlocks,
	float NewShellThicknessBlocks,
	FVector NewBlockDimensions,
	FVector NewBlockSpacing,
	bool bPreserveCellState)
{
	ConeHeightBlocks = FMath::Clamp(NewConeHeightBlocks, 4, 256);
	BottomRadiusBlocks = FMath::Clamp(FMath::Abs(NewBottomRadiusBlocks), 2.0f, 128.0f);
	TopRadiusBlocks = FMath::Clamp(FMath::Abs(NewTopRadiusBlocks), 0.0f, BottomRadiusBlocks);
	ShellThicknessBlocks = FMath::Clamp(
		FMath::Abs(NewShellThicknessBlocks), 0.5f, BottomRadiusBlocks);
	BlockDimensions = FVector(
		FMath::Max(FMath::Abs(NewBlockDimensions.X), MinimumConeDimension),
		FMath::Max(FMath::Abs(NewBlockDimensions.Y), MinimumConeDimension),
		FMath::Max(FMath::Abs(NewBlockDimensions.Z), MinimumConeDimension));
	BlockSpacing = FVector(
		FMath::Max(NewBlockSpacing.X, 0.0),
		FMath::Max(NewBlockSpacing.Y, 0.0),
		FMath::Max(NewBlockSpacing.Z, 0.0));
	GenerateFloor(bPreserveCellState);
}

bool AChunkedDestructibleConeStructure::CalculateLayout()
{
	ConeHeightBlocks = FMath::Clamp(ConeHeightBlocks, 4, 256);
	BottomRadiusBlocks = FMath::Clamp(FMath::Abs(BottomRadiusBlocks), 2.0f, 128.0f);
	TopRadiusBlocks = FMath::Clamp(FMath::Abs(TopRadiusBlocks), 0.0f, BottomRadiusBlocks);
	ShellThicknessBlocks = FMath::Clamp(FMath::Abs(ShellThicknessBlocks), 0.5f, BottomRadiusBlocks);
	BlockDimensions = FVector(
		FMath::Max(FMath::Abs(BlockDimensions.X), MinimumConeDimension),
		FMath::Max(FMath::Abs(BlockDimensions.Y), MinimumConeDimension),
		FMath::Max(FMath::Abs(BlockDimensions.Z), MinimumConeDimension));
	BlockSpacing = FVector(
		FMath::Max(BlockSpacing.X, 0.0),
		FMath::Max(BlockSpacing.Y, 0.0),
		FMath::Max(BlockSpacing.Z, 0.0));
	PlatformCount = FMath::Clamp(PlatformCount, 0, 32);
	LowestPlatformHeightBlocks = FMath::Clamp(LowestPlatformHeightBlocks, 0, ConeHeightBlocks - 1);
	PlatformVerticalSpacingBlocks = FMath::Clamp(PlatformVerticalSpacingBlocks, 1, 128);
	PlatformOverhangBlocks = FMath::Clamp(FMath::Abs(PlatformOverhangBlocks), 0.0f, 32.0f);
	PlatformThicknessBlocks = FMath::Clamp(PlatformThicknessBlocks, 1, 16);
	EntranceWidthBlocks = FMath::Clamp(EntranceWidthBlocks, 1, 64);
	EntranceHeightBlocks = FMath::Clamp(EntranceHeightBlocks, 1, ConeHeightBlocks);
	CircularSegments = FMath::Clamp(CircularSegments, 32, 512);
	MaxRuntimeCircularSegments = FMath::Clamp(MaxRuntimeCircularSegments, 32, 512);

	const float PitchX = BlockDimensions.X + BlockSpacing.X;
	const float PitchY = BlockDimensions.Y + BlockSpacing.Y;
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float NewRadialBlockUnit = FMath::Max(FMath::Min(PitchX, PitchY), MinimumConeDimension);
	const float MaximumRadius = (BottomRadiusBlocks +
		(bGeneratePlatforms && PlatformCount > 0 ? PlatformOverhangBlocks : 0.0f)) * NewRadialBlockUnit;
	const int32 RadiusInGridCells = FMath::CeilToInt(
		MaximumRadius / FMath::Max(FMath::Min(PitchX, PitchY), MinimumConeDimension)) + 1;
	const int32 NewGridDiameterBlocks = RadiusInGridCells * 2 + 1;
	const int32 NewGridHeightBlocks = ConeHeightBlocks;
	const float GridWidth = BlockDimensions.X * NewGridDiameterBlocks +
		BlockSpacing.X * FMath::Max(NewGridDiameterBlocks - 1, 0);
	const float GridDepth = BlockDimensions.Y * NewGridDiameterBlocks +
		BlockSpacing.Y * FMath::Max(NewGridDiameterBlocks - 1, 0);
	const float GridHeight = BlockDimensions.Z * NewGridHeightBlocks +
		BlockSpacing.Z * FMath::Max(NewGridHeightBlocks - 1, 0);
	const FVector NewGridLocalMin(-GridWidth * 0.5f, -GridDepth * 0.5f, 0.0f);
	const float TotalConeHeight = FMath::Max(GridHeight, MinimumConeDimension);

	auto GetPhysicalCenter = [&](int32 X, int32 Y, int32 Z)
	{
		return FVector(
			NewGridLocalMin.X + BlockDimensions.X * 0.5f + X * PitchX,
			NewGridLocalMin.Y + BlockDimensions.Y * 0.5f + Y * PitchY,
			BlockDimensions.Z * 0.5f + Z * PitchZ);
	};
	auto GetOuterRadius = [&](float LocalZ)
	{
		const float Alpha = FMath::Clamp(LocalZ / TotalConeHeight, 0.0f, 1.0f);
		return FMath::Lerp(BottomRadiusBlocks, TopRadiusBlocks, Alpha) * NewRadialBlockUnit;
	};
	auto GetInnerRadius = [&](float LocalZ)
	{
		return FMath::Max(
			GetOuterRadius(LocalZ) - ShellThicknessBlocks * NewRadialBlockUnit,
			0.0f);
	};
	auto GetRadialRange = [&](int32 X, int32 Y, float& OutMinimumRadius, float& OutMaximumRadius)
	{
		const FVector Center = GetPhysicalCenter(X, Y, 0);
		const float NearX = FMath::Max(FMath::Abs(Center.X) - BlockDimensions.X * 0.5f, 0.0f);
		const float NearY = FMath::Max(FMath::Abs(Center.Y) - BlockDimensions.Y * 0.5f, 0.0f);
		const float FarX = FMath::Abs(Center.X) + BlockDimensions.X * 0.5f;
		const float FarY = FMath::Abs(Center.Y) + BlockDimensions.Y * 0.5f;
		OutMinimumRadius = FVector2D(NearX, NearY).Size();
		OutMaximumRadius = FVector2D(FarX, FarY).Size();
	};
	auto IsConeCell = [&](int32 X, int32 Y, int32 Z)
	{
		float MinimumRadius;
		float MaximumCellRadius;
		GetRadialRange(X, Y, MinimumRadius, MaximumCellRadius);
		const float BottomZ = Z * PitchZ;
		const float TopZ = BottomZ + BlockDimensions.Z;
		// Test the complete vertical extent. Center-only classification omitted small pieces of
		// steep cone bands and was the source of pinholes in the intact smooth surface.
		const float MaximumOuterRadius = FMath::Max(GetOuterRadius(BottomZ), GetOuterRadius(TopZ));
		const float MinimumInnerRadius = FMath::Min(GetInnerRadius(BottomZ), GetInnerRadius(TopZ));
		return MinimumRadius <= MaximumOuterRadius + ConeSpacingTolerance &&
			MaximumCellRadius >= MinimumInnerRadius - ConeSpacingTolerance;
	};
	auto IsPlatformBackingCell = [&](int32 X, int32 Y, int32 Z)
	{
		if (!bGeneratePlatforms || PlatformCount <= 0)
		{
			return false;
		}
		for (int32 PlatformIndex = 0; PlatformIndex < PlatformCount; ++PlatformIndex)
		{
			const int32 BottomZ = LowestPlatformHeightBlocks +
				PlatformIndex * PlatformVerticalSpacingBlocks;
			if (BottomZ >= NewGridHeightBlocks)
			{
				break;
			}
			if (Z < BottomZ || Z >= FMath::Min(BottomZ + PlatformThicknessBlocks, NewGridHeightBlocks))
			{
				continue;
			}
			const float PlatformLocalZ = BlockDimensions.Z * 0.5f + BottomZ * PitchZ;
			const float InnerRadius = GetInnerRadius(PlatformLocalZ);
			const float OuterRadius = GetOuterRadius(PlatformLocalZ) +
				PlatformOverhangBlocks * NewRadialBlockUnit;
			float MinimumRadius;
			float MaximumCellRadius;
			GetRadialRange(X, Y, MinimumRadius, MaximumCellRadius);
			return MinimumRadius <= OuterRadius && MaximumCellRadius >= InnerRadius;
		}
		return false;
	};

	// Store only cells that can own cone/platform geometry. The previous diameter^2*height
	// backing volume exceeded the generation limit for large but otherwise valid hollow cones.
	TArray<TArray<FIntPoint>> NewPhysicalCellsByLayer;
	NewPhysicalCellsByLayer.SetNum(NewGridHeightBlocks);
	int32 MaximumLayerCellCount = 0;
	for (int32 Z = 0; Z < NewGridHeightBlocks; ++Z)
	{
		TArray<FIntPoint>& LayerCells = NewPhysicalCellsByLayer[Z];
		for (int32 Y = 0; Y < NewGridDiameterBlocks; ++Y)
		{
			for (int32 X = 0; X < NewGridDiameterBlocks; ++X)
			{
				if (IsConeCell(X, Y, Z) || IsPlatformBackingCell(X, Y, Z))
				{
					LayerCells.Emplace(X, Y);
				}
			}
		}
		MaximumLayerCellCount = FMath::Max(MaximumLayerCellCount, LayerCells.Num());
	}

	const int64 TotalCells = static_cast<int64>(MaximumLayerCellCount) * NewGridHeightBlocks;
	const int32 EffectiveCellLimit = FMath::Max3(MaxGeneratedCells, MaxCompactConeCells, 1);
	if (TotalCells <= 0 || TotalCells > EffectiveCellLimit)
	{
		UE_LOG(LogTemp, Error,
			TEXT("%s requested %lld compact cone cells, exceeding its effective limit (%d)."),
			*GetName(), TotalCells, EffectiveCellLimit);
		return false;
	}

	GridDiameterBlocks = NewGridDiameterBlocks;
	GridHeightBlocks = NewGridHeightBlocks;
	ConeGridLocalMin = NewGridLocalMin;
	CachedRadialBlockUnit = NewRadialBlockUnit;
	PhysicalCellsByLayer = MoveTemp(NewPhysicalCellsByLayer);
	PhysicalCellSlotsByLayer.Reset(GridHeightBlocks);
	PhysicalCellSlotsByLayer.SetNum(GridHeightBlocks);
	for (int32 Z = 0; Z < GridHeightBlocks; ++Z)
	{
		TMap<int32, int32>& LayerSlots = PhysicalCellSlotsByLayer[Z];
		const TArray<FIntPoint>& LayerCells = PhysicalCellsByLayer[Z];
		LayerSlots.Reserve(LayerCells.Num());
		for (int32 Slot = 0; Slot < LayerCells.Num(); ++Slot)
		{
			LayerSlots.Add(LayerCells[Slot].X + LayerCells[Slot].Y * GridDiameterBlocks, Slot);
		}
	}

	CellCount = FIntPoint(MaximumLayerCellCount, GridHeightBlocks);
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	ChunkCount = FIntPoint(
		FMath::DivideAndRoundUp(CellCount.X, SafeChunkSide),
		FMath::DivideAndRoundUp(CellCount.Y, SafeChunkSide));

	EffectiveCellDimensions = BlockDimensions;
	CellDimensions = BlockDimensions;
	CellSpacing = FVector2D(BlockSpacing.X, BlockSpacing.Y);
	GridLocalMin = FVector2D(ConeGridLocalMin.X, 0.0);
	EffectiveGridSize = FVector2D(FMath::Max(GridWidth, GridDepth), GridHeight);
	AreaSize = EffectiveGridSize;
	return true;
}

FChunkedDestructibleConeStructureMetrics AChunkedDestructibleConeStructure::GetConeStructureMetrics() const
{
	FChunkedDestructibleConeStructureMetrics Metrics;
	Metrics.BackingGridBlockCount = FIntVector(
		GridDiameterBlocks, GridDiameterBlocks, GridHeightBlocks);
	Metrics.WorldDimensions = FVector(
		BlockDimensions.X * GridDiameterBlocks + BlockSpacing.X * FMath::Max(GridDiameterBlocks - 1, 0),
		BlockDimensions.Y * GridDiameterBlocks + BlockSpacing.Y * FMath::Max(GridDiameterBlocks - 1, 0),
		BlockDimensions.Z * GridHeightBlocks + BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0));
	for (const FFloorCellData& Cell : Cells)
	{
		Metrics.GeneratedBlockCount += Cell.bBelongsToGeneratedShape ? 1 : 0;
	}
	const float BottomOuterRadius = BottomRadiusBlocks * CachedRadialBlockUnit;
	const float BottomInnerRadius = FMath::Max(
		BottomOuterRadius - ShellThicknessBlocks * CachedRadialBlockUnit, 0.0f);
	Metrics.BottomInteriorDiameter = BottomInnerRadius * 2.0f;
	return Metrics;
}

bool AChunkedDestructibleConeStructure::DecodeCell(
	FIntPoint Cell,
	int32& OutX,
	int32& OutY,
	int32& OutZ) const
{
	if (Cell.X < 0 || Cell.Y < 0 || Cell.X >= CellCount.X ||
		Cell.Y >= GridHeightBlocks || GridDiameterBlocks <= 0 ||
		!PhysicalCellsByLayer.IsValidIndex(Cell.Y) ||
		!PhysicalCellsByLayer[Cell.Y].IsValidIndex(Cell.X))
	{
		return false;
	}
	const FIntPoint PhysicalCell = PhysicalCellsByLayer[Cell.Y][Cell.X];
	OutX = PhysicalCell.X;
	OutY = PhysicalCell.Y;
	OutZ = Cell.Y;
	return true;
}

FIntPoint AChunkedDestructibleConeStructure::EncodeCell(int32 X, int32 Y, int32 Z) const
{
	if (X < 0 || Y < 0 || Z < 0 || X >= GridDiameterBlocks ||
		Y >= GridDiameterBlocks || Z >= GridHeightBlocks ||
		!PhysicalCellSlotsByLayer.IsValidIndex(Z))
	{
		return FIntPoint::NoneValue;
	}
	const int32* Slot = PhysicalCellSlotsByLayer[Z].Find(X + Y * GridDiameterBlocks);
	return Slot ? FIntPoint(*Slot, Z) : FIntPoint::NoneValue;
}

FVector AChunkedDestructibleConeStructure::GetPhysicalCellLocalCenter(
	int32 X,
	int32 Y,
	int32 Z) const
{
	return FVector(
		ConeGridLocalMin.X + BlockDimensions.X * 0.5 + X * (BlockDimensions.X + BlockSpacing.X),
		ConeGridLocalMin.Y + BlockDimensions.Y * 0.5 + Y * (BlockDimensions.Y + BlockSpacing.Y),
		BlockDimensions.Z * 0.5 + Z * (BlockDimensions.Z + BlockSpacing.Z));
}

FVector AChunkedDestructibleConeStructure::GetCellLocalCenter(FIntPoint Cell) const
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return FVector::ZeroVector;
	}
	return GetPhysicalCellLocalCenter(X, Y, Z);
}

bool AChunkedDestructibleConeStructure::WorldLocationToCell(
	FVector WorldLocation,
	FIntPoint& OutCell) const
{
	OutCell = FIntPoint::NoneValue;
	if (GridDiameterBlocks <= 0 || GridHeightBlocks <= 0)
	{
		return false;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const double PitchX = BlockDimensions.X + BlockSpacing.X;
	const double PitchY = BlockDimensions.Y + BlockSpacing.Y;
	const double PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	int32 X = FMath::FloorToInt((Local.X - ConeGridLocalMin.X) / PitchX);
	int32 Y = FMath::FloorToInt((Local.Y - ConeGridLocalMin.Y) / PitchY);
	int32 Z = FMath::FloorToInt(Local.Z / PitchZ);
	const double GridMaxX = -ConeGridLocalMin.X;
	const double GridMaxY = -ConeGridLocalMin.Y;
	X = X == GridDiameterBlocks && FMath::IsNearlyEqual(Local.X, GridMaxX, 0.1) ? X - 1 : X;
	Y = Y == GridDiameterBlocks && FMath::IsNearlyEqual(Local.Y, GridMaxY, 0.1) ? Y - 1 : Y;
	const double GridTop = BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	Z = Z == GridHeightBlocks && FMath::IsNearlyEqual(Local.Z, GridTop, 0.1) ? Z - 1 : Z;

	const FIntPoint Candidate = EncodeCell(X, Y, Z);
	if (Candidate == FIntPoint::NoneValue)
	{
		return false;
	}
	const FVector Center = GetCellLocalCenter(Candidate);
	constexpr double Tolerance = 0.1;
	if (FMath::Abs(Local.X - Center.X) > BlockDimensions.X * 0.5 + Tolerance ||
		FMath::Abs(Local.Y - Center.Y) > BlockDimensions.Y * 0.5 + Tolerance ||
		FMath::Abs(Local.Z - Center.Z) > BlockDimensions.Z * 0.5 + Tolerance)
	{
		return false;
	}
	OutCell = Candidate;
	return true;
}

float AChunkedDestructibleConeStructure::GetConeOuterRadiusAtHeight(float LocalZ) const
{
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const float Alpha = FMath::Clamp(LocalZ / FMath::Max(TotalHeight, MinimumConeDimension), 0.0f, 1.0f);
	return FMath::Lerp(BottomRadiusBlocks, TopRadiusBlocks, Alpha) * CachedRadialBlockUnit;
}

float AChunkedDestructibleConeStructure::GetConeInnerRadiusAtHeight(float LocalZ) const
{
	return FMath::Max(
		GetConeOuterRadiusAtHeight(LocalZ) - ShellThicknessBlocks * CachedRadialBlockUnit,
		0.0f);
}

void AChunkedDestructibleConeStructure::GetCellRadialRange(
	int32 X,
	int32 Y,
	float& OutMinimumRadius,
	float& OutMaximumRadius) const
{
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, 0);
	const float HalfX = BlockDimensions.X * 0.5f;
	const float HalfY = BlockDimensions.Y * 0.5f;
	const float NearX = FMath::Max(FMath::Abs(Center.X) - HalfX, 0.0f);
	const float NearY = FMath::Max(FMath::Abs(Center.Y) - HalfY, 0.0f);
	const float FarX = FMath::Abs(Center.X) + HalfX;
	const float FarY = FMath::Abs(Center.Y) + HalfY;
	OutMinimumRadius = FVector2D(NearX, NearY).Size();
	OutMaximumRadius = FVector2D(FarX, FarY).Size();
}

bool AChunkedDestructibleConeStructure::IsConeShellCell(int32 X, int32 Y, int32 Z) const
{
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float BottomZ = Z * PitchZ;
	const float TopZ = BottomZ + BlockDimensions.Z;
	const float OuterRadius = FMath::Max(
		GetConeOuterRadiusAtHeight(BottomZ), GetConeOuterRadiusAtHeight(TopZ));
	const float InnerRadius = FMath::Min(
		GetConeInnerRadiusAtHeight(BottomZ), GetConeInnerRadiusAtHeight(TopZ));
	float MinimumRadius;
	float MaximumRadius;
	GetCellRadialRange(X, Y, MinimumRadius, MaximumRadius);
	return MinimumRadius <= OuterRadius && MaximumRadius >= InnerRadius;
}

bool AChunkedDestructibleConeStructure::IsPlatformCell(int32 X, int32 Y, int32 Z) const
{
	if (!bGeneratePlatforms || PlatformCount <= 0)
	{
		return false;
	}
	for (int32 PlatformIndex = 0; PlatformIndex < PlatformCount; ++PlatformIndex)
	{
		const int32 BottomZ = LowestPlatformHeightBlocks +
			PlatformIndex * PlatformVerticalSpacingBlocks;
		if (BottomZ >= GridHeightBlocks)
		{
			break;
		}
		if (Z < BottomZ || Z >= FMath::Min(BottomZ + PlatformThicknessBlocks, GridHeightBlocks))
		{
			continue;
		}
		const float PlatformLocalZ = BlockDimensions.Z * 0.5f +
			BottomZ * (BlockDimensions.Z + BlockSpacing.Z);
		const float ConeOuterRadius = GetConeOuterRadiusAtHeight(PlatformLocalZ);
		const float InnerRadius = GetConeInnerRadiusAtHeight(PlatformLocalZ);
		const float OuterRadius = ConeOuterRadius + PlatformOverhangBlocks * CachedRadialBlockUnit;
		float MinimumRadius;
		float MaximumRadius;
		GetCellRadialRange(X, Y, MinimumRadius, MaximumRadius);
		return MinimumRadius <= OuterRadius && MaximumRadius >= InnerRadius;
	}
	return false;
}

bool AChunkedDestructibleConeStructure::IsEntranceCell(int32 X, int32 Y, int32 Z) const
{
	if (!bGenerateEntrance || Z < 0 || Z >= EntranceHeightBlocks)
	{
		return false;
	}
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	const float AngleRadians = FMath::DegreesToRadians(EntranceAngleDegrees);
	const FVector2D FrontDirection(FMath::Sin(AngleRadians), -FMath::Cos(AngleRadians));
	const FVector2D RightDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians));
	const FVector2D Position(Center.X, Center.Y);
	const float ForwardDistance = FVector2D::DotProduct(Position, FrontDirection);
	const float LateralDistance = FMath::Abs(FVector2D::DotProduct(Position, RightDirection));
	const float OpeningHalfWidth = EntranceWidthBlocks * CachedRadialBlockUnit * 0.5f;
	return ForwardDistance >= 0.0f && LateralDistance < OpeningHalfWidth;
}

bool AChunkedDestructibleConeStructure::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return false;
	}
	const bool bBelongsToStructure = IsConeShellCell(X, Y, Z) || IsPlatformCell(X, Y, Z);
	return bBelongsToStructure && !IsEntranceCell(X, Y, Z);
}

int32 AChunkedDestructibleConeStructure::EvaluatePatternMaterial(FIntPoint Cell) const
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return 0;
	}
	return IsPlatformCell(X, Y, Z) ? 1 : 0;
}

bool AChunkedDestructibleConeStructure::CellExists3D(int32 X, int32 Y, int32 Z) const
{
	const FIntPoint Cell = EncodeCell(X, Y, Z);
	return Cell != FIntPoint::NoneValue && Cells.IsValidIndex(ToCellIndex(Cell)) &&
		Cells[ToCellIndex(Cell)].bExists;
}

void AChunkedDestructibleConeStructure::ConfigureChunkComponent(
	UDynamicMeshComponent* Component) const
{
	Super::ConfigureChunkComponent(Component);
	if (!Component)
	{
		return;
	}
	Component->bUseAsyncCooking = bUseAsyncConeCollisionCooking;

	UMaterialInterface* ResolvedConeMaterial = ResolveStructureMaterial(false);
	UMaterialInterface* ResolvedPlatformMaterial = ResolveStructureMaterial(true);

	Component->SetNumMaterials(2);
	Component->SetMaterial(0, ResolvedConeMaterial);
	Component->SetMaterial(1, ResolvedPlatformMaterial);
	const UDynamicMesh* DynamicMesh = Component->GetDynamicMesh();
	const int32 TriangleCount = DynamicMesh ? DynamicMesh->GetTriangleCount() : 0;
	const bool bUseComplexCollisionForComponent = bUseSmoothComplexCollision &&
		TriangleCount > 0 && TriangleCount <= FMath::Clamp(MaxSmoothCollisionTrianglesPerChunk, 10000, 240000);
	Component->SetComplexAsSimpleCollisionEnabled(bUseComplexCollisionForComponent, false);
}

void AChunkedDestructibleConeStructure::AppendClippedSurfaceTriangle(
	FDynamicMesh3& Mesh,
	int32 ChunkIndex,
	int32 LogicalZ,
	const FVector3d& A,
	const FVector3d& B,
	const FVector3d& C,
	const FVector3f& Normal,
	int32 MaterialIndex) const
{
	if (LogicalZ < 0 || LogicalZ >= GridHeightBlocks)
	{
		return;
	}
	const FVector3d TriangleMin(
		FMath::Min3(A.X, B.X, C.X), FMath::Min3(A.Y, B.Y, C.Y), 0.0);
	const FVector3d TriangleMax(
		FMath::Max3(A.X, B.X, C.X), FMath::Max3(A.Y, B.Y, C.Y), 0.0);
	const double PitchX = BlockDimensions.X + BlockSpacing.X;
	const double PitchY = BlockDimensions.Y + BlockSpacing.Y;
	const int32 MinimumX = FMath::Clamp(
		FMath::FloorToInt((TriangleMin.X - ConeGridLocalMin.X) / PitchX), 0, GridDiameterBlocks - 1);
	const int32 MaximumX = FMath::Clamp(
		FMath::FloorToInt((TriangleMax.X - ConeGridLocalMin.X) / PitchX), 0, GridDiameterBlocks - 1);
	const int32 MinimumY = FMath::Clamp(
		FMath::FloorToInt((TriangleMin.Y - ConeGridLocalMin.Y) / PitchY), 0, GridDiameterBlocks - 1);
	const int32 MaximumY = FMath::Clamp(
		FMath::FloorToInt((TriangleMax.Y - ConeGridLocalMin.Y) / PitchY), 0, GridDiameterBlocks - 1);
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);

	for (int32 Y = MinimumY; Y <= MaximumY; ++Y)
	{
		for (int32 X = MinimumX; X <= MaximumX; ++X)
		{
			const FIntPoint Cell = EncodeCell(X, Y, LogicalZ);
			if (Cell == FIntPoint::NoneValue ||
				Cell.X / SafeChunkSide + (Cell.Y / SafeChunkSide) * ChunkCount.X != ChunkIndex ||
				!Cells.IsValidIndex(ToCellIndex(Cell)) || !Cells[ToCellIndex(Cell)].bExists)
			{
				continue;
			}

			const FVector CellCenter = GetCellLocalCenter(Cell);
			const double CellMinX = CellCenter.X - BlockDimensions.X * 0.5;
			const double CellMaxX = CellCenter.X + BlockDimensions.X * 0.5;
			const double CellMinY = CellCenter.Y - BlockDimensions.Y * 0.5;
			const double CellMaxY = CellCenter.Y + BlockDimensions.Y * 0.5;
			TArray<FVector3d> Polygon = { A, B, C };
			ClipSurfacePolygonToPlane(Polygon, 0, CellMinX, true);
			ClipSurfacePolygonToPlane(Polygon, 0, CellMaxX, false);
			ClipSurfacePolygonToPlane(Polygon, 1, CellMinY, true);
			ClipSurfacePolygonToPlane(Polygon, 1, CellMaxY, false);
			RemoveDuplicateSurfacePoints(Polygon);
			if (Polygon.Num() < 3)
			{
				continue;
			}

			for (int32 PointIndex = 1; PointIndex + 1 < Polygon.Num(); ++PointIndex)
			{
				FVector3d PointA = Polygon[0];
				FVector3d PointB = Polygon[PointIndex];
				FVector3d PointC = Polygon[PointIndex + 1];
				FVector3d GeometricNormal = FVector3d::CrossProduct(PointB - PointA, PointC - PointA);
				if (GeometricNormal.SquaredLength() <= FMath::Square(ConeSurfaceTolerance))
				{
					continue;
				}
				if (FVector3d::DotProduct(GeometricNormal, FVector3d(Normal)) < 0.0)
				{
					Swap(PointB, PointC);
				}

				auto MakeUV = [Normal, SafeUVScale](const FVector3d& Point)
				{
					if (FMath::Abs(Normal.Z) > 0.75f)
					{
						return FVector2f(Point.X / SafeUVScale, Point.Y / SafeUVScale);
					}
					return FMath::Abs(Normal.X) > FMath::Abs(Normal.Y)
						? FVector2f(Point.Y / SafeUVScale, Point.Z / SafeUVScale)
						: FVector2f(Point.X / SafeUVScale, Point.Z / SafeUVScale);
				};
				AppendMeshTriangle(
					Mesh, PointA, PointB, PointC, Normal, MaterialIndex,
					MakeUV(PointA), MakeUV(PointB), MakeUV(PointC));
			}
		}
	}
}

void AChunkedDestructibleConeStructure::AppendAnnularSurface(
	FDynamicMesh3& Mesh,
	int32 ChunkIndex,
	int32 LogicalZ,
	float LocalZ,
	float InnerRadius,
	float OuterRadius,
	bool bTopFace,
	int32 MaterialIndex) const
{
	if (OuterRadius <= KINDA_SMALL_NUMBER || InnerRadius >= OuterRadius || ChunkCount.X <= 0)
	{
		return;
	}
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinLogicalZ = ChunkY * SafeChunkSide;
	const int32 MaxLogicalZ = FMath::Min(MinLogicalZ + SafeChunkSide, GridHeightBlocks);
	if (LogicalZ < MinLogicalZ || LogicalZ >= MaxLogicalZ)
	{
		return;
	}
	const int32 SegmentCount = GetEffectiveCircularSegments();
	const FVector3f Normal = bTopFace ? FVector3f::UnitZ() : -FVector3f::UnitZ();
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		const double AngleA = UE_TWO_PI * SegmentIndex / SegmentCount;
		const double AngleB = UE_TWO_PI * (SegmentIndex + 1) / SegmentCount;
		const FVector3d InnerA(FMath::Cos(AngleA) * InnerRadius, FMath::Sin(AngleA) * InnerRadius, LocalZ);
		const FVector3d OuterA(FMath::Cos(AngleA) * OuterRadius, FMath::Sin(AngleA) * OuterRadius, LocalZ);
		const FVector3d OuterB(FMath::Cos(AngleB) * OuterRadius, FMath::Sin(AngleB) * OuterRadius, LocalZ);
		const FVector3d InnerB(FMath::Cos(AngleB) * InnerRadius, FMath::Sin(AngleB) * InnerRadius, LocalZ);
		AppendClippedSurfaceTriangle(
			Mesh, ChunkIndex, LogicalZ, InnerA, OuterA, OuterB, Normal, MaterialIndex);
		AppendClippedSurfaceTriangle(
			Mesh, ChunkIndex, LogicalZ, InnerA, OuterB, InnerB, Normal, MaterialIndex);
	}
}

void AChunkedDestructibleConeStructure::AppendConeSurfaces(
	FDynamicMesh3& Mesh,
	int32 ChunkIndex) const
{
	if (ChunkCount.X <= 0)
	{
		return;
	}
	const int32 SegmentCount = GetEffectiveCircularSegments();
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinLogicalZ = ChunkY * SafeChunkSide;
	const int32 MaxLogicalZ = FMath::Min(MinLogicalZ + SafeChunkSide, GridHeightBlocks);
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const float RadiusSlope = (TopRadiusBlocks - BottomRadiusBlocks) *
		CachedRadialBlockUnit / FMath::Max(TotalHeight, MinimumConeDimension);
	for (int32 Z = MinLogicalZ; Z < MaxLogicalZ; ++Z)
	{
		const float BottomZ = Z * PitchZ;
		const float TopZ = BottomZ + BlockDimensions.Z;
		const float OuterBottomRadius = GetConeOuterRadiusAtHeight(BottomZ);
		const float OuterTopRadius = GetConeOuterRadiusAtHeight(TopZ);
		const float InnerBottomRadius = GetConeInnerRadiusAtHeight(BottomZ);
		const float InnerTopRadius = GetConeInnerRadiusAtHeight(TopZ);
		for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
		{
			const double AngleA = UE_TWO_PI * SegmentIndex / SegmentCount;
			const double AngleB = UE_TWO_PI * (SegmentIndex + 1) / SegmentCount;
			const double MidAngle = (AngleA + AngleB) * 0.5;
			const FVector3f OuterNormal = FVector3f(
				FMath::Cos(MidAngle), FMath::Sin(MidAngle), -RadiusSlope).GetSafeNormal();
			const FVector3f InnerNormal = -OuterNormal;

			const FVector3d OuterBottomA(FMath::Cos(AngleA) * OuterBottomRadius, FMath::Sin(AngleA) * OuterBottomRadius, BottomZ);
			const FVector3d OuterBottomB(FMath::Cos(AngleB) * OuterBottomRadius, FMath::Sin(AngleB) * OuterBottomRadius, BottomZ);
			const FVector3d OuterTopA(FMath::Cos(AngleA) * OuterTopRadius, FMath::Sin(AngleA) * OuterTopRadius, TopZ);
			const FVector3d OuterTopB(FMath::Cos(AngleB) * OuterTopRadius, FMath::Sin(AngleB) * OuterTopRadius, TopZ);
			AppendClippedSurfaceTriangle(Mesh, ChunkIndex, Z,
				OuterBottomA, OuterBottomB, OuterTopB, OuterNormal, 0);
			AppendClippedSurfaceTriangle(Mesh, ChunkIndex, Z,
				OuterBottomA, OuterTopB, OuterTopA, OuterNormal, 0);

			if (InnerBottomRadius > KINDA_SMALL_NUMBER || InnerTopRadius > KINDA_SMALL_NUMBER)
			{
				const FVector3d InnerBottomA(FMath::Cos(AngleA) * InnerBottomRadius, FMath::Sin(AngleA) * InnerBottomRadius, BottomZ);
				const FVector3d InnerBottomB(FMath::Cos(AngleB) * InnerBottomRadius, FMath::Sin(AngleB) * InnerBottomRadius, BottomZ);
				const FVector3d InnerTopA(FMath::Cos(AngleA) * InnerTopRadius, FMath::Sin(AngleA) * InnerTopRadius, TopZ);
				const FVector3d InnerTopB(FMath::Cos(AngleB) * InnerTopRadius, FMath::Sin(AngleB) * InnerTopRadius, TopZ);
				AppendClippedSurfaceTriangle(Mesh, ChunkIndex, Z,
					InnerBottomA, InnerTopB, InnerBottomB, InnerNormal, 0);
				AppendClippedSurfaceTriangle(Mesh, ChunkIndex, Z,
					InnerBottomA, InnerTopA, InnerTopB, InnerNormal, 0);
			}
		}
	}

	AppendAnnularSurface(Mesh, ChunkIndex, 0, 0.0f,
		GetConeInnerRadiusAtHeight(0.0f), GetConeOuterRadiusAtHeight(0.0f), false, 0);
	AppendAnnularSurface(Mesh, ChunkIndex, GridHeightBlocks - 1, TotalHeight,
		GetConeInnerRadiusAtHeight(TotalHeight), GetConeOuterRadiusAtHeight(TotalHeight), true, 0);
}

void AChunkedDestructibleConeStructure::AppendPlatformSurfaces(
	FDynamicMesh3& Mesh,
	int32 ChunkIndex) const
{
	if (!bGeneratePlatforms || PlatformCount <= 0 || ChunkCount.X <= 0)
	{
		return;
	}
	const int32 SegmentCount = GetEffectiveCircularSegments();
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinLogicalZ = ChunkY * SafeChunkSide;
	const int32 MaxLogicalZ = FMath::Min(MinLogicalZ + SafeChunkSide, GridHeightBlocks);
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	for (int32 PlatformIndex = 0; PlatformIndex < PlatformCount; ++PlatformIndex)
	{
		const int32 PlatformBottomZ = LowestPlatformHeightBlocks +
			PlatformIndex * PlatformVerticalSpacingBlocks;
		if (PlatformBottomZ >= GridHeightBlocks)
		{
			break;
		}
		const int32 PlatformTopZ = FMath::Min(
			PlatformBottomZ + PlatformThicknessBlocks - 1, GridHeightBlocks - 1);
		if (PlatformTopZ < MinLogicalZ || PlatformBottomZ >= MaxLogicalZ)
		{
			continue;
		}
		const float PlatformReferenceZ = BlockDimensions.Z * 0.5f + PlatformBottomZ * PitchZ;
		const float InnerRadius = GetConeInnerRadiusAtHeight(PlatformReferenceZ);
		const float OuterRadius = GetConeOuterRadiusAtHeight(PlatformReferenceZ) +
			PlatformOverhangBlocks * CachedRadialBlockUnit;

		for (int32 Z = FMath::Max(PlatformBottomZ, MinLogicalZ);
			Z <= FMath::Min(PlatformTopZ, MaxLogicalZ - 1); ++Z)
		{
			const float BottomZ = Z * PitchZ;
			const float TopZ = BottomZ + BlockDimensions.Z;
			const bool bSeparateLayer = BlockSpacing.Z > ConeSpacingTolerance;
			if (Z == PlatformBottomZ || bSeparateLayer)
			{
				AppendAnnularSurface(Mesh, ChunkIndex, Z, BottomZ,
					InnerRadius, OuterRadius, false, 1);
			}
			if (Z == PlatformTopZ || bSeparateLayer)
			{
				AppendAnnularSurface(Mesh, ChunkIndex, Z, TopZ,
					InnerRadius, OuterRadius, true, 1);
			}

			for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
			{
				const double AngleA = UE_TWO_PI * SegmentIndex / SegmentCount;
				const double AngleB = UE_TWO_PI * (SegmentIndex + 1) / SegmentCount;
				const double MidAngle = (AngleA + AngleB) * 0.5;
				const FVector3f OuterNormal(FMath::Cos(MidAngle), FMath::Sin(MidAngle), 0.0f);
				const FVector3f InnerNormal = -OuterNormal;
				const FVector3d OuterBottomA(FMath::Cos(AngleA) * OuterRadius, FMath::Sin(AngleA) * OuterRadius, BottomZ);
				const FVector3d OuterBottomB(FMath::Cos(AngleB) * OuterRadius, FMath::Sin(AngleB) * OuterRadius, BottomZ);
				const FVector3d OuterTopA(FMath::Cos(AngleA) * OuterRadius, FMath::Sin(AngleA) * OuterRadius, TopZ);
				const FVector3d OuterTopB(FMath::Cos(AngleB) * OuterRadius, FMath::Sin(AngleB) * OuterRadius, TopZ);
				AppendClippedSurfaceTriangle(Mesh, ChunkIndex, Z,
					OuterBottomA, OuterBottomB, OuterTopB, OuterNormal, 1);
				AppendClippedSurfaceTriangle(Mesh, ChunkIndex, Z,
					OuterBottomA, OuterTopB, OuterTopA, OuterNormal, 1);

				if (InnerRadius > KINDA_SMALL_NUMBER)
				{
					const FVector3d InnerBottomA(FMath::Cos(AngleA) * InnerRadius, FMath::Sin(AngleA) * InnerRadius, BottomZ);
					const FVector3d InnerBottomB(FMath::Cos(AngleB) * InnerRadius, FMath::Sin(AngleB) * InnerRadius, BottomZ);
					const FVector3d InnerTopA(FMath::Cos(AngleA) * InnerRadius, FMath::Sin(AngleA) * InnerRadius, TopZ);
					const FVector3d InnerTopB(FMath::Cos(AngleB) * InnerRadius, FMath::Sin(AngleB) * InnerRadius, TopZ);
					AppendClippedSurfaceTriangle(Mesh, ChunkIndex, Z,
						InnerBottomA, InnerTopB, InnerBottomB, InnerNormal, 1);
					AppendClippedSurfaceTriangle(Mesh, ChunkIndex, Z,
						InnerBottomA, InnerTopA, InnerTopB, InnerNormal, 1);
				}
			}
		}
	}
}

bool AChunkedDestructibleConeStructure::ShouldAppendPlanarCutFace(
	int32 X,
	int32 Y,
	int32 Z) const
{
	const FIntPoint Neighbor = EncodeCell(X, Y, Z);
	if (Neighbor == FIntPoint::NoneValue)
	{
		return false;
	}
	if (IsEntranceCell(X, Y, Z))
	{
		return true;
	}
	return false;
}

void AChunkedDestructibleConeStructure::AppendDestroyedCellCutFaces(
	FDynamicMesh3& Mesh,
	FIntPoint Cell) const
{
	if (!Cells.IsValidIndex(ToCellIndex(Cell)))
	{
		return;
	}
	const FFloorCellData& CellData = Cells[ToCellIndex(Cell)];
	if (!CellData.bBelongsToGeneratedShape || CellData.bExists)
	{
		return;
	}

	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return;
	}

	const FVector Center = GetCellLocalCenter(Cell);
	const double MinX = Center.X - BlockDimensions.X * 0.5;
	const double MaxX = Center.X + BlockDimensions.X * 0.5;
	const double MinY = Center.Y - BlockDimensions.Y * 0.5;
	const double MaxY = Center.Y + BlockDimensions.Y * 0.5;
	const double MinZ = Center.Z - BlockDimensions.Z * 0.5;
	const double MaxZ = Center.Z + BlockDimensions.Z * 0.5;
	const int32 MaterialIndex = CellData.MaterialIndex;

	if (BlockSpacing.X <= ConeSpacingTolerance && CellExists3D(X - 1, Y, Z))
	{
		AppendMeshQuad(Mesh,
			FVector3d(MinX, MaxY, MinZ), FVector3d(MinX, MaxY, MaxZ),
			FVector3d(MinX, MinY, MaxZ), FVector3d(MinX, MinY, MinZ),
			FVector3f::UnitX(), MaterialIndex, BlockDimensions.Z, BlockDimensions.Y, UVWorldUnits);
	}
	if (BlockSpacing.X <= ConeSpacingTolerance && CellExists3D(X + 1, Y, Z))
	{
		AppendMeshQuad(Mesh,
			FVector3d(MaxX, MinY, MinZ), FVector3d(MaxX, MinY, MaxZ),
			FVector3d(MaxX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MinZ),
			-FVector3f::UnitX(), MaterialIndex, BlockDimensions.Z, BlockDimensions.Y, UVWorldUnits);
	}
	if (BlockSpacing.Y <= ConeSpacingTolerance && CellExists3D(X, Y - 1, Z))
	{
		AppendMeshQuad(Mesh,
			FVector3d(MinX, MinY, MinZ), FVector3d(MinX, MinY, MaxZ),
			FVector3d(MaxX, MinY, MaxZ), FVector3d(MaxX, MinY, MinZ),
			FVector3f::UnitY(), MaterialIndex, BlockDimensions.Z, BlockDimensions.X, UVWorldUnits);
	}
	if (BlockSpacing.Y <= ConeSpacingTolerance && CellExists3D(X, Y + 1, Z))
	{
		AppendMeshQuad(Mesh,
			FVector3d(MaxX, MaxY, MinZ), FVector3d(MaxX, MaxY, MaxZ),
			FVector3d(MinX, MaxY, MaxZ), FVector3d(MinX, MaxY, MinZ),
			-FVector3f::UnitY(), MaterialIndex, BlockDimensions.Z, BlockDimensions.X, UVWorldUnits);
	}
	if (BlockSpacing.Z <= ConeSpacingTolerance && CellExists3D(X, Y, Z - 1))
	{
		AppendMeshQuad(Mesh,
			FVector3d(MinX, MinY, MinZ), FVector3d(MaxX, MinY, MinZ),
			FVector3d(MaxX, MaxY, MinZ), FVector3d(MinX, MaxY, MinZ),
			FVector3f::UnitZ(), MaterialIndex, BlockDimensions.X, BlockDimensions.Y, UVWorldUnits);
	}
	if (BlockSpacing.Z <= ConeSpacingTolerance && CellExists3D(X, Y, Z + 1))
	{
		AppendMeshQuad(Mesh,
			FVector3d(MinX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MaxZ),
			FVector3d(MaxX, MinY, MaxZ), FVector3d(MinX, MinY, MaxZ),
			-FVector3f::UnitZ(), MaterialIndex, BlockDimensions.X, BlockDimensions.Y, UVWorldUnits);
	}
}

void AChunkedDestructibleConeStructure::BuildChunkMesh(
	int32 ChunkIndex,
	FDynamicMesh3& OutMesh) const
{
	OutMesh.EnableVertexNormals(FVector3f::UnitZ());
	OutMesh.EnableAttributes();
	OutMesh.Attributes()->SetNumUVLayers(1);
	OutMesh.Attributes()->EnableMaterialID();

	AppendConeSurfaces(OutMesh, ChunkIndex);
	AppendPlatformSurfaces(OutMesh, ChunkIndex);

	// Only cell gaps, doorway reveals and newly exposed destruction cuts remain planar. The intact
	// exterior is emitted above from exact circular and conical surfaces, eliminating voxel steps.
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
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
				AppendDestroyedCellCutFaces(OutMesh, Cell);
				continue;
			}
			int32 X;
			int32 Y;
			int32 DecodedZ;
			if (!DecodeCell(Cell, X, Y, DecodedZ))
			{
				continue;
			}
			const FVector Center = GetCellLocalCenter(Cell);
			const double MinX = Center.X - BlockDimensions.X * 0.5;
			const double MaxX = Center.X + BlockDimensions.X * 0.5;
			const double MinY = Center.Y - BlockDimensions.Y * 0.5;
			const double MaxY = Center.Y + BlockDimensions.Y * 0.5;
			const double MinCellZ = Center.Z - BlockDimensions.Z * 0.5;
			const double MaxCellZ = Center.Z + BlockDimensions.Z * 0.5;
			const int32 MaterialIndex = CellData.MaterialIndex;
			auto NeedsFace = [this](int32 NeighborX, int32 NeighborY, int32 NeighborZ, bool bAxisHasGap)
			{
				return ShouldAppendPlanarCutFace(NeighborX, NeighborY, NeighborZ) ||
					(bAxisHasGap && CellExists3D(NeighborX, NeighborY, NeighborZ));
			};

			if (NeedsFace(X - 1, Y, DecodedZ, BlockSpacing.X > ConeSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MinX, MinY, MinCellZ), FVector3d(MinX, MinY, MaxCellZ),
					FVector3d(MinX, MaxY, MaxCellZ), FVector3d(MinX, MaxY, MinCellZ),
					-FVector3f::UnitX(), MaterialIndex, BlockDimensions.Z, BlockDimensions.Y, UVWorldUnits);
			}
			if (NeedsFace(X + 1, Y, DecodedZ, BlockSpacing.X > ConeSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MaxX, MaxY, MinCellZ), FVector3d(MaxX, MaxY, MaxCellZ),
					FVector3d(MaxX, MinY, MaxCellZ), FVector3d(MaxX, MinY, MinCellZ),
					FVector3f::UnitX(), MaterialIndex, BlockDimensions.Z, BlockDimensions.Y, UVWorldUnits);
			}
			if (NeedsFace(X, Y - 1, DecodedZ, BlockSpacing.Y > ConeSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MaxX, MinY, MinCellZ), FVector3d(MaxX, MinY, MaxCellZ),
					FVector3d(MinX, MinY, MaxCellZ), FVector3d(MinX, MinY, MinCellZ),
					-FVector3f::UnitY(), MaterialIndex, BlockDimensions.Z, BlockDimensions.X, UVWorldUnits);
			}
			if (NeedsFace(X, Y + 1, DecodedZ, BlockSpacing.Y > ConeSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MinX, MaxY, MinCellZ), FVector3d(MinX, MaxY, MaxCellZ),
					FVector3d(MaxX, MaxY, MaxCellZ), FVector3d(MaxX, MaxY, MinCellZ),
					FVector3f::UnitY(), MaterialIndex, BlockDimensions.Z, BlockDimensions.X, UVWorldUnits);
			}
			if (NeedsFace(X, Y, DecodedZ - 1, BlockSpacing.Z > ConeSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MinX, MaxY, MinCellZ), FVector3d(MaxX, MaxY, MinCellZ),
					FVector3d(MaxX, MinY, MinCellZ), FVector3d(MinX, MinY, MinCellZ),
					-FVector3f::UnitZ(), MaterialIndex, BlockDimensions.X, BlockDimensions.Y, UVWorldUnits);
			}
			if (NeedsFace(X, Y, DecodedZ + 1, BlockSpacing.Z > ConeSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MinX, MinY, MaxCellZ), FVector3d(MaxX, MinY, MaxCellZ),
					FVector3d(MaxX, MaxY, MaxCellZ), FVector3d(MinX, MaxY, MaxCellZ),
					FVector3f::UnitZ(), MaterialIndex, BlockDimensions.X, BlockDimensions.Y, UVWorldUnits);
			}
		}
	}
}

void AChunkedDestructibleConeStructure::BuildChunkSimpleCollision(
	int32 ChunkIndex,
	FKAggregateGeom& OutCollision) const
{
	OutCollision.EmptyElements();
	if (!bGenerateCollision || CanUseSmoothCollisionForChunk(ChunkIndex) || ChunkCount.X <= 0)
	{
		return;
	}
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinEncodedX = ChunkX * SafeChunkSide;
	const int32 MinZ = ChunkY * SafeChunkSide;
	const int32 MaxEncodedX = FMath::Min(MinEncodedX + SafeChunkSide, CellCount.X);
	const int32 MaxZ = FMath::Min(MinZ + SafeChunkSide, CellCount.Y);
	TSet<int32> OccupiedPhysicalCells;
	TArray<FIntVector> SeedCells;
	auto PhysicalKey = [this](int32 X, int32 Y, int32 Z)
	{
		return X + Y * GridDiameterBlocks +
			Z * GridDiameterBlocks * GridDiameterBlocks;
	};
	for (int32 Z = MinZ; Z < MaxZ; ++Z)
	{
		for (int32 EncodedX = MinEncodedX; EncodedX < MaxEncodedX; ++EncodedX)
		{
			const FIntPoint Cell(EncodedX, Z);
			if (!Cells[ToCellIndex(Cell)].bExists)
			{
				continue;
			}
			int32 X;
			int32 Y;
			int32 DecodedZ;
			if (DecodeCell(Cell, X, Y, DecodedZ))
			{
				OccupiedPhysicalCells.Add(PhysicalKey(X, Y, DecodedZ));
				SeedCells.Emplace(X, Y, DecodedZ);
			}
		}
	}

	SeedCells.Sort([](const FIntVector& A, const FIntVector& B)
	{
		if (A.Z != B.Z)
		{
			return A.Z < B.Z;
		}
		if (A.Y != B.Y)
		{
			return A.Y < B.Y;
		}
		return A.X < B.X;
	});
	TSet<int32> VisitedPhysicalCells;
	auto CanMergeCell = [this, &OccupiedPhysicalCells, &VisitedPhysicalCells, &PhysicalKey](
		int32 X, int32 Y, int32 Z)
	{
		if (X < 0 || X >= GridDiameterBlocks ||
			Y < 0 || Y >= GridDiameterBlocks ||
			Z < 0 || Z >= GridHeightBlocks)
		{
			return false;
		}
		const int32 Key = PhysicalKey(X, Y, Z);
		return OccupiedPhysicalCells.Contains(Key) && !VisitedPhysicalCells.Contains(Key);
	};
	const bool bCanMergeX = BlockSpacing.X <= ConeSpacingTolerance;
	const bool bCanMergeY = BlockSpacing.Y <= ConeSpacingTolerance;
	const bool bCanMergeZ = BlockSpacing.Z <= ConeSpacingTolerance;

	for (const FIntVector& Seed : SeedCells)
	{
		if (!CanMergeCell(Seed.X, Seed.Y, Seed.Z))
		{
			continue;
		}

		int32 MergeX = 1;
		if (bCanMergeX)
		{
			while (CanMergeCell(Seed.X + MergeX, Seed.Y, Seed.Z))
			{
				++MergeX;
			}
		}

		int32 MergeY = 1;
		if (bCanMergeY)
		{
			bool bCanGrow = true;
			while (bCanGrow)
			{
				for (int32 OffsetX = 0; OffsetX < MergeX; ++OffsetX)
				{
					if (!CanMergeCell(Seed.X + OffsetX, Seed.Y + MergeY, Seed.Z))
					{
						bCanGrow = false;
						break;
					}
				}
				if (bCanGrow)
				{
					++MergeY;
				}
			}
		}

		int32 MergeZ = 1;
		if (bCanMergeZ)
		{
			bool bCanGrow = true;
			while (bCanGrow)
			{
				for (int32 OffsetY = 0; OffsetY < MergeY && bCanGrow; ++OffsetY)
				{
					for (int32 OffsetX = 0; OffsetX < MergeX; ++OffsetX)
					{
						if (!CanMergeCell(
							Seed.X + OffsetX,
							Seed.Y + OffsetY,
							Seed.Z + MergeZ))
						{
							bCanGrow = false;
							break;
						}
					}
				}
				if (bCanGrow)
				{
					++MergeZ;
				}
			}
		}

		for (int32 OffsetZ = 0; OffsetZ < MergeZ; ++OffsetZ)
		{
			for (int32 OffsetY = 0; OffsetY < MergeY; ++OffsetY)
			{
				for (int32 OffsetX = 0; OffsetX < MergeX; ++OffsetX)
				{
					VisitedPhysicalCells.Add(PhysicalKey(
						Seed.X + OffsetX,
						Seed.Y + OffsetY,
						Seed.Z + OffsetZ));
				}
			}
		}

		const FVector FirstCenter = GetPhysicalCellLocalCenter(Seed.X, Seed.Y, Seed.Z);
		const FVector LastCenter = GetPhysicalCellLocalCenter(
			Seed.X + MergeX - 1,
			Seed.Y + MergeY - 1,
			Seed.Z + MergeZ - 1);
		const FVector BoxMinimum = FirstCenter - BlockDimensions * 0.5;
		const FVector BoxMaximum = LastCenter + BlockDimensions * 0.5;
		const FVector BoxSize = BoxMaximum - BoxMinimum;
		FKBoxElem& Box = OutCollision.BoxElems.Emplace_GetRef(
			static_cast<float>(BoxSize.X),
			static_cast<float>(BoxSize.Y),
			static_cast<float>(BoxSize.Z));
		Box.Center = (BoxMinimum + BoxMaximum) * 0.5;
	}
}

FTransform AChunkedDestructibleConeStructure::GetCellFlashTransform(FIntPoint Cell) const
{
	return FTransform(
		FQuat::Identity,
		GetCellLocalCenter(Cell),
		BlockDimensions / 100.0 * FMath::Max(FlashScale, 0.01f));
}

bool AChunkedDestructibleConeStructure::ResolveExistingCellFromHit(
	const FHitResult& Hit,
	FIntPoint& OutCell) const
{
	OutCell = FIntPoint::NoneValue;
	const FVector InteriorPoint = GetInteriorImpactPoint(Hit);
	FIntPoint Candidate;
	if (WorldLocationToCell(InteriorPoint, Candidate) &&
		Cells.IsValidIndex(ToCellIndex(Candidate)) && Cells[ToCellIndex(Candidate)].bExists)
	{
		OutCell = Candidate;
		return true;
	}

	// Smooth collision triangles are clipped to backing cells, but an exact hit on a grid edge can
	// numerically resolve into the empty neighbour. Search the local 3D neighbourhood and choose
	// the closest intact cell AABB; the hit is already known to belong to this Actor.
	const FVector LocalImpact = GetActorTransform().InverseTransformPosition(
		Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint);
	const float PitchX = BlockDimensions.X + BlockSpacing.X;
	const float PitchY = BlockDimensions.Y + BlockSpacing.Y;
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const int32 BaseX = FMath::FloorToInt((LocalImpact.X - ConeGridLocalMin.X) / PitchX);
	const int32 BaseY = FMath::FloorToInt((LocalImpact.Y - ConeGridLocalMin.Y) / PitchY);
	const int32 BaseZ = FMath::FloorToInt(LocalImpact.Z / PitchZ);
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (int32 Z = BaseZ - 2; Z <= BaseZ + 2; ++Z)
	{
		for (int32 Y = BaseY - 2; Y <= BaseY + 2; ++Y)
		{
			for (int32 X = BaseX - 2; X <= BaseX + 2; ++X)
			{
				const FIntPoint TestCell = EncodeCell(X, Y, Z);
				if (TestCell == FIntPoint::NoneValue || !Cells.IsValidIndex(ToCellIndex(TestCell)) ||
					!Cells[ToCellIndex(TestCell)].bExists)
				{
					continue;
				}
				const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
				const FVector HalfSize = BlockDimensions * 0.5;
				const FVector DistanceToBox(
					FMath::Max(FMath::Abs(LocalImpact.X - Center.X) - HalfSize.X, 0.0),
					FMath::Max(FMath::Abs(LocalImpact.Y - Center.Y) - HalfSize.Y, 0.0),
					FMath::Max(FMath::Abs(LocalImpact.Z - Center.Z) - HalfSize.Z, 0.0));
				const double DistanceSquared = DistanceToBox.SquaredLength();
				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					OutCell = TestCell;
				}
			}
		}
	}
	return OutCell != FIntPoint::NoneValue;
}

int32 AChunkedDestructibleConeStructure::SelectCellsInRadius(
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
		const FVector ScaledDelta(
			LocalDelta.X * Scale.X, LocalDelta.Y * Scale.Y, LocalDelta.Z * Scale.Z);
		float Distance = 0.0f;
		switch (Shape)
		{
		case EChunkedFloorBlastShape::Square:
			Distance = FMath::Max3(
				FMath::Abs(ScaledDelta.X), FMath::Abs(ScaledDelta.Y), FMath::Abs(ScaledDelta.Z));
			break;
		case EChunkedFloorBlastShape::Diamond:
			Distance = FMath::Abs(ScaledDelta.X) + FMath::Abs(ScaledDelta.Y) +
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

void AChunkedDestructibleConeStructure::MarkCellChunksDirty(FIntPoint Cell)
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return;
	}
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	auto MarkEncodedCellChunk = [this, SafeChunkSide](FIntPoint EncodedCell)
	{
		if (EncodedCell == FIntPoint::NoneValue || ChunkCount.X <= 0)
		{
			return;
		}
		const int32 ChunkX = EncodedCell.X / SafeChunkSide;
		const int32 ChunkY = EncodedCell.Y / SafeChunkSide;
		DirtyChunkIndices.Add(ChunkX + ChunkY * ChunkCount.X);
	};
	MarkEncodedCellChunk(Cell);

	struct FNeighborDependency
	{
		FIntVector Offset;
		bool bHasSpacing;
	};
	const FNeighborDependency Dependencies[] = {
		{ FIntVector(-1, 0, 0), BlockSpacing.X > ConeSpacingTolerance },
		{ FIntVector(1, 0, 0), BlockSpacing.X > ConeSpacingTolerance },
		{ FIntVector(0, -1, 0), BlockSpacing.Y > ConeSpacingTolerance },
		{ FIntVector(0, 1, 0), BlockSpacing.Y > ConeSpacingTolerance },
		{ FIntVector(0, 0, -1), BlockSpacing.Z > ConeSpacingTolerance },
		{ FIntVector(0, 0, 1), BlockSpacing.Z > ConeSpacingTolerance }
	};
	for (const FNeighborDependency& Dependency : Dependencies)
	{
		if (Dependency.bHasSpacing)
		{
			continue;
		}
		const FIntPoint Neighbor = EncodeCell(
			X + Dependency.Offset.X,
			Y + Dependency.Offset.Y,
			Z + Dependency.Offset.Z);
		if (Neighbor != FIntPoint::NoneValue && Cells.IsValidIndex(ToCellIndex(Neighbor)))
		{
			const FFloorCellData& NeighborData = Cells[ToCellIndex(Neighbor)];
			if (NeighborData.bBelongsToGeneratedShape && !NeighborData.bExists)
			{
				MarkEncodedCellChunk(Neighbor);
			}
		}
	}
}

int32 AChunkedDestructibleConeStructure::GetEffectiveConeChunkSideLength() const
{
	// A 128x128 logical chunk can push a highly tessellated cone past DynamicMesh's hard
	// 250k-complex-collision limit. 64 keeps exact collision available for normal chunks.
	return FMath::Clamp(ChunkSideLength, 4, 64);
}

int32 AChunkedDestructibleConeStructure::GetEffectiveCircularSegments() const
{
	return FMath::Min(
		FMath::Clamp(CircularSegments, 32, 512),
		FMath::Clamp(MaxRuntimeCircularSegments, 32, 512));
}

int32 AChunkedDestructibleConeStructure::GetEffectiveDebrisLimit() const
{
	return FMath::Min(
		FMath::Clamp(MaxActiveDebrisPieces, 0, 1024),
		FMath::Clamp(MaxRuntimeDebrisPieces, 0, 1024));
}

bool AChunkedDestructibleConeStructure::CanUseSmoothCollisionForChunk(int32 ChunkIndex) const
{
	if (!bUseSmoothComplexCollision || !ChunkComponents.IsValidIndex(ChunkIndex) ||
		!ChunkComponents[ChunkIndex] || !ChunkComponents[ChunkIndex]->GetDynamicMesh())
	{
		return false;
	}
	return ChunkComponents[ChunkIndex]->GetDynamicMesh()->GetTriangleCount() <=
		FMath::Clamp(MaxSmoothCollisionTrianglesPerChunk, 10000, 240000);
}

UMaterialInterface* AChunkedDestructibleConeStructure::ResolveStructureMaterial(bool bPlatform) const
{
	UMaterialInterface* Material = bPlatform ? PlatformMaterial.Get() : ConeMaterial.Get();
	const int32 PaletteIndex = bPlatform ? PlatformMaterialIndex : ConeMaterialIndex;
	if (!Material && FloorMaterials.IsValidIndex(PaletteIndex))
	{
		Material = FloorMaterials[PaletteIndex].Get();
	}
	if (!Material && bPlatform)
	{
		Material = ResolveStructureMaterial(false);
	}
	return Material ? Material : UMaterial::GetDefaultMaterial(MD_Surface);
}

void AChunkedDestructibleConeStructure::HandleConeCellDestroyed(
	FIntPoint Cell,
	FVector WorldLocation,
	bool bRadialDestruction)
{
	if (bScatterDestroyedPieces)
	{
		SpawnDebrisForCell(Cell);
	}
}

void AChunkedDestructibleConeStructure::SpawnDebrisForCell(FIntPoint Cell)
{
	const int32 EffectiveDebrisLimit = GetEffectiveDebrisLimit();
	if (!DebrisCubeMesh || EffectiveDebrisLimit <= 0 || !GetWorld())
	{
		return;
	}
	ActiveDebrisComponents.RemoveAll(
		[](const TWeakObjectPtr<UStaticMeshComponent>& Component)
		{
			return !Component.IsValid();
		});
	if (ActiveDebrisComponents.Num() >= EffectiveDebrisLimit)
	{
		return;
	}

	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return;
	}
	const FVector LocalCenter = GetPhysicalCellLocalCenter(X, Y, Z);
	const bool bPlatformCell = IsPlatformCell(X, Y, Z);
	UMaterialInterface* DebrisMaterial = ResolveStructureMaterial(bPlatformCell);
	const float MinimumScale = FMath::Clamp(
		static_cast<float>(FMath::Min(DebrisScaleRange.X, DebrisScaleRange.Y)), 0.05f, 1.0f);
	const float MaximumScale = FMath::Clamp(
		static_cast<float>(FMath::Max(DebrisScaleRange.X, DebrisScaleRange.Y)), MinimumScale, 1.0f);
	const int32 SpawnCount = FMath::Min(
		FMath::Clamp(DebrisPiecesPerCell, 1, 8),
		EffectiveDebrisLimit - ActiveDebrisComponents.Num());

	for (int32 PieceIndex = 0; PieceIndex < SpawnCount; ++PieceIndex)
	{
		const float FragmentScale = FMath::FRandRange(MinimumScale, MaximumScale);
		const FVector FragmentSize = BlockDimensions * FragmentScale;
		const FVector LocalOffset(
			FMath::FRandRange(-BlockDimensions.X, BlockDimensions.X) * 0.2f,
			FMath::FRandRange(-BlockDimensions.Y, BlockDimensions.Y) * 0.2f,
			FMath::FRandRange(-BlockDimensions.Z, BlockDimensions.Z) * 0.2f);
		const FVector FragmentWorldLocation = GetActorTransform().TransformPosition(LocalCenter + LocalOffset);
		const FQuat FragmentWorldRotation = GetActorQuat() * FQuat(FRotator(
			FMath::FRandRange(-25.0f, 25.0f),
			FMath::FRandRange(-180.0f, 180.0f),
			FMath::FRandRange(-25.0f, 25.0f)));

		UStaticMeshComponent* Debris = NewObject<UStaticMeshComponent>(this);
		AddInstanceComponent(Debris);
		Debris->SetMobility(EComponentMobility::Movable);
		Debris->SetStaticMesh(DebrisCubeMesh);
		Debris->SetMaterial(0, DebrisMaterial);
		Debris->SetCastShadow(false);
		Debris->SetGenerateOverlapEvents(false);
		Debris->SetCanEverAffectNavigation(false);
		Debris->SetCollisionProfileName(TEXT("PhysicsActor"));
		Debris->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Debris->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
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
		const float SafeDebrisLifetime = FMath::Max(DebrisLifetime, 0.1f);
		const float SafePhysicsDuration = FMath::Clamp(
			DebrisPhysicsDuration, 0.05f, SafeDebrisLifetime);
		if (SafePhysicsDuration < SafeDebrisLifetime)
		{
			FTimerDelegate SleepDelegate = FTimerDelegate::CreateWeakLambda(
				this,
				[WeakDebris]()
				{
					if (UStaticMeshComponent* Component = WeakDebris.Get())
					{
						Component->SetSimulatePhysics(false);
						Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					}
				});
			FTimerHandle SleepHandle;
			GetWorld()->GetTimerManager().SetTimer(
				SleepHandle, MoveTemp(SleepDelegate), SafePhysicsDuration, false);
		}
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
			SafeDebrisLifetime,
			false);
	}
}

void AChunkedDestructibleConeStructure::ClearDebris()
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
