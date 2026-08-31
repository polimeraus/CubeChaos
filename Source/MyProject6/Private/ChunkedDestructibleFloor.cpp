// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleFloor.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
	constexpr float MinimumFloorDimension = 1.0f;
	constexpr float SpacingTolerance = 0.01f;
	constexpr double SpiralSurfaceLift = 0.20;
	constexpr double SpiralPolygonTolerance = 0.001;
	constexpr float FlashCleanupInterval = 1.0f / 30.0f;
	const FName ChunkComponentTag(TEXT("ChunkedDestructibleFloorChunk"));

	double SpiralCross2D(const FVector2D& A, const FVector2D& B)
	{
		return A.X * B.Y - A.Y * B.X;
	}

	double SpiralSignedArea(const TArray<FVector2D>& Polygon)
	{
		double TwiceArea = 0.0;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			TwiceArea += SpiralCross2D(Polygon[Index], Polygon[(Index + 1) % Polygon.Num()]);
		}
		return TwiceArea * 0.5;
	}

	void AddSharedSpiralTriangle(
		TArray<FChunkedCachedSpiralTriangle>& Triangles,
		FVector2D A,
		FVector2D B,
		FVector2D C)
	{
		const double Area = SpiralCross2D(B - A, C - A);
		if (FMath::Abs(Area) <= UE_DOUBLE_SMALL_NUMBER)
		{
			return;
		}
		if (Area < 0.0)
		{
			Swap(B, C);
		}

		FChunkedCachedSpiralTriangle& Triangle = Triangles.Emplace_GetRef();
		Triangle.A = A;
		Triangle.B = B;
		Triangle.C = C;
		Triangle.BoundsMin = FVector2D(
			FMath::Min3(A.X, B.X, C.X),
			FMath::Min3(A.Y, B.Y, C.Y));
		Triangle.BoundsMax = FVector2D(
			FMath::Max3(A.X, B.X, C.X),
			FMath::Max3(A.Y, B.Y, C.Y));
	}

	TArray<FVector2D> ClipSharedSpiralTriangle(
		const FChunkedCachedSpiralTriangle& Triangle,
		const TArray<FVector2D>& ClipPolygon)
	{
		TArray<FVector2D> Fragment = { Triangle.A, Triangle.B, Triangle.C };
		if (ClipPolygon.Num() < 3)
		{
			Fragment.Reset();
			return Fragment;
		}

		const double Orientation = SpiralSignedArea(ClipPolygon) >= 0.0 ? 1.0 : -1.0;
		for (int32 EdgeIndex = 0; EdgeIndex < ClipPolygon.Num() && !Fragment.IsEmpty(); ++EdgeIndex)
		{
			const FVector2D ClipStart = ClipPolygon[EdgeIndex];
			const FVector2D ClipEdge = ClipPolygon[(EdgeIndex + 1) % ClipPolygon.Num()] - ClipStart;
			const TArray<FVector2D> Input = Fragment;
			Fragment.Reset();
			FVector2D Previous = Input.Last();
			double PreviousDistance = Orientation * SpiralCross2D(ClipEdge, Previous - ClipStart);
			bool bPreviousInside = PreviousDistance >= -SpiralPolygonTolerance;
			for (const FVector2D& Current : Input)
			{
				const double CurrentDistance = Orientation * SpiralCross2D(ClipEdge, Current - ClipStart);
				const bool bCurrentInside = CurrentDistance >= -SpiralPolygonTolerance;
				if (bCurrentInside != bPreviousInside)
				{
					const double Denominator = PreviousDistance - CurrentDistance;
					if (!FMath::IsNearlyZero(Denominator))
					{
						Fragment.Add(Previous + (Current - Previous) *
							FMath::Clamp(PreviousDistance / Denominator, 0.0, 1.0));
					}
				}
				if (bCurrentInside)
				{
					Fragment.Add(Current);
				}
				Previous = Current;
				PreviousDistance = CurrentDistance;
				bPreviousInside = bCurrentInside;
			}
		}
		return Fragment;
	}
}

void AChunkedDestructibleFloor::AppendMeshQuad(
		FDynamicMesh3& Mesh,
		const FVector3d& A,
		const FVector3d& B,
		const FVector3d& C,
		const FVector3d& D,
		const FVector3f& Normal,
		int32 MaterialIndex,
		float ULength,
		float VLength,
		float UVWorldUnits)
	{
		const int32 VertexA = Mesh.AppendVertex(A);
		const int32 VertexB = Mesh.AppendVertex(B);
		const int32 VertexC = Mesh.AppendVertex(C);
		const int32 VertexD = Mesh.AppendVertex(D);

		Mesh.SetVertexNormal(VertexA, Normal);
		Mesh.SetVertexNormal(VertexB, Normal);
		Mesh.SetVertexNormal(VertexC, Normal);
		Mesh.SetVertexNormal(VertexD, Normal);

		// DynamicMesh uses Unreal's clockwise front-face convention. The quad corners are
		// supplied counter-clockwise when viewed along the requested normal, so reverse
		// the triangle winding here. Keeping the render face and normal orientation in
		// agreement is essential for correct lighting on the horizontal floor surface.
		const int32 TriangleA = Mesh.AppendTriangle(VertexA, VertexC, VertexB);
		const int32 TriangleB = Mesh.AppendTriangle(VertexA, VertexD, VertexC);
		if (TriangleA < 0 || TriangleB < 0 || !Mesh.HasAttributes())
		{
			return;
		}

		FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals();
		FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV();
		FDynamicMeshMaterialAttribute* MaterialIDs = Mesh.Attributes()->GetMaterialID();

		if (NormalOverlay)
		{
			const int32 NormalA = NormalOverlay->AppendElement(Normal);
			const int32 NormalB = NormalOverlay->AppendElement(Normal);
			const int32 NormalC = NormalOverlay->AppendElement(Normal);
			const int32 NormalD = NormalOverlay->AppendElement(Normal);
			NormalOverlay->SetTriangle(TriangleA, FIndex3i(NormalA, NormalC, NormalB));
			NormalOverlay->SetTriangle(TriangleB, FIndex3i(NormalA, NormalD, NormalC));
		}

		if (UVOverlay)
		{
			const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
			const float U = FMath::Max(ULength, 1.0f) / SafeUVScale;
			const float V = FMath::Max(VLength, 1.0f) / SafeUVScale;
			const int32 UVA = UVOverlay->AppendElement(FVector2f(0.0f, 0.0f));
			const int32 UVB = UVOverlay->AppendElement(FVector2f(U, 0.0f));
			const int32 UVC = UVOverlay->AppendElement(FVector2f(U, V));
			const int32 UVD = UVOverlay->AppendElement(FVector2f(0.0f, V));
			UVOverlay->SetTriangle(TriangleA, FIndex3i(UVA, UVC, UVB));
			UVOverlay->SetTriangle(TriangleB, FIndex3i(UVA, UVD, UVC));
		}

		if (MaterialIDs)
		{
			MaterialIDs->SetValue(TriangleA, FMath::Max(MaterialIndex, 0));
			MaterialIDs->SetValue(TriangleB, FMath::Max(MaterialIndex, 0));
		}
}

void AChunkedDestructibleFloor::AppendMeshTriangle(
	FDynamicMesh3& Mesh,
	const FVector3d& A,
	const FVector3d& B,
	const FVector3d& C,
	const FVector3f& Normal,
	int32 MaterialIndex,
	const FVector2f& UVA,
	const FVector2f& UVB,
	const FVector2f& UVC)
{
	const int32 VertexA = Mesh.AppendVertex(A);
	const int32 VertexB = Mesh.AppendVertex(B);
	const int32 VertexC = Mesh.AppendVertex(C);
	Mesh.SetVertexNormal(VertexA, Normal);
	Mesh.SetVertexNormal(VertexB, Normal);
	Mesh.SetVertexNormal(VertexC, Normal);

	const int32 Triangle = Mesh.AppendTriangle(VertexA, VertexC, VertexB);
	if (Triangle < 0 || !Mesh.HasAttributes())
	{
		return;
	}

	if (FDynamicMeshNormalOverlay* NormalOverlay = Mesh.Attributes()->PrimaryNormals())
	{
		const int32 NormalA = NormalOverlay->AppendElement(Normal);
		const int32 NormalB = NormalOverlay->AppendElement(Normal);
		const int32 NormalC = NormalOverlay->AppendElement(Normal);
		NormalOverlay->SetTriangle(Triangle, FIndex3i(NormalA, NormalC, NormalB));
	}
	if (FDynamicMeshUVOverlay* UVOverlay = Mesh.Attributes()->PrimaryUV())
	{
		const int32 ElementA = UVOverlay->AppendElement(UVA);
		const int32 ElementB = UVOverlay->AppendElement(UVB);
		const int32 ElementC = UVOverlay->AppendElement(UVC);
		UVOverlay->SetTriangle(Triangle, FIndex3i(ElementA, ElementC, ElementB));
	}
	if (FDynamicMeshMaterialAttribute* MaterialIDs = Mesh.Attributes()->GetMaterialID())
	{
		MaterialIDs->SetValue(Triangle, FMath::Max(MaterialIndex, 0));
	}
}

