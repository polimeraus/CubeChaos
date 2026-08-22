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

	bool DoesSurfaceTriangleIntersectBox(
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3d& BoxMinimum,
		const FVector3d& BoxMaximum)
	{
		TArray<FVector3d> Polygon = { A, B, C };
		for (int32 Axis = 0; Axis < 3 && Polygon.Num() >= 3; ++Axis)
		{
			ClipSurfacePolygonToPlane(Polygon, Axis, BoxMinimum[Axis], true);
			ClipSurfacePolygonToPlane(Polygon, Axis, BoxMaximum[Axis], false);
		}
		RemoveDuplicateSurfacePoints(Polygon);
		if (Polygon.Num() < 3)
		{
			return false;
		}

		// A triangle which only touches a cell at an edge or a corner must not make that cell
		// own geometry. Requiring real clipped area also prevents an endpoint from leaking into
		// the vertical layer immediately above or below it.
		for (int32 PointIndex = 1; PointIndex + 1 < Polygon.Num(); ++PointIndex)
		{
			if (FVector3d::CrossProduct(
				Polygon[PointIndex] - Polygon[0],
				Polygon[PointIndex + 1] - Polygon[0]).SquaredLength() >
				FMath::Square(ConeSurfaceTolerance))
			{
				return true;
			}
		}
		return false;
	}

	bool DoesSurfaceQuadIntersectBox(
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3d& D,
		const FVector3d& BoxMinimum,
		const FVector3d& BoxMaximum)
	{
		return DoesSurfaceTriangleIntersectBox(A, B, C, BoxMinimum, BoxMaximum) ||
			DoesSurfaceTriangleIntersectBox(A, C, D, BoxMinimum, BoxMaximum);
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

void AChunkedDestructibleConeStructure::ClearAdditionalRuntimeEffects()
{
	for (const TWeakObjectPtr<UDynamicMeshComponent>& WeakFlash : ActiveSurfaceFlashComponents)
	{
		if (UDynamicMeshComponent* SurfaceFlash = WeakFlash.Get())
		{
			RemoveInstanceComponent(SurfaceFlash);
			SurfaceFlash->DestroyComponent();
		}
	}
	ActiveSurfaceFlashComponents.Reset();
	CurrentSurfaceFlashComponent.Reset();
	CurrentSurfaceFlashCells.Reset();
	CurrentSurfaceFlashTime = -TNumericLimits<double>::Max();
	SurfaceFlashCellFilter.Reset();
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

void AChunkedDestructibleConeStructure::SetPlatformRingWidth(
	float NewRingWidthBlocks,
	bool bPreserveCellState)
{
	PlatformOverhangBlocks = FMath::Clamp(FMath::Abs(NewRingWidthBlocks), 0.0f, 32.0f);
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleConeStructure::SetPlatformStyle(
	EConeStructurePlatformStyle NewStyle,
	bool bPreserveCellState)
{
	PlatformStyle = NewStyle;
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleConeStructure::ConfigureArchedDoors(
	int32 NewDoorCount,
	int32 NewDoorWidthBlocks,
	int32 NewDoorHeightBlocks,
	float NewDoorArchHeightBlocks,
	int32 NewFirstDoorBottomHeightBlocks,
	int32 NewDoorVerticalSpacingBlocks,
	float NewFirstDoorAngleDegrees,
	float NewDoorAngleStepDegrees,
	bool bPreserveCellState)
{
	bGenerateEntrance = NewDoorCount > 0;
	DoorCount = FMath::Clamp(NewDoorCount, 1, 32);
	EntranceWidthBlocks = FMath::Clamp(NewDoorWidthBlocks, 1, 64);
	EntranceHeightBlocks = FMath::Clamp(NewDoorHeightBlocks, 1, ConeHeightBlocks);
	DoorArchHeightBlocks = FMath::Clamp(
		FMath::Abs(NewDoorArchHeightBlocks), 0.0f, static_cast<float>(EntranceHeightBlocks));
	FirstDoorBottomHeightBlocks = FMath::Clamp(
		NewFirstDoorBottomHeightBlocks, 0, ConeHeightBlocks - 1);
	DoorVerticalSpacingBlocks = FMath::Clamp(NewDoorVerticalSpacingBlocks, 1, 128);
	EntranceAngleDegrees = NewFirstDoorAngleDegrees;
	DoorAngleStepDegrees = NewDoorAngleStepDegrees;
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
	if (!ResolveExistingCellFromHit(Hit, Cell))
	{
		return false;
	}

	TArray<FIntPoint, TInlineAllocator<4>> CellsToDestroy;
	CellsToDestroy.Add(Cell);

	// A rising spiral can be several backing cells thick. Removing only the Z layer touched by a
	// side hit leaves the other layer's coplanar outer wall behind, which reads as a detached,
	// paper-thin face rather than a missing volume. Resolve the helical cross-section at the actual
	// impact and remove every vertical backing layer occupied by that same section in one batch.
	if (bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral)
	{
		int32 HitX;
		int32 HitY;
		int32 HitZ;
		if (DecodeCell(Cell, HitX, HitY, HitZ))
		{
			const FVector WorldImpact = Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint;
			const FVector LocalImpact = GetActorTransform().InverseTransformPosition(WorldImpact);
			const float ImpactRadius = FVector2D(LocalImpact.X, LocalImpact.Y).Size();
			const float ImpactAngle = FMath::Atan2(LocalImpact.Y, LocalImpact.X);
			const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
			const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
			const float TotalSpiralAngle = UE_TWO_PI * PlatformSpiralTurns;
			const float StartBottom = GetSpiralBottomHeight(false);
			const float EndBottom = GetSpiralBottomHeight(true);
			const float Thickness = GetSpiralThickness();
			const float InnerOverlap = SpiralWallOverlapBlocks * CachedRadialBlockUnit;
			const float OuterOverhang = PlatformOverhangBlocks * CachedRadialBlockUnit;
			float BaseProgressAngle = FMath::Fmod(
				Direction * (ImpactAngle - StartAngle), UE_TWO_PI);
			if (BaseProgressAngle < 0.0f)
			{
				BaseProgressAngle += UE_TWO_PI;
			}

			float ClosestSpiralBottom = 0.0f;
			float ClosestDistanceSquared = TNumericLimits<float>::Max();
			const int32 RevolutionCount = FMath::CeilToInt(PlatformSpiralTurns);
			for (int32 Revolution = 0; Revolution <= RevolutionCount; ++Revolution)
			{
				const float ProgressAngle = BaseProgressAngle + UE_TWO_PI * Revolution;
				if (ProgressAngle > TotalSpiralAngle + KINDA_SMALL_NUMBER)
				{
					continue;
				}

				const float Alpha = FMath::Clamp(
					ProgressAngle / FMath::Max(TotalSpiralAngle, KINDA_SMALL_NUMBER),
					0.0f,
					1.0f);
				const float SpiralBottom = FMath::Lerp(StartBottom, EndBottom, Alpha);
				const float SpiralTop = SpiralBottom + Thickness;
				const float ConeRadius = GetConeOuterRadiusAtHeight(SpiralTop);
				const float InnerRadius = FMath::Max(ConeRadius - InnerOverlap, 0.0f);
				const float OuterRadius = ConeRadius + OuterOverhang;
				const float RadialDistance = ImpactRadius < InnerRadius
					? InnerRadius - ImpactRadius
					: (ImpactRadius > OuterRadius ? ImpactRadius - OuterRadius : 0.0f);
				const float VerticalDistance = LocalImpact.Z < SpiralBottom
					? SpiralBottom - LocalImpact.Z
					: (LocalImpact.Z > SpiralTop ? LocalImpact.Z - SpiralTop : 0.0f);
				const float DistanceSquared = FMath::Square(RadialDistance) +
					FMath::Square(VerticalDistance);
				if (DistanceSquared < ClosestDistanceSquared)
				{
					ClosestDistanceSquared = DistanceSquared;
					ClosestSpiralBottom = SpiralBottom;
				}
			}

			const float SurfaceTolerance = FMath::Max(ConeSpacingTolerance, 0.1f);
			if (ClosestDistanceSquared <= FMath::Square(SurfaceTolerance))
			{
				const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
				const int32 MinimumZ = FMath::Clamp(
					FMath::FloorToInt((ClosestSpiralBottom + SurfaceTolerance) / PitchZ),
					0,
					GridHeightBlocks - 1);
				const int32 MaximumZ = FMath::Clamp(
					FMath::FloorToInt(
						(ClosestSpiralBottom + Thickness - SurfaceTolerance) / PitchZ),
					0,
					GridHeightBlocks - 1);
				for (int32 Z = MinimumZ; Z <= MaximumZ; ++Z)
				{
					const FIntPoint LayerCell = EncodeCell(HitX, HitY, Z);
					if (LayerCell != FIntPoint::NoneValue &&
						Cells.IsValidIndex(ToCellIndex(LayerCell)) &&
						Cells[ToCellIndex(LayerCell)].bExists &&
						IsSpiralPlatformCell(HitX, HitY, Z))
					{
						CellsToDestroy.AddUnique(LayerCell);
					}
				}
			}
		}
	}

	bool bQueuedAnyCell = false;
	for (const FIntPoint CellToDestroy : CellsToDestroy)
	{
		bQueuedAnyCell |= QueueCellDestruction(ToCellIndex(CellToDestroy), 0.0f, false);
	}
	if (bQueuedAnyCell)
	{
		ProcessQueuedDestructions(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	}
	return bQueuedAnyCell;
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
	MaxHorizontalBlockAspectRatio = bUseSquareHorizontalDestructionCells
		? 1.0f
		: FMath::Clamp(MaxHorizontalBlockAspectRatio, 1.0f, 16.0f);
	const double MinimumHorizontalDimension = FMath::Min(BlockDimensions.X, BlockDimensions.Y);
	const double MaximumHorizontalDimension =
		MinimumHorizontalDimension * MaxHorizontalBlockAspectRatio;
	BlockDimensions.X = FMath::Min(BlockDimensions.X, MaximumHorizontalDimension);
	BlockDimensions.Y = FMath::Min(BlockDimensions.Y, MaximumHorizontalDimension);
	BlockSpacing = FVector(
		FMath::Max(BlockSpacing.X, 0.0),
		FMath::Max(BlockSpacing.Y, 0.0),
		FMath::Max(BlockSpacing.Z, 0.0));
	PlatformCount = FMath::Clamp(PlatformCount, 0, 32);
	LowestPlatformHeightBlocks = FMath::Clamp(LowestPlatformHeightBlocks, 0, ConeHeightBlocks - 1);
	PlatformVerticalSpacingBlocks = FMath::Clamp(PlatformVerticalSpacingBlocks, 1, 128);
	PlatformOverhangBlocks = FMath::Clamp(FMath::Abs(PlatformOverhangBlocks), 0.0f, 32.0f);
	PlatformThicknessBlocks = FMath::Clamp(PlatformThicknessBlocks, 1, 16);
	SpiralEndHeightBlocks = FMath::Clamp(SpiralEndHeightBlocks, 0, ConeHeightBlocks - 1);
	PlatformSpiralTurns = FMath::Clamp(FMath::Abs(PlatformSpiralTurns), 0.25f, 32.0f);
	SpiralSegmentsPerTurn = FMath::Clamp(SpiralSegmentsPerTurn, 16, 256);
	SpiralWallOverlapBlocks = FMath::Clamp(FMath::Abs(SpiralWallOverlapBlocks), 0.0f, 4.0f);
	DoorCount = FMath::Clamp(DoorCount, 1, 32);
	EntranceWidthBlocks = FMath::Clamp(EntranceWidthBlocks, 1, 64);
	EntranceHeightBlocks = FMath::Clamp(EntranceHeightBlocks, 1, ConeHeightBlocks);
	DoorArchHeightBlocks = FMath::Clamp(
		FMath::Abs(DoorArchHeightBlocks), 0.0f, static_cast<float>(EntranceHeightBlocks));
	FirstDoorBottomHeightBlocks = FMath::Clamp(FirstDoorBottomHeightBlocks, 0, ConeHeightBlocks - 1);
	DoorVerticalSpacingBlocks = FMath::Clamp(DoorVerticalSpacingBlocks, 1, 128);
	CircularSegments = FMath::Clamp(CircularSegments, 32, 512);
	MaxRuntimeCircularSegments = FMath::Clamp(MaxRuntimeCircularSegments, 32, 512);
	SurfaceGapSealFraction = FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);

	const float PitchX = BlockDimensions.X + BlockSpacing.X;
	const float PitchY = BlockDimensions.Y + BlockSpacing.Y;
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float NewRadialBlockUnit = FMath::Max(FMath::Min(PitchX, PitchY), MinimumConeDimension);
	const bool bHasSecondaryStructure = bGeneratePlatforms &&
		(PlatformStyle == EConeStructurePlatformStyle::RisingSpiral || PlatformCount > 0);
	const float MaximumRadius = (BottomRadiusBlocks +
		(bHasSecondaryStructure ? PlatformOverhangBlocks : 0.0f)) * NewRadialBlockUnit;
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
		if (!bGeneratePlatforms)
		{
			return false;
		}
		if (PlatformStyle == EConeStructurePlatformStyle::CircularRings)
		{
			if (PlatformCount <= 0)
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
		}
		if (PlatformOverhangBlocks <= KINDA_SMALL_NUMBER &&
			SpiralWallOverlapBlocks <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		const float SpiralThickness = BlockDimensions.Z * PlatformThicknessBlocks +
			BlockSpacing.Z * FMath::Max(PlatformThicknessBlocks - 1, 0);
		const float MaximumSpiralBottom = FMath::Max(TotalConeHeight - SpiralThickness, 0.0f);
		const float StartBottom = FMath::Clamp(
			LowestPlatformHeightBlocks * PitchZ, 0.0f, MaximumSpiralBottom);
		const float EndBottom = FMath::Clamp(
			SpiralEndHeightBlocks * PitchZ, 0.0f, MaximumSpiralBottom);
		const float CellBottom = Z * PitchZ;
		const float CellTop = CellBottom + BlockDimensions.Z;
		const FVector Center = GetPhysicalCenter(X, Y, Z);
		const FVector3d CellMinimum = FVector3d(Center - BlockDimensions * 0.5f);
		const FVector3d CellMaximum = FVector3d(Center + BlockDimensions * 0.5f);
		const float TotalSpiralAngle = UE_TWO_PI * PlatformSpiralTurns;
		const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
		const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
		const float RadialOverhang = PlatformOverhangBlocks * NewRadialBlockUnit;
		const float WallOverlap = SpiralWallOverlapBlocks * NewRadialBlockUnit;
		auto EndCapIntersectsCell = [&](bool bEnd)
		{
			const float Alpha = bEnd ? 1.0f : 0.0f;
			const float Bottom = FMath::Lerp(StartBottom, EndBottom, Alpha);
			const float Top = Bottom + SpiralThickness;
			const double CapAngle = StartAngle + Direction * TotalSpiralAngle * Alpha;
			const double CosAngle = FMath::Cos(CapAngle);
			const double SinAngle = FMath::Sin(CapAngle);
			const float ConeRadius = GetOuterRadius(Top);
			const float InnerRadius = FMath::Max(ConeRadius - WallOverlap, 0.0f);
			const float OuterRadius = ConeRadius + RadialOverhang;
			const FVector3d InnerBottom(CosAngle * InnerRadius, SinAngle * InnerRadius, Bottom);
			const FVector3d OuterBottom(CosAngle * OuterRadius, SinAngle * OuterRadius, Bottom);
			const FVector3d OuterTop(CosAngle * OuterRadius, SinAngle * OuterRadius, Top);
			const FVector3d InnerTop(CosAngle * InnerRadius, SinAngle * InnerRadius, Top);
			return DoesSurfaceQuadIntersectBox(
				InnerBottom, OuterBottom, OuterTop, InnerTop, CellMinimum, CellMaximum);
		};
		if (EndCapIntersectsCell(false) || EndCapIntersectsCell(true))
		{
			return true;
		}

		const float CenterRadius = FVector2D(Center.X, Center.Y).Size();
		const float HalfDiagonal = FVector2D(BlockDimensions.X, BlockDimensions.Y).Size() * 0.5f;
		const float CellAngularHalfWidth = FMath::Atan2(
			HalfDiagonal, FMath::Max(CenterRadius - HalfDiagonal, NewRadialBlockUnit * 0.25f));
		const float HeightPerRadian = FMath::Abs(EndBottom - StartBottom) /
			FMath::Max(TotalSpiralAngle, KINDA_SMALL_NUMBER);
		const float HeightMargin = HeightPerRadian *
			(CellAngularHalfWidth + UE_PI / SpiralSegmentsPerTurn) + ConeSpacingTolerance;
		const float Angle = FMath::Atan2(Center.Y, Center.X);
		float BaseProgressAngle = FMath::Fmod(Direction * (Angle - StartAngle), UE_TWO_PI);
		if (BaseProgressAngle < 0.0f)
		{
			BaseProgressAngle += UE_TWO_PI;
		}
		float MinimumRadius;
		float MaximumCellRadius;
		GetRadialRange(X, Y, MinimumRadius, MaximumCellRadius);
		const int32 RevolutionCount = FMath::CeilToInt(PlatformSpiralTurns);
		for (int32 Revolution = 0; Revolution <= RevolutionCount; ++Revolution)
		{
			const float ProgressAngle = BaseProgressAngle + UE_TWO_PI * Revolution;
			if (ProgressAngle > TotalSpiralAngle + CellAngularHalfWidth)
			{
				continue;
			}
			const float Alpha = FMath::Clamp(ProgressAngle / TotalSpiralAngle, 0.0f, 1.0f);
			const float SpiralBottom = FMath::Lerp(StartBottom, EndBottom, Alpha);
			if (CellTop < SpiralBottom - HeightMargin ||
				CellBottom > SpiralBottom + SpiralThickness + HeightMargin)
			{
				continue;
			}
			const float ReferenceZ = SpiralBottom + SpiralThickness;
			const float ConeRadius = GetOuterRadius(ReferenceZ);
			const float InnerRadius = FMath::Max(
				ConeRadius - SpiralWallOverlapBlocks * NewRadialBlockUnit, 0.0f);
			const float OuterRadius = ConeRadius + PlatformOverhangBlocks * NewRadialBlockUnit;
			if (MinimumRadius <= OuterRadius + ConeSpacingTolerance &&
				MaximumCellRadius >= InnerRadius - ConeSpacingTolerance)
			{
				return true;
			}
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
	return MinimumRadius <= OuterRadius + ConeSpacingTolerance &&
		MaximumRadius >= InnerRadius - ConeSpacingTolerance;
}

bool AChunkedDestructibleConeStructure::IsPlatformCell(int32 X, int32 Y, int32 Z) const
{
	if (!bGeneratePlatforms)
	{
		return false;
	}
	return PlatformStyle == EConeStructurePlatformStyle::RisingSpiral
		? IsSpiralPlatformCell(X, Y, Z)
		: IsCircularPlatformCell(X, Y, Z);
}

bool AChunkedDestructibleConeStructure::IsCircularPlatformCell(int32 X, int32 Y, int32 Z) const
{
	if (PlatformCount <= 0)
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

float AChunkedDestructibleConeStructure::GetSpiralThickness() const
{
	return BlockDimensions.Z * PlatformThicknessBlocks +
		BlockSpacing.Z * FMath::Max(PlatformThicknessBlocks - 1, 0);
}

float AChunkedDestructibleConeStructure::GetSpiralBottomHeight(bool bEnd) const
{
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const float MaximumBottom = FMath::Max(TotalHeight - GetSpiralThickness(), 0.0f);
	const int32 HeightBlocks = bEnd ? SpiralEndHeightBlocks : LowestPlatformHeightBlocks;
	return FMath::Clamp(HeightBlocks * PitchZ, 0.0f, MaximumBottom);
}

bool AChunkedDestructibleConeStructure::IsSpiralPlatformCell(int32 X, int32 Y, int32 Z) const
{
	if (PlatformOverhangBlocks <= KINDA_SMALL_NUMBER &&
		SpiralWallOverlapBlocks <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float SpiralThickness = GetSpiralThickness();
	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const float CellBottom = Z * PitchZ;
	const float CellTop = CellBottom + BlockDimensions.Z;
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	const FVector3d CellMinimum = FVector3d(Center - BlockDimensions * 0.5f);
	const FVector3d CellMaximum = FVector3d(Center + BlockDimensions * 0.5f);
	const float TotalSpiralAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	const float RadialOverhang = PlatformOverhangBlocks * CachedRadialBlockUnit;
	const float WallOverlap = SpiralWallOverlapBlocks * CachedRadialBlockUnit;
	auto EndCapIntersectsCell = [&](bool bEnd)
	{
		const float Alpha = bEnd ? 1.0f : 0.0f;
		const float Bottom = FMath::Lerp(StartBottom, EndBottom, Alpha);
		const float Top = Bottom + SpiralThickness;
		const double CapAngle = StartAngle + Direction * TotalSpiralAngle * Alpha;
		const double CosAngle = FMath::Cos(CapAngle);
		const double SinAngle = FMath::Sin(CapAngle);
		const float ConeRadius = GetConeOuterRadiusAtHeight(Top);
		const float InnerRadius = FMath::Max(ConeRadius - WallOverlap, 0.0f);
		const float OuterRadius = ConeRadius + RadialOverhang;
		const FVector3d InnerBottom(CosAngle * InnerRadius, SinAngle * InnerRadius, Bottom);
		const FVector3d OuterBottom(CosAngle * OuterRadius, SinAngle * OuterRadius, Bottom);
		const FVector3d OuterTop(CosAngle * OuterRadius, SinAngle * OuterRadius, Top);
		const FVector3d InnerTop(CosAngle * InnerRadius, SinAngle * InnerRadius, Top);
		return DoesSurfaceQuadIntersectBox(
			InnerBottom, OuterBottom, OuterTop, InnerTop, CellMinimum, CellMaximum);
	};
	if (EndCapIntersectsCell(false) || EndCapIntersectsCell(true))
	{
		return true;
	}

	const float CenterRadius = FVector2D(Center.X, Center.Y).Size();
	const float HalfDiagonal = FVector2D(BlockDimensions.X, BlockDimensions.Y).Size() * 0.5f;
	const float CellAngularHalfWidth = FMath::Atan2(
		HalfDiagonal, FMath::Max(CenterRadius - HalfDiagonal, CachedRadialBlockUnit * 0.25f));
	const float HeightPerRadian = FMath::Abs(EndBottom - StartBottom) /
		FMath::Max(TotalSpiralAngle, KINDA_SMALL_NUMBER);
	const float HeightMargin = HeightPerRadian *
		(CellAngularHalfWidth + UE_PI / SpiralSegmentsPerTurn) + ConeSpacingTolerance;
	const float Angle = FMath::Atan2(Center.Y, Center.X);
	float BaseProgressAngle = FMath::Fmod(Direction * (Angle - StartAngle), UE_TWO_PI);
	if (BaseProgressAngle < 0.0f)
	{
		BaseProgressAngle += UE_TWO_PI;
	}
	float MinimumRadius;
	float MaximumRadius;
	GetCellRadialRange(X, Y, MinimumRadius, MaximumRadius);
	const int32 RevolutionCount = FMath::CeilToInt(PlatformSpiralTurns);
	for (int32 Revolution = 0; Revolution <= RevolutionCount; ++Revolution)
	{
		const float ProgressAngle = BaseProgressAngle + UE_TWO_PI * Revolution;
		if (ProgressAngle > TotalSpiralAngle + CellAngularHalfWidth)
		{
			continue;
		}
		const float Alpha = FMath::Clamp(ProgressAngle / TotalSpiralAngle, 0.0f, 1.0f);
		const float SpiralBottom = FMath::Lerp(StartBottom, EndBottom, Alpha);
		if (CellTop < SpiralBottom - HeightMargin ||
			CellBottom > SpiralBottom + SpiralThickness + HeightMargin)
		{
			continue;
		}
		const float ConeRadius = GetConeOuterRadiusAtHeight(SpiralBottom + SpiralThickness);
		const float InnerRadius = FMath::Max(
			ConeRadius - SpiralWallOverlapBlocks * CachedRadialBlockUnit, 0.0f);
		const float OuterRadius = ConeRadius + PlatformOverhangBlocks * CachedRadialBlockUnit;
		if (MinimumRadius <= OuterRadius + ConeSpacingTolerance &&
			MaximumRadius >= InnerRadius - ConeSpacingTolerance)
		{
			return true;
		}
	}
	return false;
}

bool AChunkedDestructibleConeStructure::IsEntranceCell(int32 X, int32 Y, int32 Z) const
{
	if (!bGenerateEntrance || DoorCount <= 0 || Z < 0 || Z >= GridHeightBlocks)
	{
		return false;
	}
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float CellBottom = Z * PitchZ;
	const float CellTop = CellBottom + BlockDimensions.Z;
	const float DoorHeight = BlockDimensions.Z * EntranceHeightBlocks +
		BlockSpacing.Z * FMath::Max(EntranceHeightBlocks - 1, 0);
	const float ArchHeight = FMath::Min(DoorArchHeightBlocks * PitchZ, DoorHeight);
	const float StraightHeight = DoorHeight - ArchHeight;
	const float OpeningHalfWidth = EntranceWidthBlocks * CachedRadialBlockUnit * 0.5f;
	const FVector2D Position(Center.X, Center.Y);
	for (int32 DoorIndex = 0; DoorIndex < DoorCount; ++DoorIndex)
	{
		const int32 DoorBottomBlock = FirstDoorBottomHeightBlocks +
			DoorIndex * DoorVerticalSpacingBlocks;
		if (DoorBottomBlock >= GridHeightBlocks)
		{
			break;
		}
		const float DoorBottom = DoorBottomBlock * PitchZ;
		const float DoorTop = DoorBottom + DoorHeight;
		if (CellTop <= DoorBottom + ConeSpacingTolerance ||
			CellBottom >= DoorTop - ConeSpacingTolerance)
		{
			continue;
		}

		const float AngleRadians = FMath::DegreesToRadians(
			EntranceAngleDegrees + DoorIndex * DoorAngleStepDegrees);
		const FVector2D FrontDirection(FMath::Sin(AngleRadians), -FMath::Cos(AngleRadians));
		const FVector2D RightDirection(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians));
		const float ForwardDistance = FVector2D::DotProduct(Position, FrontDirection);
		if (ForwardDistance < 0.0f)
		{
			continue;
		}
		const float LateralDistance = FMath::Abs(FVector2D::DotProduct(Position, RightDirection));
		const float RelativeSampleHeight = FMath::Clamp(
			Center.Z - DoorBottom, 0.0f, DoorHeight);
		float AvailableHalfWidth = OpeningHalfWidth;
		if (RelativeSampleHeight > StraightHeight && ArchHeight > KINDA_SMALL_NUMBER)
		{
			const float ArchAlpha = FMath::Clamp(
				(RelativeSampleHeight - StraightHeight) / ArchHeight, 0.0f, 1.0f);
			AvailableHalfWidth *= FMath::Sqrt(FMath::Max(1.0f - ArchAlpha * ArchAlpha, 0.0f));
		}
		if (LateralDistance < AvailableHalfWidth - ConeSpacingTolerance)
		{
			return true;
		}
	}
	return false;
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
		FMath::Min3(A.X, B.X, C.X),
		FMath::Min3(A.Y, B.Y, C.Y),
		FMath::Min3(A.Z, B.Z, C.Z));
	const FVector3d TriangleMax(
		FMath::Max3(A.X, B.X, C.X),
		FMath::Max3(A.Y, B.Y, C.Y),
		FMath::Max3(A.Z, B.Z, C.Z));
	const double PitchX = BlockDimensions.X + BlockSpacing.X;
	const double PitchY = BlockDimensions.Y + BlockSpacing.Y;
	const int32 MinimumX = FMath::Clamp(
		FMath::FloorToInt((TriangleMin.X - ConeGridLocalMin.X) / PitchX) - 1,
		0, GridDiameterBlocks - 1);
	const int32 MaximumX = FMath::Clamp(
		FMath::FloorToInt((TriangleMax.X - ConeGridLocalMin.X) / PitchX) + 1,
		0, GridDiameterBlocks - 1);
	const int32 MinimumY = FMath::Clamp(
		FMath::FloorToInt((TriangleMin.Y - ConeGridLocalMin.Y) / PitchY) - 1,
		0, GridDiameterBlocks - 1);
	const int32 MaximumY = FMath::Clamp(
		FMath::FloorToInt((TriangleMax.Y - ConeGridLocalMin.Y) / PitchY) + 1,
		0, GridDiameterBlocks - 1);
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	const double SealFraction = FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);
	const double VisualHalfX = BlockDimensions.X * 0.5 + BlockSpacing.X * 0.5 * SealFraction;
	const double VisualHalfY = BlockDimensions.Y * 0.5 + BlockSpacing.Y * 0.5 * SealFraction;
	const double VisualHalfZ = BlockDimensions.Z * 0.5 + BlockSpacing.Z * 0.5 * SealFraction;

	for (int32 Y = MinimumY; Y <= MaximumY; ++Y)
	{
		for (int32 X = MinimumX; X <= MaximumX; ++X)
		{
			const FIntPoint Cell = EncodeCell(X, Y, LogicalZ);
			const bool bBuildingSurfaceFlash = !SurfaceFlashCellFilter.IsEmpty();
			const bool bSelectedFlashCell = bBuildingSurfaceFlash && SurfaceFlashCellFilter.Contains(Cell);
			if (Cell == FIntPoint::NoneValue ||
				Cell.X / SafeChunkSide + (Cell.Y / SafeChunkSide) * ChunkCount.X != ChunkIndex ||
				!Cells.IsValidIndex(ToCellIndex(Cell)) ||
				(bBuildingSurfaceFlash && !bSelectedFlashCell) ||
				(!Cells[ToCellIndex(Cell)].bExists && !bSelectedFlashCell))
			{
				continue;
			}

			const FVector CellCenter = GetCellLocalCenter(Cell);
			const double CellMinX = CellCenter.X - VisualHalfX;
			const double CellMaxX = CellCenter.X + VisualHalfX;
			const double CellMinY = CellCenter.Y - VisualHalfY;
			const double CellMaxY = CellCenter.Y + VisualHalfY;
			const double CellMinZ = CellCenter.Z - VisualHalfZ;
			const double CellMaxZ = CellCenter.Z + VisualHalfZ;
			TArray<FVector3d> Polygon = { A, B, C };
			ClipSurfacePolygonToPlane(Polygon, 0, CellMinX, true);
			ClipSurfacePolygonToPlane(Polygon, 0, CellMaxX, false);
			ClipSurfacePolygonToPlane(Polygon, 1, CellMinY, true);
			ClipSurfacePolygonToPlane(Polygon, 1, CellMaxY, false);
			ClipSurfacePolygonToPlane(Polygon, 2, CellMinZ, true);
			ClipSurfacePolygonToPlane(Polygon, 2, CellMaxZ, false);
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

void AChunkedDestructibleConeStructure::AppendClippedSurfaceTriangleAcrossLayers(
	FDynamicMesh3& Mesh,
	int32 ChunkIndex,
	const FVector3d& A,
	const FVector3d& B,
	const FVector3d& C,
	const FVector3f& Normal,
	int32 MaterialIndex) const
{
	if (GridHeightBlocks <= 0)
	{
		return;
	}
	const double PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const double MinimumTriangleZ = FMath::Min3(A.Z, B.Z, C.Z);
	const double MaximumTriangleZ = FMath::Max3(A.Z, B.Z, C.Z);
	const int32 MinimumZ = FMath::Clamp(
		FMath::FloorToInt(MinimumTriangleZ / PitchZ) - 1, 0, GridHeightBlocks - 1);
	const int32 MaximumZ = FMath::Clamp(
		FMath::FloorToInt(MaximumTriangleZ / PitchZ) + 1, 0, GridHeightBlocks - 1);
	for (int32 LogicalZ = MinimumZ; LogicalZ <= MaximumZ; ++LogicalZ)
	{
		AppendClippedSurfaceTriangle(
			Mesh, ChunkIndex, LogicalZ, A, B, C, Normal, MaterialIndex);
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
	const float VerticalSeal = BlockSpacing.Z * 0.5f *
		FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);
	for (int32 Z = MinLogicalZ; Z < MaxLogicalZ; ++Z)
	{
		const float BottomZ = FMath::Max(Z * PitchZ - VerticalSeal, 0.0f);
		const float TopZ = FMath::Min(
			Z * PitchZ + BlockDimensions.Z + VerticalSeal, TotalHeight);
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
	if (!bGeneratePlatforms || ChunkCount.X <= 0)
	{
		return;
	}
	if (PlatformStyle == EConeStructurePlatformStyle::RisingSpiral)
	{
		AppendSpiralSurfaces(Mesh, ChunkIndex);
		return;
	}
	if (PlatformCount <= 0)
	{
		return;
	}
	const int32 SegmentCount = GetEffectiveCircularSegments();
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinLogicalZ = ChunkY * SafeChunkSide;
	const int32 MaxLogicalZ = FMath::Min(MinLogicalZ + SafeChunkSide, GridHeightBlocks);
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float VerticalSeal = BlockSpacing.Z * 0.5f *
		FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);
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
			const float BottomZ = FMath::Max(Z * PitchZ - VerticalSeal, 0.0f);
			const float TopZ = FMath::Min(
				Z * PitchZ + BlockDimensions.Z + VerticalSeal,
				BlockDimensions.Z * GridHeightBlocks +
					BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0));
			const bool bSeparateLayer = BlockSpacing.Z > ConeSpacingTolerance &&
				SurfaceGapSealFraction < 1.0f - KINDA_SMALL_NUMBER;
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

void AChunkedDestructibleConeStructure::AppendSpiralSurfaces(
	FDynamicMesh3& Mesh,
	int32 ChunkIndex) const
{
	if (!bGeneratePlatforms || PlatformStyle != EConeStructurePlatformStyle::RisingSpiral ||
		ChunkCount.X <= 0 ||
		(PlatformOverhangBlocks <= KINDA_SMALL_NUMBER &&
			SpiralWallOverlapBlocks <= KINDA_SMALL_NUMBER))
	{
		return;
	}

	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const float Thickness = GetSpiralThickness();
	const float TotalAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	const int32 SegmentCount = FMath::Clamp(
		FMath::CeilToInt(PlatformSpiralTurns * SpiralSegmentsPerTurn), 1, 8192);
	const float RadialOverhang = PlatformOverhangBlocks * CachedRadialBlockUnit;
	const float WallOverlap = SpiralWallOverlapBlocks * CachedRadialBlockUnit;

	auto MakeSection = [&](int32 SectionIndex,
		FVector3d& OutInnerBottom,
		FVector3d& OutOuterBottom,
		FVector3d& OutInnerTop,
		FVector3d& OutOuterTop)
	{
		const float Alpha = static_cast<float>(SectionIndex) / SegmentCount;
		const double Angle = StartAngle + Direction * TotalAngle * Alpha;
		const float BottomZ = FMath::Lerp(StartBottom, EndBottom, Alpha);
		const float TopZ = BottomZ + Thickness;
		const float ConeRadius = GetConeOuterRadiusAtHeight(TopZ);
		const float InnerRadius = FMath::Max(ConeRadius - WallOverlap, 0.0f);
		const float OuterRadius = ConeRadius + RadialOverhang;
		const double CosAngle = FMath::Cos(Angle);
		const double SinAngle = FMath::Sin(Angle);
		OutInnerBottom = FVector3d(CosAngle * InnerRadius, SinAngle * InnerRadius, BottomZ);
		OutOuterBottom = FVector3d(CosAngle * OuterRadius, SinAngle * OuterRadius, BottomZ);
		OutInnerTop = FVector3d(CosAngle * InnerRadius, SinAngle * InnerRadius, TopZ);
		OutOuterTop = FVector3d(CosAngle * OuterRadius, SinAngle * OuterRadius, TopZ);
	};

	auto AppendOrientedTriangle = [&](FVector3d A, FVector3d B, FVector3d C, const FVector3d& DesiredNormal)
	{
		FVector3d GeometricNormal = FVector3d::CrossProduct(B - A, C - A);
		if (GeometricNormal.SquaredLength() <= FMath::Square(ConeSurfaceTolerance))
		{
			return;
		}
		if (FVector3d::DotProduct(GeometricNormal, DesiredNormal) < 0.0)
		{
			Swap(B, C);
			GeometricNormal = -GeometricNormal;
		}
		const FVector3d UnitNormal = GeometricNormal.GetSafeNormal();
		AppendClippedSurfaceTriangleAcrossLayers(
			Mesh, ChunkIndex, A, B, C,
			FVector3f(
				static_cast<float>(UnitNormal.X),
				static_cast<float>(UnitNormal.Y),
				static_cast<float>(UnitNormal.Z)),
			1);
	};

	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		FVector3d InnerBottomA;
		FVector3d OuterBottomA;
		FVector3d InnerTopA;
		FVector3d OuterTopA;
		FVector3d InnerBottomB;
		FVector3d OuterBottomB;
		FVector3d InnerTopB;
		FVector3d OuterTopB;
		MakeSection(SegmentIndex,
			InnerBottomA, OuterBottomA, InnerTopA, OuterTopA);
		MakeSection(SegmentIndex + 1,
			InnerBottomB, OuterBottomB, InnerTopB, OuterTopB);

		const float MidAlpha = (SegmentIndex + 0.5f) / SegmentCount;
		const double MidAngle = StartAngle + Direction * TotalAngle * MidAlpha;
		const FVector3d OutwardNormal(FMath::Cos(MidAngle), FMath::Sin(MidAngle), 0.0);
		const FVector3d TangentNormal(
			Direction * -FMath::Sin(MidAngle),
			Direction * FMath::Cos(MidAngle),
			0.0);

		AppendOrientedTriangle(InnerTopA, OuterTopA, OuterTopB, FVector3d::UnitZ());
		AppendOrientedTriangle(InnerTopA, OuterTopB, InnerTopB, FVector3d::UnitZ());
		AppendOrientedTriangle(InnerBottomA, OuterBottomB, OuterBottomA, -FVector3d::UnitZ());
		AppendOrientedTriangle(InnerBottomA, InnerBottomB, OuterBottomB, -FVector3d::UnitZ());
		AppendOrientedTriangle(OuterBottomA, OuterTopB, OuterTopA, OutwardNormal);
		AppendOrientedTriangle(OuterBottomA, OuterBottomB, OuterTopB, OutwardNormal);
		AppendOrientedTriangle(InnerBottomA, InnerTopA, InnerTopB, -OutwardNormal);
		AppendOrientedTriangle(InnerBottomA, InnerTopB, InnerBottomB, -OutwardNormal);

		if (SegmentIndex == 0)
		{
			AppendOrientedTriangle(InnerBottomA, OuterBottomA, OuterTopA, -TangentNormal);
			AppendOrientedTriangle(InnerBottomA, OuterTopA, InnerTopA, -TangentNormal);
		}
		if (SegmentIndex + 1 == SegmentCount)
		{
			AppendOrientedTriangle(InnerBottomB, OuterTopB, OuterBottomB, TangentNormal);
			AppendOrientedTriangle(InnerBottomB, InnerTopB, OuterTopB, TangentNormal);
		}
	}
}

bool AChunkedDestructibleConeStructure::IsPointInsideRenderedStructure(
	const FVector3d& LocalPoint) const
{
	const double Radius = FVector2d(LocalPoint.X, LocalPoint.Y).Length();
	const double Angle = FMath::Atan2(LocalPoint.Y, LocalPoint.X);
	const double PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const double TotalHeight = BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const double VerticalSeal = BlockSpacing.Z * 0.5 *
		FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);
	constexpr double BoundaryTolerance = 0.01;

	auto IsInsideRenderedLayer = [&](int32 LayerIndex)
	{
		if (LayerIndex < 0 || LayerIndex >= GridHeightBlocks)
		{
			return false;
		}
		const double LayerBottom = FMath::Max(LayerIndex * PitchZ - VerticalSeal, 0.0);
		const double LayerTop = FMath::Min(
			LayerIndex * PitchZ + BlockDimensions.Z + VerticalSeal,
			TotalHeight);
		return LocalPoint.Z >= LayerBottom - BoundaryTolerance &&
			LocalPoint.Z <= LayerTop + BoundaryTolerance;
	};

	auto GetFacetedRadius = [](double VertexRadius, double SampleAngle, int32 SegmentCount)
	{
		if (VertexRadius <= 0.0 || SegmentCount < 3)
		{
			return FMath::Max(VertexRadius, 0.0);
		}
		const double Step = UE_TWO_PI / SegmentCount;
		double WrappedAngle = FMath::Fmod(SampleAngle, UE_TWO_PI);
		if (WrappedAngle < 0.0)
		{
			WrappedAngle += UE_TWO_PI;
		}
		const int32 SegmentIndex = FMath::Clamp(
			FMath::FloorToInt(WrappedAngle / Step), 0, SegmentCount - 1);
		const double SegmentCenter = (SegmentIndex + 0.5) * Step;
		const double AngleFromCenter = WrappedAngle - SegmentCenter;
		return VertexRadius * FMath::Cos(Step * 0.5) /
			FMath::Max(FMath::Cos(AngleFromCenter), UE_DOUBLE_SMALL_NUMBER);
	};

	const int32 CircularSegmentCount = GetEffectiveCircularSegments();
	if (LocalPoint.Z >= -BoundaryTolerance &&
		LocalPoint.Z <= TotalHeight + BoundaryTolerance)
	{
		const int32 BaseLayer = FMath::FloorToInt(LocalPoint.Z / FMath::Max(PitchZ, 1.0));
		if (IsInsideRenderedLayer(BaseLayer) || IsInsideRenderedLayer(BaseLayer - 1) ||
			IsInsideRenderedLayer(BaseLayer + 1))
		{
			const double SafeZ = FMath::Clamp(LocalPoint.Z, 0.0, TotalHeight);
			const double OuterRadius = GetFacetedRadius(
				GetConeOuterRadiusAtHeight(SafeZ), Angle, CircularSegmentCount);
			const double InnerRadius = GetFacetedRadius(
				GetConeInnerRadiusAtHeight(SafeZ), Angle, CircularSegmentCount);
			if (Radius >= InnerRadius - BoundaryTolerance &&
				Radius <= OuterRadius + BoundaryTolerance)
			{
				return true;
			}
		}
	}

	if (!bGeneratePlatforms)
	{
		return false;
	}

	if (PlatformStyle == EConeStructurePlatformStyle::CircularRings)
	{
		for (int32 PlatformIndex = 0; PlatformIndex < PlatformCount; ++PlatformIndex)
		{
			const int32 PlatformBottomLayer = LowestPlatformHeightBlocks +
				PlatformIndex * PlatformVerticalSpacingBlocks;
			if (PlatformBottomLayer >= GridHeightBlocks)
			{
				break;
			}
			const int32 PlatformTopLayer = FMath::Min(
				PlatformBottomLayer + PlatformThicknessBlocks - 1,
				GridHeightBlocks - 1);
			bool bInsidePlatformHeight = false;
			for (int32 LayerIndex = PlatformBottomLayer;
				LayerIndex <= PlatformTopLayer && !bInsidePlatformHeight;
				++LayerIndex)
			{
				bInsidePlatformHeight = IsInsideRenderedLayer(LayerIndex);
			}
			if (!bInsidePlatformHeight)
			{
				continue;
			}

			const double ReferenceZ = BlockDimensions.Z * 0.5 +
				PlatformBottomLayer * PitchZ;
			const double InnerRadius = GetFacetedRadius(
				GetConeInnerRadiusAtHeight(ReferenceZ), Angle, CircularSegmentCount);
			const double OuterRadius = GetFacetedRadius(
				GetConeOuterRadiusAtHeight(ReferenceZ) +
					PlatformOverhangBlocks * CachedRadialBlockUnit,
				Angle,
				CircularSegmentCount);
			if (Radius >= InnerRadius - BoundaryTolerance &&
				Radius <= OuterRadius + BoundaryTolerance)
			{
				return true;
			}
		}
		return false;
	}

	if ((PlatformOverhangBlocks <= KINDA_SMALL_NUMBER &&
			SpiralWallOverlapBlocks <= KINDA_SMALL_NUMBER) ||
		PlatformSpiralTurns <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const double StartBottom = GetSpiralBottomHeight(false);
	const double EndBottom = GetSpiralBottomHeight(true);
	const double Thickness = GetSpiralThickness();
	const double TotalSpiralAngle = UE_TWO_PI * PlatformSpiralTurns;
	const double StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const double Direction = bPlatformSpiralClockwise ? -1.0 : 1.0;
	double BaseProgressAngle = FMath::Fmod(Direction * (Angle - StartAngle), UE_TWO_PI);
	if (BaseProgressAngle < 0.0)
	{
		BaseProgressAngle += UE_TWO_PI;
	}
	const int32 SpiralSegmentCount = FMath::Clamp(
		FMath::CeilToInt(PlatformSpiralTurns * SpiralSegmentsPerTurn), 1, 8192);
	const double SegmentProgress = TotalSpiralAngle / SpiralSegmentCount;
	const int32 RevolutionCount = FMath::CeilToInt(PlatformSpiralTurns);

	auto IntersectPolarEdgeRadius = [](double RadiusA, double AngleA, double RadiusB,
		double AngleB, double RayAngle)
	{
		const FVector2d PointA(FMath::Cos(AngleA) * RadiusA, FMath::Sin(AngleA) * RadiusA);
		const FVector2d PointB(FMath::Cos(AngleB) * RadiusB, FMath::Sin(AngleB) * RadiusB);
		const FVector2d Edge = PointB - PointA;
		const FVector2d Ray(FMath::Cos(RayAngle), FMath::Sin(RayAngle));
		const double Denominator = Edge.X * Ray.Y - Edge.Y * Ray.X;
		if (FMath::Abs(Denominator) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return (RadiusA + RadiusB) * 0.5;
		}
		const double Numerator = PointA.X * Ray.Y - PointA.Y * Ray.X;
		const double EdgeAlpha = FMath::Clamp(-Numerator / Denominator, 0.0, 1.0);
		return FVector2d::DotProduct(PointA + Edge * EdgeAlpha, Ray);
	};

	for (int32 Revolution = 0; Revolution <= RevolutionCount; ++Revolution)
	{
		const double ProgressAngle = BaseProgressAngle + UE_TWO_PI * Revolution;
		if (ProgressAngle < -BoundaryTolerance ||
			ProgressAngle > TotalSpiralAngle + BoundaryTolerance)
		{
			continue;
		}
		const int32 SegmentIndex = FMath::Clamp(
			FMath::FloorToInt(ProgressAngle / FMath::Max(SegmentProgress, UE_DOUBLE_SMALL_NUMBER)),
			0,
			SpiralSegmentCount - 1);
		const double ProgressA = SegmentIndex * SegmentProgress;
		const double ProgressB = (SegmentIndex + 1) * SegmentProgress;
		const double AlphaA = ProgressA / TotalSpiralAngle;
		const double AlphaB = ProgressB / TotalSpiralAngle;
		const double SegmentAlpha = FMath::Clamp(
			(ProgressAngle - ProgressA) / FMath::Max(ProgressB - ProgressA, UE_DOUBLE_SMALL_NUMBER),
			0.0,
			1.0);
		const double BottomA = FMath::Lerp(StartBottom, EndBottom, AlphaA);
		const double BottomB = FMath::Lerp(StartBottom, EndBottom, AlphaB);
		const double BottomZ = FMath::Lerp(BottomA, BottomB, SegmentAlpha);
		const double TopZ = BottomZ + Thickness;
		if (LocalPoint.Z < BottomZ - BoundaryTolerance ||
			LocalPoint.Z > TopZ + BoundaryTolerance)
		{
			continue;
		}

		const double AngleA = StartAngle + Direction * ProgressA;
		const double AngleB = StartAngle + Direction * ProgressB;
		const double OuterRadiusA = GetConeOuterRadiusAtHeight(BottomA + Thickness) +
			PlatformOverhangBlocks * CachedRadialBlockUnit;
		const double OuterRadiusB = GetConeOuterRadiusAtHeight(BottomB + Thickness) +
			PlatformOverhangBlocks * CachedRadialBlockUnit;
		const double InnerRadiusA = FMath::Max(
			GetConeOuterRadiusAtHeight(BottomA + Thickness) -
				SpiralWallOverlapBlocks * CachedRadialBlockUnit,
			0.0f);
		const double InnerRadiusB = FMath::Max(
			GetConeOuterRadiusAtHeight(BottomB + Thickness) -
				SpiralWallOverlapBlocks * CachedRadialBlockUnit,
			0.0f);
		const double OuterRadius = IntersectPolarEdgeRadius(
			OuterRadiusA, AngleA, OuterRadiusB, AngleB, Angle);
		const double InnerRadius = IntersectPolarEdgeRadius(
			InnerRadiusA, AngleA, InnerRadiusB, AngleB, Angle);
		if (Radius >= InnerRadius - BoundaryTolerance &&
			Radius <= OuterRadius + BoundaryTolerance)
		{
			return true;
		}
	}
	return false;
}

void AChunkedDestructibleConeStructure::AppendSampledDestructionCutFace(
	FDynamicMesh3& Mesh,
	const FVector3d& A,
	const FVector3d& B,
	const FVector3d& C,
	const FVector3d& D,
	const FVector3f& Normal,
	int32 MaterialIndex) const
{
	const FVector3d AxisU = B - A;
	const FVector3d AxisV = D - A;
	const double LengthU = AxisU.Length();
	const double LengthV = AxisV.Length();
	if (LengthU <= ConeSurfaceTolerance || LengthV <= ConeSurfaceTolerance)
	{
		return;
	}
	(void)C;

	const double TargetEdgeLength = FMath::Clamp(
		static_cast<double>(CachedRadialBlockUnit) * 0.25,
		10.0,
		50.0);
	const int32 SubdivisionsU = FMath::Clamp(FMath::CeilToInt(LengthU / TargetEdgeLength), 1, 48);
	const int32 SubdivisionsV = FMath::Clamp(FMath::CeilToInt(LengthV / TargetEdgeLength), 1, 48);
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);

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

	auto FindBoundary = [this](const FVector3d& First, const FVector3d& Second, bool bFirstInside)
	{
		FVector3d Inside = bFirstInside ? First : Second;
		FVector3d Outside = bFirstInside ? Second : First;
		for (int32 Iteration = 0; Iteration < 10; ++Iteration)
		{
			const FVector3d Midpoint = (Inside + Outside) * 0.5;
			if (IsPointInsideRenderedStructure(Midpoint))
			{
				Inside = Midpoint;
			}
			else
			{
				Outside = Midpoint;
			}
		}
		return (Inside + Outside) * 0.5;
	};

	auto AppendPolygon = [&](TArray<FVector3d>& Polygon)
	{
		RemoveDuplicateSurfacePoints(Polygon);
		if (Polygon.Num() < 3)
		{
			return;
		}
		for (int32 PointIndex = 1; PointIndex + 1 < Polygon.Num(); ++PointIndex)
		{
			FVector3d PointA = Polygon[0];
			FVector3d PointB = Polygon[PointIndex];
			FVector3d PointC = Polygon[PointIndex + 1];
			const FVector3d GeometricNormal = FVector3d::CrossProduct(PointB - PointA, PointC - PointA);
			if (GeometricNormal.SquaredLength() <= FMath::Square(ConeSurfaceTolerance))
			{
				continue;
			}
			if (FVector3d::DotProduct(GeometricNormal, FVector3d(Normal)) < 0.0)
			{
				Swap(PointB, PointC);
			}
			AppendMeshTriangle(
				Mesh,
				PointA,
				PointB,
				PointC,
				Normal,
				MaterialIndex,
				MakeUV(PointA),
				MakeUV(PointB),
				MakeUV(PointC));
		}
	};

	auto AppendClippedTriangle = [&](const FVector3d& PointA, const FVector3d& PointB,
		const FVector3d& PointC)
	{
		const FVector3d Points[] = { PointA, PointB, PointC };
		const bool bInside[] = {
			IsPointInsideRenderedStructure(PointA),
			IsPointInsideRenderedStructure(PointB),
			IsPointInsideRenderedStructure(PointC)
		};
		const int32 InsideCount = static_cast<int32>(bInside[0]) +
			static_cast<int32>(bInside[1]) + static_cast<int32>(bInside[2]);
		TArray<FVector3d> Polygon;
		if (InsideCount == 0)
		{
			const FVector3d Center = (PointA + PointB + PointC) / 3.0;
			if (!IsPointInsideRenderedStructure(Center))
			{
				return;
			}
			Polygon.Add(FindBoundary(Center, PointA, true));
			Polygon.Add(FindBoundary(Center, PointB, true));
			Polygon.Add(FindBoundary(Center, PointC, true));
			AppendPolygon(Polygon);
			return;
		}

		for (int32 CurrentIndex = 0; CurrentIndex < 3; ++CurrentIndex)
		{
			const int32 PreviousIndex = (CurrentIndex + 2) % 3;
			if (bInside[PreviousIndex] != bInside[CurrentIndex])
			{
				Polygon.Add(FindBoundary(
					Points[PreviousIndex],
					Points[CurrentIndex],
					bInside[PreviousIndex]));
			}
			if (bInside[CurrentIndex])
			{
				Polygon.Add(Points[CurrentIndex]);
			}
		}
		AppendPolygon(Polygon);
	};

	for (int32 VIndex = 0; VIndex < SubdivisionsV; ++VIndex)
	{
		const double V0 = static_cast<double>(VIndex) / SubdivisionsV;
		const double V1 = static_cast<double>(VIndex + 1) / SubdivisionsV;
		for (int32 UIndex = 0; UIndex < SubdivisionsU; ++UIndex)
		{
			const double U0 = static_cast<double>(UIndex) / SubdivisionsU;
			const double U1 = static_cast<double>(UIndex + 1) / SubdivisionsU;
			const FVector3d Point00 = A + AxisU * U0 + AxisV * V0;
			const FVector3d Point10 = A + AxisU * U1 + AxisV * V0;
			const FVector3d Point11 = A + AxisU * U1 + AxisV * V1;
			const FVector3d Point01 = A + AxisU * U0 + AxisV * V1;
			AppendClippedTriangle(Point00, Point10, Point11);
			AppendClippedTriangle(Point00, Point11, Point01);
		}
	}
}

void AChunkedDestructibleConeStructure::AppendClippedDestructionCutFace(
	FDynamicMesh3& Mesh,
	const FVector3d& A,
	const FVector3d& B,
	const FVector3d& C,
	const FVector3d& D,
	const FVector3f& Normal,
	int32 MaterialIndex) const
{
	const FVector3d FaceCorners[] = { A, B, C, D };
	FVector3d FaceMinimum = A;
	FVector3d FaceMaximum = A;
	for (const FVector3d& Corner : FaceCorners)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			FaceMinimum[Axis] = FMath::Min(FaceMinimum[Axis], Corner[Axis]);
			FaceMaximum[Axis] = FMath::Max(FaceMaximum[Axis], Corner[Axis]);
		}
	}

	int32 PlaneAxis = 2;
	if (FMath::Abs(Normal.X) > FMath::Abs(Normal.Y) &&
		FMath::Abs(Normal.X) > FMath::Abs(Normal.Z))
	{
		PlaneAxis = 0;
	}
	else if (FMath::Abs(Normal.Y) > FMath::Abs(Normal.Z))
	{
		PlaneAxis = 1;
	}
	const double Plane = A[PlaneAxis];
	const FVector3d SortAxisU = (B - A).GetSafeNormal();
	const FVector3d SortAxisV = (D - A).GetSafeNormal();
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	constexpr double IntersectionTolerance = 0.001;
	static const FIntPoint ElementEdges[] = {
		FIntPoint(0, 1), FIntPoint(0, 2), FIntPoint(1, 3), FIntPoint(2, 3),
		FIntPoint(4, 5), FIntPoint(4, 6), FIntPoint(5, 7), FIntPoint(6, 7),
		FIntPoint(0, 4), FIntPoint(1, 5), FIntPoint(2, 6), FIntPoint(3, 7)
	};

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

	auto AppendElementIntersection = [&](const TArray<FVector3d, TInlineAllocator<8>>& Vertices)
	{
		if (Vertices.Num() != 8)
		{
			return;
		}
		FVector3d ElementMinimum = Vertices[0];
		FVector3d ElementMaximum = Vertices[0];
		for (const FVector3d& Vertex : Vertices)
		{
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				ElementMinimum[Axis] = FMath::Min(ElementMinimum[Axis], Vertex[Axis]);
				ElementMaximum[Axis] = FMath::Max(ElementMaximum[Axis], Vertex[Axis]);
			}
		}
		if (Plane < ElementMinimum[PlaneAxis] - IntersectionTolerance ||
			Plane > ElementMaximum[PlaneAxis] + IntersectionTolerance)
		{
			return;
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis != PlaneAxis &&
				(ElementMaximum[Axis] < FaceMinimum[Axis] - IntersectionTolerance ||
					ElementMinimum[Axis] > FaceMaximum[Axis] + IntersectionTolerance))
			{
				return;
			}
		}

		TArray<FVector3d> Polygon;
		Polygon.Reserve(12);
		auto AddUniquePoint = [&Polygon](const FVector3d& Point)
		{
			for (const FVector3d& Existing : Polygon)
			{
				if ((Existing - Point).SquaredLength() <= FMath::Square(IntersectionTolerance))
				{
					return;
				}
			}
			Polygon.Add(Point);
		};

		for (const FIntPoint Edge : ElementEdges)
		{
			const FVector3d& Start = Vertices[Edge.X];
			const FVector3d& End = Vertices[Edge.Y];
			const double StartDistance = Start[PlaneAxis] - Plane;
			const double EndDistance = End[PlaneAxis] - Plane;
			if (FMath::Abs(StartDistance) <= IntersectionTolerance)
			{
				AddUniquePoint(Start);
			}
			if (FMath::Abs(EndDistance) <= IntersectionTolerance)
			{
				AddUniquePoint(End);
			}
			if ((StartDistance < -IntersectionTolerance && EndDistance > IntersectionTolerance) ||
				(StartDistance > IntersectionTolerance && EndDistance < -IntersectionTolerance))
			{
				const double EdgeAlpha = StartDistance / (StartDistance - EndDistance);
				AddUniquePoint(Start + (End - Start) * EdgeAlpha);
			}
		}
		if (Polygon.Num() < 3)
		{
			return;
		}

		FVector3d Center = FVector3d::Zero();
		for (const FVector3d& Point : Polygon)
		{
			Center += Point;
		}
		Center /= Polygon.Num();
		Polygon.Sort([&](const FVector3d& Left, const FVector3d& Right)
		{
			const FVector3d LeftDelta = Left - Center;
			const FVector3d RightDelta = Right - Center;
			const double LeftAngle = FMath::Atan2(
				FVector3d::DotProduct(LeftDelta, SortAxisV),
				FVector3d::DotProduct(LeftDelta, SortAxisU));
			const double RightAngle = FMath::Atan2(
				FVector3d::DotProduct(RightDelta, SortAxisV),
				FVector3d::DotProduct(RightDelta, SortAxisU));
			return LeftAngle < RightAngle;
		});

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis == PlaneAxis)
			{
				continue;
			}
			ClipSurfacePolygonToPlane(Polygon, Axis, FaceMinimum[Axis], true);
			ClipSurfacePolygonToPlane(Polygon, Axis, FaceMaximum[Axis], false);
		}
		RemoveDuplicateSurfacePoints(Polygon);
		if (Polygon.Num() < 3)
		{
			return;
		}

		for (int32 PointIndex = 1; PointIndex + 1 < Polygon.Num(); ++PointIndex)
		{
			FVector3d PointA = Polygon[0];
			FVector3d PointB = Polygon[PointIndex];
			FVector3d PointC = Polygon[PointIndex + 1];
			const FVector3d GeometricNormal = FVector3d::CrossProduct(PointB - PointA, PointC - PointA);
			if (GeometricNormal.SquaredLength() <= FMath::Square(ConeSurfaceTolerance))
			{
				continue;
			}
			if (FVector3d::DotProduct(GeometricNormal, FVector3d(Normal)) < 0.0)
			{
				Swap(PointB, PointC);
			}
			AppendMeshTriangle(
				Mesh,
				PointA,
				PointB,
				PointC,
				Normal,
				MaterialIndex,
				MakeUV(PointA),
				MakeUV(PointB),
				MakeUV(PointC));
		}
	};

	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
		BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const float VerticalSeal = BlockSpacing.Z * 0.5f *
		FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);

	if (MaterialIndex == 0 || !bGeneratePlatforms)
	{
		const int32 SegmentCount = GetEffectiveCircularSegments();
		const int32 MinimumLayer = FMath::Clamp(
			FMath::FloorToInt(FaceMinimum.Z / FMath::Max(PitchZ, 1.0f)) - 1,
			0,
			GridHeightBlocks - 1);
		const int32 MaximumLayer = FMath::Clamp(
			FMath::FloorToInt(FaceMaximum.Z / FMath::Max(PitchZ, 1.0f)) + 1,
			0,
			GridHeightBlocks - 1);
		for (int32 Z = MinimumLayer; Z <= MaximumLayer; ++Z)
		{
			const float BottomZ = FMath::Max(Z * PitchZ - VerticalSeal, 0.0f);
			const float TopZ = FMath::Min(Z * PitchZ + BlockDimensions.Z + VerticalSeal, TotalHeight);
			const float OuterBottomRadius = GetConeOuterRadiusAtHeight(BottomZ);
			const float OuterTopRadius = GetConeOuterRadiusAtHeight(TopZ);
			const float InnerBottomRadius = GetConeInnerRadiusAtHeight(BottomZ);
			const float InnerTopRadius = GetConeInnerRadiusAtHeight(TopZ);
			for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
			{
				const double AngleA = UE_TWO_PI * SegmentIndex / SegmentCount;
				const double AngleB = UE_TWO_PI * (SegmentIndex + 1) / SegmentCount;
				TArray<FVector3d, TInlineAllocator<8>> Vertices = {
					FVector3d(FMath::Cos(AngleA) * InnerBottomRadius, FMath::Sin(AngleA) * InnerBottomRadius, BottomZ),
					FVector3d(FMath::Cos(AngleA) * OuterBottomRadius, FMath::Sin(AngleA) * OuterBottomRadius, BottomZ),
					FVector3d(FMath::Cos(AngleA) * InnerTopRadius, FMath::Sin(AngleA) * InnerTopRadius, TopZ),
					FVector3d(FMath::Cos(AngleA) * OuterTopRadius, FMath::Sin(AngleA) * OuterTopRadius, TopZ),
					FVector3d(FMath::Cos(AngleB) * InnerBottomRadius, FMath::Sin(AngleB) * InnerBottomRadius, BottomZ),
					FVector3d(FMath::Cos(AngleB) * OuterBottomRadius, FMath::Sin(AngleB) * OuterBottomRadius, BottomZ),
					FVector3d(FMath::Cos(AngleB) * InnerTopRadius, FMath::Sin(AngleB) * InnerTopRadius, TopZ),
					FVector3d(FMath::Cos(AngleB) * OuterTopRadius, FMath::Sin(AngleB) * OuterTopRadius, TopZ)
				};
				AppendElementIntersection(Vertices);
			}
		}
		return;
	}

	if (PlatformStyle == EConeStructurePlatformStyle::CircularRings)
	{
		const int32 SegmentCount = GetEffectiveCircularSegments();
		for (int32 PlatformIndex = 0; PlatformIndex < PlatformCount; ++PlatformIndex)
		{
			const int32 BottomLayer = LowestPlatformHeightBlocks +
				PlatformIndex * PlatformVerticalSpacingBlocks;
			if (BottomLayer >= GridHeightBlocks)
			{
				break;
			}
			const int32 TopLayer = FMath::Min(
				BottomLayer + PlatformThicknessBlocks - 1,
				GridHeightBlocks - 1);
			const float BottomZ = FMath::Max(BottomLayer * PitchZ - VerticalSeal, 0.0f);
			const float TopZ = FMath::Min(
				TopLayer * PitchZ + BlockDimensions.Z + VerticalSeal,
				TotalHeight);
			if (TopZ < FaceMinimum.Z - IntersectionTolerance ||
				BottomZ > FaceMaximum.Z + IntersectionTolerance)
			{
				continue;
			}
			const float ReferenceZ = BlockDimensions.Z * 0.5f + BottomLayer * PitchZ;
			const float InnerRadius = GetConeInnerRadiusAtHeight(ReferenceZ);
			const float OuterRadius = GetConeOuterRadiusAtHeight(ReferenceZ) +
				PlatformOverhangBlocks * CachedRadialBlockUnit;
			for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
			{
				const double AngleA = UE_TWO_PI * SegmentIndex / SegmentCount;
				const double AngleB = UE_TWO_PI * (SegmentIndex + 1) / SegmentCount;
				TArray<FVector3d, TInlineAllocator<8>> Vertices = {
					FVector3d(FMath::Cos(AngleA) * InnerRadius, FMath::Sin(AngleA) * InnerRadius, BottomZ),
					FVector3d(FMath::Cos(AngleA) * OuterRadius, FMath::Sin(AngleA) * OuterRadius, BottomZ),
					FVector3d(FMath::Cos(AngleA) * InnerRadius, FMath::Sin(AngleA) * InnerRadius, TopZ),
					FVector3d(FMath::Cos(AngleA) * OuterRadius, FMath::Sin(AngleA) * OuterRadius, TopZ),
					FVector3d(FMath::Cos(AngleB) * InnerRadius, FMath::Sin(AngleB) * InnerRadius, BottomZ),
					FVector3d(FMath::Cos(AngleB) * OuterRadius, FMath::Sin(AngleB) * OuterRadius, BottomZ),
					FVector3d(FMath::Cos(AngleB) * InnerRadius, FMath::Sin(AngleB) * InnerRadius, TopZ),
					FVector3d(FMath::Cos(AngleB) * OuterRadius, FMath::Sin(AngleB) * OuterRadius, TopZ)
				};
				AppendElementIntersection(Vertices);
			}
		}
		return;
	}

	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const float Thickness = GetSpiralThickness();
	const float TotalAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	const int32 SegmentCount = FMath::Clamp(
		FMath::CeilToInt(PlatformSpiralTurns * SpiralSegmentsPerTurn), 1, 8192);
	const float RadialOverhang = PlatformOverhangBlocks * CachedRadialBlockUnit;
	const float WallOverlap = SpiralWallOverlapBlocks * CachedRadialBlockUnit;
	auto MakeSection = [&](int32 SectionIndex, FVector3d& InnerBottom, FVector3d& OuterBottom,
		FVector3d& InnerTop, FVector3d& OuterTop)
	{
		const float Alpha = static_cast<float>(SectionIndex) / SegmentCount;
		const double Angle = StartAngle + Direction * TotalAngle * Alpha;
		const float BottomZ = FMath::Lerp(StartBottom, EndBottom, Alpha);
		const float TopZ = BottomZ + Thickness;
		const float ConeOuterRadius = GetConeOuterRadiusAtHeight(TopZ);
		const float InnerRadius = FMath::Max(ConeOuterRadius - WallOverlap, 0.0f);
		const float OuterRadius = ConeOuterRadius + RadialOverhang;
		InnerBottom = FVector3d(FMath::Cos(Angle) * InnerRadius, FMath::Sin(Angle) * InnerRadius, BottomZ);
		OuterBottom = FVector3d(FMath::Cos(Angle) * OuterRadius, FMath::Sin(Angle) * OuterRadius, BottomZ);
		InnerTop = FVector3d(FMath::Cos(Angle) * InnerRadius, FMath::Sin(Angle) * InnerRadius, TopZ);
		OuterTop = FVector3d(FMath::Cos(Angle) * OuterRadius, FMath::Sin(Angle) * OuterRadius, TopZ);
	};
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		FVector3d InnerBottomA;
		FVector3d OuterBottomA;
		FVector3d InnerTopA;
		FVector3d OuterTopA;
		FVector3d InnerBottomB;
		FVector3d OuterBottomB;
		FVector3d InnerTopB;
		FVector3d OuterTopB;
		MakeSection(SegmentIndex, InnerBottomA, OuterBottomA, InnerTopA, OuterTopA);
		MakeSection(SegmentIndex + 1, InnerBottomB, OuterBottomB, InnerTopB, OuterTopB);
		TArray<FVector3d, TInlineAllocator<8>> Vertices = {
			InnerBottomA, OuterBottomA, InnerTopA, OuterTopA,
			InnerBottomB, OuterBottomB, InnerTopB, OuterTopB
		};
		AppendElementIntersection(Vertices);
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
		AppendClippedDestructionCutFace(Mesh,
			FVector3d(MinX, MaxY, MinZ), FVector3d(MinX, MaxY, MaxZ),
			FVector3d(MinX, MinY, MaxZ), FVector3d(MinX, MinY, MinZ),
			FVector3f::UnitX(), MaterialIndex);
	}
	if (BlockSpacing.X <= ConeSpacingTolerance && CellExists3D(X + 1, Y, Z))
	{
		AppendClippedDestructionCutFace(Mesh,
			FVector3d(MaxX, MinY, MinZ), FVector3d(MaxX, MinY, MaxZ),
			FVector3d(MaxX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MinZ),
			-FVector3f::UnitX(), MaterialIndex);
	}
	if (BlockSpacing.Y <= ConeSpacingTolerance && CellExists3D(X, Y - 1, Z))
	{
		AppendClippedDestructionCutFace(Mesh,
			FVector3d(MinX, MinY, MinZ), FVector3d(MinX, MinY, MaxZ),
			FVector3d(MaxX, MinY, MaxZ), FVector3d(MaxX, MinY, MinZ),
			FVector3f::UnitY(), MaterialIndex);
	}
	if (BlockSpacing.Y <= ConeSpacingTolerance && CellExists3D(X, Y + 1, Z))
	{
		AppendClippedDestructionCutFace(Mesh,
			FVector3d(MaxX, MaxY, MinZ), FVector3d(MaxX, MaxY, MaxZ),
			FVector3d(MinX, MaxY, MaxZ), FVector3d(MinX, MaxY, MinZ),
			-FVector3f::UnitY(), MaterialIndex);
	}
	if (BlockSpacing.Z <= ConeSpacingTolerance && CellExists3D(X, Y, Z - 1))
	{
		AppendClippedDestructionCutFace(Mesh,
			FVector3d(MinX, MinY, MinZ), FVector3d(MaxX, MinY, MinZ),
			FVector3d(MaxX, MaxY, MinZ), FVector3d(MinX, MaxY, MinZ),
			FVector3f::UnitZ(), MaterialIndex);
	}
	if (BlockSpacing.Z <= ConeSpacingTolerance && CellExists3D(X, Y, Z + 1))
	{
		AppendClippedDestructionCutFace(Mesh,
			FVector3d(MinX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MaxZ),
			FVector3d(MaxX, MinY, MaxZ), FVector3d(MinX, MinY, MaxZ),
			-FVector3f::UnitZ(), MaterialIndex);
	}
}

