// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleHouseActorDestructable.h"

#include "UObject/UnrealType.h"

namespace
{
	constexpr int32 MaximumDestructableFloorCount = 100;
	constexpr int32 MaximumDestructableWindowsPerFloor = 20;
	constexpr float MinimumDestructableCubeSize = 5.0f;

	int32 SnapLengthToCellCount(float Length, float CubeSize, float Spacing, int32 MinimumCount)
	{
		const float Pitch = CubeSize + Spacing;
		return FMath::Max(
			FMath::RoundToInt((FMath::Max(Length, CubeSize) + Spacing) / Pitch),
			MinimumCount);
	}

	bool IsPointInsideOpening(
		ESimpleHouseDestructableWindowType Type,
		float PointHorizontal,
		float PointZ,
		float OpeningCenterHorizontal,
		float OpeningCenterZ,
		float OpeningWidth,
		float OpeningHeight,
		float RoundedDegree,
		float MinimumHalfExtent)
	{
		const float HalfWidth = FMath::Max(OpeningWidth * 0.5f, MinimumHalfExtent);
		const float HalfHeight = FMath::Max(OpeningHeight * 0.5f, MinimumHalfExtent);
		const float LocalX = PointHorizontal - OpeningCenterHorizontal;
		const float LocalZ = PointZ - OpeningCenterZ;

		switch (Type)
		{
		case ESimpleHouseDestructableWindowType::Circular:
		{
			const float Radius = HalfWidth;
			return LocalX * LocalX + LocalZ * LocalZ <= Radius * Radius;
		}

		case ESimpleHouseDestructableWindowType::Arched:
		{
			const float ArchHeight = FMath::Min(HalfWidth, HalfHeight * 1.8f);
			const float ShoulderZ = HalfHeight - ArchHeight;
			if (FMath::Abs(LocalX) > HalfWidth || LocalZ < -HalfHeight)
			{
				return false;
			}
			if (LocalZ <= ShoulderZ)
			{
				return true;
			}
			const float NormalizedX = LocalX / HalfWidth;
			const float NormalizedZ = (LocalZ - ShoulderZ) / ArchHeight;
			return NormalizedX * NormalizedX + NormalizedZ * NormalizedZ <= 1.0f;
		}

		case ESimpleHouseDestructableWindowType::RoundedCorners:
		{
			if (FMath::Abs(LocalX) > HalfWidth || FMath::Abs(LocalZ) > HalfHeight)
			{
				return false;
			}
			const float MaximumRadius = FMath::Min(HalfWidth, HalfHeight);
			const float Radius = MaximumRadius * FMath::Clamp(RoundedDegree, 0.0f, 100.0f) / 100.0f;
			if (Radius <= KINDA_SMALL_NUMBER)
			{
				return true;
			}
			const float CornerX = FMath::Max(FMath::Abs(LocalX) - (HalfWidth - Radius), 0.0f);
			const float CornerZ = FMath::Max(FMath::Abs(LocalZ) - (HalfHeight - Radius), 0.0f);
			return CornerX * CornerX + CornerZ * CornerZ <= Radius * Radius;
		}

		case ESimpleHouseDestructableWindowType::Rectangular:
		default:
			return FMath::Abs(LocalX) <= HalfWidth && FMath::Abs(LocalZ) <= HalfHeight;
		}
	}
}

ASimpleHouseActorDestructable::ASimpleHouseActorDestructable()
{
	// Match SimpleHouseActor19's default silhouette on a 50 cm destruction grid.
	FrontFacade.bEnableOpenings = true;
	SynchronizeFloorWindowSettings();
	if (!FrontFacade.FloorWindows.IsEmpty())
	{
		FrontFacade.FloorWindows[0].bHasWindows = false;
	}

	FloorPattern = EChunkedFloorPattern::Solid;
	bHollowInterior = true;
	bGenerateDoor = false;
	bGenerateWindows = false;
	bAddChimney = false;
	ChunkSideLength = 24;
	ApplySettingsToCubeGrid();
}

void ASimpleHouseActorDestructable::OnConstruction(const FTransform& Transform)
{
	SynchronizeFloorWindowSettings();
	ApplySettingsToCubeGrid();
	Super::OnConstruction(Transform);
}