AChunkedDestructibleFloor::AChunkedDestructibleFloor()
{
	PrimaryActorTick.bCanEverTick = true;
	// The floor only needs an Actor tick while a delayed destruction ripple is active. Flash
	// expiry is timer-driven, so idle floors should contribute no per-frame tick overhead.
	PrimaryActorTick.bStartWithTickEnabled = false;

	FloorRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FloorRoot"));
	SetRootComponent(FloorRoot);
	FloorRoot->SetMobility(EComponentMobility::Movable);

	FlashInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DestructionFlashes"));
	FlashInstances->SetupAttachment(FloorRoot);
	FlashInstances->SetMobility(EComponentMobility::Movable);
	FlashInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FlashInstances->SetGenerateOverlapEvents(false);
	FlashInstances->SetCastShadow(false);
	FlashInstances->SetCanEverAffectNavigation(false);
	FlashInstances->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		FlashInstances->SetStaticMesh(CubeFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PrimaryMaterialFinder(
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SecondaryMaterialFinder(
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_TopDark.MI_PrototypeGrid_TopDark"));
	if (PrimaryMaterialFinder.Succeeded())
	{
		FloorMaterials.Add(PrimaryMaterialFinder.Object);
	}
	if (SecondaryMaterialFinder.Succeeded())
	{
		FloorMaterials.Add(SecondaryMaterialFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FlashMaterialFinder(
		TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	if (FlashMaterialFinder.Succeeded())
	{
		FlashMaterial = FlashMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<USoundBase> DestroySoundFinder(
		TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));
	if (DestroySoundFinder.Succeeded())
	{
		DestroyTickSound = DestroySoundFinder.Object;
	}
}

void AChunkedDestructibleFloor::PostLoad()
{
	Super::PostLoad();

	const FChunkedSpiralPatternSettings DefaultSettings;
	const bool bStructSettingsAreDefault =
		SpiralSettings.Turns == DefaultSettings.Turns &&
		SpiralSettings.BandFraction == DefaultSettings.BandFraction &&
		SpiralSettings.RotationDegrees == DefaultSettings.RotationDegrees &&
		SpiralSettings.CenterOffset == DefaultSettings.CenterOffset &&
		SpiralSettings.CurveResolution == DefaultSettings.CurveResolution &&
		SpiralSettings.bClockwise == DefaultSettings.bClockwise;
	const bool bLegacySettingsAreCustomized =
		SpiralTurns != DefaultSettings.Turns ||
		SpiralBandFraction != DefaultSettings.BandFraction ||
		SpiralRotationDegrees != DefaultSettings.RotationDegrees ||
		SpiralCenterOffset != DefaultSettings.CenterOffset ||
		SpiralCurveResolution != DefaultSettings.CurveResolution ||
		bSpiralClockwise != DefaultSettings.bClockwise;

	if (bStructSettingsAreDefault && bLegacySettingsAreCustomized)
	{
		// Preserve values serialized by maps and Blueprints created before the
		// native Spiral settings group existed.
		SyncSpiralSettingsFromLegacyFields();
	}
	else
	{
		SyncLegacyFieldsFromSpiralSettings();
	}
}

void AChunkedDestructibleFloor::OnConstruction(const FTransform& Transform)
{
	SyncLegacyFieldsFromSpiralSettings();
	Super::OnConstruction(Transform);
	GenerateFloor(false);
}

#if WITH_EDITOR
void AChunkedDestructibleFloor::PostEditChangeProperty(
	FPropertyChangedEvent& PropertyChangedEvent)
{
	SyncLegacyFieldsFromSpiralSettings();
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

void AChunkedDestructibleFloor::SyncSpiralSettingsFromLegacyFields()
{
	SpiralSettings.Turns = SpiralTurns;
	SpiralSettings.BandFraction = SpiralBandFraction;
	SpiralSettings.RotationDegrees = SpiralRotationDegrees;
	SpiralSettings.CenterOffset = SpiralCenterOffset;
	SpiralSettings.CurveResolution = SpiralCurveResolution;
	SpiralSettings.bClockwise = bSpiralClockwise;
}

void AChunkedDestructibleFloor::SyncLegacyFieldsFromSpiralSettings()
{
	SpiralTurns = SpiralSettings.Turns;
	SpiralBandFraction = SpiralSettings.BandFraction;
	SpiralRotationDegrees = SpiralSettings.RotationDegrees;
	SpiralCenterOffset = SpiralSettings.CenterOffset;
	SpiralCurveResolution = SpiralSettings.CurveResolution;
	bSpiralClockwise = SpiralSettings.bClockwise;
}

void AChunkedDestructibleFloor::BeginPlay()
{
	Super::BeginPlay();

	// Runtime-spawned and cooked actors both get a clean authoritative grid here.
	GenerateFloor(false);
	SetActorTickEnabled(false);
}

void AChunkedDestructibleFloor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearRuntimeEffects();
	Super::EndPlay(EndPlayReason);
}

void AChunkedDestructibleFloor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ProcessQueuedDestructions(CurrentTime);
}

bool AChunkedDestructibleFloor::CalculateLayout()
{
	SpiralTurns = FMath::Clamp(SpiralTurns, 1.0f, 12.0f);
	SpiralBandFraction = FMath::Clamp(SpiralBandFraction, 0.1f, 0.9f);
	SpiralCurveResolution = FMath::Clamp(SpiralCurveResolution, 128, 4096);
	SyncSpiralSettingsFromLegacyFields();
	const FVector2D SafeArea(
		FMath::Max(FMath::Abs(AreaSize.X), MinimumFloorDimension),
		FMath::Max(FMath::Abs(AreaSize.Y), MinimumFloorDimension));
	const FVector SafeRequestedCell(
		FMath::Max(FMath::Abs(CellDimensions.X), MinimumFloorDimension),
		FMath::Max(FMath::Abs(CellDimensions.Y), MinimumFloorDimension),
		FMath::Max(FMath::Abs(CellDimensions.Z), MinimumFloorDimension));
	const FVector2D SafeSpacing(
		FMath::Max(CellSpacing.X, 0.0),
		FMath::Max(CellSpacing.Y, 0.0));

	auto CalculateAxis = [this](double Area, double RequestedCell, double Spacing, int32& OutCount, double& OutCell)
	{
		const double Pitch = RequestedCell + Spacing;
		const double DesiredCount = (Area + Spacing) / FMath::Max(Pitch, static_cast<double>(MinimumFloorDimension));

		switch (FillRule)
		{
		case EChunkedFloorFillRule::InsideArea:
			OutCount = FMath::Max(1, FMath::FloorToInt(DesiredCount));
			OutCell = RequestedCell;
			break;

		case EChunkedFloorFillRule::StretchCells:
			OutCount = FMath::Max(1, FMath::RoundToInt(DesiredCount));
			while (OutCount > 1 && Spacing * static_cast<float>(OutCount - 1) >= Area)
			{
				--OutCount;
			}
			OutCell = FMath::Max(
				(Area - Spacing * static_cast<float>(OutCount - 1)) / static_cast<float>(OutCount),
				static_cast<double>(MinimumFloorDimension));
			break;

		case EChunkedFloorFillRule::CoverArea:
		default:
			OutCount = FMath::Max(1, FMath::CeilToInt(DesiredCount));
			OutCell = RequestedCell;
			break;
		}
	};

	FIntPoint NewCellCount;
	FVector NewEffectiveCell = SafeRequestedCell;
	CalculateAxis(SafeArea.X, SafeRequestedCell.X, SafeSpacing.X, NewCellCount.X, NewEffectiveCell.X);
	CalculateAxis(SafeArea.Y, SafeRequestedCell.Y, SafeSpacing.Y, NewCellCount.Y, NewEffectiveCell.Y);

	const int64 RequestedCellCount = static_cast<int64>(NewCellCount.X) * static_cast<int64>(NewCellCount.Y);
	if (RequestedCellCount <= 0 || RequestedCellCount > FMath::Max(MaxGeneratedCells, 1))
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("%s requested %lld floor cells, exceeding MaxGeneratedCells (%d). Generation was cancelled."),
			*GetName(),
			RequestedCellCount,
			MaxGeneratedCells);
		return false;
	}

	CellCount = NewCellCount;
	EffectiveCellDimensions = NewEffectiveCell;
	EffectiveGridSize = FVector2D(
		EffectiveCellDimensions.X * CellCount.X + SafeSpacing.X * FMath::Max(CellCount.X - 1, 0),
		EffectiveCellDimensions.Y * CellCount.Y + SafeSpacing.Y * FMath::Max(CellCount.Y - 1, 0));
	GridLocalMin = -EffectiveGridSize * 0.5;

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	ChunkCount = FIntPoint(
		FMath::DivideAndRoundUp(CellCount.X, SafeChunkSide),
		FMath::DivideAndRoundUp(CellCount.Y, SafeChunkSide));
	return true;
}

void AChunkedDestructibleFloor::GenerateFloor(bool bPreserveCellState)
{
	const FIntPoint PreviousCellCount = CellCount;
	TArray<FFloorCellData> PreviousCells;
	if (bPreserveCellState)
	{
		PreviousCells = Cells;
	}

	ResetSharedGeometryCaches();
	if (!CalculateLayout())
	{
		return;
	}

	ClearRuntimeEffects();

	TArray<FFloorCellData> NewCells;
	NewCells.SetNum(CellCount.X * CellCount.Y);
	for (int32 Y = 0; Y < CellCount.Y; ++Y)
	{
		for (int32 X = 0; X < CellCount.X; ++X)
		{
			FFloorCellData& NewCell = NewCells[X + Y * CellCount.X];
			NewCell.bBelongsToGeneratedShape = ShouldCellExistInGeneratedShape(FIntPoint(X, Y));
			NewCell.bExists = NewCell.bBelongsToGeneratedShape;
			NewCell.bDestructible = bDestructibleByDefault;
			NewCell.Health = FMath::Max(DefaultCellHealth, 0.01f);
			NewCell.MaterialIndex = EvaluatePatternMaterial(FIntPoint(X, Y));
		}
	}

	if (bPreserveCellState && !PreviousCells.IsEmpty())
	{
		const int32 CopyWidth = FMath::Min(PreviousCellCount.X, CellCount.X);
		const int32 CopyHeight = FMath::Min(PreviousCellCount.Y, CellCount.Y);
		for (int32 Y = 0; Y < CopyHeight; ++Y)
		{
			for (int32 X = 0; X < CopyWidth; ++X)
			{
				FFloorCellData& NewCell = NewCells[X + Y * CellCount.X];
				const FFloorCellData& PreviousCell = PreviousCells[X + Y * PreviousCellCount.X];
				if (NewCell.bBelongsToGeneratedShape && PreviousCell.bBelongsToGeneratedShape)
				{
					const bool bBelongsToNewShape = NewCell.bBelongsToGeneratedShape;
					NewCell = PreviousCell;
					NewCell.bBelongsToGeneratedShape = bBelongsToNewShape;
				}
			}
		}
	}

	Cells = MoveTemp(NewCells);
	ClearChunkComponents();
	CreateChunkComponents();
	RefreshFlashMaterial();
	RebuildAllChunks();
	OnFloorRegenerated.Broadcast();
}

void AChunkedDestructibleFloor::ResetSharedGeometryCaches()
{
	CachedSpiralSourceTriangles.Reset();
	CachedSpiralCellTriangles.Reset();
	CachedSpiralCellBuilt.Reset();
	bCachedSpiralGeometryKeyValid = false;
}

void AChunkedDestructibleFloor::ConfigureFloor(
	FVector2D NewAreaSize,
	FVector NewCellDimensions,
	FVector2D NewCellSpacing,
	bool bPreserveCellState)
{
	AreaSize = NewAreaSize;
	CellDimensions = NewCellDimensions;
	CellSpacing = NewCellSpacing;
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleFloor::SetCellDimensions(FVector NewCellDimensions, bool bPreserveCellState)
{
	CellDimensions = NewCellDimensions;
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleFloor::SetCellSpacing(FVector2D NewCellSpacing, bool bPreserveCellState)
{
	CellSpacing = NewCellSpacing;
	GenerateFloor(bPreserveCellState);
}

void AChunkedDestructibleFloor::SetFloorPattern(EChunkedFloorPattern NewPattern)
{
	FloorPattern = NewPattern;
	ApplyMaterialPattern();
}

void AChunkedDestructibleFloor::ConfigureMaterialPattern(
	EChunkedFloorPattern NewPattern,
	int32 NewPrimaryMaterialIndex,
	int32 NewSecondaryMaterialIndex,
	int32 NewPatternCellScale,
	int32 NewRandomSeed)
{
	FloorPattern = NewPattern;
	PrimaryMaterialIndex = FMath::Max(NewPrimaryMaterialIndex, 0);
	SecondaryMaterialIndex = FMath::Max(NewSecondaryMaterialIndex, 0);
	PatternCellScale = FMath::Max(NewPatternCellScale, 1);
	PatternRandomSeed = NewRandomSeed;
	ApplyMaterialPattern();
}

void AChunkedDestructibleFloor::ApplyMaterialPattern()
{
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		Cells[CellIndex].MaterialIndex = EvaluatePatternMaterial(FromCellIndex(CellIndex));
	}
	RebuildAllChunks();
}

void AChunkedDestructibleFloor::RefreshFloorMaterials()
{
	const int32 MaximumMaterialIndex = FMath::Max(FloorMaterials.Num() - 1, 0);
	for (FFloorCellData& CellData : Cells)
	{
		CellData.MaterialIndex = FMath::Clamp(CellData.MaterialIndex, 0, MaximumMaterialIndex);
	}
	RebuildAllChunks();
}

void AChunkedDestructibleFloor::SetMaterialPalette(const TArray<UMaterialInterface*>& NewMaterials)
{
	FloorMaterials.Reset(NewMaterials.Num());
	for (UMaterialInterface* Material : NewMaterials)
	{
		FloorMaterials.Add(Material);
	}
	RefreshFloorMaterials();
}

void AChunkedDestructibleFloor::ConfigureDestructionFlash(
	UMaterialInterface* NewFlashMaterial,
	FLinearColor NewFlashColor,
	float NewFlashIntensity,
	float NewFlashDuration)
{
	FlashMaterial = NewFlashMaterial;
	FlashColor = NewFlashColor;
	FlashIntensity = FMath::Max(NewFlashIntensity, 0.0f);
	FlashDuration = FMath::Max(NewFlashDuration, 0.0f);
	RefreshFlashMaterial();
}

void AChunkedDestructibleFloor::ConfigureDestroyTickSound(
	USoundBase* NewSound,
	float NewVolume,
	FVector2D NewPitchRange)
{
	DestroyTickSound = NewSound;
	DestroySoundVolume = FMath::Max(NewVolume, 0.0f);
	DestroySoundPitchRange = FVector2D(
		FMath::Max(NewPitchRange.X, 0.01),
		FMath::Max(NewPitchRange.Y, 0.01));
}

void AChunkedDestructibleFloor::ClearRuntimeEffects()
{
	ClearAdditionalRuntimeEffects();
	QueuedDestructions.Reset();
	PendingCellIndices.Reset();
	DirtyChunkIndices.Reset();
	ActiveFlashes.Reset();
	RecentDestroySoundTimes.Reset();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FlashCleanupTimerHandle);
	}
	if (FlashInstances)
	{
		FlashInstances->ClearInstances();
		FlashInstances->SetVisibility(false);
	}
	SetActorTickEnabled(false);
}

void AChunkedDestructibleFloor::ClearChunkComponents()
{
	// Do not rely only on ChunkComponents here. That array is transient, while PIE can duplicate
	// dynamically-created owned components from the editor world without copying the transient
	// array entries. In that case an orphaned full-floor render mesh would remain directly under
	// the newly generated chunks: damage state and collision would update, but the old mesh would
	// continue to draw the destroyed cell. Sweep the Actor's actual owned components so every old
	// generation is removed before a new one is created.
	TInlineComponentArray<UDynamicMeshComponent*> OwnedDynamicMeshComponents(this);
	for (UDynamicMeshComponent* Component : OwnedDynamicMeshComponents)
	{
		if (!IsValid(Component))
		{
			continue;
		}

		const bool bIsFloorChunk = Component->ComponentHasTag(ChunkComponentTag) ||
			Component->GetName().StartsWith(TEXT("FloorChunk_"));
		if (!bIsFloorChunk)
		{
			continue;
		}

		RemoveInstanceComponent(Component);
		Component->DestroyComponent();
	}
	ChunkComponents.Reset();
}

void AChunkedDestructibleFloor::CreateChunkComponents()
{
	const int32 TotalChunkCount = ChunkCount.X * ChunkCount.Y;
	ChunkComponents.Reserve(TotalChunkCount);

	for (int32 ChunkY = 0; ChunkY < ChunkCount.Y; ++ChunkY)
	{
		for (int32 ChunkX = 0; ChunkX < ChunkCount.X; ++ChunkX)
		{
			const FName DesiredName(*FString::Printf(TEXT("FloorChunk_%d_%d"), ChunkX, ChunkY));
			const FName UniqueName = MakeUniqueObjectName(this, UDynamicMeshComponent::StaticClass(), DesiredName);
			UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(this, UniqueName, RF_Transient);
			Component->ComponentTags.Add(ChunkComponentTag);
			Component->SetupAttachment(FloorRoot);
			Component->SetRelativeTransform(FTransform::Identity);
			Component->SetMobility(EComponentMobility::Movable);
			AddInstanceComponent(Component);
			Component->RegisterComponent();
			ConfigureChunkComponent(Component);
			ChunkComponents.Add(Component);
		}
	}
}

void AChunkedDestructibleFloor::ConfigureChunkComponent(UDynamicMeshComponent* Component) const
{
	if (!Component)
	{
		return;
	}

	const int32 MaterialCount = FMath::Max(FloorMaterials.Num(), 1);
	Component->SetNumMaterials(MaterialCount);
	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		UMaterialInterface* Material = FloorMaterials.IsValidIndex(MaterialIndex)
			? FloorMaterials[MaterialIndex].Get()
			: nullptr;
		if (!Material)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		Component->SetMaterial(MaterialIndex, Material);
	}
	Component->SetCastShadow(bCastFloorShadow);
	Component->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
	// A whole chunk is replaced after cell destruction, so always invalidate and rebuild the
	// render proxy when the owned UDynamicMesh reports a general edit.
	Component->SetInvalidateProxyOnChangeEnabled(true);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCollisionObjectType(ECC_WorldStatic);
	Component->SetCollisionResponseToAllChannels(ECR_Block);
	Component->SetCollisionEnabled(
		bGenerateCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	// Character movement over disconnected complex triangles can catch on coplanar seams.
	// Runtime-generated, greedily merged box shapes provide a stable walkable surface instead.
	Component->SetComplexAsSimpleCollisionEnabled(false, false);
	Component->SetDeferredCollisionUpdatesEnabled(true, false);
	// Simple boxes are cheap to rebuild. Synchronous cooking makes a destroyed cell non-solid
	// in the exact frame its render mesh disappears, avoiding stale collision under the player.
	Component->bUseAsyncCooking = false;
}

void AChunkedDestructibleFloor::RebuildAllChunks()
{
	for (int32 ChunkIndex = 0; ChunkIndex < ChunkComponents.Num(); ++ChunkIndex)
	{
		RebuildChunk(ChunkIndex);
	}
	DirtyChunkIndices.Reset();
}

void AChunkedDestructibleFloor::RebuildChunk(int32 ChunkIndex)
{
	if (!ChunkComponents.IsValidIndex(ChunkIndex) || !ChunkComponents[ChunkIndex])
	{
		return;
	}

	UDynamicMeshComponent* Component = ChunkComponents[ChunkIndex];
	if (!Component->GetDynamicMesh())
	{
		return;
	}

	FDynamicMesh3 NewMesh;
	BuildChunkMesh(ChunkIndex, NewMesh);

	// A single-cell edit can leave the registered PIE scene proxy on its previous buffers even
	// after a full UDynamicMesh swap. Re-registering only the affected chunk releases that proxy
	// before the replacement is attached. Bombs still perform this once per dirty chunk because
	// their cell deletions are batched before FlushDirtyChunks.
	const bool bWasRegistered = Component->IsRegistered();
	if (bWasRegistered)
	{
		Component->UnregisterComponent();
	}

	// Replace the UDynamicMesh object itself. UE 5.8 PIE can retain the old GPU buffers when the
	// contents of the same UDynamicMesh are wholesale replaced, even though CPU triangle data and
	// collision are already current. Swapping the render source object makes the proxy rebuild
	// unambiguous and keeps destroyed cells from remaining as visual-only ghosts.
	UDynamicMesh* ReplacementMesh = NewObject<UDynamicMesh>(Component);
	ReplacementMesh->SetMesh(MoveTemp(NewMesh));
	Component->SetDynamicMesh(ReplacementMesh);
	ConfigureChunkComponent(Component);
	if (bWasRegistered)
	{
		Component->RegisterComponent();
	}
	Component->InvalidateAutoCalculatedTangents();
	Component->NotifyMeshUpdated();
	Component->SetVisibility(Component->GetDynamicMesh()->GetTriangleCount() > 0, true);
	Component->MarkRenderStateDirty();
	if (bGenerateCollision)
	{
		FKAggregateGeom SimpleCollision;
		BuildChunkSimpleCollision(ChunkIndex, SimpleCollision);
		Component->SetSimpleCollisionShapes(SimpleCollision, false);
		Component->UpdateCollision(false);
	}
	else
	{
		Component->ClearSimpleCollisionShapes(false);
	}
}

void AChunkedDestructibleFloor::BuildChunkMesh(int32 ChunkIndex, FDynamicMesh3& OutMesh) const
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

	AppendGreedyHorizontalFaces(OutMesh, MinCellX, MinCellY, MaxCellX, MaxCellY, true);
	if (bGenerateBottomFaces)
	{
		AppendGreedyHorizontalFaces(OutMesh, MinCellX, MinCellY, MaxCellX, MaxCellY, false);
	}

	const bool bGapX = CellSpacing.X > SpacingTolerance;
	const bool bGapY = CellSpacing.Y > SpacingTolerance;
	const double HalfX = EffectiveCellDimensions.X * 0.5;
	const double HalfY = EffectiveCellDimensions.Y * 0.5;
	const double BottomZ = -EffectiveCellDimensions.Z;

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

			const FVector Center = GetCellLocalCenter(Cell);
			const double MinX = Center.X - HalfX;
			const double MaxX = Center.X + HalfX;
			const double MinY = Center.Y - HalfY;
			const double MaxY = Center.Y + HalfY;
			const int32 MaterialIndex = CellData.MaterialIndex;

			auto CellExists = [this](int32 TestX, int32 TestY)
			{
				const FIntPoint TestCell(TestX, TestY);
				return IsValidCell(TestCell) && Cells[ToCellIndex(TestCell)].bExists;
			};

			if (bGapX || !CellExists(X - 1, Y))
			{
				AppendMeshQuad(
					OutMesh,
					FVector3d(MinX, MinY, BottomZ), FVector3d(MinX, MinY, 0.0),
					FVector3d(MinX, MaxY, 0.0), FVector3d(MinX, MaxY, BottomZ),
					-FVector3f::UnitX(), MaterialIndex,
					EffectiveCellDimensions.Z, EffectiveCellDimensions.Y, UVWorldUnits);
			}

			if (bGapX || !CellExists(X + 1, Y))
			{
				AppendMeshQuad(
					OutMesh,
					FVector3d(MaxX, MaxY, BottomZ), FVector3d(MaxX, MaxY, 0.0),
					FVector3d(MaxX, MinY, 0.0), FVector3d(MaxX, MinY, BottomZ),
					FVector3f::UnitX(), MaterialIndex,
					EffectiveCellDimensions.Z, EffectiveCellDimensions.Y, UVWorldUnits);
			}

			if (bGapY || !CellExists(X, Y - 1))
			{
				AppendMeshQuad(
					OutMesh,
					FVector3d(MaxX, MinY, BottomZ), FVector3d(MaxX, MinY, 0.0),
					FVector3d(MinX, MinY, 0.0), FVector3d(MinX, MinY, BottomZ),
					-FVector3f::UnitY(), MaterialIndex,
					EffectiveCellDimensions.Z, EffectiveCellDimensions.X, UVWorldUnits);
			}

			if (bGapY || !CellExists(X, Y + 1))
			{
				AppendMeshQuad(
					OutMesh,
					FVector3d(MinX, MaxY, BottomZ), FVector3d(MinX, MaxY, 0.0),
					FVector3d(MaxX, MaxY, 0.0), FVector3d(MaxX, MaxY, BottomZ),
					FVector3f::UnitY(), MaterialIndex,
					EffectiveCellDimensions.Z, EffectiveCellDimensions.X, UVWorldUnits);
			}
		}
	}

	if (FloorPattern == EChunkedFloorPattern::HypnoticSpiral)
	{
		const FVector2D PatternCenter = GridLocalMin + EffectiveGridSize * 0.5;
		const double FurthestRadius = (EffectiveGridSize * 0.5).Size();
		AppendHypnoticSpiralSurface2D(
			OutMesh,
			ChunkIndex,
			PatternCenter,
			FurthestRadius,
			[this](FIntPoint Cell, TArray<TArray<FVector2D>>& OutPolygons)
			{
				OutPolygons.Reset();
				if (!IsValidCell(Cell) || !Cells[ToCellIndex(Cell)].bExists)
				{
					return false;
				}
				const FVector Center = GetCellLocalCenter(Cell);
				const FVector2D HalfSize(EffectiveCellDimensions.X * 0.5, EffectiveCellDimensions.Y * 0.5);
				OutPolygons.Add({
					FVector2D(Center.X, Center.Y) + FVector2D(-HalfSize.X, -HalfSize.Y),
					FVector2D(Center.X, Center.Y) + FVector2D(HalfSize.X, -HalfSize.Y),
					FVector2D(Center.X, Center.Y) + FVector2D(HalfSize.X, HalfSize.Y),
					FVector2D(Center.X, Center.Y) + FVector2D(-HalfSize.X, HalfSize.Y)
				});
				return true;
			},
			[this, &OutMesh](FIntPoint, const FVector2D& A, const FVector2D& B, const FVector2D& C, int32 MaterialIndex)
			{
				const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
				AppendMeshTriangle(
					OutMesh,
					FVector3d(A.X, A.Y, SpiralSurfaceLift),
					FVector3d(B.X, B.Y, SpiralSurfaceLift),
					FVector3d(C.X, C.Y, SpiralSurfaceLift),
					FVector3f::UnitZ(),
					MaterialIndex,
					FVector2f(A.X / SafeUVScale, A.Y / SafeUVScale),
					FVector2f(B.X / SafeUVScale, B.Y / SafeUVScale),
					FVector2f(C.X / SafeUVScale, C.Y / SafeUVScale));
			});
	}
}