void AChunkedDestructibleConeStructure::AppendSurfaceFlashBoundaryFaces(
	FDynamicMesh3& Mesh,
	const TSet<FIntPoint>& FlashCells) const
{
	const double SealFraction = FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);
	const FVector VisualHalfSize = BlockDimensions * 0.5 +
		BlockSpacing * (0.5 * SealFraction);
	for (const FIntPoint Cell : FlashCells)
	{
		if (!Cells.IsValidIndex(ToCellIndex(Cell)))
		{
			continue;
		}
		int32 X;
		int32 Y;
		int32 Z;
		if (!DecodeCell(Cell, X, Y, Z))
		{
			continue;
		}
		const FVector Center = GetCellLocalCenter(Cell);
		const double MinX = Center.X - VisualHalfSize.X;
		const double MaxX = Center.X + VisualHalfSize.X;
		const double MinY = Center.Y - VisualHalfSize.Y;
		const double MaxY = Center.Y + VisualHalfSize.Y;
		const double MinZ = Center.Z - VisualHalfSize.Z;
		const double MaxZ = Center.Z + VisualHalfSize.Z;
		const int32 MaterialIndex = Cells[ToCellIndex(Cell)].MaterialIndex;
		auto IsSelectedNeighbor = [&](int32 NeighborX, int32 NeighborY, int32 NeighborZ)
		{
			const FIntPoint Neighbor = EncodeCell(NeighborX, NeighborY, NeighborZ);
			return Neighbor != FIntPoint::NoneValue && FlashCells.Contains(Neighbor);
		};

		if (!IsSelectedNeighbor(X - 1, Y, Z))
		{
			AppendClippedDestructionCutFace(Mesh,
				FVector3d(MinX, MaxY, MinZ), FVector3d(MinX, MaxY, MaxZ),
				FVector3d(MinX, MinY, MaxZ), FVector3d(MinX, MinY, MinZ),
				-FVector3f::UnitX(), MaterialIndex);
		}
		if (!IsSelectedNeighbor(X + 1, Y, Z))
		{
			AppendClippedDestructionCutFace(Mesh,
				FVector3d(MaxX, MinY, MinZ), FVector3d(MaxX, MinY, MaxZ),
				FVector3d(MaxX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MinZ),
				FVector3f::UnitX(), MaterialIndex);
		}
		if (!IsSelectedNeighbor(X, Y - 1, Z))
		{
			AppendClippedDestructionCutFace(Mesh,
				FVector3d(MinX, MinY, MinZ), FVector3d(MinX, MinY, MaxZ),
				FVector3d(MaxX, MinY, MaxZ), FVector3d(MaxX, MinY, MinZ),
				-FVector3f::UnitY(), MaterialIndex);
		}
		if (!IsSelectedNeighbor(X, Y + 1, Z))
		{
			AppendClippedDestructionCutFace(Mesh,
				FVector3d(MaxX, MaxY, MinZ), FVector3d(MaxX, MaxY, MaxZ),
				FVector3d(MinX, MaxY, MaxZ), FVector3d(MinX, MaxY, MinZ),
				FVector3f::UnitY(), MaterialIndex);
		}
		if (!IsSelectedNeighbor(X, Y, Z - 1))
		{
			AppendClippedDestructionCutFace(Mesh,
				FVector3d(MinX, MinY, MinZ), FVector3d(MaxX, MinY, MinZ),
				FVector3d(MaxX, MaxY, MinZ), FVector3d(MinX, MaxY, MinZ),
				-FVector3f::UnitZ(), MaterialIndex);
		}
		if (!IsSelectedNeighbor(X, Y, Z + 1))
		{
			AppendClippedDestructionCutFace(Mesh,
				FVector3d(MinX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MaxZ),
				FVector3d(MaxX, MinY, MaxZ), FVector3d(MinX, MinY, MaxZ),
				FVector3f::UnitZ(), MaterialIndex);
		}
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
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	}

	// The regular floor flashes a complete backing cube. Cone cells can be very anisotropic
	// (700 x 100 x 100 in the placed structure) and only a curved sliver of that cube is rendered.
	// Find a compact cube wholly inside the actual cone/ring/spiral solid instead, so the emissive
	// frame cannot extend beyond a circular edge even when FlashScale is greater than one.
	const FVector CellCenter = GetPhysicalCellLocalCenter(X, Y, Z);
	const FVector CellHalfSize = BlockDimensions * 0.5;
	const float ReferenceSize = FMath::Max(
		FMath::Min3(
			static_cast<float>(BlockDimensions.X),
			static_cast<float>(BlockDimensions.Y),
			static_cast<float>(BlockDimensions.Z)),
		1.0f);
	const float DesiredEdge = ReferenceSize * 0.5f *
		FMath::Clamp(FlashScale, 0.01f, 1.0f);
	static constexpr float CandidateFractions[] = { -0.8f, -0.4f, 0.0f, 0.4f, 0.8f };
	static constexpr float ProbeFractions[] = { -1.0f, 0.0f, 1.0f };

	auto IsCubeInsideRenderedSolid = [this](const FVector& Center, float HalfEdge)
	{
		for (const float ZFraction : ProbeFractions)
		{
			for (const float YFraction : ProbeFractions)
			{
				for (const float XFraction : ProbeFractions)
				{
					if (!IsPointInsideRenderedStructure(FVector3d(
						Center + FVector(XFraction, YFraction, ZFraction) * HalfEdge)))
					{
						return false;
					}
				}
			}
		}
		return true;
	};

	for (int32 SizeAttempt = 0; SizeAttempt < 4; ++SizeAttempt)
	{
		const float Edge = DesiredEdge * FMath::Pow(0.5f, SizeAttempt);
		const float HalfEdge = Edge * 0.5f;
		FVector BestCenter = CellCenter;
		double BestDistanceSquared = TNumericLimits<double>::Max();
		bool bFoundCandidate = false;
		for (const float ZFraction : CandidateFractions)
		{
			for (const float YFraction : CandidateFractions)
			{
				for (const float XFraction : CandidateFractions)
				{
					const FVector Candidate = CellCenter + FVector(
						CellHalfSize.X * XFraction,
						CellHalfSize.Y * YFraction,
						CellHalfSize.Z * ZFraction);
					if (!IsCubeInsideRenderedSolid(Candidate, HalfEdge))
					{
						continue;
					}
					const double DistanceSquared = FVector::DistSquared(Candidate, CellCenter);
					if (DistanceSquared < BestDistanceSquared)
					{
						BestCenter = Candidate;
						BestDistanceSquared = DistanceSquared;
						bFoundCandidate = true;
					}
				}
			}
		}
		if (bFoundCandidate)
		{
			return FTransform(
				FQuat::Identity,
				BestCenter,
				FVector(Edge / 100.0f));
		}
	}

	// A zero-sized instance is preferable to a visible cuboid outside an exceptionally thin solid.
	return FTransform(FQuat::Identity, CellCenter, FVector::ZeroVector);
}