#if WITH_EDITOR
bool ASimpleHouseActorDestructable::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		const FName PropertyName = InProperty->GetFName();
		static const TSet<FName> DerivedSettingNames = {
			FName(TEXT("WallBlockCount")),
			FName(TEXT("RoofHeightBlocks")),
			FName(TEXT("RoofOverhangBlocks")),
			FName(TEXT("BlockDimensions")),
			FName(TEXT("BlockSpacing")),
			FName(TEXT("bHollowInterior")),
			FName(TEXT("bGenerateDoor")),
			FName(TEXT("DoorWidthBlocks")),
			FName(TEXT("DoorHeightBlocks")),
			FName(TEXT("DoorHorizontalOffsetBlocks")),
			FName(TEXT("bGenerateWindows")),
			FName(TEXT("WindowColumns")),
			FName(TEXT("WindowRows")),
			FName(TEXT("WindowWidthBlocks")),
			FName(TEXT("WindowHeightBlocks")),
			FName(TEXT("WindowBottomBlocks")),
			FName(TEXT("WindowVerticalSpacingBlocks")),
			FName(TEXT("bWindowsOnBackWall")),
			FName(TEXT("bAddChimney"))
		};
		if (DerivedSettingNames.Contains(PropertyName))
		{
			return false;
		}
	}
	return Super::CanEditChange(InProperty);
}
#endif

void ASimpleHouseActorDestructable::SynchronizeFloorWindowSettings()
{
	Building.FloorCount = FMath::Clamp(Building.FloorCount, 1, MaximumDestructableFloorCount);

	auto SynchronizeFacade = [this](FSimpleHouseDestructableFacadeSettings& Facade)
	{
		Facade.FloorWindows.SetNum(Building.FloorCount);
		for (int32 FloorIndex = 0; FloorIndex < Facade.FloorWindows.Num(); ++FloorIndex)
		{
			FSimpleHouseDestructableFloorWindowSettings& WindowSettings = Facade.FloorWindows[FloorIndex];
			WindowSettings.FloorNumber = FloorIndex + 1;
			WindowSettings.WindowCount = FMath::Clamp(
				WindowSettings.WindowCount,
				0,
				MaximumDestructableWindowsPerFloor);
		}
	};

	SynchronizeFacade(FrontFacade);
	SynchronizeFacade(BackFacade);
	SynchronizeFacade(SideWall1Facade);
	SynchronizeFacade(SideWall2Facade);
}

void ASimpleHouseActorDestructable::ApplySettingsToCubeGrid()
{
	const float SafeCubeSize = FMath::Max(Building.CubeSize, MinimumDestructableCubeSize);
	const float SafeSpacing = FMath::Max(Building.CubeSpacing, 0.0f);
	const float SafeFloorHeight = FMath::Max(Building.FloorHeight, SafeCubeSize);
	const float TotalWallHeight = SafeFloorHeight * FMath::Max(Building.FloorCount, 1);

	WallBlockCount = FIntVector(
		SnapLengthToCellCount(Building.BaseWidth, SafeCubeSize, SafeSpacing, 3),
		SnapLengthToCellCount(Building.BaseDepth, SafeCubeSize, SafeSpacing, 3),
		SnapLengthToCellCount(TotalWallHeight, SafeCubeSize, SafeSpacing, 2));
	RoofHeightBlocks = SnapLengthToCellCount(Roof.RoofHeight, SafeCubeSize, SafeSpacing, 1);
	RoofOverhangBlocks = FMath::Clamp(
		FMath::RoundToInt(FMath::Max(Roof.Overhang, 0.0f) / (SafeCubeSize + SafeSpacing)),
		0,
		16);
	BlockDimensions = FVector(SafeCubeSize);
	BlockSpacing = FVector(SafeSpacing);

	// The facade implementation below owns the openings. These inherited switches
	// stay off so the base class does not cut a second, unrelated opening layout.
	bHollowInterior = true;
	bGenerateDoor = false;
	bGenerateWindows = false;
	bAddChimney = false;
}

void ASimpleHouseActorDestructable::RebuildDestructableHouse(bool bPreserveCellState)
{
	SynchronizeFloorWindowSettings();
	ApplySettingsToCubeGrid();
	RebuildHouse(bPreserveCellState);
}

FVector ASimpleHouseActorDestructable::GetSnappedBuildingDimensions() const
{
	const FVector Pitch = BlockDimensions + BlockSpacing;
	return FVector(
		WallBlockCount.X * Pitch.X - BlockSpacing.X,
		WallBlockCount.Y * Pitch.Y - BlockSpacing.Y,
		WallBlockCount.Z * Pitch.Z - BlockSpacing.Z);
}

