// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleHalloweenPumpkin.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DestructibleBoxCollision.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
	constexpr float MinimumPumpkinDimension = 1.0f;
	constexpr float PumpkinSpacingTolerance = 0.01f;

	double Cross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	bool IsPointInsideTriangle(
		const FVector2D& Point,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C)
	{
		const double SignA = Cross2D(B - A, Point - A);
		const double SignB = Cross2D(C - B, Point - B);
		const double SignC = Cross2D(A - C, Point - C);
		const bool bHasNegative = SignA < 0.0 || SignB < 0.0 || SignC < 0.0;
		const bool bHasPositive = SignA > 0.0 || SignB > 0.0 || SignC > 0.0;
		return !(bHasNegative && bHasPositive);
	}
}

AChunkedDestructibleHalloweenPumpkin::AChunkedDestructibleHalloweenPumpkin()
{
	FloorPattern = EChunkedFloorPattern::Solid;
	ChunkSideLength = 64;
	bGenerateBottomFaces = true;
	bDestructibleByDefault = true;
	DefaultCellHealth = 25.0f;
	FlashColor = FLinearColor(1.0f, 0.18f, 0.01f, 1.0f);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlatMaterialFinder(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	if (FlatMaterialFinder.Succeeded())
	{
		DefaultFlatMaterial = FlatMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterialFinder(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	if (GlowMaterialFinder.Succeeded())
	{
		DefaultGlowMaterial = GlowMaterialFinder.Object;
	}

	UMaterialInterface* PaletteMaterial = DefaultFlatMaterial
		? DefaultFlatMaterial.Get()
		: UMaterial::GetDefaultMaterial(MD_Surface);
	FloorMaterials.Reset(4);
	for (int32 MaterialIndex = 0; MaterialIndex < 4; ++MaterialIndex)
	{
		FloorMaterials.Add(PaletteMaterial);
	}
}

void AChunkedDestructibleHalloweenPumpkin::OnConstruction(const FTransform& Transform)
{
	EnsurePumpkinMaterials();
	Super::OnConstruction(Transform);
}

void AChunkedDestructibleHalloweenPumpkin::BeginPlay()
{
	EnsurePumpkinMaterials();
	Super::BeginPlay();
}

#if WITH_EDITOR
bool AChunkedDestructibleHalloweenPumpkin::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		const FName PropertyName = InProperty->GetFName();
		if (PropertyName == FName(TEXT("AreaSize")) ||
			PropertyName == FName(TEXT("CellDimensions")) ||
			PropertyName == FName(TEXT("CellSpacing")) ||
			PropertyName == FName(TEXT("FillRule")) ||
			PropertyName == FName(TEXT("bGenerateBottomFaces")) ||
			PropertyName == FName(TEXT("FloorMaterials")) ||
			PropertyName == FName(TEXT("FloorPattern")) ||
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

void AChunkedDestructibleHalloweenPumpkin::RebuildPumpkin(bool bPreserveCellState)
{
	EnsurePumpkinMaterials();
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleHalloweenPumpkin::ConfigurePumpkin(
	FVector2D NewBodyRadiusBlocks,
	float NewBodyHalfHeightBlocks,
	float NewStemHeightBlocks,
	FVector NewBlockDimensions,
	FVector NewBlockSpacing,
	bool bPreserveCellState)
{
	BodyRadiusBlocks = FVector2D(
		FMath::Clamp(FMath::Abs(NewBodyRadiusBlocks.X), 3.0, 32.0),
		FMath::Clamp(FMath::Abs(NewBodyRadiusBlocks.Y), 3.0, 32.0));
	BodyHalfHeightBlocks = FMath::Clamp(FMath::Abs(NewBodyHalfHeightBlocks), 3.0f, 32.0f);
	StemHeightBlocks = FMath::Clamp(FMath::Abs(NewStemHeightBlocks), 1.0f, 16.0f);
	BlockDimensions = FVector(
		FMath::Max(FMath::Abs(NewBlockDimensions.X), MinimumPumpkinDimension),
		FMath::Max(FMath::Abs(NewBlockDimensions.Y), MinimumPumpkinDimension),
		FMath::Max(FMath::Abs(NewBlockDimensions.Z), MinimumPumpkinDimension));
	BlockSpacing = FVector(
		FMath::Max(NewBlockSpacing.X, 0.0),
		FMath::Max(NewBlockSpacing.Y, 0.0),
		FMath::Max(NewBlockSpacing.Z, 0.0));
	RebuildPumpkin(bPreserveCellState);
}

void AChunkedDestructibleHalloweenPumpkin::SetJackOLanternFace(
	bool bEnabled,
	float NewCarveDepthBlocks,
	bool bPreserveCellState)
{
	bCarveJackOLanternFace = bEnabled;
	FaceCarveDepthBlocks = FMath::Clamp(FMath::Abs(NewCarveDepthBlocks), 1.0f, 8.0f);
	RebuildPumpkin(bPreserveCellState);
}

void AChunkedDestructibleHalloweenPumpkin::SetPumpkinColors(
	FLinearColor NewBodyColor,
	FLinearColor NewRibColor,
	FLinearColor NewFaceColor,
	FLinearColor NewStemColor,
	bool bNewUseGlowingFace,
	float NewFaceGlowIntensity)
{
	BodyColor = NewBodyColor;
	RibColor = NewRibColor;
	FaceColor = NewFaceColor;
	StemColor = NewStemColor;
	bUseGlowingFace = bNewUseGlowingFace;
	FaceGlowIntensity = FMath::Max(NewFaceGlowIntensity, 0.0f);
	EnsurePumpkinMaterials();
	UpdatePumpkinMaterialParameters();
	for (UDynamicMeshComponent* Component : ChunkComponents)
	{
		ConfigureChunkComponent(Component);
		if (Component)
		{
			Component->MarkRenderStateDirty();
		}
	}
}

bool AChunkedDestructibleHalloweenPumpkin::CalculateLayout()
{
	BodyRadiusBlocks = FVector2D(
		FMath::Clamp(FMath::Abs(BodyRadiusBlocks.X), 3.0, 32.0),
		FMath::Clamp(FMath::Abs(BodyRadiusBlocks.Y), 3.0, 32.0));
	BodyHalfHeightBlocks = FMath::Clamp(FMath::Abs(BodyHalfHeightBlocks), 3.0f, 32.0f);
	VerticalProfilePower = FMath::Clamp(VerticalProfilePower, 1.5f, 6.0f);
	RibCount = FMath::Clamp(RibCount, 6, 20);
	RibDepth = FMath::Clamp(RibDepth, 0.0f, 0.32f);
	FaceCarveDepthBlocks = FMath::Clamp(FMath::Abs(FaceCarveDepthBlocks), 1.0f, 8.0f);
	StemRadiusBlocks = FMath::Clamp(FMath::Abs(StemRadiusBlocks), 0.5f, 5.0f);
	StemHeightBlocks = FMath::Clamp(FMath::Abs(StemHeightBlocks), 1.0f, 16.0f);
	StemLeanBlocks.X = FMath::Clamp(StemLeanBlocks.X, -8.0, 8.0);
	StemLeanBlocks.Y = FMath::Clamp(StemLeanBlocks.Y, -8.0, 8.0);
	FaceGlowIntensity = FMath::Max(FaceGlowIntensity, 0.0f);
	BlockDimensions = FVector(
		FMath::Max(FMath::Abs(BlockDimensions.X), MinimumPumpkinDimension),
		FMath::Max(FMath::Abs(BlockDimensions.Y), MinimumPumpkinDimension),
		FMath::Max(FMath::Abs(BlockDimensions.Z), MinimumPumpkinDimension));
	BlockSpacing = FVector(
		FMath::Max(BlockSpacing.X, 0.0),
		FMath::Max(BlockSpacing.Y, 0.0),
		FMath::Max(BlockSpacing.Z, 0.0));

	const int32 HalfGridX = FMath::CeilToInt(FMath::Max(
		BodyRadiusBlocks.X,
		StemRadiusBlocks + FMath::Abs(StemLeanBlocks.X))) + 2;
	const int32 HalfGridY = FMath::CeilToInt(FMath::Max(
		BodyRadiusBlocks.Y,
		StemRadiusBlocks + FMath::Abs(StemLeanBlocks.Y))) + 2;
	const float StemTopBlocks = BodyHalfHeightBlocks * 2.0f - 0.75f + StemHeightBlocks;
	PumpkinGridCellCount = FIntVector(
		HalfGridX * 2 + 1,
		HalfGridY * 2 + 1,
		FMath::Max(FMath::CeilToInt(StemTopBlocks) + 1, 1));

	const int64 EncodedHorizontalCellCount =
		static_cast<int64>(PumpkinGridCellCount.X) * PumpkinGridCellCount.Y;
	const int64 TotalBackingCellCount = EncodedHorizontalCellCount * PumpkinGridCellCount.Z;
	if (EncodedHorizontalCellCount > MAX_int32 || TotalBackingCellCount <= 0 ||
		TotalBackingCellCount > FMath::Max(MaxGeneratedCells, 1))
	{
		UE_LOG(LogTemp, Error,
			TEXT("%s requested %lld pumpkin backing cells, exceeding MaxGeneratedCells (%d)."),
			*GetName(), TotalBackingCellCount, MaxGeneratedCells);
		return false;
	}

	const FVector Pitch = BlockDimensions + BlockSpacing;
	CachedWorldDimensions = FVector(
		BlockDimensions.X * PumpkinGridCellCount.X +
			BlockSpacing.X * FMath::Max(PumpkinGridCellCount.X - 1, 0),
		BlockDimensions.Y * PumpkinGridCellCount.Y +
			BlockSpacing.Y * FMath::Max(PumpkinGridCellCount.Y - 1, 0),
		BlockDimensions.Z * PumpkinGridCellCount.Z +
			BlockSpacing.Z * FMath::Max(PumpkinGridCellCount.Z - 1, 0));
	PumpkinGridLocalMin = FVector(
		-CachedWorldDimensions.X * 0.5f,
		-CachedWorldDimensions.Y * 0.5f,
		0.0f);

	CellCount = FIntPoint(static_cast<int32>(EncodedHorizontalCellCount), PumpkinGridCellCount.Z);
	const int32 SafeChunkSide = GetEffectivePumpkinChunkSideLength();
	ChunkCount = FIntPoint(
		FMath::DivideAndRoundUp(CellCount.X, SafeChunkSide),
		FMath::DivideAndRoundUp(CellCount.Y, SafeChunkSide));

	EffectiveCellDimensions = BlockDimensions;
	CellDimensions = BlockDimensions;
	CellSpacing = FVector2D(BlockSpacing.X, BlockSpacing.Y);
	GridLocalMin = FVector2D(PumpkinGridLocalMin.X, PumpkinGridLocalMin.Y);
	EffectiveGridSize = FVector2D(CachedWorldDimensions.X, CachedWorldDimensions.Y);
	AreaSize = EffectiveGridSize;
	return true;
}

bool AChunkedDestructibleHalloweenPumpkin::DecodeCell(
	FIntPoint Cell,
	int32& OutX,
	int32& OutY,
	int32& OutZ) const
{
	if (Cell.X < 0 || Cell.Y < 0 || Cell.X >= CellCount.X ||
		Cell.Y >= PumpkinGridCellCount.Z || PumpkinGridCellCount.X <= 0 ||
		PumpkinGridCellCount.Y <= 0)
	{
		return false;
	}
	OutX = Cell.X % PumpkinGridCellCount.X;
	OutY = Cell.X / PumpkinGridCellCount.X;
	OutZ = Cell.Y;
	return OutY >= 0 && OutY < PumpkinGridCellCount.Y;
}

FIntPoint AChunkedDestructibleHalloweenPumpkin::EncodeCell(int32 X, int32 Y, int32 Z) const
{
	if (X < 0 || Y < 0 || Z < 0 ||
		X >= PumpkinGridCellCount.X || Y >= PumpkinGridCellCount.Y ||
		Z >= PumpkinGridCellCount.Z)
	{
		return FIntPoint::NoneValue;
	}
	return FIntPoint(X + Y * PumpkinGridCellCount.X, Z);
}

FVector AChunkedDestructibleHalloweenPumpkin::GetPhysicalCellLocalCenter(
	int32 X,
	int32 Y,
	int32 Z) const
{
	const FVector Pitch = BlockDimensions + BlockSpacing;
	return FVector(
		PumpkinGridLocalMin.X + BlockDimensions.X * 0.5f + X * Pitch.X,
		PumpkinGridLocalMin.Y + BlockDimensions.Y * 0.5f + Y * Pitch.Y,
		BlockDimensions.Z * 0.5f + Z * Pitch.Z);
}

FVector AChunkedDestructibleHalloweenPumpkin::GetCellLocalCenter(FIntPoint Cell) const
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

float AChunkedDestructibleHalloweenPumpkin::GetBodyCenterZ() const
{
	return BodyHalfHeightBlocks * (BlockDimensions.Z + BlockSpacing.Z);
}

float AChunkedDestructibleHalloweenPumpkin::GetStemBottomZ() const
{
	return GetBodyCenterZ() +
		(BodyHalfHeightBlocks - 0.75f) * (BlockDimensions.Z + BlockSpacing.Z);
}

bool AChunkedDestructibleHalloweenPumpkin::IsPumpkinBodyCell(int32 X, int32 Y, int32 Z) const
{
	if (X < 0 || Y < 0 || Z < 0 ||
		X >= PumpkinGridCellCount.X || Y >= PumpkinGridCellCount.Y ||
		Z >= PumpkinGridCellCount.Z)
	{
		return false;
	}

	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const double RadiusX = FMath::Max(BodyRadiusBlocks.X * Pitch.X, MinimumPumpkinDimension);
	const double RadiusY = FMath::Max(BodyRadiusBlocks.Y * Pitch.Y, MinimumPumpkinDimension);
	const double HalfHeight = FMath::Max(BodyHalfHeightBlocks * Pitch.Z, MinimumPumpkinDimension);
	const double NormalizedZ = (Center.Z - GetBodyCenterZ()) / HalfHeight;
	if (FMath::Abs(NormalizedZ) > 1.0)
	{
		return false;
	}

	const double SafeProfilePower = FMath::Clamp(VerticalProfilePower, 1.5f, 6.0f);
	const double CrossSection = FMath::Pow(
		FMath::Max(1.0 - FMath::Pow(FMath::Abs(NormalizedZ), SafeProfilePower), 0.0),
		1.0 / SafeProfilePower);
	const double NormalizedX = Center.X / RadiusX;
	const double NormalizedY = Center.Y / RadiusY;
	const double NormalizedRadius = FMath::Sqrt(
		NormalizedX * NormalizedX + NormalizedY * NormalizedY);
	const double Angle = FMath::Atan2(NormalizedY, NormalizedX);
	const double GrooveWave = 0.5 * (1.0 + FMath::Cos(RibCount * Angle));
	const double LobeRadius = CrossSection * (1.0 - RibDepth * GrooveWave);
	return NormalizedRadius <= LobeRadius + UE_DOUBLE_SMALL_NUMBER;
}

bool AChunkedDestructibleHalloweenPumpkin::IsStemCell(int32 X, int32 Y, int32 Z) const
{
	if (X < 0 || Y < 0 || Z < 0 ||
		X >= PumpkinGridCellCount.X || Y >= PumpkinGridCellCount.Y ||
		Z >= PumpkinGridCellCount.Z)
	{
		return false;
	}

	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const float StemBottom = GetStemBottomZ();
	const float StemHeight = StemHeightBlocks * Pitch.Z;
	const float Alpha = (Center.Z - StemBottom) / FMath::Max(StemHeight, MinimumPumpkinDimension);
	if (Alpha < 0.0f || Alpha > 1.0f)
	{
		return false;
	}

	const FVector2D StemCenter(
		StemLeanBlocks.X * Pitch.X * Alpha,
		StemLeanBlocks.Y * Pitch.Y * Alpha);
	const double Taper = FMath::Lerp(1.0, 0.62, Alpha);
	const double RadiusX = FMath::Max(StemRadiusBlocks * Pitch.X * Taper, MinimumPumpkinDimension);
	const double RadiusY = FMath::Max(StemRadiusBlocks * Pitch.Y * Taper, MinimumPumpkinDimension);
	const double DeltaX = (Center.X - StemCenter.X) / RadiusX;
	const double DeltaY = (Center.Y - StemCenter.Y) / RadiusY;
	return DeltaX * DeltaX + DeltaY * DeltaY <= 1.0;
}

bool AChunkedDestructibleHalloweenPumpkin::IsFaceProjection(const FVector& LocalCenter) const
{
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const double Horizontal = LocalCenter.Y /
		FMath::Max(BodyRadiusBlocks.Y * Pitch.Y, MinimumPumpkinDimension);
	const double Vertical = (LocalCenter.Z - GetBodyCenterZ()) /
		FMath::Max(BodyHalfHeightBlocks * Pitch.Z, MinimumPumpkinDimension);
	const FVector2D Point(Horizontal, Vertical);

	const bool bLeftEye = IsPointInsideTriangle(
		Point,
		FVector2D(-0.66, 0.18),
		FVector2D(-0.13, 0.18),
		FVector2D(-0.39, 0.55));
	const bool bRightEye = IsPointInsideTriangle(
		Point,
		FVector2D(0.13, 0.18),
		FVector2D(0.66, 0.18),
		FVector2D(0.39, 0.55));
	const bool bNose = IsPointInsideTriangle(
		Point,
		FVector2D(-0.15, -0.04),
		FVector2D(0.15, -0.04),
		FVector2D(0.0, 0.20));

	const double AbsoluteHorizontal = FMath::Abs(Horizontal);
	bool bMouth = false;
	if (AbsoluteHorizontal <= 0.78)
	{
		const double Across = AbsoluteHorizontal / 0.78;
		const double MouthCenter = -0.39 + 0.22 * FMath::Pow(Across, 1.55);
		bMouth = FMath::Abs(Vertical - MouthCenter) <= 0.145;

		// Keep alternating orange teeth in the cutout to preserve the classic jagged grin.
		const bool bUpperTooth = Vertical > MouthCenter + 0.025 &&
			((Horizontal > -0.47 && Horizontal < -0.27) ||
			 (Horizontal > 0.19 && Horizontal < 0.39));
		const bool bLowerTooth = Vertical < MouthCenter - 0.025 &&
			((Horizontal > -0.13 && Horizontal < 0.07) ||
			 (Horizontal > 0.48 && Horizontal < 0.64));
		bMouth = bMouth && !bUpperTooth && !bLowerTooth;
	}

	return bLeftEye || bRightEye || bNose || bMouth;
}

int32 AChunkedDestructibleHalloweenPumpkin::CountBodyCellsTowardFront(
	int32 X,
	int32 Y,
	int32 Z) const
{
	int32 Count = 0;
	for (int32 TestX = X + 1; TestX < PumpkinGridCellCount.X; ++TestX)
	{
		if (IsPumpkinBodyCell(TestX, Y, Z))
		{
			++Count;
		}
	}
	return Count;
}

bool AChunkedDestructibleHalloweenPumpkin::IsFaceCarvedCell(int32 X, int32 Y, int32 Z) const
{
	if (!bCarveJackOLanternFace || !IsPumpkinBodyCell(X, Y, Z))
	{
		return false;
	}
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	if (Center.X <= 0.0f || !IsFaceProjection(Center))
	{
		return false;
	}
	return CountBodyCellsTowardFront(X, Y, Z) < FMath::CeilToInt(FaceCarveDepthBlocks);
}

bool AChunkedDestructibleHalloweenPumpkin::IsFaceBackingCell(int32 X, int32 Y, int32 Z) const
{
	if (!bCarveJackOLanternFace || !IsPumpkinBodyCell(X, Y, Z))
	{
		return false;
	}
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	return Center.X > 0.0f && IsFaceProjection(Center) &&
		CountBodyCellsTowardFront(X, Y, Z) == FMath::CeilToInt(FaceCarveDepthBlocks);
}

bool AChunkedDestructibleHalloweenPumpkin::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return false;
	}
	return IsStemCell(X, Y, Z) ||
		(IsPumpkinBodyCell(X, Y, Z) && !IsFaceCarvedCell(X, Y, Z));
}

int32 AChunkedDestructibleHalloweenPumpkin::EvaluatePatternMaterial(FIntPoint Cell) const
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return BodyMaterialSlot;
	}
	if (IsStemCell(X, Y, Z))
	{
		return StemMaterialSlot;
	}
	if (IsFaceBackingCell(X, Y, Z))
	{
		return FaceMaterialSlot;
	}

	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const double NormalizedX = Center.X /
		FMath::Max(BodyRadiusBlocks.X * Pitch.X, MinimumPumpkinDimension);
	const double NormalizedY = Center.Y /
		FMath::Max(BodyRadiusBlocks.Y * Pitch.Y, MinimumPumpkinDimension);
	const double GrooveWave = FMath::Cos(RibCount * FMath::Atan2(NormalizedY, NormalizedX));
	return GrooveWave > 0.42 ? RibMaterialSlot : BodyMaterialSlot;
}

bool AChunkedDestructibleHalloweenPumpkin::CellExists3D(int32 X, int32 Y, int32 Z) const
{
	const FIntPoint Cell = EncodeCell(X, Y, Z);
	return Cell != FIntPoint::NoneValue && Cells.IsValidIndex(ToCellIndex(Cell)) &&
		Cells[ToCellIndex(Cell)].bExists;
}

void AChunkedDestructibleHalloweenPumpkin::BuildChunkMesh(
	int32 ChunkIndex,
	FDynamicMesh3& OutMesh) const
{
	OutMesh.EnableVertexNormals(FVector3f::UnitZ());
	OutMesh.EnableAttributes();
	OutMesh.Attributes()->SetNumUVLayers(1);
	OutMesh.Attributes()->EnableMaterialID();

	if (ChunkCount.X <= 0)
	{
		return;
	}
	const int32 SafeChunkSide = GetEffectivePumpkinChunkSideLength();
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
			const FVector Center = GetPhysicalCellLocalCenter(X, Y, DecodedZ);
			const double MinX = Center.X - BlockDimensions.X * 0.5;
			const double MaxX = Center.X + BlockDimensions.X * 0.5;
			const double MinY = Center.Y - BlockDimensions.Y * 0.5;
			const double MaxY = Center.Y + BlockDimensions.Y * 0.5;
			const double MinCellZ = Center.Z - BlockDimensions.Z * 0.5;
			const double MaxCellZ = Center.Z + BlockDimensions.Z * 0.5;
			const int32 MaterialIndex = CellData.MaterialIndex;
			auto NeedsFace = [this](int32 NeighborX, int32 NeighborY, int32 NeighborZ, bool bAxisHasGap)
			{
				return bAxisHasGap || !CellExists3D(NeighborX, NeighborY, NeighborZ);
			};

			if (NeedsFace(X - 1, Y, DecodedZ, BlockSpacing.X > PumpkinSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MinX, MinY, MinCellZ), FVector3d(MinX, MinY, MaxCellZ),
					FVector3d(MinX, MaxY, MaxCellZ), FVector3d(MinX, MaxY, MinCellZ),
					-FVector3f::UnitX(), MaterialIndex,
					BlockDimensions.Z, BlockDimensions.Y, UVWorldUnits);
			}
			if (NeedsFace(X + 1, Y, DecodedZ, BlockSpacing.X > PumpkinSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MaxX, MaxY, MinCellZ), FVector3d(MaxX, MaxY, MaxCellZ),
					FVector3d(MaxX, MinY, MaxCellZ), FVector3d(MaxX, MinY, MinCellZ),
					FVector3f::UnitX(), MaterialIndex,
					BlockDimensions.Z, BlockDimensions.Y, UVWorldUnits);
			}
			if (NeedsFace(X, Y - 1, DecodedZ, BlockSpacing.Y > PumpkinSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MaxX, MinY, MinCellZ), FVector3d(MaxX, MinY, MaxCellZ),
					FVector3d(MinX, MinY, MaxCellZ), FVector3d(MinX, MinY, MinCellZ),
					-FVector3f::UnitY(), MaterialIndex,
					BlockDimensions.Z, BlockDimensions.X, UVWorldUnits);
			}
			if (NeedsFace(X, Y + 1, DecodedZ, BlockSpacing.Y > PumpkinSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MinX, MaxY, MinCellZ), FVector3d(MinX, MaxY, MaxCellZ),
					FVector3d(MaxX, MaxY, MaxCellZ), FVector3d(MaxX, MaxY, MinCellZ),
					FVector3f::UnitY(), MaterialIndex,
					BlockDimensions.Z, BlockDimensions.X, UVWorldUnits);
			}
			if (NeedsFace(X, Y, DecodedZ - 1, BlockSpacing.Z > PumpkinSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MinX, MaxY, MinCellZ), FVector3d(MaxX, MaxY, MinCellZ),
					FVector3d(MaxX, MinY, MinCellZ), FVector3d(MinX, MinY, MinCellZ),
					-FVector3f::UnitZ(), MaterialIndex,
					BlockDimensions.X, BlockDimensions.Y, UVWorldUnits);
			}
			if (NeedsFace(X, Y, DecodedZ + 1, BlockSpacing.Z > PumpkinSpacingTolerance))
			{
				AppendMeshQuad(OutMesh,
					FVector3d(MinX, MinY, MaxCellZ), FVector3d(MaxX, MinY, MaxCellZ),
					FVector3d(MaxX, MaxY, MaxCellZ), FVector3d(MinX, MaxY, MaxCellZ),
					FVector3f::UnitZ(), MaterialIndex,
					BlockDimensions.X, BlockDimensions.Y, UVWorldUnits);
			}
		}
	}
}