void AChunkedDestructibleFloor::AppendHypnoticSpiralSurface2D(
	FDynamicMesh3& Mesh,
	int32 ChunkIndex,
	FVector2D PatternCenter,
	double FurthestRadius,
	TFunctionRef<bool(FIntPoint, TArray<TArray<FVector2D>>&)> GetCellPolygons,
	TFunctionRef<void(FIntPoint, const FVector2D&, const FVector2D&, const FVector2D&, int32)> AppendSurfaceTriangle) const
{
	if (FloorMaterials.IsEmpty() || ChunkCount.X <= 0 || FurthestRadius <= UE_DOUBLE_SMALL_NUMBER)
	{
		return;
	}

	const bool bGeometryKeyChanged = !bCachedSpiralGeometryKeyValid ||
		!CachedSpiralPatternCenter.Equals(PatternCenter, KINDA_SMALL_NUMBER) ||
		!FMath::IsNearlyEqual(CachedSpiralFurthestRadius, FurthestRadius) ||
		!FMath::IsNearlyEqual(CachedSpiralTurns, SpiralTurns) ||
		!FMath::IsNearlyEqual(CachedSpiralBandFraction, SpiralBandFraction) ||
		!FMath::IsNearlyEqual(CachedSpiralRotationDegrees, SpiralRotationDegrees) ||
		!CachedSpiralCenterOffset.Equals(SpiralCenterOffset, KINDA_SMALL_NUMBER) ||
		CachedSpiralCurveResolution != SpiralCurveResolution ||
		bCachedSpiralClockwise != bSpiralClockwise ||
		CachedSpiralCellTriangles.Num() != Cells.Num();
	if (bGeometryKeyChanged)
	{
		CachedSpiralSourceTriangles.Reset();
		CachedSpiralCellTriangles.SetNum(Cells.Num());
		CachedSpiralCellBuilt.Init(false, Cells.Num());
		bCachedSpiralGeometryKeyValid = true;
		CachedSpiralPatternCenter = PatternCenter;
		CachedSpiralFurthestRadius = FurthestRadius;
		CachedSpiralTurns = SpiralTurns;
		CachedSpiralBandFraction = SpiralBandFraction;
		CachedSpiralRotationDegrees = SpiralRotationDegrees;
		CachedSpiralCenterOffset = SpiralCenterOffset;
		CachedSpiralCurveResolution = SpiralCurveResolution;
		bCachedSpiralClockwise = bSpiralClockwise;
	}

	const FVector2D SpiralCenter = PatternCenter + SpiralCenterOffset;
	const double SafeTurns = FMath::Clamp(static_cast<double>(SpiralTurns), 1.0, 12.0);
	const double RadialPitch = FMath::Max(FurthestRadius / SafeTurns, 1.0);
	const double BandWidth = RadialPitch * FMath::Clamp(static_cast<double>(SpiralBandFraction), 0.1, 0.9);
	const double OuterRadius = FurthestRadius + RadialPitch;
	const double MaximumTheta = SafeTurns * UE_TWO_PI;
	const double Direction = bSpiralClockwise ? -1.0 : 1.0;
	const double RotationRadians = FMath::DegreesToRadians(static_cast<double>(SpiralRotationDegrees));
	// At least 192 segments per turn keeps even the tight inner rotations visually round.
	const int32 AdaptiveResolution = FMath::CeilToInt(SafeTurns * 192.0);
	const int32 SampleCount = FMath::Clamp(
		FMath::Max(SpiralCurveResolution, AdaptiveResolution),
		128,
		4096);

	if (CachedSpiralSourceTriangles.IsEmpty())
	{
		TArray<FVector2D> LeftEdge;
		TArray<FVector2D> RightEdge;
		LeftEdge.Reserve(SampleCount + 1);
		RightEdge.Reserve(SampleCount + 1);
		for (int32 SampleIndex = 0; SampleIndex <= SampleCount; ++SampleIndex)
		{
			const double Alpha = static_cast<double>(SampleIndex) / SampleCount;
			const double CurveTheta = MaximumTheta * Alpha;
			const double Angle = RotationRadians + Direction * CurveTheta;
			const double Radius = OuterRadius * Alpha;
			const double CosAngle = FMath::Cos(Angle);
			const double SinAngle = FMath::Sin(Angle);
			const FVector2D Centerline = SpiralCenter + FVector2D(CosAngle, SinAngle) * Radius;
			const double RadiusDerivative = OuterRadius / MaximumTheta;
			const FVector2D Tangent(
				RadiusDerivative * CosAngle - Direction * Radius * SinAngle,
				RadiusDerivative * SinAngle + Direction * Radius * CosAngle);
			const FVector2D Normal = FVector2D(-Tangent.Y, Tangent.X).GetSafeNormal();
			LeftEdge.Add(Centerline + Normal * (BandWidth * 0.5));
			RightEdge.Add(Centerline - Normal * (BandWidth * 0.5));
		}

		CachedSpiralSourceTriangles.Reserve(SampleCount * 2 + 48);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			AddSharedSpiralTriangle(
				CachedSpiralSourceTriangles,
				LeftEdge[SampleIndex],
				RightEdge[SampleIndex],
				RightEdge[SampleIndex + 1]);
			AddSharedSpiralTriangle(
				CachedSpiralSourceTriangles,
				LeftEdge[SampleIndex],
				RightEdge[SampleIndex + 1],
				LeftEdge[SampleIndex + 1]);
		}

		// The round inner cap removes the visible wedge/kink where an Archimedean strip begins.
		constexpr int32 CenterCapSegments = 48;
		for (int32 SegmentIndex = 0; SegmentIndex < CenterCapSegments; ++SegmentIndex)
		{
			const double AngleA = UE_TWO_PI * SegmentIndex / CenterCapSegments;
			const double AngleB = UE_TWO_PI * (SegmentIndex + 1) / CenterCapSegments;
			AddSharedSpiralTriangle(
				CachedSpiralSourceTriangles,
				SpiralCenter,
				SpiralCenter + FVector2D(FMath::Cos(AngleA), FMath::Sin(AngleA)) * (BandWidth * 0.5),
				SpiralCenter + FVector2D(FMath::Cos(AngleB), FMath::Sin(AngleB)) * (BandWidth * 0.5));
		}
	}

	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);
	const int32 ChunkX = ChunkIndex % ChunkCount.X;
	const int32 ChunkY = ChunkIndex / ChunkCount.X;
	const int32 MinCellX = ChunkX * SafeChunkSide;
	const int32 MinCellY = ChunkY * SafeChunkSide;
	const int32 MaxCellX = FMath::Min(MinCellX + SafeChunkSide, CellCount.X);
	const int32 MaxCellY = FMath::Min(MinCellY + SafeChunkSide, CellCount.Y);
	const int32 StripeMaterialIndex = FMath::Clamp(SecondaryMaterialIndex, 0, FloorMaterials.Num() - 1);

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const FIntPoint Cell(X, Y);
			const int32 CellIndex = ToCellIndex(Cell);
			if (!Cells.IsValidIndex(CellIndex) || !Cells[CellIndex].bExists ||
				!CachedSpiralCellTriangles.IsValidIndex(CellIndex) ||
				!CachedSpiralCellBuilt.IsValidIndex(CellIndex))
			{
				continue;
			}

			TArray<FChunkedCachedSpiralTriangle>& CachedCellTriangles =
				CachedSpiralCellTriangles[CellIndex];
			if (CachedSpiralCellBuilt[CellIndex])
			{
				for (const FChunkedCachedSpiralTriangle& Triangle : CachedCellTriangles)
				{
					AppendSurfaceTriangle(Cell, Triangle.A, Triangle.B, Triangle.C, StripeMaterialIndex);
				}
				continue;
			}

			TArray<TArray<FVector2D>> CellPolygons;
			if (!GetCellPolygons(Cell, CellPolygons))
			{
				CachedSpiralCellBuilt[CellIndex] = true;
				continue;
			}
			CachedCellTriangles.Reset();

			for (const TArray<FVector2D>& CellPolygon : CellPolygons)
			{
				if (CellPolygon.Num() < 3)
				{
					continue;
				}
				FVector2D CellBoundsMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
				FVector2D CellBoundsMax(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
				for (const FVector2D& Point : CellPolygon)
				{
					CellBoundsMin.X = FMath::Min(CellBoundsMin.X, Point.X);
					CellBoundsMin.Y = FMath::Min(CellBoundsMin.Y, Point.Y);
					CellBoundsMax.X = FMath::Max(CellBoundsMax.X, Point.X);
					CellBoundsMax.Y = FMath::Max(CellBoundsMax.Y, Point.Y);
				}

				for (const FChunkedCachedSpiralTriangle& Triangle : CachedSpiralSourceTriangles)
				{
					if (Triangle.BoundsMax.X < CellBoundsMin.X || Triangle.BoundsMin.X > CellBoundsMax.X ||
						Triangle.BoundsMax.Y < CellBoundsMin.Y || Triangle.BoundsMin.Y > CellBoundsMax.Y)
					{
						continue;
					}
					const TArray<FVector2D> Fragment = ClipSharedSpiralTriangle(Triangle, CellPolygon);
					for (int32 PointIndex = 1; PointIndex + 1 < Fragment.Num(); ++PointIndex)
					{
						FVector2D A = Fragment[0];
						FVector2D B = Fragment[PointIndex];
						FVector2D C = Fragment[PointIndex + 1];
						const double Area = SpiralCross2D(B - A, C - A);
						if (FMath::Abs(Area) <= UE_DOUBLE_SMALL_NUMBER)
						{
							continue;
						}
						if (Area < 0.0)
						{
							Swap(B, C);
						}
						FChunkedCachedSpiralTriangle& CachedTriangle = CachedCellTriangles.Emplace_GetRef();
						CachedTriangle.A = A;
						CachedTriangle.B = B;
						CachedTriangle.C = C;
					}
				}
			}
			CachedSpiralCellBuilt[CellIndex] = true;
			for (const FChunkedCachedSpiralTriangle& Triangle : CachedCellTriangles)
			{
				AppendSurfaceTriangle(Cell, Triangle.A, Triangle.B, Triangle.C, StripeMaterialIndex);
			}
		}
	}
}