bool ASimpleHouseActorDestructable::FacadeHasOpeningAtCell(
	const FSimpleHouseDestructableFacadeSettings& Facade,
	int32 HorizontalCell,
	int32 HorizontalCellCount,
	int32 Z) const
{
	if (!Facade.bEnableOpenings || HorizontalCellCount <= 0 || Z < 0)
	{
		return false;
	}

	const float CubeSize = BlockDimensions.X;
	const float Spacing = BlockSpacing.X;
	const float Pitch = CubeSize + Spacing;
	const float FacadeSpan = HorizontalCellCount * CubeSize + FMath::Max(HorizontalCellCount - 1, 0) * Spacing;
	const float HorizontalCenter = -FacadeSpan * 0.5f + CubeSize * 0.5f + HorizontalCell * Pitch;
	const float ZCenter = BlockDimensions.Z * 0.5f + Z * (BlockDimensions.Z + BlockSpacing.Z);
	const float MinimumHalfExtent = FMath::Min(BlockDimensions.X, BlockDimensions.Z) * 0.5f + KINDA_SMALL_NUMBER;

	if (Facade.Door.bHasDoor)
	{
		const ESimpleHouseDestructableWindowType DoorShape =
			Facade.Door.DoorType == ESimpleHouseDestructableDoorType::Arched
				? ESimpleHouseDestructableWindowType::Arched
				: ESimpleHouseDestructableWindowType::Rectangular;
		if (IsPointInsideOpening(
			DoorShape,
			HorizontalCenter,
			ZCenter,
			0.0f,
			FMath::Max(Facade.Door.DoorHeight, 1.0f) * 0.5f,
			FMath::Max(Facade.Door.DoorWidth, 1.0f),
			FMath::Max(Facade.Door.DoorHeight, 1.0f),
			0.0f,
			MinimumHalfExtent))
		{
			return true;
		}
	}

	const int32 SafeFloorCount = FMath::Min(Building.FloorCount, Facade.FloorWindows.Num());
	for (int32 FloorIndex = 0; FloorIndex < SafeFloorCount; ++FloorIndex)
	{
		const FSimpleHouseDestructableFloorWindowSettings& Windows = Facade.FloorWindows[FloorIndex];
		const int32 WindowCount = FMath::Clamp(Windows.WindowCount, 0, MaximumDestructableWindowsPerFloor);
		if (!Windows.bHasWindows || WindowCount <= 0)
		{
			continue;
		}

		const float RequestedHeight =
			Windows.WindowType == ESimpleHouseDestructableWindowType::Circular
				? Windows.WindowWidth
				: Windows.WindowHeight;
		const float WindowWidth = FMath::Max(Windows.WindowWidth, 1.0f);
		const float WindowHeight = FMath::Max(RequestedHeight, 1.0f);
		const float WindowSpacing = FMath::Max(Windows.WindowSpacing, 0.0f);
		const float EdgeMargin = CubeSize;
		const float UsableLeft = -FacadeSpan * 0.5f + EdgeMargin;
		const float UsableRight = FacadeSpan * 0.5f - EdgeMargin;
		const float UsableWidth = FMath::Max(UsableRight - UsableLeft, CubeSize);
		const float SafeSpacing = WindowCount > 1
			? FMath::Min(WindowSpacing, FMath::Max((UsableWidth - WindowCount * CubeSize) / (WindowCount - 1), 0.0f))
			: 0.0f;
		const float SafeWindowWidth = FMath::Min(
			WindowWidth,
			FMath::Max((UsableWidth - (WindowCount - 1) * SafeSpacing) / WindowCount, CubeSize));
		const float GroupWidth = WindowCount * SafeWindowWidth + (WindowCount - 1) * SafeSpacing;
		float FirstWindowLeft = UsableLeft;
		if (Windows.Alignment == ESimpleHouseDestructableWindowAlignment::Right)
		{
			FirstWindowLeft = UsableRight - GroupWidth;
		}
		else if (Windows.Alignment == ESimpleHouseDestructableWindowAlignment::Center)
		{
			FirstWindowLeft = (UsableLeft + UsableRight - GroupWidth) * 0.5f;
		}

		const float WindowCenterZ = (FloorIndex + 0.5f) * FMath::Max(Building.FloorHeight, CubeSize);
		for (int32 WindowIndex = 0; WindowIndex < WindowCount; ++WindowIndex)
		{
			const float WindowCenterHorizontal =
				FirstWindowLeft + SafeWindowWidth * 0.5f + WindowIndex * (SafeWindowWidth + SafeSpacing);
			if (IsPointInsideOpening(
				Windows.WindowType,
				HorizontalCenter,
				ZCenter,
				WindowCenterHorizontal,
				WindowCenterZ,
				SafeWindowWidth,
				WindowHeight,
				Windows.RoundedDegree,
				MinimumHalfExtent))
			{
				return true;
			}
		}
	}

	return false;
}