void AChunkedDestructibleHalloweenPumpkin::BuildChunkSimpleCollision(
	int32 ChunkIndex,
	FKAggregateGeom& OutCollision) const
{
	OutCollision.EmptyElements();
	if (!bGenerateCollision || ChunkCount.X <= 0)
	{
		return;
	}

	const int32 SafeChunkSide = GetEffectivePumpkinChunkSideLength();
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinEncodedX = ChunkX * SafeChunkSide;
	const int32 MinZ = ChunkY * SafeChunkSide;
	const int32 MaxEncodedX = FMath::Min(MinEncodedX + SafeChunkSide, CellCount.X);
	const int32 MaxZ = FMath::Min(MinZ + SafeChunkSide, CellCount.Y);
	const int32 GridWidth = PumpkinGridCellCount.X;
	if (GridWidth <= 0 || MinEncodedX >= MaxEncodedX)
	{
		return;
	}
	const int32 FirstY = MinEncodedX / GridWidth;
	const int32 LastY = (MaxEncodedX - 1) / GridWidth + 1;
	const bool bSingleRow = LastY == FirstY + 1;
	const int32 FirstX = bSingleRow ? MinEncodedX % GridWidth : 0;
	const int32 LastX = bSingleRow ? (MaxEncodedX - 1) % GridWidth + 1 : GridWidth;
	DestructibleCollision::AppendBoxRuns(
		OutCollision, FIntVector(FirstX, FirstY, MinZ), FIntVector(LastX, LastY, MaxZ),
		BlockDimensions, BlockSpacing, FVector(PumpkinGridLocalMin.X, PumpkinGridLocalMin.Y, 0.0),
		[this, MinEncodedX, MaxEncodedX](int32 X, int32 Y, int32 Z)
		{
			const FIntPoint Cell = EncodeCell(X, Y, Z);
			return Cell.X >= MinEncodedX && Cell.X < MaxEncodedX &&
				Cells[ToCellIndex(Cell)].bExists;
		});
}

