// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedFloor.h"
#include "Materials/Material.h"

AChunkedFloor::AChunkedFloor()
{
	// A neutral default makes the two main drop-downs (shape and pattern) immediately useful.
	OuterShape = EChunkedDecorativeFloorShape::Grid;
	ShapeSize = FVector2D(5000.0, 5000.0);
	bUseBrightBorder = false;
	FloorPattern = EChunkedFloorPattern::Checkerboard;
}

void AChunkedFloor::SetFloorShape(
	EChunkedDecorativeFloorShape NewShape,
	FVector2D NewShapeSize,
	bool bPreserveCellState)
{
	SetDecorativeFloorShape(NewShape, NewShapeSize, bPreserveCellState);
}

void AChunkedFloor::ConfigureChunkedFloor(
	EChunkedDecorativeFloorShape NewShape,
	FVector2D NewShapeSize,
	FVector NewCellDimensions,
	FVector2D NewCellSpacing,
	bool bPreserveCellState)
{
	ConfigureDecorativeFloor(
		NewShape,
		NewShapeSize,
		NewCellDimensions,
		NewCellSpacing,
		bPreserveCellState);
}

void AChunkedFloor::RandomizeFloorParameters(
	float MinFloorSize,
	float MaxFloorSize,
	FRandomStream& RandomStream)
{
	const float SafeMinSize = FMath::Max(MinFloorSize, 1.0f);
	const float SafeMaxSize = FMath::Max(MaxFloorSize, SafeMinSize);
	const float SizeX = RandomStream.FRandRange(SafeMinSize, SafeMaxSize);
	const float SizeY = RandomStream.FRandRange(SafeMinSize, SafeMaxSize);

	static constexpr EChunkedDecorativeFloorShape AvailableShapes[] =
	{
		EChunkedDecorativeFloorShape::Grid,
		EChunkedDecorativeFloorShape::Triangle,
		EChunkedDecorativeFloorShape::RoundedBlob,
		EChunkedDecorativeFloorShape::Square,
		EChunkedDecorativeFloorShape::Circle,
		EChunkedDecorativeFloorShape::Star
	};
	OuterShape = AvailableShapes[RandomStream.RandRange(0, UE_ARRAY_COUNT(AvailableShapes) - 1)];
	ShapeSize = FVector2D(SizeX, SizeY);
	if (OuterShape == EChunkedDecorativeFloorShape::Square ||
		OuterShape == EChunkedDecorativeFloorShape::Circle)
	{
		ShapeSize.Y = ShapeSize.X;
	}
	AreaSize = ShapeSize;

	const float ShortestSide = FMath::Min(ShapeSize.X, ShapeSize.Y);
	const float MaximumCellWidth = FMath::Max(1.0f, FMath::Min(ShortestSide * 0.3f, 250.0f));
	const float MinimumCellWidth = FMath::Min(MaximumCellWidth, 40.0f);
	CellDimensions = FVector(
		RandomStream.FRandRange(MinimumCellWidth, MaximumCellWidth),
		RandomStream.FRandRange(MinimumCellWidth, MaximumCellWidth),
		RandomStream.FRandRange(20.0f, 100.0f));
	const float MaximumSpacing = FMath::Min(CellDimensions.X, CellDimensions.Y) * 0.15f;
	CellSpacing = RandomStream.FRand() < 0.35f
		? FVector2D::ZeroVector
		: FVector2D(
			RandomStream.FRandRange(0.0f, MaximumSpacing),
			RandomStream.FRandRange(0.0f, MaximumSpacing));
	FillRule = static_cast<EChunkedFloorFillRule>(RandomStream.RandRange(0, 2));
	ChunkSideLength = RandomStream.RandRange(4, 32);
	bGenerateBottomFaces = RandomStream.RandRange(0, 1) != 0;
	bGenerateCollision = true;
	bCastFloorShadow = RandomStream.RandRange(0, 1) != 0;
	UVWorldUnits = RandomStream.FRandRange(25.0f, 250.0f);

	static constexpr EChunkedFloorPattern AvailablePatterns[] =
	{
		EChunkedFloorPattern::Solid,
		EChunkedFloorPattern::Checkerboard,
		EChunkedFloorPattern::StripesX,
		EChunkedFloorPattern::StripesY,
		EChunkedFloorPattern::DiagonalStripes,
		EChunkedFloorPattern::ConcentricRings,
		EChunkedFloorPattern::SeededRandom,
		EChunkedFloorPattern::HypnoticSpiral,
		EChunkedFloorPattern::HeartMotif,
		EChunkedFloorPattern::StarMotif,
		EChunkedFloorPattern::FlowerMotif
	};
	FloorPattern = AvailablePatterns[RandomStream.RandRange(0, UE_ARRAY_COUNT(AvailablePatterns) - 1)];

	// A Blueprint can replace the inherited palette with an empty or partly empty array.
	// Normalize it here so every material ID emitted by the generated mesh has a real material.
	UMaterialInterface* FallbackMaterial = nullptr;
	for (const TObjectPtr<UMaterialInterface>& Material : FloorMaterials)
	{
		if (Material)
		{
			FallbackMaterial = Material.Get();
			break;
		}
	}
	if (!FallbackMaterial)
	{
		FallbackMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	while (FloorMaterials.Num() < 3)
	{
		FloorMaterials.Add(FallbackMaterial);
	}
	for (TObjectPtr<UMaterialInterface>& Material : FloorMaterials)
	{
		if (!Material)
		{
			Material = FallbackMaterial;
		}
	}
	if (!FlashMaterial)
	{
		FlashMaterial = FallbackMaterial;
	}

	const int32 MaterialCount = FloorMaterials.Num();
	PrimaryMaterialIndex = RandomStream.RandRange(0, MaterialCount - 1);
	SecondaryMaterialIndex = (PrimaryMaterialIndex + RandomStream.RandRange(1, MaterialCount - 1)) % MaterialCount;
	PatternCellScale = RandomStream.RandRange(1, 5);
	PatternRandomSeed = RandomStream.RandRange(-1000000, 1000000);

	SpiralSettings.Turns = RandomStream.FRandRange(1.0f, 12.0f);
	SpiralSettings.BandFraction = RandomStream.FRandRange(0.1f, 0.9f);
	SpiralSettings.RotationDegrees = RandomStream.FRandRange(0.0f, 360.0f);
	SpiralSettings.CenterOffset = FVector2D(
		RandomStream.FRandRange(-ShapeSize.X * 0.15f, ShapeSize.X * 0.15f),
		RandomStream.FRandRange(-ShapeSize.Y * 0.15f, ShapeSize.Y * 0.15f));
	SpiralSettings.CurveResolution = RandomStream.RandRange(128, 1024);
	SpiralSettings.bClockwise = RandomStream.RandRange(0, 1) != 0;

	TriangleApexOffset = RandomStream.FRandRange(-1.0f, 1.0f);
	StarPointCount = RandomStream.RandRange(3, 12);
	StarInnerRadius = RandomStream.FRandRange(0.1f, 0.9f);
	StarRotationDegrees = RandomStream.FRandRange(0.0f, 360.0f);
	CircleEdgeSegments = RandomStream.RandRange(16, 128);
	BlobLobeCount = RandomStream.RandRange(3, 16);
	BlobLobeStrength = RandomStream.FRandRange(0.0f, 0.45f);
	BlobEdgeRoundness = RandomStream.FRandRange(0.0f, 1.0f);
	BlobShapeSeed = RandomStream.RandRange(-1000000, 1000000);
	BlobOutlineSamplesPerLobe = RandomStream.RandRange(4, 16);
	bUseBrightBorder = RandomStream.RandRange(0, 1) != 0;
	BorderWidth = RandomStream.FRandRange(0.0f, ShortestSide * 0.2f);
	BorderMaterialIndex = RandomStream.RandRange(0, MaterialCount - 1);

	auto RandomMotifSize = [&RandomStream, this]()
	{
		return FVector2D(
			RandomStream.FRandRange(ShapeSize.X * 0.3f, ShapeSize.X * 0.9f),
			RandomStream.FRandRange(ShapeSize.Y * 0.3f, ShapeSize.Y * 0.9f));
	};
	auto RandomMotifOffset = [&RandomStream, this]()
	{
		return FVector2D(
			RandomStream.FRandRange(-ShapeSize.X * 0.15f, ShapeSize.X * 0.15f),
			RandomStream.FRandRange(-ShapeSize.Y * 0.15f, ShapeSize.Y * 0.15f));
	};
	auto AssignRandomMaterialSet = [&RandomStream, MaterialCount](
		int32& BackgroundIndex,
		int32& AccentIndex,
		int32& InnerIndex)
	{
		BackgroundIndex = RandomStream.RandRange(0, MaterialCount - 1);
		AccentIndex = (BackgroundIndex + RandomStream.RandRange(1, MaterialCount - 1)) % MaterialCount;
		do
		{
			InnerIndex = RandomStream.RandRange(0, MaterialCount - 1);
		}
		while (InnerIndex == BackgroundIndex || InnerIndex == AccentIndex);
	};

	AssignRandomMaterialSet(
		HeartMotifSettings.BackgroundMaterialIndex,
		HeartMotifSettings.AccentMaterialIndex,
		HeartMotifSettings.InnerMaterialIndex);
	HeartMotifSettings.Size = RandomMotifSize();
	HeartMotifSettings.InnerScale = RandomStream.FRandRange(0.05f, 0.9f);
	HeartMotifSettings.bAutoCenter = RandomStream.RandRange(0, 1) != 0;
	HeartMotifSettings.Offset = RandomMotifOffset();
	HeartMotifSettings.RotationDegrees = RandomStream.FRandRange(0.0f, 360.0f);
	HeartMotifSettings.CurveResolution = RandomStream.RandRange(32, 256);

	AssignRandomMaterialSet(
		StarMotifSettings.BackgroundMaterialIndex,
		StarMotifSettings.AccentMaterialIndex,
		StarMotifSettings.InnerMaterialIndex);
	StarMotifSettings.Size = RandomMotifSize();
	StarMotifSettings.PointCount = RandomStream.RandRange(3, 16);
	StarMotifSettings.InnerRadius = RandomStream.FRandRange(0.1f, 0.9f);
	StarMotifSettings.InnerScale = RandomStream.FRandRange(0.05f, 0.9f);
	StarMotifSettings.bAutoCenter = RandomStream.RandRange(0, 1) != 0;
	StarMotifSettings.Offset = RandomMotifOffset();
	StarMotifSettings.RotationDegrees = RandomStream.FRandRange(0.0f, 360.0f);

	FlowerMotifSettings.FlowerType = static_cast<EChunkedFlowerType>(RandomStream.RandRange(0, 2));
	AssignRandomMaterialSet(
		FlowerMotifSettings.BackgroundMaterialIndex,
		FlowerMotifSettings.OutlineMaterialIndex,
		FlowerMotifSettings.CenterMaterialIndex);
	FlowerMotifSettings.FlowerSize = RandomMotifSize();
	FlowerMotifSettings.PetalCount = RandomStream.RandRange(3, 24);
	FlowerMotifSettings.PetalLengthRatio = RandomStream.FRandRange(0.15f, 0.75f);
	FlowerMotifSettings.PetalWidthRatio = RandomStream.FRandRange(0.08f, 0.65f);
	FlowerMotifSettings.CenterRadiusRatio = RandomStream.FRandRange(0.05f, 0.55f);
	FlowerMotifSettings.LargeTypeScale = RandomStream.FRandRange(1.0f, 4.0f);
	FlowerMotifSettings.SimplePetalCount = RandomStream.RandRange(3, 12);
	FlowerMotifSettings.SimplePetalLengthRatio = RandomStream.FRandRange(0.3f, 0.8f);
	FlowerMotifSettings.SimplePetalWidthRatio = RandomStream.FRandRange(0.2f, 0.8f);
	FlowerMotifSettings.SimpleCenterRadiusRatio = RandomStream.FRandRange(0.15f, 0.65f);
	FlowerMotifSettings.OutlineWidth = RandomStream.FRandRange(0.0f, ShortestSide * 0.05f);
	FlowerMotifSettings.bAutoCenter = RandomStream.RandRange(0, 1) != 0;
	FlowerMotifSettings.Offset = RandomMotifOffset();
	FlowerMotifSettings.RotationDegrees = RandomStream.FRandRange(0.0f, 360.0f);
	FlowerMotifSettings.CurveResolution = RandomStream.RandRange(8, 64);

	// The default projectile deals 25 damage, so every generated cell disappears when hit.
	DefaultCellHealth = RandomStream.FRandRange(1.0f, 25.0f);
	bDestructibleByDefault = true;
	DefaultBlastShape = static_cast<EChunkedFloorBlastShape>(RandomStream.RandRange(0, 2));
	bUseRadialDamageFalloff = RandomStream.RandRange(0, 1) != 0;
	MinimumRadialDamageFraction = RandomStream.FRandRange(0.0f, 1.0f);
	bUseDestructionRipple = RandomStream.RandRange(0, 1) != 0;
	DestructionRippleSpeed = RandomStream.FRandRange(500.0f, 6000.0f);
	FlashColor = FLinearColor(
		RandomStream.FRandRange(0.2f, 1.0f),
		RandomStream.FRandRange(0.2f, 1.0f),
		RandomStream.FRandRange(0.2f, 1.0f));
	FlashIntensity = RandomStream.FRandRange(2.0f, 30.0f);
	FlashDuration = RandomStream.FRandRange(0.03f, 0.25f);
	FlashScale = RandomStream.FRandRange(1.01f, 1.12f);
	bPlayDestroyTickSound = RandomStream.RandRange(0, 1) != 0;
	DestroySoundVolume = RandomStream.FRandRange(0.1f, 1.0f);
	const float MinimumPitch = RandomStream.FRandRange(0.5f, 1.0f);
	DestroySoundPitchRange = FVector2D(MinimumPitch, RandomStream.FRandRange(MinimumPitch, 1.5f));
	MaxDestroySoundsPerSecond = RandomStream.RandRange(4, 32);
}