bool ASimpleHouseActorDestructable::IsOpeningCell(int32 X, int32 Y, int32 Z) const
{
	const int32 MinX = RoofOverhangBlocks;
	const int32 MinY = RoofOverhangBlocks;
	const int32 MaxX = MinX + WallBlockCount.X - 1;
	const int32 MaxY = MinY + WallBlockCount.Y - 1;
	const int32 LocalX = X - MinX;
	const int32 LocalY = Y - MinY;

	return
		(Y == MinY && FacadeHasOpeningAtCell(FrontFacade, LocalX, WallBlockCount.X, Z)) ||
		(Y == MaxY && FacadeHasOpeningAtCell(BackFacade, LocalX, WallBlockCount.X, Z)) ||
		(X == MinX && FacadeHasOpeningAtCell(SideWall1Facade, LocalY, WallBlockCount.Y, Z)) ||
		(X == MaxX && FacadeHasOpeningAtCell(SideWall2Facade, LocalY, WallBlockCount.Y, Z));
}

bool ASimpleHouseActorDestructable::IsWallCell(int32 X, int32 Y, int32 Z) const
{
	if (Z < 0 || Z >= WallBlockCount.Z)
	{
		return false;
	}

	const int32 MinX = RoofOverhangBlocks;
	const int32 MinY = RoofOverhangBlocks;
	const int32 MaxX = MinX + WallBlockCount.X - 1;
	const int32 MaxY = MinY + WallBlockCount.Y - 1;
	if (X < MinX || X > MaxX || Y < MinY || Y > MaxY)
	{
		return false;
	}

	const bool bBoundaryWall = X == MinX || X == MaxX || Y == MinY || Y == MaxY;
	if (bBoundaryWall)
	{
		return !IsOpeningCell(X, Y, Z);
	}
	return (bHasGroundFloor && Z == 0) || (Roof.bHasCeiling && Z == WallBlockCount.Z - 1);
}

bool ASimpleHouseActorDestructable::GetCellPolygonXZ(
	FIntPoint Cell,
	TArray<FVector2D>& OutPolygon) const
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
	if (IsWallCell(X, Y, Z) || IsChimneyCell(X, Y, Z))
	{
		OutPolygon = {
			FVector2D(MinX, MinZ), FVector2D(MaxX, MinZ),
			FVector2D(MaxX, MaxZ), FVector2D(MinX, MaxZ)
		};
		return true;
	}

	if (Z < WallBlockCount.Z || Z >= WallBlockCount.Z + RoofHeightBlocks)
	{
		return false;
	}

	FVector2D RoofLeft;
	FVector2D RoofRight;
	FVector2D RoofApex;
	GetRoofTriangle(RoofLeft, RoofRight, RoofApex);
	const double CenterX = (MinX + MaxX) * 0.5;
	const double CenterZ = (MinZ + MaxZ) * 0.5;
	const double HalfSpan = FMath::Max((RoofRight.X - RoofLeft.X) * 0.5, 1.0);
	const double HorizontalAlpha = FMath::Clamp(FMath::Abs(CenterX) / HalfSpan, 0.0, 1.0);
	const double RoofSurfaceZ = FMath::Lerp(RoofApex.Y, RoofLeft.Y, HorizontalAlpha);
	if (CenterZ > RoofSurfaceZ + KINDA_SMALL_NUMBER)
	{
		return false;
	}

	// Return the whole rectangle: unlike the base house's clipped wedges, this
	// variant intentionally keeps every damage cell as a true cube.
	OutPolygon = {
		FVector2D(MinX, MinZ), FVector2D(MaxX, MinZ),
		FVector2D(MaxX, MaxZ), FVector2D(MinX, MaxZ)
	};
	return true;
}