bool AChunkedDestructibleHalloweenPumpkin::WorldLocationToCell(
	FVector WorldLocation,
	FIntPoint& OutCell) const
{
	OutCell = FIntPoint::NoneValue;
	if (PumpkinGridCellCount.X <= 0 || PumpkinGridCellCount.Y <= 0 ||
		PumpkinGridCellCount.Z <= 0)
	{
		return false;
	}

	const FVector Local = GetActorTransform().InverseTransformPosition(WorldLocation);
	const FVector Pitch = BlockDimensions + BlockSpacing;
	int32 X = FMath::FloorToInt((Local.X - PumpkinGridLocalMin.X) / Pitch.X);
	int32 Y = FMath::FloorToInt((Local.Y - PumpkinGridLocalMin.Y) / Pitch.Y);
	int32 Z = FMath::FloorToInt(Local.Z / Pitch.Z);
	X = X == PumpkinGridCellCount.X &&
		FMath::IsNearlyEqual(Local.X, PumpkinGridLocalMin.X + CachedWorldDimensions.X, 0.1)
		? X - 1 : X;
	Y = Y == PumpkinGridCellCount.Y &&
		FMath::IsNearlyEqual(Local.Y, PumpkinGridLocalMin.Y + CachedWorldDimensions.Y, 0.1)
		? Y - 1 : Y;
	Z = Z == PumpkinGridCellCount.Z &&
		FMath::IsNearlyEqual(Local.Z, CachedWorldDimensions.Z, 0.1)
		? Z - 1 : Z;

	const FIntPoint Candidate = EncodeCell(X, Y, Z);
	if (Candidate == FIntPoint::NoneValue)
	{
		return false;
	}
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
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

bool AChunkedDestructibleHalloweenPumpkin::ResolveExistingCellFromHit(
	const FHitResult& Hit,
	FIntPoint& OutCell) const
{
	if (Super::ResolveExistingCellFromHit(Hit, OutCell))
	{
		return true;
	}

	OutCell = FIntPoint::NoneValue;
	const FVector LocalImpact = GetActorTransform().InverseTransformPosition(
		Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint);
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const int32 BaseX = FMath::FloorToInt((LocalImpact.X - PumpkinGridLocalMin.X) / Pitch.X);
	const int32 BaseY = FMath::FloorToInt((LocalImpact.Y - PumpkinGridLocalMin.Y) / Pitch.Y);
	const int32 BaseZ = FMath::FloorToInt(LocalImpact.Z / Pitch.Z);
	double BestDistanceSquared = TNumericLimits<double>::Max();
	for (int32 Z = BaseZ - 2; Z <= BaseZ + 2; ++Z)
	{
		for (int32 Y = BaseY - 2; Y <= BaseY + 2; ++Y)
		{
			for (int32 X = BaseX - 2; X <= BaseX + 2; ++X)
			{
				const FIntPoint TestCell = EncodeCell(X, Y, Z);
				if (TestCell == FIntPoint::NoneValue ||
					!Cells.IsValidIndex(ToCellIndex(TestCell)) ||
					!Cells[ToCellIndex(TestCell)].bExists)
				{
					continue;
				}
				const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
				const FVector HalfSize = BlockDimensions * 0.5f;
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

int32 AChunkedDestructibleHalloweenPumpkin::SelectCellsInRadius(
	FVector WorldLocation,
	float Radius,
	EChunkedFloorBlastShape Shape,
	TFunctionRef<void(int32, float)> CellFunction)
{
	if (Radius <= 0.0f || Cells.IsEmpty() || PumpkinGridCellCount.X <= 0 ||
		PumpkinGridCellCount.Y <= 0 || PumpkinGridCellCount.Z <= 0)
	{
		return 0;
	}
	const FVector LocalImpact = GetActorTransform().InverseTransformPosition(WorldLocation);
	const FVector ActorScale = GetActorScale3D().GetAbs();
	const FVector SafeScale(
		FMath::Max(ActorScale.X, KINDA_SMALL_NUMBER),
		FMath::Max(ActorScale.Y, KINDA_SMALL_NUMBER),
		FMath::Max(ActorScale.Z, KINDA_SMALL_NUMBER));
	const FVector LocalRadius(
		Radius / SafeScale.X,
		Radius / SafeScale.Y,
		Radius / SafeScale.Z);
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const int32 MinX = FMath::Clamp(FMath::FloorToInt(
		(LocalImpact.X - LocalRadius.X - PumpkinGridLocalMin.X) / Pitch.X),
		0, PumpkinGridCellCount.X - 1);
	const int32 MaxX = FMath::Clamp(FMath::FloorToInt(
		(LocalImpact.X + LocalRadius.X - PumpkinGridLocalMin.X) / Pitch.X),
		0, PumpkinGridCellCount.X - 1);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt(
		(LocalImpact.Y - LocalRadius.Y - PumpkinGridLocalMin.Y) / Pitch.Y),
		0, PumpkinGridCellCount.Y - 1);
	const int32 MaxY = FMath::Clamp(FMath::FloorToInt(
		(LocalImpact.Y + LocalRadius.Y - PumpkinGridLocalMin.Y) / Pitch.Y),
		0, PumpkinGridCellCount.Y - 1);
	const int32 MinZ = FMath::Clamp(FMath::FloorToInt(
		(LocalImpact.Z - LocalRadius.Z - PumpkinGridLocalMin.Z) / Pitch.Z),
		0, PumpkinGridCellCount.Z - 1);
	const int32 MaxZ = FMath::Clamp(FMath::FloorToInt(
		(LocalImpact.Z + LocalRadius.Z - PumpkinGridLocalMin.Z) / Pitch.Z),
		0, PumpkinGridCellCount.Z - 1);
	int32 SelectedCount = 0;
	for (int32 Z = MinZ; Z <= MaxZ; ++Z)
	{
		for (int32 Y = MinY; Y <= MaxY; ++Y)
		{
			for (int32 X = MinX; X <= MaxX; ++X)
			{
				const FIntPoint Cell = EncodeCell(X, Y, Z);
				const int32 CellIndex = ToCellIndex(Cell);
				if (!Cells.IsValidIndex(CellIndex) || !Cells[CellIndex].bExists)
				{
					continue;
				}
				const FVector LocalDelta = GetPhysicalCellLocalCenter(X, Y, Z) - LocalImpact;
				const FVector ScaledDelta(
					LocalDelta.X * ActorScale.X,
					LocalDelta.Y * ActorScale.Y,
					LocalDelta.Z * ActorScale.Z);
				float Distance = 0.0f;
				switch (Shape)
				{
				case EChunkedFloorBlastShape::Square:
					Distance = FMath::Max3(
						FMath::Abs(ScaledDelta.X),
						FMath::Abs(ScaledDelta.Y),
						FMath::Abs(ScaledDelta.Z));
					break;
				case EChunkedFloorBlastShape::Diamond:
					Distance = FMath::Abs(ScaledDelta.X) +
						FMath::Abs(ScaledDelta.Y) + FMath::Abs(ScaledDelta.Z);
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
		}
	}
	return SelectedCount;
}

void AChunkedDestructibleHalloweenPumpkin::MarkCellChunksDirty(FIntPoint Cell)
{
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return;
	}
	const int32 SafeChunkSide = GetEffectivePumpkinChunkSideLength();
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
		bool bAxisHasGap = false;
	};
	const FNeighborDependency Dependencies[] = {
		{ FIntVector(-1, 0, 0), BlockSpacing.X > PumpkinSpacingTolerance },
		{ FIntVector(1, 0, 0), BlockSpacing.X > PumpkinSpacingTolerance },
		{ FIntVector(0, -1, 0), BlockSpacing.Y > PumpkinSpacingTolerance },
		{ FIntVector(0, 1, 0), BlockSpacing.Y > PumpkinSpacingTolerance },
		{ FIntVector(0, 0, -1), BlockSpacing.Z > PumpkinSpacingTolerance },
		{ FIntVector(0, 0, 1), BlockSpacing.Z > PumpkinSpacingTolerance }
	};
	for (const FNeighborDependency& Dependency : Dependencies)
	{
		if (!Dependency.bAxisHasGap)
		{
			MarkEncodedCellChunk(EncodeCell(
				X + Dependency.Offset.X,
				Y + Dependency.Offset.Y,
				Z + Dependency.Offset.Z));
		}
	}
}

FChunkedDestructibleHalloweenPumpkinMetrics
AChunkedDestructibleHalloweenPumpkin::GetPumpkinMetrics() const
{
	FChunkedDestructibleHalloweenPumpkinMetrics Metrics;
	Metrics.BackingGridBlockCount = PumpkinGridCellCount;
	Metrics.WorldDimensions = CachedWorldDimensions;
	for (const FFloorCellData& Cell : Cells)
	{
		Metrics.GeneratedBlockCount += Cell.bBelongsToGeneratedShape ? 1 : 0;
	}
	for (int32 Z = 0; Z < PumpkinGridCellCount.Z; ++Z)
	{
		for (int32 Y = 0; Y < PumpkinGridCellCount.Y; ++Y)
		{
			for (int32 X = 0; X < PumpkinGridCellCount.X; ++X)
			{
				Metrics.CarvedFaceBlockCount += IsFaceCarvedCell(X, Y, Z) ? 1 : 0;
			}
		}
	}
	return Metrics;
}

int32 AChunkedDestructibleHalloweenPumpkin::GetEffectivePumpkinChunkSideLength() const
{
	return FMath::Clamp(ChunkSideLength, 4, 128);
}

void AChunkedDestructibleHalloweenPumpkin::ApplyColorParameters(
	UMaterialInstanceDynamic* MaterialInstance,
	FLinearColor Color,
	bool bEmissive,
	float Intensity)
{
	if (!MaterialInstance)
	{
		return;
	}
	const float SafeIntensity = FMath::Max(Intensity, 0.0f);
	const FLinearColor OutputColor = bEmissive ? Color * SafeIntensity : Color;
	MaterialInstance->SetVectorParameterValue(TEXT("Base Color"), Color);
	MaterialInstance->SetVectorParameterValue(TEXT("BaseColor"), Color);
	MaterialInstance->SetVectorParameterValue(TEXT("Tint"), Color);
	MaterialInstance->SetVectorParameterValue(TEXT("Color"), OutputColor);
	MaterialInstance->SetVectorParameterValue(TEXT("GlowColor"), OutputColor);
	MaterialInstance->SetVectorParameterValue(TEXT("EmissiveColor"), OutputColor);
	MaterialInstance->SetScalarParameterValue(TEXT("Alpha"), 1.0f);
	MaterialInstance->SetScalarParameterValue(TEXT("Intensity"), SafeIntensity);
	MaterialInstance->SetScalarParameterValue(TEXT("GlowIntensity"), SafeIntensity);
	MaterialInstance->SetScalarParameterValue(TEXT("EmissiveIntensity"), SafeIntensity);
	MaterialInstance->SetScalarParameterValue(TEXT("Metallic"), 0.0f);
	MaterialInstance->SetScalarParameterValue(TEXT("Roughness"), 0.72f);
}

void AChunkedDestructibleHalloweenPumpkin::EnsurePumpkinMaterials()
{
	UMaterialInterface* FlatParent = DefaultFlatMaterial
		? DefaultFlatMaterial.Get()
		: UMaterial::GetDefaultMaterial(MD_Surface);
	UMaterialInterface* GlowParent = DefaultGlowMaterial
		? DefaultGlowMaterial.Get()
		: FlatParent;
	if (!BodyMaterialInstance)
	{
		BodyMaterialInstance = UMaterialInstanceDynamic::Create(FlatParent, this);
	}
	if (!RibMaterialInstance)
	{
		RibMaterialInstance = UMaterialInstanceDynamic::Create(FlatParent, this);
	}
	if (!DarkFaceMaterialInstance)
	{
		DarkFaceMaterialInstance = UMaterialInstanceDynamic::Create(FlatParent, this);
	}
	if (!GlowFaceMaterialInstance)
	{
		GlowFaceMaterialInstance = UMaterialInstanceDynamic::Create(GlowParent, this);
	}
	if (!StemMaterialInstance)
	{
		StemMaterialInstance = UMaterialInstanceDynamic::Create(FlatParent, this);
	}
	UpdatePumpkinMaterialParameters();
}

void AChunkedDestructibleHalloweenPumpkin::UpdatePumpkinMaterialParameters()
{
	ApplyColorParameters(BodyMaterialInstance, BodyColor, false, 1.0f);
	ApplyColorParameters(RibMaterialInstance, RibColor, false, 1.0f);
	ApplyColorParameters(
		DarkFaceMaterialInstance,
		FLinearColor(FaceColor.R * 0.012f, FaceColor.G * 0.006f, FaceColor.B * 0.003f, 1.0f),
		false,
		1.0f);
	ApplyColorParameters(GlowFaceMaterialInstance, FaceColor, true, FaceGlowIntensity);
	ApplyColorParameters(StemMaterialInstance, StemColor, false, 1.0f);
}

UMaterialInterface* AChunkedDestructibleHalloweenPumpkin::ResolvePumpkinMaterial(
	int32 MaterialSlot) const
{
	switch (MaterialSlot)
	{
	case BodyMaterialSlot:
		return BodyMaterialOverride ? BodyMaterialOverride.Get() : BodyMaterialInstance.Get();
	case RibMaterialSlot:
		return RibMaterialOverride ? RibMaterialOverride.Get() : RibMaterialInstance.Get();
	case FaceMaterialSlot:
		if (FaceMaterialOverride)
		{
			return FaceMaterialOverride.Get();
		}
		return bUseGlowingFace ? GlowFaceMaterialInstance.Get() : DarkFaceMaterialInstance.Get();
	case StemMaterialSlot:
		return StemMaterialOverride ? StemMaterialOverride.Get() : StemMaterialInstance.Get();
	default:
		return nullptr;
	}
}

void AChunkedDestructibleHalloweenPumpkin::ConfigureChunkComponent(
	UDynamicMeshComponent* Component) const
{
	Super::ConfigureChunkComponent(Component);
	if (!Component)
	{
		return;
	}
	Component->SetNumMaterials(FMath::Max(FloorMaterials.Num(), 4));
	for (int32 MaterialSlot = 0; MaterialSlot < 4; ++MaterialSlot)
	{
		UMaterialInterface* Material = ResolvePumpkinMaterial(MaterialSlot);
		if (!Material)
		{
			Material = DefaultFlatMaterial
				? DefaultFlatMaterial.Get()
				: UMaterial::GetDefaultMaterial(MD_Surface);
		}
		Component->SetMaterial(MaterialSlot, Material);
	}
}