void AChunkedDestructibleFloor::AppendGreedyHorizontalFaces(
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
	const bool bCanMergeX = CellSpacing.X <= SpacingTolerance;
	const bool bCanMergeY = CellSpacing.Y <= SpacingTolerance;

	auto LocalIndex = [Width, MinCellX, MinCellY](int32 X, int32 Y)
	{
		return (X - MinCellX) + (Y - MinCellY) * Width;
	};

	for (int32 Y = MinCellY; Y < MaxCellY; ++Y)
	{
		for (int32 X = MinCellX; X < MaxCellX; ++X)
		{
			const int32 StartVisitedIndex = LocalIndex(X, Y);
			if (Visited[StartVisitedIndex])
			{
				continue;
			}

			const FFloorCellData& StartCell = Cells[ToCellIndex(FIntPoint(X, Y))];
			if (!StartCell.bExists)
			{
				Visited[StartVisitedIndex] = true;
				continue;
			}

			const int32 MaterialIndex = StartCell.MaterialIndex;
			int32 MergeWidth = 1;
			if (bCanMergeX)
			{
				while (X + MergeWidth < MaxCellX)
				{
					const int32 TestVisitedIndex = LocalIndex(X + MergeWidth, Y);
					const FFloorCellData& TestCell = Cells[ToCellIndex(FIntPoint(X + MergeWidth, Y))];
					if (Visited[TestVisitedIndex] || !TestCell.bExists || TestCell.MaterialIndex != MaterialIndex)
					{
						break;
					}
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
						const int32 TestVisitedIndex = LocalIndex(TestX, TestY);
						const FFloorCellData& TestCell = Cells[ToCellIndex(FIntPoint(TestX, TestY))];
						if (Visited[TestVisitedIndex] || !TestCell.bExists || TestCell.MaterialIndex != MaterialIndex)
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

			const FVector FirstCenter = GetCellLocalCenter(FIntPoint(X, Y));
			const FVector LastCenter = GetCellLocalCenter(FIntPoint(X + MergeWidth - 1, Y + MergeHeight - 1));
			const double MinX = FirstCenter.X - EffectiveCellDimensions.X * 0.5;
			const double MaxX = LastCenter.X + EffectiveCellDimensions.X * 0.5;
			const double MinY = FirstCenter.Y - EffectiveCellDimensions.Y * 0.5;
			const double MaxY = LastCenter.Y + EffectiveCellDimensions.Y * 0.5;
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

void AChunkedDestructibleFloor::BuildChunkSimpleCollision(
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
	const bool bCanMergeX = CellSpacing.X <= SpacingTolerance;
	const bool bCanMergeY = CellSpacing.Y <= SpacingTolerance;

	auto LocalIndex = [Width, MinCellX, MinCellY](int32 X, int32 Y)
	{
		return (X - MinCellX) + (Y - MinCellY) * Width;
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

			if (!Cells[ToCellIndex(FIntPoint(X, Y))].bExists)
			{
				Visited[StartIndex] = true;
				continue;
			}

			int32 MergeWidth = 1;
			if (bCanMergeX)
			{
				while (X + MergeWidth < MaxCellX)
				{
					const int32 TestIndex = LocalIndex(X + MergeWidth, Y);
					if (Visited[TestIndex] || !Cells[ToCellIndex(FIntPoint(X + MergeWidth, Y))].bExists)
					{
						break;
					}
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
						const int32 TestIndex = LocalIndex(TestX, TestY);
						if (Visited[TestIndex] || !Cells[ToCellIndex(FIntPoint(TestX, TestY))].bExists)
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

			const FVector FirstCenter = GetCellLocalCenter(FIntPoint(X, Y));
			const FVector LastCenter = GetCellLocalCenter(FIntPoint(X + MergeWidth - 1, Y + MergeHeight - 1));
			const double BoxMinX = FirstCenter.X - EffectiveCellDimensions.X * 0.5;
			const double BoxMaxX = LastCenter.X + EffectiveCellDimensions.X * 0.5;
			const double BoxMinY = FirstCenter.Y - EffectiveCellDimensions.Y * 0.5;
			const double BoxMaxY = LastCenter.Y + EffectiveCellDimensions.Y * 0.5;

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
}

int32 AChunkedDestructibleFloor::ToCellIndex(FIntPoint Cell) const
{
	return Cell.X + Cell.Y * CellCount.X;
}

FIntPoint AChunkedDestructibleFloor::FromCellIndex(int32 CellIndex) const
{
	return CellCount.X > 0
		? FIntPoint(CellIndex % CellCount.X, CellIndex / CellCount.X)
		: FIntPoint::ZeroValue;
}

bool AChunkedDestructibleFloor::IsValidCell(FIntPoint Cell) const
{
	return Cell.X >= 0 && Cell.Y >= 0 && Cell.X < CellCount.X && Cell.Y < CellCount.Y;
}

FVector AChunkedDestructibleFloor::GetCellLocalCenter(FIntPoint Cell) const
{
	return FVector(
		GridLocalMin.X + EffectiveCellDimensions.X * 0.5 + Cell.X * (EffectiveCellDimensions.X + FMath::Max(CellSpacing.X, 0.0)),
		GridLocalMin.Y + EffectiveCellDimensions.Y * 0.5 + Cell.Y * (EffectiveCellDimensions.Y + FMath::Max(CellSpacing.Y, 0.0)),
		-EffectiveCellDimensions.Z * 0.5);
}

FVector AChunkedDestructibleFloor::GetCellWorldCenter(FIntPoint Cell) const
{
	return GetActorTransform().TransformPosition(GetCellLocalCenter(Cell));
}

int32 AChunkedDestructibleFloor::EvaluatePatternMaterial(FIntPoint Cell) const
{
	const int32 SafePrimary = FMath::Max(PrimaryMaterialIndex, 0);
	const int32 SafeSecondary = FMath::Max(SecondaryMaterialIndex, 0);
	const int32 Scale = FMath::Max(PatternCellScale, 1);
	const int32 BlockX = Cell.X / Scale;
	const int32 BlockY = Cell.Y / Scale;
	bool bUseSecondary = false;

	switch (FloorPattern)
	{
	case EChunkedFloorPattern::Checkerboard:
		bUseSecondary = ((BlockX + BlockY) & 1) != 0;
		break;
	case EChunkedFloorPattern::StripesX:
		bUseSecondary = (BlockX & 1) != 0;
		break;
	case EChunkedFloorPattern::StripesY:
		bUseSecondary = (BlockY & 1) != 0;
		break;
	case EChunkedFloorPattern::DiagonalStripes:
		bUseSecondary = (((Cell.X + Cell.Y) / Scale) & 1) != 0;
		break;
	case EChunkedFloorPattern::ConcentricRings:
	{
		const FVector2D Center((CellCount.X - 1) * 0.5, (CellCount.Y - 1) * 0.5);
		const int32 Ring = FMath::FloorToInt(FVector2D::Distance(FVector2D(Cell), Center) / Scale);
		bUseSecondary = (Ring & 1) != 0;
		break;
	}
	case EChunkedFloorPattern::HypnoticSpiral:
		// The shared mesh overlay draws the continuous stripe; backing cells stay one color.
		bUseSecondary = false;
		break;
	case EChunkedFloorPattern::SeededRandom:
	{
		uint32 Hash = HashCombineFast(GetTypeHash(BlockX), GetTypeHash(BlockY));
		Hash = HashCombineFast(Hash, GetTypeHash(PatternRandomSeed));
		bUseSecondary = (Hash & 1u) != 0;
		break;
	}
	case EChunkedFloorPattern::Solid:
	default:
		break;
	}

	const int32 Selected = bUseSecondary ? SafeSecondary : SafePrimary;
	return FloorMaterials.IsEmpty() ? 0 : FMath::Clamp(Selected, 0, FloorMaterials.Num() - 1);
}

bool AChunkedDestructibleFloor::WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const
{
	OutCell = FIntPoint::NoneValue;
	if (CellCount.X <= 0 || CellCount.Y <= 0)
	{
		return false;
	}

	const FVector LocalLocation = GetActorTransform().InverseTransformPosition(WorldLocation);
	const double PitchX = EffectiveCellDimensions.X + FMath::Max(CellSpacing.X, 0.0);
	const double PitchY = EffectiveCellDimensions.Y + FMath::Max(CellSpacing.Y, 0.0);
	const double RelativeX = LocalLocation.X - GridLocalMin.X;
	const double RelativeY = LocalLocation.Y - GridLocalMin.Y;
	int32 X = FMath::FloorToInt(RelativeX / PitchX);
	int32 Y = FMath::FloorToInt(RelativeY / PitchY);
	if (X == CellCount.X && FMath::IsNearlyEqual(RelativeX, EffectiveGridSize.X, 0.1))
	{
		X = CellCount.X - 1;
	}
	if (Y == CellCount.Y && FMath::IsNearlyEqual(RelativeY, EffectiveGridSize.Y, 0.1))
	{
		Y = CellCount.Y - 1;
	}
	const FIntPoint Candidate(X, Y);
	if (!IsValidCell(Candidate))
	{
		return false;
	}

	const FVector Center = GetCellLocalCenter(Candidate);
	const double Tolerance = 0.1;
	if (FMath::Abs(LocalLocation.X - Center.X) > EffectiveCellDimensions.X * 0.5 + Tolerance ||
		FMath::Abs(LocalLocation.Y - Center.Y) > EffectiveCellDimensions.Y * 0.5 + Tolerance)
	{
		return false;
	}

	OutCell = Candidate;
	return true;
}

bool AChunkedDestructibleFloor::GetCellInfo(FIntPoint Cell, FChunkedFloorCellInfo& OutInfo) const
{
	OutInfo = FChunkedFloorCellInfo();
	if (!IsValidCell(Cell))
	{
		return false;
	}

	const int32 CellIndex = ToCellIndex(Cell);
	const FFloorCellData& CellData = Cells[CellIndex];
	OutInfo.bExists = CellData.bExists;
	OutInfo.bDestructible = CellData.bDestructible;
	OutInfo.bPendingDestruction = PendingCellIndices.Contains(CellIndex);
	OutInfo.MaterialIndex = CellData.MaterialIndex;
	OutInfo.Health = CellData.Health;
	return true;
}

bool AChunkedDestructibleFloor::DamageCellAtWorldLocation(FVector WorldLocation, float DamageAmount)
{
	FIntPoint Cell;
	if (DamageAmount <= 0.0f || !WorldLocationToCell(WorldLocation, Cell))
	{
		return false;
	}

	const int32 CellIndex = ToCellIndex(Cell);
	FFloorCellData& CellData = Cells[CellIndex];
	if (!CellData.bExists || !CellData.bDestructible || PendingCellIndices.Contains(CellIndex))
	{
		return false;
	}

	CellData.Health -= DamageAmount;
	if (CellData.Health <= 0.0f)
	{
		QueueCellDestruction(CellIndex, 0.0f, false);
		ProcessQueuedDestructions(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	}
	return true;
}

FVector AChunkedDestructibleFloor::GetInteriorImpactPoint(const FHitResult& Hit) const
{
	const FVector ImpactPoint = Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint;
	FVector InwardWorldDirection = -Hit.ImpactNormal.GetSafeNormal();
	if (InwardWorldDirection.IsNearlyZero())
	{
		InwardWorldDirection = -Hit.Normal.GetSafeNormal();
	}
	if (InwardWorldDirection.IsNearlyZero())
	{
		// A trace or sweep travels from TraceStart to TraceEnd, so this direction points into
		// the surface at the impact. It is a useful fallback for manually constructed hits.
		InwardWorldDirection = (Hit.TraceEnd - Hit.TraceStart).GetSafeNormal();
	}
	if (InwardWorldDirection.IsNearlyZero())
	{
		return ImpactPoint;
	}

	const FTransform ActorTransform = GetActorTransform();
	const FVector LocalImpact = ActorTransform.InverseTransformPosition(ImpactPoint);
	const FVector LocalInwardDirection = ActorTransform
		.InverseTransformVectorNoScale(InwardWorldDirection)
		.GetSafeNormal();

	// A relative probe remains large enough to cross floating-point boundary noise while never
	// crossing the opposite face of even a very small cell.
	const float SmallestCellDimension = FMath::Max(
		FMath::Min3(EffectiveCellDimensions.X, EffectiveCellDimensions.Y, EffectiveCellDimensions.Z),
		MinimumFloorDimension);
	const float InteriorProbeDistance = FMath::Min(
		FMath::Clamp(SmallestCellDimension * 0.02f, 0.25f, 2.0f),
		SmallestCellDimension * 0.25f);

	return ActorTransform.TransformPosition(LocalImpact + LocalInwardDirection * InteriorProbeDistance);
}

bool AChunkedDestructibleFloor::ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& OutCell) const
{
	OutCell = FIntPoint::NoneValue;
	const FVector InteriorPoint = GetInteriorImpactPoint(Hit);
	FIntPoint Candidate;
	if (WorldLocationToCell(InteriorPoint, Candidate) &&
		Cells.IsValidIndex(ToCellIndex(Candidate)) &&
		Cells[ToCellIndex(Candidate)].bExists)
	{
		OutCell = Candidate;
		return true;
	}

	// At the exact edge or corner of a face, one tangential coordinate can also sit on a grid
	// boundary. Probe both sides of X/Y by a tiny amount and select only an existing cell. This
	// keeps exposed corners reliable without ever selecting a cell that has already disappeared.
	const FTransform ActorTransform = GetActorTransform();
	const FVector LocalInterior = ActorTransform.InverseTransformPosition(InteriorPoint);
	const float SmallestHorizontalDimension = FMath::Max(
		FMath::Min(EffectiveCellDimensions.X, EffectiveCellDimensions.Y),
		MinimumFloorDimension);
	const float TangentialProbeDistance = FMath::Min(
		FMath::Clamp(SmallestHorizontalDimension * 0.002f, 0.1f, 1.0f),
		SmallestHorizontalDimension * 0.1f);
	const FVector2D ProbeOffsets[] =
	{
		FVector2D(-TangentialProbeDistance, 0.0),
		FVector2D(TangentialProbeDistance, 0.0),
		FVector2D(0.0, -TangentialProbeDistance),
		FVector2D(0.0, TangentialProbeDistance),
		FVector2D(-TangentialProbeDistance, -TangentialProbeDistance),
		FVector2D(-TangentialProbeDistance, TangentialProbeDistance),
		FVector2D(TangentialProbeDistance, -TangentialProbeDistance),
		FVector2D(TangentialProbeDistance, TangentialProbeDistance)
	};

	for (const FVector2D& Offset : ProbeOffsets)
	{
		const FVector ProbeLocal = LocalInterior + FVector(Offset.X, Offset.Y, 0.0);
		if (WorldLocationToCell(ActorTransform.TransformPosition(ProbeLocal), Candidate) &&
			Cells.IsValidIndex(ToCellIndex(Candidate)) &&
			Cells[ToCellIndex(Candidate)].bExists)
		{
			OutCell = Candidate;
			return true;
		}
	}

	return false;
}

bool AChunkedDestructibleFloor::DamageCellFromHit(const FHitResult& Hit, float DamageAmount)
{
	FIntPoint Cell;
	if (DamageAmount <= 0.0f || !ResolveExistingCellFromHit(Hit, Cell))
	{
		return false;
	}

	// Resolve once from the full hit, then use the unambiguous center of that cell so all existing
	// health, destructibility, flash, sound and chunk-rebuild behavior stays in one code path.
	return DamageCellAtWorldLocation(GetCellWorldCenter(Cell), DamageAmount);
}

int32 AChunkedDestructibleFloor::ApplyExplosionAtWorldLocation(
	FVector WorldLocation,
	float Radius,
	float DamageAmount)
{
	return DamageCellsInRadius(
		WorldLocation,
		Radius,
		DamageAmount,
		DefaultBlastShape,
		bUseRadialDamageFalloff);
}

int32 AChunkedDestructibleFloor::ApplyExplosionFromHit(
	const FHitResult& Hit,
	float Radius,
	float DamageAmount)
{
	return ApplyExplosionAtWorldLocation(GetInteriorImpactPoint(Hit), Radius, DamageAmount);
}

float AChunkedDestructibleFloor::MeasureBlastDistance(FVector2D Delta, EChunkedFloorBlastShape Shape) const
{
	switch (Shape)
	{
	case EChunkedFloorBlastShape::Square:
		return FMath::Max(FMath::Abs(Delta.X), FMath::Abs(Delta.Y));
	case EChunkedFloorBlastShape::Diamond:
		return FMath::Abs(Delta.X) + FMath::Abs(Delta.Y);
	case EChunkedFloorBlastShape::Circle:
	default:
		return Delta.Size();
	}
}

int32 AChunkedDestructibleFloor::SelectCellsInRadius(
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
	const FVector ActorScale = GetActorScale3D().GetAbs();
	const float ScaleX = FMath::Max(ActorScale.X, KINDA_SMALL_NUMBER);
	const float ScaleY = FMath::Max(ActorScale.Y, KINDA_SMALL_NUMBER);
	const float LocalRadiusX = Radius / ScaleX;
	const float LocalRadiusY = Radius / ScaleY;
	const float PitchX = EffectiveCellDimensions.X + FMath::Max(CellSpacing.X, 0.0);
	const float PitchY = EffectiveCellDimensions.Y + FMath::Max(CellSpacing.Y, 0.0);

	const int32 MinX = FMath::Clamp(
		FMath::FloorToInt((LocalImpact.X - LocalRadiusX - GridLocalMin.X) / PitchX), 0, CellCount.X - 1);
	const int32 MaxX = FMath::Clamp(
		FMath::FloorToInt((LocalImpact.X + LocalRadiusX - GridLocalMin.X) / PitchX), 0, CellCount.X - 1);
	const int32 MinY = FMath::Clamp(
		FMath::FloorToInt((LocalImpact.Y - LocalRadiusY - GridLocalMin.Y) / PitchY), 0, CellCount.Y - 1);
	const int32 MaxY = FMath::Clamp(
		FMath::FloorToInt((LocalImpact.Y + LocalRadiusY - GridLocalMin.Y) / PitchY), 0, CellCount.Y - 1);

	int32 SelectedCount = 0;
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const FIntPoint Cell(X, Y);
			const FVector LocalCenter = GetCellLocalCenter(Cell);
			const FVector2D ScaledDelta(
				(LocalCenter.X - LocalImpact.X) * ScaleX,
				(LocalCenter.Y - LocalImpact.Y) * ScaleY);
			const float Distance = MeasureBlastDistance(ScaledDelta, Shape);
			if (Distance <= Radius)
			{
				CellFunction(ToCellIndex(Cell), Distance);
				++SelectedCount;
			}
		}
	}
	return SelectedCount;
}

int32 AChunkedDestructibleFloor::DamageCellsInRadius(
	FVector WorldLocation,
	float Radius,
	float DamageAmount,
	EChunkedFloorBlastShape Shape,
	bool bUseDamageFalloff)
{
	if (DamageAmount <= 0.0f)
	{
		return 0;
	}

	int32 DamagedCount = 0;
	SelectCellsInRadius(WorldLocation, Radius, Shape,
		[this, Radius, DamageAmount, bUseDamageFalloff, &DamagedCount](int32 CellIndex, float Distance)
		{
			FFloorCellData& CellData = Cells[CellIndex];
			if (!CellData.bExists || !CellData.bDestructible || PendingCellIndices.Contains(CellIndex))
			{
				return;
			}

			const float Alpha = Radius > KINDA_SMALL_NUMBER ? FMath::Clamp(Distance / Radius, 0.0f, 1.0f) : 0.0f;
			const float DamageScale = bUseDamageFalloff
				? FMath::Lerp(1.0f, FMath::Clamp(MinimumRadialDamageFraction, 0.0f, 1.0f), Alpha)
				: 1.0f;
			CellData.Health -= DamageAmount * DamageScale;
			++DamagedCount;

			if (CellData.Health <= 0.0f)
			{
				const float Delay = bUseDestructionRipple
					? Distance / FMath::Max(DestructionRippleSpeed, 1.0f)
					: 0.0f;
				QueueCellDestruction(CellIndex, Delay, true);
			}
		});

	ProcessQueuedDestructions(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	return DamagedCount;
}

bool AChunkedDestructibleFloor::DestroyCell(FIntPoint Cell)
{
	if (!IsValidCell(Cell))
	{
		return false;
	}

	const bool bQueued = QueueCellDestruction(ToCellIndex(Cell), 0.0f, false);
	if (bQueued)
	{
		ProcessQueuedDestructions(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	}
	return bQueued;
}

int32 AChunkedDestructibleFloor::DestroyCellsInRadius(
	FVector WorldLocation,
	float Radius,
	EChunkedFloorBlastShape Shape)
{
	int32 QueuedCount = 0;
	SelectCellsInRadius(WorldLocation, Radius, Shape,
		[this, &QueuedCount](int32 CellIndex, float Distance)
		{
			const float Delay = bUseDestructionRipple
				? Distance / FMath::Max(DestructionRippleSpeed, 1.0f)
				: 0.0f;
			if (QueueCellDestruction(CellIndex, Delay, true))
			{
				++QueuedCount;
			}
		});

	ProcessQueuedDestructions(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	return QueuedCount;
}

bool AChunkedDestructibleFloor::QueueCellDestruction(int32 CellIndex, float Delay, bool bRadial)
{
	if (!Cells.IsValidIndex(CellIndex) || PendingCellIndices.Contains(CellIndex))
	{
		return false;
	}

	const FFloorCellData& CellData = Cells[CellIndex];
	if (!CellData.bExists || !CellData.bDestructible)
	{
		return false;
	}

	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	FQueuedCellDestruction Queued;
	Queued.CellIndex = CellIndex;
	Queued.ExecuteTime = CurrentTime + FMath::Max(Delay, 0.0f);
	Queued.bRadial = bRadial;
	QueuedDestructions.HeapPush(MoveTemp(Queued));
	PendingCellIndices.Add(CellIndex);
	SetActorTickEnabled(true);
	return true;
}

void AChunkedDestructibleFloor::ProcessQueuedDestructions(double CurrentTime)
{
	bool bDestroyedAnyCell = false;
	while (!QueuedDestructions.IsEmpty() && QueuedDestructions[0].ExecuteTime <= CurrentTime)
	{
		FQueuedCellDestruction Queued;
		QueuedDestructions.HeapPop(Queued, EAllowShrinking::No);
		if (!PendingCellIndices.Remove(Queued.CellIndex) || !Cells.IsValidIndex(Queued.CellIndex))
		{
			continue;
		}

		FFloorCellData& CellData = Cells[Queued.CellIndex];
		if (!CellData.bExists || !CellData.bDestructible)
		{
			continue;
		}

		CellData.bExists = false;
		CellData.Health = 0.0f;
		const FIntPoint Cell = FromCellIndex(Queued.CellIndex);
		const FVector WorldCenter = GetCellWorldCenter(Cell);
		MarkCellChunksDirty(Cell);
		SpawnCellFlash(Cell, CurrentTime);
		PlayDestroySound(WorldCenter, CurrentTime);
		OnCellDestroyed.Broadcast(Cell, WorldCenter, Queued.bRadial);
		bDestroyedAnyCell = true;
	}

	if (bDestroyedAnyCell)
	{
		FlushDirtyChunks();
	}
	if (QueuedDestructions.IsEmpty())
	{
		SetActorTickEnabled(false);
	}
}

void AChunkedDestructibleFloor::MarkCellChunksDirty(FIntPoint Cell)
{
	static const FIntPoint Offsets[] = {
		FIntPoint(0, 0), FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(0, 1)
	};
	const int32 SafeChunkSide = FMath::Clamp(ChunkSideLength, 4, 128);

	for (const FIntPoint Offset : Offsets)
	{
		const FIntPoint TestCell = Cell + Offset;
		if (!IsValidCell(TestCell))
		{
			continue;
		}

		const int32 ChunkX = TestCell.X / SafeChunkSide;
		const int32 ChunkY = TestCell.Y / SafeChunkSide;
		DirtyChunkIndices.Add(ChunkX + ChunkY * ChunkCount.X);
	}
}

void AChunkedDestructibleFloor::FlushDirtyChunks()
{
	for (const int32 ChunkIndex : DirtyChunkIndices)
	{
		RebuildChunk(ChunkIndex);
	}
	DirtyChunkIndices.Reset();
}

void AChunkedDestructibleFloor::RefreshFlashMaterial()
{
	if (!FlashInstances)
	{
		return;
	}

	UMaterialInterface* ParentMaterial = FlashMaterial
		? FlashMaterial.Get()
		: UMaterial::GetDefaultMaterial(MD_Surface);
	// An ISM rendered with a regular surface material silently falls back to Unreal's gray
	// checker shader. Use the bundled, precompiled flash material whenever the requested
	// material was not authored for Instanced Static Meshes.
	if (!ParentMaterial || !ParentMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
	{
		static TWeakObjectPtr<UMaterialInterface> CompatibleFlashMaterial;
		if (!CompatibleFlashMaterial.IsValid())
		{
			CompatibleFlashMaterial = LoadObject<UMaterialInterface>(
				nullptr,
				TEXT("/Game/LevelPrototyping/Interactable/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
		}
		if (CompatibleFlashMaterial.IsValid() &&
			CompatibleFlashMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes))
		{
			ParentMaterial = CompatibleFlashMaterial.Get();
		}
	}
	FlashMaterialInstance = UMaterialInstanceDynamic::Create(ParentMaterial, this);
	if (FlashMaterialInstance)
	{
		const FLinearColor EmissiveColor = FlashColor * FMath::Max(FlashIntensity, 0.0f);
		FlashMaterialInstance->SetVectorParameterValue(TEXT("Color"), EmissiveColor);
		FlashMaterialInstance->SetVectorParameterValue(TEXT("BaseColor"), FlashColor);
		FlashMaterialInstance->SetVectorParameterValue(TEXT("GlowColor"), EmissiveColor);
		FlashMaterialInstance->SetVectorParameterValue(TEXT("EmissiveColor"), EmissiveColor);
		FlashMaterialInstance->SetScalarParameterValue(TEXT("Alpha"), 1.0f);
		FlashMaterialInstance->SetScalarParameterValue(TEXT("Intensity"), FlashIntensity);
		FlashMaterialInstance->SetScalarParameterValue(TEXT("GlowIntensity"), FlashIntensity);
		FlashMaterialInstance->SetScalarParameterValue(TEXT("EmissiveIntensity"), FlashIntensity);
		FlashInstances->SetMaterial(0, FlashMaterialInstance);
	}
}

void AChunkedDestructibleFloor::SpawnCellFlash(FIntPoint Cell, double CurrentTime)
{
	if (!FlashInstances || FlashDuration <= 0.0f)
	{
		return;
	}

	// RebuildFlashInstances detaches the mesh when the last flash expires. Detaching the render
	// source (rather than only clearing instances) prevents UE 5.8 PIE from keeping a stale final
	// cube in the ISM scene proxy. Restore the shared cube lazily for the next impact.
	if (!FlashInstances->GetStaticMesh())
	{
		UStaticMesh* FlashCubeMesh = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Engine/BasicShapes/Cube.Cube"));
		FlashInstances->SetStaticMesh(FlashCubeMesh);
	}
	if (!FlashInstances->GetStaticMesh())
	{
		return;
	}

	const FTransform InstanceTransform = GetCellFlashTransform(Cell);
	const int32 InstanceIndex = FlashInstances->AddInstance(InstanceTransform);
	if (InstanceIndex != INDEX_NONE)
	{
		FActiveFlash& Flash = ActiveFlashes.AddDefaulted_GetRef();
		Flash.Transform = InstanceTransform;
		Flash.ExpireTime = CurrentTime + FlashDuration;
		FlashInstances->SetVisibility(true);

		// One low-frequency cleanup timer batches all expirations. A timer per cell caused mass
		// destruction to rebuild the complete ISM repeatedly in the same frame.
		if (UWorld* World = GetWorld())
		{
			if (!World->GetTimerManager().IsTimerActive(FlashCleanupTimerHandle))
			{
				World->GetTimerManager().SetTimer(
					FlashCleanupTimerHandle,
					this,
					&AChunkedDestructibleFloor::UpdateFlashes,
					FlashCleanupInterval,
					true,
					FlashCleanupInterval);
			}
		}
	}
}

FTransform AChunkedDestructibleFloor::GetCellFlashTransform(FIntPoint Cell) const
{
	const FVector Scale = EffectiveCellDimensions / 100.0 * FMath::Max(FlashScale, 0.01f);
	return FTransform(FQuat::Identity, GetCellLocalCenter(Cell), Scale);
}

void AChunkedDestructibleFloor::UpdateFlashes()
{
	const double CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const int32 RemovedCount = ActiveFlashes.RemoveAllSwap(
		[CurrentTime](const FActiveFlash& Flash)
		{
			return Flash.ExpireTime <= CurrentTime;
		},
		EAllowShrinking::No);
	if (RemovedCount > 0)
	{
		RebuildFlashInstances();
	}
	if (ActiveFlashes.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(FlashCleanupTimerHandle);
	}
}

void AChunkedDestructibleFloor::RebuildFlashInstances()
{
	if (!FlashInstances)
	{
		return;
	}

	FlashInstances->ClearInstances();
	if (ActiveFlashes.IsEmpty())
	{
		// ClearInstances alone can leave the final ISM render buffer visible in PIE. Removing the
		// source mesh destroys that proxy unequivocally; SpawnCellFlash restores it on demand.
		FlashInstances->SetVisibility(false, true);
		FlashInstances->SetStaticMesh(nullptr);
		FlashInstances->MarkRenderStateDirty();
		return;
	}

	if (!FlashInstances->GetStaticMesh())
	{
		UStaticMesh* FlashCubeMesh = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Engine/BasicShapes/Cube.Cube"));
		FlashInstances->SetStaticMesh(FlashCubeMesh);
	}
	TArray<FTransform> InstanceTransforms;
	InstanceTransforms.Reserve(ActiveFlashes.Num());
	for (const FActiveFlash& Flash : ActiveFlashes)
	{
		InstanceTransforms.Add(Flash.Transform);
	}
	FlashInstances->AddInstances(InstanceTransforms, false, false, false);
	FlashInstances->SetVisibility(true, true);
	FlashInstances->MarkRenderStateDirty();
}

void AChunkedDestructibleFloor::PlayDestroySound(FVector WorldLocation, double CurrentTime)
{
	if (!bPlayDestroyTickSound || !DestroyTickSound || DestroySoundVolume <= 0.0f)
	{
		return;
	}

	RecentDestroySoundTimes.RemoveAll(
		[CurrentTime](double SoundTime)
		{
			return CurrentTime - SoundTime >= 1.0;
		});
	if (MaxDestroySoundsPerSecond > 0 && RecentDestroySoundTimes.Num() >= MaxDestroySoundsPerSecond)
	{
		return;
	}

	const float MinPitch = FMath::Max(FMath::Min(DestroySoundPitchRange.X, DestroySoundPitchRange.Y), 0.01f);
	const float MaxPitch = FMath::Max(FMath::Max(DestroySoundPitchRange.X, DestroySoundPitchRange.Y), MinPitch);
	UGameplayStatics::PlaySoundAtLocation(
		this,
		DestroyTickSound,
		WorldLocation,
		DestroySoundVolume,
		FMath::FRandRange(MinPitch, MaxPitch),
		0.0f,
		DestroySoundAttenuation);
	RecentDestroySoundTimes.Add(CurrentTime);
}

bool AChunkedDestructibleFloor::RestoreCell(FIntPoint Cell)
{
	if (!IsValidCell(Cell) || !ShouldCellExistInGeneratedShape(Cell))
	{
		return false;
	}

	const int32 CellIndex = ToCellIndex(Cell);
	FFloorCellData& CellData = Cells[CellIndex];
	const bool bChanged = !CellData.bExists || PendingCellIndices.Contains(CellIndex);
	PendingCellIndices.Remove(CellIndex);
	CellData.bExists = true;
	CellData.Health = FMath::Max(DefaultCellHealth, 0.01f);
	if (bChanged)
	{
		MarkCellChunksDirty(Cell);
		FlushDirtyChunks();
	}
	return bChanged;
}

void AChunkedDestructibleFloor::RestoreAllCells()
{
	QueuedDestructions.Reset();
	PendingCellIndices.Reset();
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		FFloorCellData& CellData = Cells[CellIndex];
		CellData.bBelongsToGeneratedShape = ShouldCellExistInGeneratedShape(FromCellIndex(CellIndex));
		CellData.bExists = CellData.bBelongsToGeneratedShape;
		CellData.Health = FMath::Max(DefaultCellHealth, 0.01f);
	}
	RebuildAllChunks();
}

bool AChunkedDestructibleFloor::SetCellDestructible(FIntPoint Cell, bool bNewDestructible)
{
	if (!IsValidCell(Cell))
	{
		return false;
	}

	const int32 CellIndex = ToCellIndex(Cell);
	Cells[CellIndex].bDestructible = bNewDestructible;
	if (!bNewDestructible)
	{
		PendingCellIndices.Remove(CellIndex);
	}
	return true;
}

void AChunkedDestructibleFloor::SetAllCellsDestructible(bool bNewDestructible)
{
	for (FFloorCellData& CellData : Cells)
	{
		CellData.bDestructible = bNewDestructible;
	}
	if (!bNewDestructible)
	{
		PendingCellIndices.Reset();
		QueuedDestructions.Reset();
	}
}

bool AChunkedDestructibleFloor::SetCellMaterial(FIntPoint Cell, int32 NewMaterialIndex)
{
	if (!IsValidCell(Cell) || FloorMaterials.IsEmpty())
	{
		return false;
	}

	FFloorCellData& CellData = Cells[ToCellIndex(Cell)];
	CellData.MaterialIndex = FMath::Clamp(NewMaterialIndex, 0, FloorMaterials.Num() - 1);
	MarkCellChunksDirty(Cell);
	FlushDirtyChunks();
	return true;
}