void AChunkedDestructibleConeStructure::SpawnCellFlash(FIntPoint Cell, double CurrentTime)
{
	if (FlashDuration <= 0.0f || !GetWorld() ||
		!Cells.IsValidIndex(ToCellIndex(Cell)) || ChunkCount.X <= 0)
	{
		return;
	}

	// Cone platforms are curved solids clipped into rectangular logical cells. Reusing the floor's
	// cube ISM makes every occupied Z layer appear as a separate white box. Build one combined flash
	// from the same exact cone/ring/helix triangles as the visible structure. Cells processed in the
	// same destruction batch share a component, so a thick spiral flashes as one continuous shape.
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	const bool bContinueCurrentBatch = CurrentSurfaceFlashComponent.IsValid() &&
		FMath::IsNearlyEqual(CurrentSurfaceFlashTime, CurrentTime, UE_DOUBLE_SMALL_NUMBER);
	UDynamicMeshComponent* SurfaceFlash = bContinueCurrentBatch
		? CurrentSurfaceFlashComponent.Get()
		: nullptr;
	if (!SurfaceFlash)
	{
		SurfaceFlash = NewObject<UDynamicMeshComponent>(this);
		AddInstanceComponent(SurfaceFlash);
		SurfaceFlash->SetupAttachment(GetRootComponent());
		SurfaceFlash->SetMobility(EComponentMobility::Movable);
		SurfaceFlash->SetRelativeTransform(FTransform::Identity);
		SurfaceFlash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SurfaceFlash->SetGenerateOverlapEvents(false);
		SurfaceFlash->SetCastShadow(false);
		SurfaceFlash->SetCanEverAffectNavigation(false);
		SurfaceFlash->ComponentTags.Add(TEXT("ConeSurfaceFlash"));
		SurfaceFlash->SetNumMaterials(2);
		SurfaceFlash->SetMaterial(0, FlashMaterialInstance);
		SurfaceFlash->SetMaterial(1, FlashMaterialInstance);
		SurfaceFlash->RegisterComponent();

		ActiveSurfaceFlashComponents.RemoveAll(
			[](const TWeakObjectPtr<UDynamicMeshComponent>& Component)
			{
				return !Component.IsValid();
			});
		const TWeakObjectPtr<UDynamicMeshComponent> WeakFlash(SurfaceFlash);
		ActiveSurfaceFlashComponents.Add(WeakFlash);
		CurrentSurfaceFlashComponent = WeakFlash;
		CurrentSurfaceFlashCells.Reset();
		CurrentSurfaceFlashTime = CurrentTime;
		FTimerDelegate CleanupDelegate = FTimerDelegate::CreateWeakLambda(
			this,
			[this, WeakFlash]()
			{
				if (UDynamicMeshComponent* Component = WeakFlash.Get())
				{
					RemoveInstanceComponent(Component);
					Component->DestroyComponent();
				}
				ActiveSurfaceFlashComponents.Remove(WeakFlash);
				if (CurrentSurfaceFlashComponent == WeakFlash)
				{
					CurrentSurfaceFlashComponent.Reset();
					CurrentSurfaceFlashCells.Reset();
					CurrentSurfaceFlashTime = -TNumericLimits<double>::Max();
				}
			});
		FTimerHandle CleanupHandle;
		GetWorld()->GetTimerManager().SetTimer(
			CleanupHandle,
			MoveTemp(CleanupDelegate),
			FMath::Max(FlashDuration, KINDA_SMALL_NUMBER),
			false);
	}
	CurrentSurfaceFlashCells.Add(Cell);

	FDynamicMesh3 SurfaceFlashMesh;
	SurfaceFlashMesh.EnableVertexNormals(FVector3f::UnitZ());
	SurfaceFlashMesh.EnableAttributes();
	SurfaceFlashMesh.Attributes()->SetNumUVLayers(1);
	SurfaceFlashMesh.Attributes()->EnableMaterialID();
	SurfaceFlashCellFilter = CurrentSurfaceFlashCells;
	TSet<int32> FlashChunkIndices;
	bool bNeedsConeSurface = false;
	bool bNeedsPlatformSurface = false;
	for (const FIntPoint FlashCell : CurrentSurfaceFlashCells)
	{
		if (Cells.IsValidIndex(ToCellIndex(FlashCell)) &&
			Cells[ToCellIndex(FlashCell)].MaterialIndex == 1)
		{
			bNeedsPlatformSurface = true;
		}
		else
		{
			bNeedsConeSurface = true;
		}
		const int32 ChunkIndex = FlashCell.X / SafeChunkSide +
			(FlashCell.Y / SafeChunkSide) * ChunkCount.X;
		if (ChunkIndex >= 0 && ChunkIndex < ChunkCount.X * ChunkCount.Y)
		{
			FlashChunkIndices.Add(ChunkIndex);
		}
	}
	for (const int32 ChunkIndex : FlashChunkIndices)
	{
		if (bNeedsConeSurface)
		{
			AppendConeSurfaces(SurfaceFlashMesh, ChunkIndex);
		}
		if (bNeedsPlatformSurface)
		{
			AppendPlatformSurfaces(SurfaceFlashMesh, ChunkIndex);
		}
	}
	AppendSurfaceFlashBoundaryFaces(SurfaceFlashMesh, CurrentSurfaceFlashCells);
	SurfaceFlashCellFilter.Reset();
	if (SurfaceFlashMesh.TriangleCount() <= 0)
	{
		return;
	}

	UDynamicMesh* DynamicMesh = NewObject<UDynamicMesh>(SurfaceFlash);
	DynamicMesh->SetMesh(MoveTemp(SurfaceFlashMesh));
	SurfaceFlash->SetDynamicMesh(DynamicMesh);
	SurfaceFlash->NotifyMeshUpdated();
	SurfaceFlash->SetVisibility(true, true);
	SurfaceFlash->MarkRenderStateDirty();
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
	const FVector LocalCenter = GetCellFlashTransform(Cell).GetLocation();
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
		const float ReferenceSize = FMath::Max(
			FMath::Min3(
				static_cast<float>(BlockDimensions.X),
				static_cast<float>(BlockDimensions.Y),
				static_cast<float>(BlockDimensions.Z)),
			1.0f);
		const float FragmentEdge = FMath::Min(
			ReferenceSize * FragmentScale,
			FMath::Clamp(MaxDebrisPieceSize, 2.0f, 100.0f));
		const FVector FragmentSize = FVector(
			FragmentEdge * FMath::FRandRange(0.75f, 1.0f),
			FragmentEdge * FMath::FRandRange(0.75f, 1.0f),
			FragmentEdge * FMath::FRandRange(0.75f, 1.0f));
		const float OffsetRadius = ReferenceSize * 0.12f;
		const FVector LocalOffset(
			FMath::FRandRange(-OffsetRadius, OffsetRadius),
			FMath::FRandRange(-OffsetRadius, OffsetRadius),
			FMath::FRandRange(-OffsetRadius, OffsetRadius));
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
