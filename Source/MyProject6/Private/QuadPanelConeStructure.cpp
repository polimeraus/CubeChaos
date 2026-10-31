// Copyright Epic Games, Inc. All Rights Reserved.

#include "QuadPanelConeStructure.h"
#include "BlastGeometry.h"
#include "Components/ChildActorComponent.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CompGeom/PolygonTriangulation.h"
#include "ConstrainedDelaunay2.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RotationMatrix.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "TimerManager.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
	constexpr float QuadMinimumConeDimension = 1.0f;
	constexpr float QuadConeSpacingTolerance = 0.01f;
	constexpr double QuadConeSurfaceTolerance = 0.001;
	// Surface clipping must retain sub-millimeter corners. The larger tolerance used
	// for solid queries/booleans can erase a long, thin triangle and expose the sky.
	constexpr double QuadConeClipTolerance = 1.e-8;

	void QuadClipSurfacePolygonToPlane(
		TArray<FVector3d> &Polygon,
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
		const double PreviousDistance = bKeepGreater
									  ? Previous[Axis] - Plane
									  : Plane - Previous[Axis];
		bool bPreviousInside = PreviousDistance >= 0.0;
		for (const FVector3d &Current : Input)
		{
			const double CurrentDistance = bKeepGreater
											   ? Current[Axis] - Plane
											   : Plane - Current[Axis];
			const bool bCurrentInside = CurrentDistance >= 0.0;
			if (bCurrentInside != bPreviousInside)
			{
				// Always intersect an edge in the same direction, then snap the clipped
				// coordinate. Neighboring cells/triangles get exactly the same endpoint.
				const FVector3d& Low = Previous[Axis] < Current[Axis] ? Previous : Current;
				const FVector3d& High = Previous[Axis] < Current[Axis] ? Current : Previous;
				const double Alpha = (Plane - Low[Axis]) / (High[Axis] - Low[Axis]);
				FVector3d Intersection = Low + (High - Low) * Alpha;
				Intersection[Axis] = Plane;
				Polygon.Add(Intersection);
			}
			if (bCurrentInside)
			{
				Polygon.Add(Current);
			}
			Previous = Current;
			bPreviousInside = bCurrentInside;
		}
	}

	void QuadRemoveDuplicateSurfacePoints(TArray<FVector3d> &Polygon)
	{
		for (int32 Index = Polygon.Num() - 1; Index >= 0 && Polygon.Num() > 1; --Index)
		{
			const int32 PreviousIndex = (Index - 1 + Polygon.Num()) % Polygon.Num();
			if ((Polygon[Index] - Polygon[PreviousIndex]).SquaredLength() <=
				FMath::Square(QuadConeClipTolerance))
			{
				Polygon.RemoveAt(Index, 1, EAllowShrinking::No);
			}
		}
	}

	bool QuadDoesSurfaceTriangleIntersectBox(
		const FVector3d &A,
		const FVector3d &B,
		const FVector3d &C,
		const FVector3d &BoxMinimum,
		const FVector3d &BoxMaximum)
	{
		TArray<FVector3d> Polygon = {A, B, C};
		for (int32 Axis = 0; Axis < 3 && Polygon.Num() >= 3; ++Axis)
		{
			QuadClipSurfacePolygonToPlane(Polygon, Axis, BoxMinimum[Axis], true);
			QuadClipSurfacePolygonToPlane(Polygon, Axis, BoxMaximum[Axis], false);
		}
		QuadRemoveDuplicateSurfacePoints(Polygon);
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
					Polygon[PointIndex + 1] - Polygon[0])
					.SquaredLength() >
				FMath::Square(QuadConeSurfaceTolerance))
			{
				return true;
			}
		}
		return false;
	}

	bool QuadDoesSurfaceQuadIntersectBox(
		const FVector3d &A,
		const FVector3d &B,
		const FVector3d &C,
		const FVector3d &D,
		const FVector3d &BoxMinimum,
		const FVector3d &BoxMaximum)
	{
		return QuadDoesSurfaceTriangleIntersectBox(A, B, C, BoxMinimum, BoxMaximum) ||
			   QuadDoesSurfaceTriangleIntersectBox(A, C, D, BoxMinimum, BoxMaximum);
	}

	bool QuadSpiralIntersectsCellBounds(const FVector& Center, const FVector& Dimensions,
		float MinimumRadius, float MaximumRadius, float StartBottom, float EndBottom,
		float Thickness, float TotalAngle, float StartAngle, float Direction,
		int32 SegmentCount, float InnerStart, float InnerEnd, float OuterStart, float OuterEnd)
	{
		const double Radius = FVector2D(Center.X, Center.Y).Size();
		const double HalfDiagonal = FVector2D(Dimensions.X, Dimensions.Y).Size() * .5;
		const double AngularHalfWidth = Radius <= HalfDiagonal ? UE_DOUBLE_PI :
			FMath::Asin(FMath::Clamp(HalfDiagonal / Radius, 0.0, 1.0));
		// A rendered chord uses both section endpoints, which can lie outside the
		// cell's angular footprint. Bound their heights AND radii, not just the height
		// at the cell center; taper otherwise drops corners on the outside of a turn.
		const double SegmentAngle = double(TotalAngle) / SegmentCount;
		const double Margin = AngularHalfWidth + SegmentAngle;
		double Progress = FMath::Fmod(Direction * (FMath::Atan2(Center.Y, Center.X) - StartAngle), UE_DOUBLE_TWO_PI);
		if (Progress < 0) { Progress += UE_DOUBLE_TWO_PI; }
		const double InnerChordScale = FMath::Cos(SegmentAngle * .5);
		for (int32 Turn = -1; Turn <= FMath::CeilToInt(TotalAngle / UE_TWO_PI); ++Turn)
		{
			const double Mid = Progress + UE_DOUBLE_TWO_PI * Turn;
			if (Mid + Margin < 0 || Mid - Margin > TotalAngle) { continue; }
			const float Alpha0 = FMath::Clamp((Mid - Margin) / TotalAngle, 0.0, 1.0);
			const float Alpha1 = FMath::Clamp((Mid + Margin) / TotalAngle, 0.0, 1.0);
			const float Bottom0 = FMath::Lerp(StartBottom, EndBottom, Alpha0);
			const float Bottom1 = FMath::Lerp(StartBottom, EndBottom, Alpha1);
			if (Center.Z + Dimensions.Z * .5 < FMath::Min(Bottom0, Bottom1) - QuadConeSpacingTolerance ||
				Center.Z - Dimensions.Z * .5 > FMath::Max(Bottom0, Bottom1) + Thickness + QuadConeSpacingTolerance) { continue; }
			const float Inner = FMath::Max(0.0f, FMath::Min(FMath::Lerp(InnerStart, InnerEnd, Alpha0), FMath::Lerp(InnerStart, InnerEnd, Alpha1)));
			const float Outer = FMath::Max(FMath::Lerp(OuterStart, OuterEnd, Alpha0), FMath::Lerp(OuterStart, OuterEnd, Alpha1));
			if (MinimumRadius <= Outer + QuadConeSpacingTolerance &&
				MaximumRadius >= Inner * InnerChordScale - QuadConeSpacingTolerance) { return true; }
		}
		return false;
	}
}

AQuadPanelConeStructure::AQuadPanelConeStructure()
{
    GroundLaunchPad=CreateDefaultSubobject<UChildActorComponent>(TEXT("GroundLaunchPad"));
    GroundLaunchPad->SetupAttachment(GetRootComponent());
    ZigguratTraversal::InitializeMaterials(Traversal);
	bUseSmoothComplexCollision = true;
	SpiralDestructionMode = EConeStructureSpiralDestructionMode::OneSegmentPerHit;

	FloorPattern = EChunkedFloorPattern::Solid;
	ChunkSideLength = 64;
	bDestructibleByDefault = true;
	SpatialChunkSideLength = 8;
	DestructionFlashStyle = EConeStructureDestructionFlashStyle::DetailedSurface;
	FlashColor = FLinearColor::White;

	ApexDecorationComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("ConeApexDecoration"));
	ApexDecorationComponent->SetupAttachment(FloorRoot);
	ApexDecorationComponent->SetMobility(EComponentMobility::Movable);
	ApexDecorationComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ApexDecorationComponent->SetGenerateOverlapEvents(false);
	ApexDecorationComponent->SetCastShadow(true);
	ApexDecorationComponent->SetCanEverAffectNavigation(false);
	GlowWaveMaterial = FlashMaterial;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CrispSpiralMaterialFinder(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	if (CrispSpiralMaterialFinder.Succeeded())
	{
		CrispSpiralMaterial = CrispSpiralMaterialFinder.Object;
		CandySpireMaterial = CrispSpiralMaterialFinder.Object;
		StarMaterial = CrispSpiralMaterialFinder.Object;
		MoonMaterial = CrispSpiralMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		DebrisCubeMesh = CubeFinder.Object;
	}
}

void AQuadPanelConeStructure::OnConstruction(const FTransform &Transform)
{
	OnFloorRegenerated.AddUniqueDynamic(this, &AQuadPanelConeStructure::RefreshSpiralPlatformGlowAfterGeneration);
	bSpiralCollisionReadyForAsyncUpdates = false;
	Super::OnConstruction(Transform);
	RebuildApexDecoration();
	bSpiralCollisionReadyForAsyncUpdates = true;
	OnCellDestroyed.RemoveDynamic(this, &AQuadPanelConeStructure::HandleConeCellDestroyed);
	OnCellDestroyed.AddDynamic(this, &AQuadPanelConeStructure::HandleConeCellDestroyed);
}

void AQuadPanelConeStructure::BeginPlay()
{
	OnFloorRegenerated.AddUniqueDynamic(this, &AQuadPanelConeStructure::RefreshSpiralPlatformGlowAfterGeneration);
	bSpiralCollisionReadyForAsyncUpdates = false;
	Super::BeginPlay();
	RebuildApexDecoration();
	bSpiralCollisionReadyForAsyncUpdates = true;
	OnCellDestroyed.RemoveDynamic(this, &AQuadPanelConeStructure::HandleConeCellDestroyed);
	OnCellDestroyed.AddDynamic(this, &AQuadPanelConeStructure::HandleConeCellDestroyed);
	StartCharacterSpeedBoostMonitoring();
	PlaySpiralPlatformGlow();
}

void AQuadPanelConeStructure::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnFloorRegenerated.RemoveDynamic(this, &AQuadPanelConeStructure::RefreshSpiralPlatformGlowAfterGeneration);
	StopSpiralPlatformGlow();
	StopCharacterSpeedBoostMonitoring();
	ClearDebris();
	Super::EndPlay(EndPlayReason);
}

void AQuadPanelConeStructure::ClearAdditionalRuntimeEffects()
{
	StopSpiralPlatformGlow();
	for (const TWeakObjectPtr<UDynamicMeshComponent> &WeakFlash : ActiveSurfaceFlashComponents)
	{
		if (UDynamicMeshComponent *SurfaceFlash = WeakFlash.Get())
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
	DestroyedSpiralSegmentIndices.Reset();
	DestroyedSpiralTriangleSliceIndices.Reset();
}

#if WITH_EDITOR
bool AQuadPanelConeStructure::CanEditChange(const FProperty *InProperty) const
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

void AQuadPanelConeStructure::RebuildConeStructure(bool bPreserveCellState)
{
	GenerateFloor(bPreserveCellState);
}

void AQuadPanelConeStructure::SetPlatformRingWidth(
	float NewRingWidthBlocks,
	bool bPreserveCellState)
{
	PlatformOverhangBlocks = FMath::Clamp(FMath::Abs(NewRingWidthBlocks), 0.0f, 32.0f);
	GenerateFloor(bPreserveCellState);
}

void AQuadPanelConeStructure::SetPlatformStyle(
	EConeStructurePlatformStyle NewStyle,
	bool bPreserveCellState)
{
	PlatformStyle = NewStyle;
	GenerateFloor(bPreserveCellState);
}

void AQuadPanelConeStructure::SetApexStyle(EConeStructureApexStyle NewStyle)
{
	ApexStyle = NewStyle;
	RebuildApexDecoration();
}

void AQuadPanelConeStructure::RebuildApexDecoration()
{
	if (!ApexDecorationComponent || !ApexDecorationComponent->GetDynamicMesh())
	{
		return;
	}

	FDynamicMesh3 Mesh;
	Mesh.EnableVertexNormals(FVector3f::UnitZ());
	Mesh.EnableAttributes();
	Mesh.Attributes()->SetNumUVLayers(1);
	Mesh.Attributes()->EnableMaterialID();
	if (ApexStyle == EConeStructureApexStyle::CandySpire && GridHeightBlocks > 0)
	{
		const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
		const double BaseZ = BlockDimensions.Z * GridHeightBlocks +
							 BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
		const double Height = FMath::Max(CandySpireHeightBlocks * PitchZ, 1.0f);
		const double Radius = FMath::Max(CandySpireRadiusBlocks * CachedRadialBlockUnit, 1.0f);
		const int32 Segments = 48;
		TArray<FVector2D> Profile = {
			FVector2D(0.62, 0.00), FVector2D(1.12, 0.035),
			FVector2D(1.16, 0.095), FVector2D(0.68, 0.13),
			FVector2D(0.60, 0.19), FVector2D(0.88, 0.27),
			FVector2D(1.00, 0.38), FVector2D(0.83, 0.49),
			FVector2D(0.57, 0.56), FVector2D(0.68, 0.64),
			FVector2D(0.76, 0.72), FVector2D(0.55, 0.82),
			FVector2D(0.31, 0.90), FVector2D(0.18, 0.96)};
		if (bGenerateCandySpireCenterSpike)
		{
			const double SpikeHeight = FMath::Max(
				CandySpireCenterSpikeHeightBlocks * PitchZ,
				1.0f);
			const double NormalizedSpikeHeight = SpikeHeight / Height;
			// Retain a very narrow neck at the original tip, then taper it to the
			// centreline. Keeping this in the shared profile makes one continuous skin.
			Profile.Add(FVector2D(0.10, 1.0));
			Profile.Add(FVector2D(0.07, 1.0 + NormalizedSpikeHeight * 0.72));
			Profile.Add(FVector2D(0.0, 1.0 + NormalizedSpikeHeight));
		}
		else
		{
			Profile.Add(FVector2D(0.0, 1.0));
		}

		auto PositionAt = [&](int32 ProfileIndex, int32 SegmentIndex)
		{
			const double Angle = UE_TWO_PI * SegmentIndex / Segments;
			double RingRadius = Radius * Profile[ProfileIndex].X;
			if (ProfileIndex <= 2)
			{
				// A subtle twelve-lobed collar echoes the ridged candy-tower crown.
				RingRadius *= 1.0 + 0.055 * FMath::Max(FMath::Cos(12.0 * Angle), 0.0);
			}
			return FVector3d(
				FMath::Cos(Angle) * RingRadius,
				FMath::Sin(Angle) * RingRadius,
				BaseZ + Height * Profile[ProfileIndex].Y);
		};
		auto AppendOriented = [&](FVector3d A, FVector3d B, FVector3d C)
		{
			FVector3d Normal = FVector3d::CrossProduct(B - A, C - A).GetSafeNormal();
			const FVector3d Center = (A + B + C) / 3.0;
			if (FVector3d::DotProduct(Normal, FVector3d(Center.X, Center.Y, 0.0)) < 0.0)
			{
				Swap(B, C);
				Normal = -Normal;
			}
			const float UVScale = FMath::Max(UVWorldUnits, 1.0f);
			AppendMeshTriangle(
				Mesh, A, B, C, FVector3f(Normal), 0,
				FVector2f(A.X / UVScale, A.Z / UVScale),
				FVector2f(B.X / UVScale, B.Z / UVScale),
				FVector2f(C.X / UVScale, C.Z / UVScale));
		};
		for (int32 ProfileIndex = 0; ProfileIndex + 1 < Profile.Num(); ++ProfileIndex)
		{
			for (int32 SegmentIndex = 0; SegmentIndex < Segments; ++SegmentIndex)
			{
				const int32 NextSegment = (SegmentIndex + 1) % Segments;
				const FVector3d A = PositionAt(ProfileIndex, SegmentIndex);
				const FVector3d B = PositionAt(ProfileIndex, NextSegment);
				const FVector3d C = PositionAt(ProfileIndex + 1, NextSegment);
				const FVector3d D = PositionAt(ProfileIndex + 1, SegmentIndex);
				AppendOriented(A, B, C);
				AppendOriented(A, C, D);
			}
		}

		// Close the underside of the decoration. The revolved profile starts with a
		// non-zero radius, so generating only its side walls leaves the spire hollow
		// and visibly open when it is viewed from below.
		const float UVScale = FMath::Max(UVWorldUnits, 1.0f);
		const FVector3d BottomCenter(0.0, 0.0, BaseZ);
		const FVector3f BottomNormal(0.0f, 0.0f, -1.0f);
		for (int32 SegmentIndex = 0; SegmentIndex < Segments; ++SegmentIndex)
		{
			const int32 NextSegment = (SegmentIndex + 1) % Segments;
			const FVector3d Current = PositionAt(0, SegmentIndex);
			const FVector3d Next = PositionAt(0, NextSegment);
			AppendMeshTriangle(
				Mesh,
				BottomCenter,
				Next,
				Current,
				BottomNormal,
				0,
				FVector2f(0.0f, 0.0f),
				FVector2f(Next.X / UVScale, Next.Y / UVScale),
				FVector2f(Current.X / UVScale, Current.Y / UVScale));
		}
	}
	else if (ApexStyle == EConeStructureApexStyle::Star && GridHeightBlocks > 0)
	{
		const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
		const double BaseZ = BlockDimensions.Z * GridHeightBlocks +
							 BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
		const double StarHeight = FMath::Max(StarHeightBlocks * PitchZ, 1.0f);
		const double HalfThickness = FMath::Max(
			StarThicknessBlocks * CachedRadialBlockUnit * 0.5,
			0.5);
		constexpr int32 StarPointCount = 5;
		constexpr int32 OutlinePointCount = StarPointCount * 2;
		constexpr double InnerRadiusRatio = 0.45;
		// A downward-facing outer point is the star's sole contact with the cone.
		// cos(36 degrees) is the height of the highest outer points relative to
		// the radius, so this conversion preserves the requested total height.
		const double OuterRadius = StarHeight / (1.0 + FMath::Cos(UE_PI / 5.0));
		const double CenterZ = BaseZ + OuterRadius;
		const float UVScale = FMath::Max(UVWorldUnits, 1.0f);

		TArray<FVector3d, TInlineAllocator<OutlinePointCount>> Outline;
		Outline.Reserve(OutlinePointCount);
		for (int32 PointIndex = 0; PointIndex < OutlinePointCount; ++PointIndex)
		{
			const double Angle = -UE_HALF_PI + UE_PI * PointIndex / StarPointCount;
			const double PointRadius = (PointIndex % 2 == 0)
										   ? OuterRadius
										   : OuterRadius * InnerRadiusRatio;
			Outline.Emplace(
				FMath::Cos(Angle) * PointRadius,
				0.0,
				CenterZ + FMath::Sin(Angle) * PointRadius);
		}

		const FVector3d FrontCenter(0.0, -HalfThickness, CenterZ);
		const FVector3d BackCenter(0.0, HalfThickness, CenterZ);
		auto AppendStarTriangle = [&](const FVector3d &A, const FVector3d &B, const FVector3d &C)
		{
			const FVector3f Normal(FVector3d::CrossProduct(B - A, C - A).GetSafeNormal());
			AppendMeshTriangle(
				Mesh,
				A,
				B,
				C,
				Normal,
				0,
				FVector2f(A.X / UVScale, A.Z / UVScale),
				FVector2f(B.X / UVScale, B.Z / UVScale),
				FVector2f(C.X / UVScale, C.Z / UVScale));
		};
		for (int32 PointIndex = 0; PointIndex < OutlinePointCount; ++PointIndex)
		{
			const int32 NextPointIndex = (PointIndex + 1) % OutlinePointCount;
			AppendStarTriangle(FrontCenter, Outline[PointIndex], Outline[NextPointIndex]);
			AppendStarTriangle(BackCenter, Outline[NextPointIndex], Outline[PointIndex]);
		}
	}
	else if (ApexStyle == EConeStructureApexStyle::Moon && GridHeightBlocks > 0)
	{
		const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
		const double BaseZ = BlockDimensions.Z * GridHeightBlocks +
							 BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
		const double MoonHeight = FMath::Max(MoonHeightBlocks * PitchZ, 1.0f);
		const double HalfThickness = FMath::Max(
			MoonThicknessBlocks * CachedRadialBlockUnit * 0.5,
			0.5);
		constexpr int32 ArcSegments = 32;
		constexpr double CutoutCenterOffset = 0.38;
		constexpr double CutoutRadius = 0.82;
		constexpr double MoonTiltRadians = UE_PI / 4.0;
		const double IntersectionX =
			(1.0 - FMath::Square(CutoutRadius) + FMath::Square(CutoutCenterOffset)) /
			(2.0 * CutoutCenterOffset);
		const double OuterTopAngle = FMath::Acos(IntersectionX);
		const double InnerTopAngle = FMath::Acos(
			(IntersectionX - CutoutCenterOffset) / CutoutRadius);
		const double TiltCos = FMath::Cos(MoonTiltRadians);
		const double TiltSin = FMath::Sin(MoonTiltRadians);

		TArray<FVector2D, TInlineAllocator<ArcSegments + 1>> OuterArc;
		TArray<FVector2D, TInlineAllocator<ArcSegments + 1>> InnerArc;
		OuterArc.Reserve(ArcSegments + 1);
		InnerArc.Reserve(ArcSegments + 1);
		double MinimumZ = TNumericLimits<double>::Max();
		double ContactX = 0.0;
		auto AddRotatedMoonPoint = [&](TArray<FVector2D, TInlineAllocator<ArcSegments + 1>> &Arc, double X, double Z)
		{
			const FVector2D Rotated(
				X * TiltCos - Z * TiltSin,
				X * TiltSin + Z * TiltCos);
			Arc.Add(Rotated);
			if (Rotated.Y < MinimumZ)
			{
				MinimumZ = Rotated.Y;
				ContactX = Rotated.X;
			}
		};
		for (int32 PointIndex = 0; PointIndex <= ArcSegments; ++PointIndex)
		{
			const double Alpha = static_cast<double>(PointIndex) / ArcSegments;
			const double OuterAngle = FMath::Lerp(
				OuterTopAngle,
				UE_TWO_PI - OuterTopAngle,
				Alpha);
			const double InnerAngle = FMath::Lerp(
				InnerTopAngle,
				UE_TWO_PI - InnerTopAngle,
				Alpha);
			AddRotatedMoonPoint(
				OuterArc,
				FMath::Cos(OuterAngle),
				FMath::Sin(OuterAngle));
			AddRotatedMoonPoint(
				InnerArc,
				CutoutCenterOffset + CutoutRadius * FMath::Cos(InnerAngle),
				CutoutRadius * FMath::Sin(InnerAngle));
		}

		double MaximumZ = -TNumericLimits<double>::Max();
		for (const FVector2D &Point : OuterArc)
		{
			MaximumZ = FMath::Max(MaximumZ, Point.Y);
		}
		for (const FVector2D &Point : InnerArc)
		{
			MaximumZ = FMath::Max(MaximumZ, Point.Y);
		}
		const double ShapeHeight = FMath::Max(MaximumZ - MinimumZ, UE_DOUBLE_SMALL_NUMBER);
		const double ShapeScale = MoonHeight / ShapeHeight;
		auto ToMoonPosition = [&](const FVector2D &Point, double Y)
		{
			return FVector3d(
				(Point.X - ContactX) * ShapeScale,
				Y,
				BaseZ + (Point.Y - MinimumZ) * ShapeScale);
		};
		const float UVScale = FMath::Max(UVWorldUnits, 1.0f);
		auto AppendMoonTriangle = [&](const FVector3d &A, const FVector3d &B, const FVector3d &C)
		{
			const FVector3f Normal(FVector3d::CrossProduct(B - A, C - A).GetSafeNormal());
			AppendMeshTriangle(
				Mesh,
				A,
				B,
				C,
				Normal,
				0,
				FVector2f(A.X / UVScale, A.Z / UVScale),
				FVector2f(B.X / UVScale, B.Z / UVScale),
				FVector2f(C.X / UVScale, C.Z / UVScale));
		};
		for (int32 SegmentIndex = 0; SegmentIndex < ArcSegments; ++SegmentIndex)
		{
			const FVector3d OuterFrontA = ToMoonPosition(OuterArc[SegmentIndex], -HalfThickness);
			const FVector3d OuterFrontB = ToMoonPosition(OuterArc[SegmentIndex + 1], -HalfThickness);
			const FVector3d InnerFrontA = ToMoonPosition(InnerArc[SegmentIndex], -HalfThickness);
			const FVector3d InnerFrontB = ToMoonPosition(InnerArc[SegmentIndex + 1], -HalfThickness);
			const FVector3d OuterBackA = ToMoonPosition(OuterArc[SegmentIndex], HalfThickness);
			const FVector3d OuterBackB = ToMoonPosition(OuterArc[SegmentIndex + 1], HalfThickness);
			const FVector3d InnerBackA = ToMoonPosition(InnerArc[SegmentIndex], HalfThickness);
			const FVector3d InnerBackB = ToMoonPosition(InnerArc[SegmentIndex + 1], HalfThickness);

			if (SegmentIndex == 0)
			{
				AppendMoonTriangle(OuterFrontA, OuterFrontB, InnerFrontB);
				AppendMoonTriangle(OuterBackA, InnerBackB, OuterBackB);
			}
			else if (SegmentIndex == ArcSegments - 1)
			{
				AppendMoonTriangle(OuterFrontA, OuterFrontB, InnerFrontA);
				AppendMoonTriangle(OuterBackA, InnerBackA, OuterBackB);
			}
			else
			{
				AppendMoonTriangle(OuterFrontA, OuterFrontB, InnerFrontB);
				AppendMoonTriangle(OuterFrontA, InnerFrontB, InnerFrontA);
				AppendMoonTriangle(OuterBackA, InnerBackB, OuterBackB);
				AppendMoonTriangle(OuterBackA, InnerBackA, InnerBackB);
			}

			AppendMoonTriangle(OuterFrontA, OuterBackA, OuterBackB);
			AppendMoonTriangle(OuterFrontA, OuterBackB, OuterFrontB);
			AppendMoonTriangle(InnerFrontA, InnerFrontB, InnerBackB);
			AppendMoonTriangle(InnerFrontA, InnerBackB, InnerBackA);
		}
	}

	ApexDecorationComponent->GetDynamicMesh()->SetMesh(MoveTemp(Mesh));
	ApexDecorationComponent->SetNumMaterials(1);
	UMaterialInterface *DecorationMaterial = ResolveStructureMaterial(false);
	if (ApexStyle == EConeStructureApexStyle::CandySpire && CandySpireMaterial)
	{
		DecorationMaterial = CandySpireMaterial.Get();
	}
	else if (ApexStyle == EConeStructureApexStyle::Star && StarMaterial)
	{
		DecorationMaterial = StarMaterial.Get();
	}
	else if (ApexStyle == EConeStructureApexStyle::Moon && MoonMaterial)
	{
		DecorationMaterial = MoonMaterial.Get();
	}
	ApexDecorationComponent->SetMaterial(
		0,
		DecorationMaterial);
	ApexDecorationComponent->NotifyMeshUpdated();
	ApexDecorationComponent->SetVisibility(
		ApexStyle != EConeStructureApexStyle::CurrentApex &&
			ApexDecorationComponent->GetDynamicMesh()->GetTriangleCount() > 0,
		true);
	ApexDecorationComponent->MarkRenderStateDirty();
}

void AQuadPanelConeStructure::SetPlatformSpeedBoost(
	bool bEnabled,
	float NewSpeedMultiplier)
{
	bBoostCharacterWalkSpeed = bEnabled;
	CharacterWalkSpeedMultiplier = FMath::Clamp(NewSpeedMultiplier, 1.0f, 20.0f);
	if (!HasActorBegunPlay())
	{
		return;
	}
	if (bBoostCharacterWalkSpeed)
	{
		StartCharacterSpeedBoostMonitoring();
		RefreshCharacterSpeedBoosts();
	}
	else
	{
		StopCharacterSpeedBoostMonitoring();
	}
}

void AQuadPanelConeStructure::PlayConeAnimation()
{
	PlayFixedAnimation();
}

void AQuadPanelConeStructure::StopConeAnimation()
{
	StopFixedAnimation();
}

bool AQuadPanelConeStructure::CreateFixedAnimationResources()
{
	switch (ConeAnimationType)
	{
	case EConeStructureAnimationType::BottomToTopGlowWave:
	case EConeStructureAnimationType::RisingSpiralGlow:
		CreateConeAnimationComponent();
		break;
	case EConeStructureAnimationType::BottomToTopSineRows:
		CreateSineRowAnimationComponents();
		break;
	case EConeStructureAnimationType::None:
	default:
		break;
	}
	return HasFixedAnimationResources();
}

bool AQuadPanelConeStructure::HasFixedAnimationResources() const
{
	return ConeAnimationComponent || !ConeAnimationRowComponents.IsEmpty();
}

void AQuadPanelConeStructure::DestroyFixedAnimationResources()
{
	if (ConeAnimationComponent)
	{
		RemoveInstanceComponent(ConeAnimationComponent);
		ConeAnimationComponent->DestroyComponent();
		ConeAnimationComponent = nullptr;
	}
	ConeAnimationMaterialInstance = nullptr;
	for (UDynamicMeshComponent *RowComponent : ConeAnimationRowComponents)
	{
		if (RowComponent)
		{
			RemoveInstanceComponent(RowComponent);
			RowComponent->DestroyComponent();
		}
	}
	ConeAnimationRowComponents.Reset();
	ConeAnimationRowMaterialInstances.Reset();
	ConeAnimationRowAssignments.Reset();
	bConeAnimationGeometryDirty = false;
}

void AQuadPanelConeStructure::ConfigureArchedDoors(
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

void AQuadPanelConeStructure::SetStructureMaterials(
	UMaterialInterface *NewConeMaterial,
	UMaterialInterface *NewPlatformMaterial)
{
	ConeMaterial = NewConeMaterial;
	PlatformMaterial = NewPlatformMaterial;
	ApplyMaterialPattern();
}

bool AQuadPanelConeStructure::DestroyConeCellFromHit(const FHitResult &Hit)
{
	const bool bUseSpiralSegmentDestruction = bGeneratePlatforms &&
											  PlatformStyle == EConeStructurePlatformStyle::RisingSpiral &&
											  SpiralDestructionMode != EConeStructureSpiralDestructionMode::ExistingCellDestruction;
	if (bUseSpiralSegmentDestruction)
	{
		// Segment modes apply only when the impact is actually on the spiral volume. Cone-shell and
		// base hits must continue through the normal backing-cell path below.
		if (DestroySpiralSegmentFromHit(Hit))
		{
			return true;
		}
	}

	FIntPoint Cell;
	if (!ResolveExistingCellFromHit(Hit, Cell))
	{
		return false;
	}

	TSet<int32> CellIndicesToDestroy;
	const FVector WorldImpact = Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint;
	AddSpiralCrossSectionCells(Cell, WorldImpact, CellIndicesToDestroy);

	bool bQueuedAnyCell = false;
	for (const int32 CellIndex : CellIndicesToDestroy)
	{
		bQueuedAnyCell |= QueueCellDestruction(CellIndex, 0.0f, false);
	}
	if (bQueuedAnyCell)
	{
		ProcessQueuedDestructions(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	}
	return bQueuedAnyCell;
}

int32 AQuadPanelConeStructure::DestroyConeCellsFromHit(
	const FHitResult &Hit,
	float Radius)
{
	const float SafeRadius = FMath::Max(Radius, 1.0f);
	const bool bUseSpiralSegmentDestruction = bGeneratePlatforms &&
											  PlatformStyle == EConeStructurePlatformStyle::RisingSpiral &&
											  SpiralDestructionMode != EConeStructureSpiralDestructionMode::ExistingCellDestruction;
	if (bUseSpiralSegmentDestruction)
	{
		// Smooth spiral modes own their visible top, bottom and edge faces independently from the
		// backing grid. Use that same representation for a radial hit instead of cutting grid cells
		// out from underneath it, which would leave a paper-thin top surface.
		const int32 DestroyedSegmentPieces = DestroySpiralSegmentsInRadiusFromHit(Hit, SafeRadius);
		if (DestroyedSegmentPieces > 0)
		{
			return DestroyedSegmentPieces;
		}
	}

	TSet<int32> CellIndicesToDestroy;
	const FVector BlastCenter = GetInteriorImpactPoint(Hit);
	SelectCellsInRadius(
		BlastCenter,
		SafeRadius,
		EChunkedFloorBlastShape::Circle,
		[&CellIndicesToDestroy](int32 CellIndex, float Distance)
		{
			CellIndicesToDestroy.Add(CellIndex);
		});

	// Always include the directly struck piece even when its centre lies just outside the radius.
	FIntPoint HitCell = FIntPoint::NoneValue;
	if (ResolveExistingCellFromHit(Hit, HitCell))
	{
		const FVector WorldImpact = Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint;
		AddSpiralCrossSectionCells(HitCell, WorldImpact, CellIndicesToDestroy);
	}

	if (bGeneratePlatforms && PlatformStyle == EConeStructurePlatformStyle::RisingSpiral)
	{
		// The spiral may occupy several vertical grid layers. Expand every selected platform cell
		// through its contiguous helical cross-section, matching the thickness-aware left-click path.
		const TArray<int32> InitiallySelectedIndices = CellIndicesToDestroy.Array();
		for (const int32 CellIndex : InitiallySelectedIndices)
		{
			const FIntPoint Cell = FromCellIndex(CellIndex);
			int32 X;
			int32 Y;
			int32 Z;
			if (!DecodeCell(Cell, X, Y, Z) || !IsSpiralPlatformCell(X, Y, Z))
			{
				continue;
			}

			for (int32 LayerZ = Z - 1;
				 LayerZ >= 0 && IsSpiralPlatformCell(X, Y, LayerZ);
				 --LayerZ)
			{
				const FIntPoint LayerCell = EncodeCell(X, Y, LayerZ);
				if (LayerCell != FIntPoint::NoneValue &&
					Cells.IsValidIndex(ToCellIndex(LayerCell)) &&
					Cells[ToCellIndex(LayerCell)].bExists)
				{
					CellIndicesToDestroy.Add(ToCellIndex(LayerCell));
				}
			}
			for (int32 LayerZ = Z + 1;
				 LayerZ < GridHeightBlocks && IsSpiralPlatformCell(X, Y, LayerZ);
				 ++LayerZ)
			{
				const FIntPoint LayerCell = EncodeCell(X, Y, LayerZ);
				if (LayerCell != FIntPoint::NoneValue &&
					Cells.IsValidIndex(ToCellIndex(LayerCell)) &&
					Cells[ToCellIndex(LayerCell)].bExists)
				{
					CellIndicesToDestroy.Add(ToCellIndex(LayerCell));
				}
			}
		}
	}

	int32 QueuedCount = 0;
	for (const int32 CellIndex : CellIndicesToDestroy)
	{
		if (QueueCellDestruction(CellIndex, 0.0f, true))
		{
			++QueuedCount;
		}
	}
	if (QueuedCount > 0)
	{
		ProcessQueuedDestructions(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	}
	return QueuedCount;
}

void AQuadPanelConeStructure::AddSpiralCrossSectionCells(
	FIntPoint Cell,
	FVector WorldImpact,
	TSet<int32> &OutCellIndices) const
{
	if (Cells.IsValidIndex(ToCellIndex(Cell)) && Cells[ToCellIndex(Cell)].bExists)
	{
		OutCellIndices.Add(ToCellIndex(Cell));
	}
	if (!bGeneratePlatforms || PlatformStyle != EConeStructurePlatformStyle::RisingSpiral)
	{
		return;
	}

	int32 HitX;
	int32 HitY;
	int32 HitZ;
	if (!DecodeCell(Cell, HitX, HitY, HitZ))
	{
		return;
	}

	// A rising spiral can be several backing cells thick. Resolve the helical cross-section at
	// the actual impact and include every occupied vertical layer in the same destruction batch.
	const FVector LocalImpact = GetActorTransform().InverseTransformPosition(WorldImpact);
	const float ImpactRadius = FVector2D(LocalImpact.X, LocalImpact.Y).Size();
	const float ImpactAngle = FMath::Atan2(LocalImpact.Y, LocalImpact.X);
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	const float TotalSpiralAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const float Thickness = GetSpiralThickness();
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
		const float WidthScale = GetPlatformWidthScaleAtHeight(SpiralTop);
		const float InnerRadius = FMath::Max(
			ConeRadius - SpiralWallOverlapBlocks * CachedRadialBlockUnit * WidthScale,
			0.0f);
		const float OuterRadius = GetPlatformOuterRadiusAtHeight(SpiralTop);
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

	const float SurfaceTolerance = FMath::Max(QuadConeSpacingTolerance, 0.1f);
	if (ClosestDistanceSquared > FMath::Square(SurfaceTolerance))
	{
		return;
	}

	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const int32 MinimumZ = FMath::Clamp(
		FMath::FloorToInt((ClosestSpiralBottom + SurfaceTolerance) / PitchZ),
		0,
		GridHeightBlocks - 1);
	const int32 MaximumZ = FMath::Clamp(
		FMath::FloorToInt((ClosestSpiralBottom + Thickness - SurfaceTolerance) / PitchZ),
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
			OutCellIndices.Add(ToCellIndex(LayerCell));
		}
	}
}

void AQuadPanelConeStructure::ConfigureConeStructure(
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
		FMath::Max(FMath::Abs(NewBlockDimensions.X), QuadMinimumConeDimension),
		FMath::Max(FMath::Abs(NewBlockDimensions.Y), QuadMinimumConeDimension),
		FMath::Max(FMath::Abs(NewBlockDimensions.Z), QuadMinimumConeDimension));
	BlockSpacing = FVector(
		FMath::Max(NewBlockSpacing.X, 0.0),
		FMath::Max(NewBlockSpacing.Y, 0.0),
		FMath::Max(NewBlockSpacing.Z, 0.0));
	GenerateFloor(bPreserveCellState);
}

bool AQuadPanelConeStructure::CalculateLayout()
{
	WallPanelSegments = FMath::Clamp(WallPanelSegments, 8, 128);
	PanelJointWidth = FMath::Clamp(PanelJointWidth, 0.f, 5.f);

	ConeHeightBlocks = FMath::Clamp(ConeHeightBlocks, 4, 256);
	BottomRadiusBlocks = FMath::Clamp(FMath::Abs(BottomRadiusBlocks), 2.0f, 128.0f);
	TopRadiusBlocks = FMath::Clamp(FMath::Abs(TopRadiusBlocks), 0.0f, BottomRadiusBlocks);
	ShellThicknessBlocks = FMath::Clamp(FMath::Abs(ShellThicknessBlocks), 0.5f, BottomRadiusBlocks);
	BlockDimensions = FVector(
		FMath::Max(FMath::Abs(BlockDimensions.X), QuadMinimumConeDimension),
		FMath::Max(FMath::Abs(BlockDimensions.Y), QuadMinimumConeDimension),
		FMath::Max(FMath::Abs(BlockDimensions.Z), QuadMinimumConeDimension));
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
	const float NewRadialBlockUnit = FMath::Max(FMath::Min(PitchX, PitchY), QuadMinimumConeDimension);
    SpiralRimHeight=FMath::IsFinite(SpiralRimHeight)?FMath::Clamp(SpiralRimHeight,0.f,200.f):30.f;
    SpiralRimWidth=FMath::IsFinite(SpiralRimWidth)?FMath::Clamp(SpiralRimWidth,1.f,200.f):18.f;
    const double FittedShaft=ZigguratTraversal::FitOpening(Traversal,2*(BottomRadiusBlocks-ShellThicknessBlocks)*NewRadialBlockUnit);
    EffectiveOpeningWidth=Traversal.bFloorOpenings?FittedShaft:0;
    // Account for the polygon chord, so the full requested capsule clearance survives faceting.
    const double TipSegments=FMath::Min(GetEffectiveCircularSegments(), WallPanelSegments);
    EffectiveTopRadiusBlocks=Traversal.bFloorOpenings?FMath::Min(double(BottomRadiusBlocks),
        FMath::Max(double(TopRadiusBlocks),FittedShaft*.5/FMath::Cos(UE_PI/TipSegments)/NewRadialBlockUnit+ShellThicknessBlocks)):TopRadiusBlocks;
	const bool bHasSecondaryStructure = bGeneratePlatforms &&
										(PlatformStyle == EConeStructurePlatformStyle::RisingSpiral || PlatformCount > 0);
	const float MaximumRadius = (BottomRadiusBlocks +
								 (bHasSecondaryStructure ? PlatformOverhangBlocks : 0.0f)) *
								NewRadialBlockUnit;
	const int32 RadiusInGridCells = FMath::CeilToInt(
										MaximumRadius / FMath::Max(FMath::Min(PitchX, PitchY), QuadMinimumConeDimension)) +
									1;
	const int32 NewGridDiameterBlocks = RadiusInGridCells * 2 + 1;
	const int32 NewGridHeightBlocks = ConeHeightBlocks;
	const float GridWidth = BlockDimensions.X * NewGridDiameterBlocks +
							BlockSpacing.X * FMath::Max(NewGridDiameterBlocks - 1, 0);
	const float GridDepth = BlockDimensions.Y * NewGridDiameterBlocks +
							BlockSpacing.Y * FMath::Max(NewGridDiameterBlocks - 1, 0);
	const float GridHeight = BlockDimensions.Z * NewGridHeightBlocks +
							 BlockSpacing.Z * FMath::Max(NewGridHeightBlocks - 1, 0);
	const FVector NewGridLocalMin(-GridWidth * 0.5f, -GridDepth * 0.5f, 0.0f);
	const float TotalConeHeight = FMath::Max(GridHeight, QuadMinimumConeDimension);

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
		return FMath::Lerp(BottomRadiusBlocks, EffectiveTopRadiusBlocks, Alpha) * NewRadialBlockUnit;
	};
	auto GetInnerRadius = [&](float LocalZ)
	{
		return FMath::Max(
			GetOuterRadius(LocalZ) - ShellThicknessBlocks * NewRadialBlockUnit,
			0.0f);
	};
	auto GetPlatformWidthScale = [&](float LocalZ)
	{
		if (!bTaperPlatformsUpward)
		{
			return 1.0f;
		}
		if (PlatformStyle == EConeStructurePlatformStyle::RisingSpiral)
		{
			const float SpiralThickness = BlockDimensions.Z * PlatformThicknessBlocks +
										  BlockSpacing.Z * FMath::Max(PlatformThicknessBlocks - 1, 0);
			const float MaximumSpiralBottom = FMath::Max(
				TotalConeHeight - SpiralThickness, 0.0f);
			const float StartTop = FMath::Clamp(
									   LowestPlatformHeightBlocks * PitchZ, 0.0f, MaximumSpiralBottom) +
								   SpiralThickness;
			const float EndTop = FMath::Clamp(
									 SpiralEndHeightBlocks * PitchZ, 0.0f, MaximumSpiralBottom) +
								 SpiralThickness;
			if (!FMath::IsNearlyEqual(StartTop, EndTop))
			{
				const float SpiralAlpha = FMath::Clamp(
					(LocalZ - StartTop) / (EndTop - StartTop), 0.0f, 1.0f);
				return 1.0f - SpiralAlpha;
			}
		}
		return GetOuterRadius(LocalZ) /
			   FMath::Max(GetOuterRadius(0.0f), QuadMinimumConeDimension);
	};
	auto GetPlatformOuterRadius = [&](float LocalZ)
	{
		return GetOuterRadius(LocalZ) +
			   PlatformOverhangBlocks * NewRadialBlockUnit * GetPlatformWidthScale(LocalZ);
	};
	auto GetRadialRange = [&](int32 X, int32 Y, float &OutMinimumRadius, float &OutMaximumRadius)
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
		// The rendered inner wall follows polygon chords inside the analytic circle.
		// Keep cells touching those chords too, including tiny corners at layer edges.
		return MinimumRadius <= MaximumOuterRadius + QuadConeSpacingTolerance &&
			   MaximumCellRadius >= MinimumInnerRadius * FMath::Cos(UE_PI / GetEffectiveCircularSegments()) - QuadConeSpacingTolerance;
	};
	auto IsConeBaseBackingCell = [&](int32 X, int32 Y, int32 Z)
	{
		if (!bGenerateConeBase || Z != 0)
		{
			return false;
		}
		float MinimumRadius;
		float MaximumCellRadius;
		GetRadialRange(X, Y, MinimumRadius, MaximumCellRadius);
		return MinimumRadius <= GetOuterRadius(0.0f) + QuadConeSpacingTolerance;
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
				const float OuterRadius = GetPlatformOuterRadius(PlatformLocalZ);
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
		const FVector Center = GetPhysicalCenter(X, Y, Z);
		const FVector3d CellMinimum = FVector3d(Center - BlockDimensions * 0.5f);
		const FVector3d CellMaximum = FVector3d(Center + BlockDimensions * 0.5f);
		const float TotalSpiralAngle = UE_TWO_PI * PlatformSpiralTurns;
		const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
		const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
		auto EndCapIntersectsCell = [&](bool bEnd)
		{
			const float Alpha = bEnd ? 1.0f : 0.0f;
			const float Bottom = FMath::Lerp(StartBottom, EndBottom, Alpha);
			const float Top = Bottom + SpiralThickness;
			const double CapAngle = StartAngle + Direction * TotalSpiralAngle * Alpha;
			const double CosAngle = FMath::Cos(CapAngle);
			const double SinAngle = FMath::Sin(CapAngle);
			const float ConeRadius = GetOuterRadius(Top);
			const float InnerRadius = FMath::Max(
				ConeRadius - SpiralWallOverlapBlocks * NewRadialBlockUnit *
								 GetPlatformWidthScale(Top),
				0.0f);
			const float OuterRadius = GetPlatformOuterRadius(Top);
			const FVector3d InnerBottom(CosAngle * InnerRadius, SinAngle * InnerRadius, Bottom);
			const FVector3d OuterBottom(CosAngle * OuterRadius, SinAngle * OuterRadius, Bottom);
			const FVector3d OuterTop(CosAngle * OuterRadius, SinAngle * OuterRadius, Top);
			const FVector3d InnerTop(CosAngle * InnerRadius, SinAngle * InnerRadius, Top);
			return QuadDoesSurfaceQuadIntersectBox(
				InnerBottom, OuterBottom, OuterTop, InnerTop, CellMinimum, CellMaximum);
		};
		if (EndCapIntersectsCell(false) || EndCapIntersectsCell(true))
		{
			return true;
		}

		float MinimumRadius, MaximumCellRadius;
		GetRadialRange(X, Y, MinimumRadius, MaximumCellRadius);
		auto InnerAtTop = [&](float Top)
		{
			return GetOuterRadius(Top) - SpiralWallOverlapBlocks * NewRadialBlockUnit * GetPlatformWidthScale(Top);
		};
		if (bSpiralOuterRim && SpiralRimHeight>0 && SpiralRimWidth>0)
        {
            const float Outer0=GetPlatformOuterRadius(StartBottom+SpiralThickness),Outer1=GetPlatformOuterRadius(EndBottom+SpiralThickness);
            const float Inner0=FMath::Max(Outer0-SpiralRimWidth,InnerAtTop(StartBottom+SpiralThickness));
            const float Inner1=FMath::Max(Outer1-SpiralRimWidth,InnerAtTop(EndBottom+SpiralThickness));
            if (QuadSpiralIntersectsCellBounds(Center,BlockDimensions,MinimumRadius,MaximumCellRadius,
                StartBottom+SpiralThickness,EndBottom+SpiralThickness,SpiralRimHeight,
                TotalSpiralAngle,StartAngle,Direction,GetSpiralSegmentCount(),Inner0,Inner1,Outer0,Outer1)) return true;
        }
        return QuadSpiralIntersectsCellBounds(Center, BlockDimensions, MinimumRadius, MaximumCellRadius,
			StartBottom, EndBottom, SpiralThickness, TotalSpiralAngle, StartAngle, Direction,
			GetSpiralSegmentCount(), InnerAtTop(StartBottom + SpiralThickness), InnerAtTop(EndBottom + SpiralThickness),
			GetPlatformOuterRadius(StartBottom + SpiralThickness), GetPlatformOuterRadius(EndBottom + SpiralThickness));
	};

	// Store only cells that can own cone/platform geometry. The previous diameter^2*height
	// backing volume exceeded the generation limit for large but otherwise valid hollow cones.
	TArray<TArray<FIntPoint>> NewPhysicalCellsByLayer;
	NewPhysicalCellsByLayer.SetNum(NewGridHeightBlocks);
	int32 MaximumLayerCellCount = 0;
	for (int32 Z = 0; Z < NewGridHeightBlocks; ++Z)
	{
		TArray<FIntPoint> &LayerCells = NewPhysicalCellsByLayer[Z];
		for (int32 Y = 0; Y < NewGridDiameterBlocks; ++Y)
		{
			for (int32 X = 0; X < NewGridDiameterBlocks; ++X)
			{
				if (IsConeCell(X, Y, Z) || IsConeBaseBackingCell(X, Y, Z) ||
					IsPlatformBackingCell(X, Y, Z))
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
		TMap<int32, int32> &LayerSlots = PhysicalCellSlotsByLayer[Z];
		const TArray<FIntPoint> &LayerCells = PhysicalCellsByLayer[Z];
		LayerSlots.Reserve(LayerCells.Num());
		for (int32 Slot = 0; Slot < LayerCells.Num(); ++Slot)
		{
			LayerSlots.Add(LayerCells[Slot].X + LayerCells[Slot].Y * GridDiameterBlocks, Slot);
		}
	}

	PanelCellOffset = MaximumLayerCellCount;
	CellCount = FIntPoint(PanelCellOffset + WallPanelSegments, GridHeightBlocks);
	const int32 SafeChunkSide = GetEffectiveConeChunkSideLength();
	SpatialChunkCount = FIntVector(
		FMath::DivideAndRoundUp(GridDiameterBlocks, SafeChunkSide),
		FMath::DivideAndRoundUp(GridDiameterBlocks, SafeChunkSide),
		FMath::DivideAndRoundUp(GridHeightBlocks, SafeChunkSide));
	ChunkCount = FIntPoint(
		SpatialChunkCount.X * SpatialChunkCount.Y,
		SpatialChunkCount.Z);

	PanelChunkStart = ChunkCount.X * ChunkCount.Y;
	PanelChunkColumns = FMath::DivideAndRoundUp(WallPanelSegments, 8);
	const int32 PanelChunks = PanelChunkColumns * FMath::DivideAndRoundUp(GridHeightBlocks, 4);
	ChunkCount.Y += FMath::DivideAndRoundUp(PanelChunks, ChunkCount.X);

	EffectiveCellDimensions = BlockDimensions;
	CellDimensions = BlockDimensions;
	CellSpacing = FVector2D(BlockSpacing.X, BlockSpacing.Y);
	GridLocalMin = FVector2D(ConeGridLocalMin.X, 0.0);
	EffectiveGridSize = FVector2D(FMath::Max(GridWidth, GridDepth), GridHeight);
	AreaSize = EffectiveGridSize;
	bConeAnimationGeometryDirty = true;
    ZigguratTraversal::UpdatePad(GroundLaunchPad,Traversal,FittedShaft,
        bGenerateConeBase?BlockDimensions.Z+BlockSpacing.Z*.5*SurfaceGapSealFraction:0,
        BlockDimensions.Z*GridHeightBlocks+BlockSpacing.Z*FMath::Max(GridHeightBlocks-1,0));
	return true;
}

FChunkedDestructibleConeStructureMetrics AQuadPanelConeStructure::GetConeStructureMetrics() const
{
	FChunkedDestructibleConeStructureMetrics Metrics;
	Metrics.BackingGridBlockCount = FIntVector(
		GridDiameterBlocks, GridDiameterBlocks, GridHeightBlocks);
	Metrics.WorldDimensions = FVector(
		BlockDimensions.X * GridDiameterBlocks + BlockSpacing.X * FMath::Max(GridDiameterBlocks - 1, 0),
		BlockDimensions.Y * GridDiameterBlocks + BlockSpacing.Y * FMath::Max(GridDiameterBlocks - 1, 0),
		BlockDimensions.Z * GridHeightBlocks + BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0));
	for (const FFloorCellData &Cell : Cells)
	{
		Metrics.GeneratedBlockCount += Cell.bBelongsToGeneratedShape ? 1 : 0;
	}
	const float BottomOuterRadius = BottomRadiusBlocks * CachedRadialBlockUnit;
	const float BottomInnerRadius = FMath::Max(
		BottomOuterRadius - ShellThicknessBlocks * CachedRadialBlockUnit, 0.0f);
	Metrics.BottomInteriorDiameter = BottomInnerRadius * 2.0f;
	return Metrics;
}

int32 AQuadPanelConeStructure::GetSpatialChunkIndex(
	int32 X,
	int32 Y,
	int32 Z) const
{
	if (X < 0 || Y < 0 || Z < 0 ||
		X >= GridDiameterBlocks || Y >= GridDiameterBlocks || Z >= GridHeightBlocks ||
		SpatialChunkCount.X <= 0 || SpatialChunkCount.Y <= 0 || ChunkCount.X <= 0)
	{
		return INDEX_NONE;
	}
	const int32 Side = GetEffectiveConeChunkSideLength();
	const int32 ChunkX = X / Side;
	const int32 ChunkY = Y / Side;
	const int32 ChunkZ = Z / Side;
	return ChunkX + ChunkY * SpatialChunkCount.X + ChunkZ * ChunkCount.X;
}

bool AQuadPanelConeStructure::GetSpatialChunkBounds(
	int32 ChunkIndex,
	FIntVector &OutMinimum,
	FIntVector &OutMaximum) const
{
	if (ChunkIndex >= PanelChunkStart) { return false; }

	OutMinimum = FIntVector::ZeroValue;
	OutMaximum = FIntVector::ZeroValue;
	if (ChunkIndex < 0 || ChunkIndex >= ChunkCount.X * ChunkCount.Y ||
		ChunkCount.X <= 0 || SpatialChunkCount.X <= 0 || SpatialChunkCount.Y <= 0)
	{
		return false;
	}
	const int32 Side = GetEffectiveConeChunkSideLength();
	const int32 ChunkZ = ChunkIndex / ChunkCount.X;
	const int32 PlanarChunk = ChunkIndex % ChunkCount.X;
	const int32 ChunkX = PlanarChunk % SpatialChunkCount.X;
	const int32 ChunkY = PlanarChunk / SpatialChunkCount.X;
	OutMinimum = FIntVector(ChunkX * Side, ChunkY * Side, ChunkZ * Side);
	OutMaximum = FIntVector(
		FMath::Min(OutMinimum.X + Side, GridDiameterBlocks),
		FMath::Min(OutMinimum.Y + Side, GridDiameterBlocks),
		FMath::Min(OutMinimum.Z + Side, GridHeightBlocks));
	return true;
}

bool AQuadPanelConeStructure::DecodeCell(
	FIntPoint Cell,
	int32 &OutX,
	int32 &OutY,
	int32 &OutZ) const
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

FIntPoint AQuadPanelConeStructure::EncodeCell(int32 X, int32 Y, int32 Z) const
{
	if (X < 0 || Y < 0 || Z < 0 || X >= GridDiameterBlocks ||
		Y >= GridDiameterBlocks || Z >= GridHeightBlocks ||
		!PhysicalCellSlotsByLayer.IsValidIndex(Z))
	{
		return FIntPoint::NoneValue;
	}
	const int32 *Slot = PhysicalCellSlotsByLayer[Z].Find(X + Y * GridDiameterBlocks);
	return Slot ? FIntPoint(*Slot, Z) : FIntPoint::NoneValue;
}

FVector AQuadPanelConeStructure::GetPhysicalCellLocalCenter(
	int32 X,
	int32 Y,
	int32 Z) const
{
	return FVector(
		ConeGridLocalMin.X + BlockDimensions.X * 0.5 + X * (BlockDimensions.X + BlockSpacing.X),
		ConeGridLocalMin.Y + BlockDimensions.Y * 0.5 + Y * (BlockDimensions.Y + BlockSpacing.Y),
		BlockDimensions.Z * 0.5 + Z * (BlockDimensions.Z + BlockSpacing.Z));
}

FVector AQuadPanelConeStructure::GetCellLocalCenter(FIntPoint Cell) const
{
	if (IsPanelCell(Cell))
	{
		FVector3d Corners[8]; GetPanelCorners(Cell, Corners);
		FVector Center = FVector::ZeroVector;
		for (const FVector3d& Corner : Corners) { Center += FVector(Corner) / 8.; }
		return Center;
	}

	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return FVector::ZeroVector;
	}
	return GetPhysicalCellLocalCenter(X, Y, Z);
}

bool AQuadPanelConeStructure::WorldLocationToCell(
	FVector WorldLocation,
	FIntPoint &OutCell) const
{
	if (FindPanelAtLocalPoint(GetActorTransform().InverseTransformPosition(WorldLocation), OutCell)) { return true; }

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

float AQuadPanelConeStructure::GetConeOuterRadiusAtHeight(float LocalZ) const
{
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
							  BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const float Alpha = FMath::Clamp(LocalZ / FMath::Max(TotalHeight, QuadMinimumConeDimension), 0.0f, 1.0f);
	return FMath::Lerp(BottomRadiusBlocks, EffectiveTopRadiusBlocks, Alpha) * CachedRadialBlockUnit;
}

float AQuadPanelConeStructure::GetConeInnerRadiusAtHeight(float LocalZ) const
{
	return FMath::Max(
		GetConeOuterRadiusAtHeight(LocalZ) - ShellThicknessBlocks * CachedRadialBlockUnit,
		0.0f);
}

float AQuadPanelConeStructure::GetPlatformWidthScaleAtHeight(float LocalZ) const
{
	if (!bTaperPlatformsUpward)
	{
		return 1.0f;
	}
	if (PlatformStyle == EConeStructurePlatformStyle::RisingSpiral)
	{
		const float StartTop = GetSpiralBottomHeight(false) + GetSpiralThickness();
		const float EndTop = GetSpiralBottomHeight(true) + GetSpiralThickness();
		if (!FMath::IsNearlyEqual(StartTop, EndTop))
		{
			const float SpiralAlpha = FMath::Clamp(
				(LocalZ - StartTop) / (EndTop - StartTop), 0.0f, 1.0f);
			return 1.0f - SpiralAlpha;
		}
	}
	return GetConeOuterRadiusAtHeight(LocalZ) /
		   FMath::Max(GetConeOuterRadiusAtHeight(0.0f), QuadMinimumConeDimension);
}

float AQuadPanelConeStructure::GetPlatformOuterRadiusAtHeight(float LocalZ) const
{
	return GetConeOuterRadiusAtHeight(LocalZ) +
		   PlatformOverhangBlocks * CachedRadialBlockUnit *
			   GetPlatformWidthScaleAtHeight(LocalZ);
}

void AQuadPanelConeStructure::GetCellRadialRange(
	int32 X,
	int32 Y,
	float &OutMinimumRadius,
	float &OutMaximumRadius) const
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

bool AQuadPanelConeStructure::IsConeShellCell(int32 X, int32 Y, int32 Z) const
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
	return MinimumRadius <= OuterRadius + QuadConeSpacingTolerance &&
		   MaximumRadius >= InnerRadius * FMath::Cos(UE_PI / GetEffectiveCircularSegments()) - QuadConeSpacingTolerance;
}

bool AQuadPanelConeStructure::IsConeBaseCell(int32 X, int32 Y, int32 Z) const
{
	if (!bGenerateConeBase || Z != 0)
	{
		return false;
	}
	float MinimumRadius;
	float MaximumRadius;
	GetCellRadialRange(X, Y, MinimumRadius, MaximumRadius);
	return MinimumRadius <= GetConeOuterRadiusAtHeight(0.0f) + QuadConeSpacingTolerance;
}

bool AQuadPanelConeStructure::IsPlatformCell(int32 X, int32 Y, int32 Z) const
{
	if (!bGeneratePlatforms)
	{
		return false;
	}
	return PlatformStyle == EConeStructurePlatformStyle::RisingSpiral
			   ? IsSpiralPlatformCell(X, Y, Z)
			   : IsCircularPlatformCell(X, Y, Z);
}

bool AQuadPanelConeStructure::IsCircularPlatformCell(int32 X, int32 Y, int32 Z) const
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
		const float InnerRadius = GetConeInnerRadiusAtHeight(PlatformLocalZ);
		const float OuterRadius = GetPlatformOuterRadiusAtHeight(PlatformLocalZ);
		float MinimumRadius;
		float MaximumRadius;
		GetCellRadialRange(X, Y, MinimumRadius, MaximumRadius);
		return MinimumRadius <= OuterRadius && MaximumRadius >= InnerRadius;
	}
	return false;
}

float AQuadPanelConeStructure::GetSpiralThickness() const
{
	return BlockDimensions.Z * PlatformThicknessBlocks +
		   BlockSpacing.Z * FMath::Max(PlatformThicknessBlocks - 1, 0);
}

float AQuadPanelConeStructure::GetSpiralBottomHeight(bool bEnd) const
{
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
							  BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const float MaximumBottom = FMath::Max(TotalHeight - GetSpiralThickness(), 0.0f);
	const int32 HeightBlocks = bEnd ? SpiralEndHeightBlocks : LowestPlatformHeightBlocks;
	return FMath::Clamp(HeightBlocks * PitchZ, 0.0f, MaximumBottom);
}

int32 AQuadPanelConeStructure::GetSpiralSegmentCount() const
{
	return FMath::Clamp(
		FMath::CeilToInt(PlatformSpiralTurns * SpiralSegmentsPerTurn), 1, 8192);
}

bool AQuadPanelConeStructure::IsSpiralSegmentDestroyed(int32 SegmentIndex) const
{
	return DestroyedSpiralSegmentIndices.Contains(SegmentIndex);
}

bool AQuadPanelConeStructure::IsSpiralTriangleSliceDestroyed(
	int32 SegmentIndex,
	int32 TriangleSliceIndex) const
{
	return IsSpiralSegmentDestroyed(SegmentIndex) ||
		   DestroyedSpiralTriangleSliceIndices.Contains(SegmentIndex * 2 + TriangleSliceIndex);
}

bool AQuadPanelConeStructure::ResolveSpiralSegmentFromHit(
	const FHitResult &Hit,
	int32 &OutSegmentIndex,
	int32 *OutTriangleSliceIndex) const
{
	OutSegmentIndex = INDEX_NONE;
	if (OutTriangleSliceIndex)
	{
		*OutTriangleSliceIndex = INDEX_NONE;
	}
	if (!bGeneratePlatforms || PlatformStyle != EConeStructurePlatformStyle::RisingSpiral ||
		(PlatformOverhangBlocks <= KINDA_SMALL_NUMBER &&
		 SpiralWallOverlapBlocks <= KINDA_SMALL_NUMBER))
	{
		return false;
	}

	const FVector WorldImpact = Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint;
	const FVector LocalImpact = GetActorTransform().InverseTransformPosition(WorldImpact);
	const float ImpactRadius = FVector2D(LocalImpact.X, LocalImpact.Y).Size();
	const float ImpactAngle = FMath::Atan2(LocalImpact.Y, LocalImpact.X);
	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const float Thickness = GetSpiralThickness();
	const float TotalAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	const int32 SegmentCount = GetSpiralSegmentCount();
	const float SegmentAngle = TotalAngle / SegmentCount;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	float BaseProgressAngle = FMath::Fmod(
		Direction * (ImpactAngle - StartAngle), static_cast<float>(UE_TWO_PI));
	if (BaseProgressAngle < 0.0f)
	{
		BaseProgressAngle += UE_TWO_PI;
	}

	auto IntersectPolarEdgeRadius = [](float RadiusA, float AngleA, float RadiusB,
									   float AngleB, float RayAngle) -> float
	{
		const FVector2D PointA(FMath::Cos(AngleA) * RadiusA, FMath::Sin(AngleA) * RadiusA);
		const FVector2D PointB(FMath::Cos(AngleB) * RadiusB, FMath::Sin(AngleB) * RadiusB);
		const FVector2D Edge = PointB - PointA;
		const FVector2D Ray(FMath::Cos(RayAngle), FMath::Sin(RayAngle));
		const float Denominator = Edge.X * Ray.Y - Edge.Y * Ray.X;
		if (FMath::Abs(Denominator) <= UE_SMALL_NUMBER)
		{
			return (RadiusA + RadiusB) * 0.5f;
		}
		const float Numerator = PointA.X * Ray.Y - PointA.Y * Ray.X;
		const float EdgeAlpha = FMath::Clamp(-Numerator / Denominator, 0.0f, 1.0f);
		return static_cast<float>(FVector2D::DotProduct(PointA + Edge * EdgeAlpha, Ray));
	};

	const int32 RevolutionCount = FMath::CeilToInt(PlatformSpiralTurns);
	for (int32 Revolution = 0; Revolution <= RevolutionCount; ++Revolution)
	{
		const float ProgressAngle = BaseProgressAngle + UE_TWO_PI * Revolution;
		if (ProgressAngle > TotalAngle + KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const int32 PrimarySegmentIndex = FMath::Clamp(
			FMath::FloorToInt(ProgressAngle / FMath::Max(SegmentAngle, KINDA_SMALL_NUMBER)),
			0,
			SegmentCount - 1);
		for (int32 SegmentOffset = -1; SegmentOffset <= 1; ++SegmentOffset)
		{
			const int32 SegmentIndex = PrimarySegmentIndex + SegmentOffset;
			if (SegmentIndex < 0 || SegmentIndex >= SegmentCount ||
				IsSpiralSegmentDestroyed(SegmentIndex))
			{
				continue;
			}

			const float ProgressA = SegmentIndex * SegmentAngle;
			const float ProgressB = (SegmentIndex + 1) * SegmentAngle;
			const float SegmentAlpha = FMath::Clamp(
				(ProgressAngle - ProgressA) / FMath::Max(SegmentAngle, KINDA_SMALL_NUMBER),
				0.0f,
				1.0f);
			const float AlphaA = ProgressA / TotalAngle;
			const float AlphaB = ProgressB / TotalAngle;
			const float BottomA = FMath::Lerp(StartBottom, EndBottom, AlphaA);
			const float BottomB = FMath::Lerp(StartBottom, EndBottom, AlphaB);
			const float BottomZ = FMath::Lerp(BottomA, BottomB, SegmentAlpha);
			const float TopZ = BottomZ + Thickness;
			const float AngleA = StartAngle + Direction * ProgressA;
			const float AngleB = StartAngle + Direction * ProgressB;
			const float InnerRadiusA = FMath::Max(
				GetConeOuterRadiusAtHeight(BottomA + Thickness) -
					SpiralWallOverlapBlocks * CachedRadialBlockUnit *
						GetPlatformWidthScaleAtHeight(BottomA + Thickness),
				0.0f);
			const float InnerRadiusB = FMath::Max(
				GetConeOuterRadiusAtHeight(BottomB + Thickness) -
					SpiralWallOverlapBlocks * CachedRadialBlockUnit *
						GetPlatformWidthScaleAtHeight(BottomB + Thickness),
				0.0f);
			const float InnerRadius = IntersectPolarEdgeRadius(
				InnerRadiusA, AngleA, InnerRadiusB, AngleB, ImpactAngle);
			const float OuterRadius = IntersectPolarEdgeRadius(
				GetPlatformOuterRadiusAtHeight(BottomA + Thickness),
				AngleA,
				GetPlatformOuterRadiusAtHeight(BottomB + Thickness),
				AngleB,
				ImpactAngle);
			const float MinimumRadius = FMath::Min(InnerRadius, OuterRadius);
			const float MaximumRadius = FMath::Max(InnerRadius, OuterRadius);
			const float RadialDistance = ImpactRadius < MinimumRadius
											 ? MinimumRadius - ImpactRadius
											 : (ImpactRadius > MaximumRadius ? ImpactRadius - MaximumRadius : 0.0f);
			const float HitTop=TopZ+(bSpiralOuterRim && ImpactRadius>=MaximumRadius-SpiralRimWidth-1?GetSpiralRimHeight(TopZ):0);
            const float VerticalDistance = LocalImpact.Z < BottomZ
											   ? BottomZ - LocalImpact.Z
											   : (LocalImpact.Z > HitTop ? LocalImpact.Z - HitTop : 0.0f);
			const float AngularProgressDistance = ProgressAngle < ProgressA
													  ? ProgressA - ProgressAngle
													  : (ProgressAngle > ProgressB ? ProgressAngle - ProgressB : 0.0f);
			const float AngularDistance = AngularProgressDistance *
										  FMath::Max(ImpactRadius, MinimumRadius);
			const float DistanceSquared = FMath::Square(RadialDistance) +
										  FMath::Square(VerticalDistance) + FMath::Square(AngularDistance);
			const float RadialAlpha = FMath::Clamp(
				(ImpactRadius - MinimumRadius) /
					FMath::Max(MaximumRadius - MinimumRadius, KINDA_SMALL_NUMBER),
				0.0f,
				1.0f);
			int32 TriangleSliceIndex = RadialAlpha >= SegmentAlpha ? 0 : 1;
			if (SpiralDestructionMode ==
					EConeStructureSpiralDestructionMode::OneTrianglePerHit &&
				IsSpiralTriangleSliceDestroyed(SegmentIndex, TriangleSliceIndex))
			{
				TriangleSliceIndex = 1 - TriangleSliceIndex;
				if (IsSpiralTriangleSliceDestroyed(SegmentIndex, TriangleSliceIndex))
				{
					continue;
				}
			}
			if (DistanceSquared < ClosestDistanceSquared)
			{
				ClosestDistanceSquared = DistanceSquared;
				OutSegmentIndex = SegmentIndex;
				if (OutTriangleSliceIndex)
				{
					*OutTriangleSliceIndex = TriangleSliceIndex;
				}
			}
		}
	}

	if (OutSegmentIndex == INDEX_NONE)
	{
		return false;
	}

	// The nearest-segment search also produces a candidate for points on the cone body. Reject
	// candidates that are not touching the spiral; otherwise a shell hit can delete an unrelated
	// spiral slice several layers away. Real collision impacts lie on the generated spiral surface,
	// so only a small tolerance is needed for sweep/contact and floating-point error.
	const float MinimumBlockDimension = FMath::Max(
		FMath::Min3(
			static_cast<float>(BlockDimensions.X),
			static_cast<float>(BlockDimensions.Y),
			static_cast<float>(BlockDimensions.Z)),
		QuadMinimumConeDimension);
	const float HitAcceptanceTolerance = FMath::Clamp(
		MinimumBlockDimension * 0.02f, 0.5f, 5.0f);
	if (ClosestDistanceSquared > FMath::Square(HitAcceptanceTolerance))
	{
		OutSegmentIndex = INDEX_NONE;
		if (OutTriangleSliceIndex)
		{
			*OutTriangleSliceIndex = INDEX_NONE;
		}
		return false;
	}
	return true;
}

bool AQuadPanelConeStructure::DestroySpiralSegmentFromHit(const FHitResult &Hit)
{
	int32 SegmentIndex = INDEX_NONE;
	int32 TriangleSliceIndex = INDEX_NONE;
	if (!ResolveSpiralSegmentFromHit(Hit, SegmentIndex, &TriangleSliceIndex))
	{
		return false;
	}

	if (SpiralDestructionMode == EConeStructureSpiralDestructionMode::OneTrianglePerHit)
	{
		if (TriangleSliceIndex == INDEX_NONE)
		{
			return false;
		}
		DestroyedSpiralTriangleSliceIndices.Add(SegmentIndex * 2 + TriangleSliceIndex);
	}
	else
	{
		DestroyedSpiralSegmentIndices.Add(SegmentIndex);
	}
	RebuildSpiralSegmentChunks(SegmentIndex);
	const FVector WorldImpact = Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint;
	PlayDestroySound(WorldImpact, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	return true;
}

int32 AQuadPanelConeStructure::DestroySpiralSegmentsInRadiusFromHit(
	const FHitResult &Hit,
	float Radius)
{
	int32 HitSegmentIndex = INDEX_NONE;
	int32 HitTriangleSliceIndex = INDEX_NONE;
	if (!ResolveSpiralSegmentFromHit(Hit, HitSegmentIndex, &HitTriangleSliceIndex))
	{
		return 0;
	}

	const FVector WorldImpact = Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint;
	return DestroySpiralSegmentsInSphere(WorldImpact, Radius, HitSegmentIndex, HitTriangleSliceIndex);
}

int32 AQuadPanelConeStructure::DamageCellsInRadius(FVector WorldLocation, float Radius, float DamageAmount,
	EChunkedFloorBlastShape Shape, bool bUseDamageFalloff)
{
	const int32 CellsDamaged = Super::DamageCellsInRadius(WorldLocation, Radius, DamageAmount, Shape, bUseDamageFalloff);
	if (DamageAmount <= 0.f || Shape != EChunkedFloorBlastShape::Sphere || !bGeneratePlatforms
		|| PlatformStyle != EConeStructurePlatformStyle::RisingSpiral
		|| SpiralDestructionMode == EConeStructureSpiralDestructionMode::ExistingCellDestruction) return CellsDamaged;
	// Smooth slices have their own destruction state, independent of backing grid cells.
	return CellsDamaged + DestroySpiralSegmentsInSphere(WorldLocation, Radius);
}

int32 AQuadPanelConeStructure::DestroySpiralSegmentsInSphere(const FVector& WorldImpact, float Radius,
	int32 HitSegmentIndex, int32 HitTriangleSliceIndex)
{
	const float SafeRadiusSquared = FMath::Square(FMath::Max(Radius, 1.0f));
	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const float Thickness = GetSpiralThickness();
	const float TotalAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	const int32 SegmentCount = GetSpiralSegmentCount();

	auto MakeSection = [&](int32 SectionIndex,
						   FVector &OutInnerBottom,
						   FVector &OutOuterBottom,
						   FVector &OutInnerTop,
						   FVector &OutOuterTop)
	{
		const float Alpha = static_cast<float>(SectionIndex) / SegmentCount;
		const float Angle = StartAngle + Direction * TotalAngle * Alpha;
		const float BottomZ = FMath::Lerp(StartBottom, EndBottom, Alpha);
		const float TopZ = BottomZ + Thickness;
		const float InnerRadius = FMath::Max(
			GetConeOuterRadiusAtHeight(TopZ) -
				SpiralWallOverlapBlocks * CachedRadialBlockUnit *
					GetPlatformWidthScaleAtHeight(TopZ),
			0.0f);
		const float OuterRadius = GetPlatformOuterRadiusAtHeight(TopZ);
		const float CosAngle = FMath::Cos(Angle);
		const float SinAngle = FMath::Sin(Angle);
		OutInnerBottom = FVector(CosAngle * InnerRadius, SinAngle * InnerRadius, BottomZ);
		OutOuterBottom = FVector(CosAngle * OuterRadius, SinAngle * OuterRadius, BottomZ);
		OutInnerTop = FVector(CosAngle * InnerRadius, SinAngle * InnerRadius, TopZ);
		OutOuterTop = FVector(CosAngle * OuterRadius, SinAngle * OuterRadius, TopZ);
	};

	TSet<int32> SegmentIndicesToRebuild;
	int32 DestroyedPieceCount = 0;
	for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
	{
		FVector InnerBottomA;
		FVector OuterBottomA;
		FVector InnerTopA;
		FVector OuterTopA;
		FVector InnerBottomB;
		FVector OuterBottomB;
		FVector InnerTopB;
		FVector OuterTopB;
		MakeSection(SegmentIndex, InnerBottomA, OuterBottomA, InnerTopA, OuterTopA);
		MakeSection(SegmentIndex + 1, InnerBottomB, OuterBottomB, InnerTopB, OuterTopB);

		const FVector LocalSlices[2][6] = {
			{InnerBottomA,OuterBottomA,OuterBottomB,InnerTopA,OuterTopA,OuterTopB},
			{InnerBottomA,OuterBottomB,InnerBottomB,InnerTopA,OuterTopB,InnerTopB}};
		double SliceDistances[2];
		FVector ClosestSlicePoints[2] = {WorldImpact,WorldImpact};
		for (int32 Slice = 0; Slice < 2; ++Slice)
		{
			FVector WorldVertices[6];
			FBox Bounds(ForceInit);
			for (int32 Vertex = 0; Vertex < 6; ++Vertex)
			{
				WorldVertices[Vertex] = GetActorTransform().TransformPosition(LocalSlices[Slice][Vertex]);
				Bounds += WorldVertices[Vertex];
			}
			SliceDistances[Slice] = Bounds.ComputeSquaredDistanceToPoint(WorldImpact);
			if (SliceDistances[Slice] <= SafeRadiusSquared)
				SliceDistances[Slice] = BlastGeometry::TrianglePrismDistanceSquared(WorldImpact, WorldVertices, &ClosestSlicePoints[Slice]);
		}

		if (SpiralDestructionMode == EConeStructureSpiralDestructionMode::OneTrianglePerHit)
		{
			for (int32 TriangleSliceIndex = 0; TriangleSliceIndex < 2; ++TriangleSliceIndex)
			{
				const bool bDirectlyHitPiece = SegmentIndex == HitSegmentIndex &&
											   TriangleSliceIndex == HitTriangleSliceIndex;
				if (!bDirectlyHitPiece && SliceDistances[TriangleSliceIndex] > SafeRadiusSquared)
				{
					continue;
				}
				const int32 SliceKey = SegmentIndex * 2 + TriangleSliceIndex;
				if (!IsSpiralTriangleSliceDestroyed(SegmentIndex, TriangleSliceIndex))
				{
					DestroyedSpiralTriangleSliceIndices.Add(SliceKey);
					if (bScatterDestroyedPieces && ActiveDestructionPressure.Radius > 0.f)
						SpawnDebrisAtLocalPoint(GetActorTransform().InverseTransformPosition(ClosestSlicePoints[TriangleSliceIndex]), true);
					SegmentIndicesToRebuild.Add(SegmentIndex);
					++DestroyedPieceCount;
				}
			}
		}
		else
		{
			const bool bDirectlyHitPiece = SegmentIndex == HitSegmentIndex;
			if ((bDirectlyHitPiece || FMath::Min(SliceDistances[0], SliceDistances[1]) <= SafeRadiusSquared) &&
				!IsSpiralSegmentDestroyed(SegmentIndex))
			{
				DestroyedSpiralSegmentIndices.Add(SegmentIndex);
				if (bScatterDestroyedPieces && ActiveDestructionPressure.Radius > 0.f)
					SpawnDebrisAtLocalPoint(GetActorTransform().InverseTransformPosition(
						ClosestSlicePoints[SliceDistances[0] <= SliceDistances[1] ? 0 : 1]), true);
				SegmentIndicesToRebuild.Add(SegmentIndex);
				++DestroyedPieceCount;
			}
		}
	}

	if (DestroyedPieceCount <= 0)
	{
		return 0;
	}

	// Commit all state first and rebuild each affected spatial chunk only once. The smooth spiral
	// generator adds the same bottom and boundary faces used by a normal left-click segment hit.
	RebuildSpiralSegmentChunks(SegmentIndicesToRebuild);
	PlayDestroySound(WorldImpact, GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	return DestroyedPieceCount;
}

void AQuadPanelConeStructure::RebuildSpiralSegmentChunks(int32 SegmentIndex)
{
	TSet<int32> SegmentIndices;
	SegmentIndices.Add(SegmentIndex);
	RebuildSpiralSegmentChunks(SegmentIndices);
}

void AQuadPanelConeStructure::RebuildSpiralSegmentChunks(
	const TSet<int32> &SegmentIndices)
{
	if (SpiralPlatformGlowRuntime) { SpiralPlatformGlowRuntime->RefreshCurrentSlice(); }
	if (SegmentIndices.IsEmpty())
	{
		return;
	}

	const int32 SegmentCount = GetSpiralSegmentCount();
	if (ChunkCount.X <= 0)
	{
		RebuildAllChunks();
		return;
	}

	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const float Thickness = GetSpiralThickness();
	const float TotalAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	const double PitchX = FMath::Max(
		static_cast<double>(BlockDimensions.X + BlockSpacing.X), 1.0);
	const double PitchY = FMath::Max(
		static_cast<double>(BlockDimensions.Y + BlockSpacing.Y), 1.0);
	const double PitchZ = FMath::Max(
		static_cast<double>(BlockDimensions.Z + BlockSpacing.Z), 1.0);
	TSet<int32> AffectedChunkIndices;
	for (const int32 SegmentIndex : SegmentIndices)
	{
		if (SegmentIndex < 0 || SegmentIndex >= SegmentCount)
		{
			RebuildAllChunks();
			return;
		}

		TArray<FVector3d, TInlineAllocator<8>> SegmentVertices;
		SegmentVertices.Reserve(8);
		auto AddSectionVertices = [&](int32 SectionIndex)
		{
			const float Alpha = static_cast<float>(SectionIndex) / SegmentCount;
			const double Angle = StartAngle + Direction * TotalAngle * Alpha;
			const float BottomZ = FMath::Lerp(StartBottom, EndBottom, Alpha);
			const float TopZ = BottomZ + Thickness;
			const float InnerRadius = FMath::Max(
				GetConeOuterRadiusAtHeight(TopZ) -
					SpiralWallOverlapBlocks * CachedRadialBlockUnit *
						GetPlatformWidthScaleAtHeight(TopZ),
				0.0f);
			const float OuterRadius = GetPlatformOuterRadiusAtHeight(TopZ);
			const double CosAngle = FMath::Cos(Angle);
			const double SinAngle = FMath::Sin(Angle);
			SegmentVertices.Add(FVector3d(CosAngle * InnerRadius, SinAngle * InnerRadius, BottomZ));
			SegmentVertices.Add(FVector3d(CosAngle * OuterRadius, SinAngle * OuterRadius, BottomZ));
			SegmentVertices.Add(FVector3d(CosAngle * InnerRadius, SinAngle * InnerRadius, TopZ));
			SegmentVertices.Add(FVector3d(CosAngle * OuterRadius, SinAngle * OuterRadius, TopZ));
            SegmentVertices.Add(FVector3d(CosAngle*OuterRadius,SinAngle*OuterRadius,TopZ+GetSpiralRimHeight(TopZ)));
		};
		AddSectionVertices(SegmentIndex);
		AddSectionVertices(SegmentIndex + 1);

		FVector3d BoundsMinimum(TNumericLimits<double>::Max());
		FVector3d BoundsMaximum(TNumericLimits<double>::Lowest());
		for (const FVector3d &Vertex : SegmentVertices)
		{
			BoundsMinimum.X = FMath::Min(BoundsMinimum.X, Vertex.X);
			BoundsMinimum.Y = FMath::Min(BoundsMinimum.Y, Vertex.Y);
			BoundsMinimum.Z = FMath::Min(BoundsMinimum.Z, Vertex.Z);
			BoundsMaximum.X = FMath::Max(BoundsMaximum.X, Vertex.X);
			BoundsMaximum.Y = FMath::Max(BoundsMaximum.Y, Vertex.Y);
			BoundsMaximum.Z = FMath::Max(BoundsMaximum.Z, Vertex.Z);
		}

		const int32 MinimumX = FMath::Clamp(
			FMath::FloorToInt((BoundsMinimum.X - ConeGridLocalMin.X) / PitchX) - 1,
			0,
			GridDiameterBlocks - 1);
		const int32 MaximumX = FMath::Clamp(
			FMath::FloorToInt((BoundsMaximum.X - ConeGridLocalMin.X) / PitchX) + 1,
			0,
			GridDiameterBlocks - 1);
		const int32 MinimumY = FMath::Clamp(
			FMath::FloorToInt((BoundsMinimum.Y - ConeGridLocalMin.Y) / PitchY) - 1,
			0,
			GridDiameterBlocks - 1);
		const int32 MaximumY = FMath::Clamp(
			FMath::FloorToInt((BoundsMaximum.Y - ConeGridLocalMin.Y) / PitchY) + 1,
			0,
			GridDiameterBlocks - 1);
		const int32 MinimumZ = FMath::Clamp(
			FMath::FloorToInt(BoundsMinimum.Z / PitchZ) - 1,
			0,
			GridHeightBlocks - 1);
		const int32 MaximumZ = FMath::Clamp(
			FMath::FloorToInt(BoundsMaximum.Z / PitchZ) + 1,
			0,
			GridHeightBlocks - 1);

		for (int32 Z = MinimumZ; Z <= MaximumZ; ++Z)
		{
			for (int32 Y = MinimumY; Y <= MaximumY; ++Y)
			{
				for (int32 X = MinimumX; X <= MaximumX; ++X)
				{
					if (EncodeCell(X, Y, Z) == FIntPoint::NoneValue)
					{
						continue;
					}
					const int32 ChunkIndex = GetSpatialChunkIndex(X, Y, Z);
					if (ChunkIndex >= 0 && ChunkIndex < ChunkCount.X * ChunkCount.Y)
					{
						AffectedChunkIndices.Add(ChunkIndex);
					}
				}
			}
		}
	}

	if (AffectedChunkIndices.IsEmpty())
	{
		RebuildAllChunks();
		return;
	}
	for (const int32 ChunkIndex : AffectedChunkIndices)
	{
		RebuildChunk(ChunkIndex);
	}
}

bool AQuadPanelConeStructure::IsSpiralPlatformCell(int32 X, int32 Y, int32 Z) const
{
	if (PlatformOverhangBlocks <= KINDA_SMALL_NUMBER &&
		SpiralWallOverlapBlocks <= KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float SpiralThickness = GetSpiralThickness();
	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
	const FVector3d CellMinimum = FVector3d(Center - BlockDimensions * 0.5f);
	const FVector3d CellMaximum = FVector3d(Center + BlockDimensions * 0.5f);
	const float TotalSpiralAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	auto EndCapIntersectsCell = [&](bool bEnd)
	{
		const float Alpha = bEnd ? 1.0f : 0.0f;
		const float Bottom = FMath::Lerp(StartBottom, EndBottom, Alpha);
		const float Top = Bottom + SpiralThickness;
		const double CapAngle = StartAngle + Direction * TotalSpiralAngle * Alpha;
		const double CosAngle = FMath::Cos(CapAngle);
		const double SinAngle = FMath::Sin(CapAngle);
		const float ConeRadius = GetConeOuterRadiusAtHeight(Top);
		const float InnerRadius = FMath::Max(
			ConeRadius - SpiralWallOverlapBlocks * CachedRadialBlockUnit *
							 GetPlatformWidthScaleAtHeight(Top),
			0.0f);
		const float OuterRadius = GetPlatformOuterRadiusAtHeight(Top);
		const FVector3d InnerBottom(CosAngle * InnerRadius, SinAngle * InnerRadius, Bottom);
		const FVector3d OuterBottom(CosAngle * OuterRadius, SinAngle * OuterRadius, Bottom);
		const FVector3d OuterTop(CosAngle * OuterRadius, SinAngle * OuterRadius, Top);
		const FVector3d InnerTop(CosAngle * InnerRadius, SinAngle * InnerRadius, Top);
		return QuadDoesSurfaceQuadIntersectBox(
			InnerBottom, OuterBottom, OuterTop, InnerTop, CellMinimum, CellMaximum);
	};
	if (EndCapIntersectsCell(false) || EndCapIntersectsCell(true))
	{
		return true;
	}

	float MinimumRadius, MaximumRadius;
	GetCellRadialRange(X, Y, MinimumRadius, MaximumRadius);
	auto InnerAtTop = [&](float Top)
	{
		return GetConeOuterRadiusAtHeight(Top) - SpiralWallOverlapBlocks * CachedRadialBlockUnit * GetPlatformWidthScaleAtHeight(Top);
	};
	if (bSpiralOuterRim && SpiralRimHeight>0 && SpiralRimWidth>0)
        {
            const float Outer0=GetPlatformOuterRadiusAtHeight(StartBottom+SpiralThickness),Outer1=GetPlatformOuterRadiusAtHeight(EndBottom+SpiralThickness);
            const float Inner0=FMath::Max(Outer0-SpiralRimWidth,InnerAtTop(StartBottom+SpiralThickness));
            const float Inner1=FMath::Max(Outer1-SpiralRimWidth,InnerAtTop(EndBottom+SpiralThickness));
            if (QuadSpiralIntersectsCellBounds(Center,BlockDimensions,MinimumRadius,MaximumRadius,
                StartBottom+SpiralThickness,EndBottom+SpiralThickness,SpiralRimHeight,
                TotalSpiralAngle,StartAngle,Direction,GetSpiralSegmentCount(),Inner0,Inner1,Outer0,Outer1)) return true;
        }
        return QuadSpiralIntersectsCellBounds(Center, BlockDimensions, MinimumRadius, MaximumRadius,
		StartBottom, EndBottom, SpiralThickness, TotalSpiralAngle, StartAngle, Direction,
		GetSpiralSegmentCount(), InnerAtTop(StartBottom + SpiralThickness), InnerAtTop(EndBottom + SpiralThickness),
		GetPlatformOuterRadiusAtHeight(StartBottom + SpiralThickness), GetPlatformOuterRadiusAtHeight(EndBottom + SpiralThickness));
}

bool AQuadPanelConeStructure::IsEntranceCell(int32 X, int32 Y, int32 Z) const
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
		if (CellTop <= DoorBottom + QuadConeSpacingTolerance ||
			CellBottom >= DoorTop - QuadConeSpacingTolerance)
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
		if (LateralDistance < AvailableHalfWidth - QuadConeSpacingTolerance)
		{
			return true;
		}
	}
	return false;
}

bool AQuadPanelConeStructure::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
	if (IsPanelCell(Cell)) { return !IsPanelInDoor(Cell); }

	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return false;
	}
	const bool bShellCell = IsConeShellCell(X, Y, Z);
	const bool bBaseCell = IsConeBaseCell(X, Y, Z);
	const bool bPlatformCell = IsPlatformCell(X, Y, Z);
	const bool bBelongsToStructure = bBaseCell || bPlatformCell;
	const bool bEntranceRemovesCell = IsEntranceCell(X, Y, Z) &&
									  (bShellCell || bPlatformCell);
	return bBelongsToStructure && !bEntranceRemovesCell;
}

int32 AQuadPanelConeStructure::EvaluatePatternMaterial(FIntPoint Cell) const
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

bool AQuadPanelConeStructure::CellExists3D(int32 X, int32 Y, int32 Z) const
{
	const FIntPoint Cell = EncodeCell(X, Y, Z);
	return Cell != FIntPoint::NoneValue && Cells.IsValidIndex(ToCellIndex(Cell)) &&
		   Cells[ToCellIndex(Cell)].bExists;
}

bool AQuadPanelConeStructure::DoesExistingConeCellSupportSpiral(
	float SurfaceAngle,
	float LocalZ) const
{
	FIntPoint Panel;
	const float Radius = GetConeOuterRadiusAtHeight(LocalZ) - ShellThicknessBlocks * CachedRadialBlockUnit * .5f;
	if (FindPanelAtLocalPoint(FVector(FMath::Cos(SurfaceAngle) * Radius, FMath::Sin(SurfaceAngle) * Radius, LocalZ), Panel))
	{ return Cells.IsValidIndex(ToCellIndex(Panel)) && Cells[ToCellIndex(Panel)].bExists; }

	if (GridDiameterBlocks <= 0 || GridHeightBlocks <= 0)
	{
		return false;
	}

	const float PitchX = BlockDimensions.X + BlockSpacing.X;
	const float PitchY = BlockDimensions.Y + BlockSpacing.Y;
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const int32 Z = FMath::Clamp(
		FMath::FloorToInt(LocalZ / FMath::Max(PitchZ, 1.0f)),
		0,
		GridHeightBlocks - 1);
	// Sample near the radial center of the outer shell cell instead of on its
	// outside face. This makes the lookup stable around square-cell boundaries.
	const float RadialInset = FMath::Min(BlockDimensions.X, BlockDimensions.Y) * 0.35f;
	const float SampleRadius = FMath::Max(
		GetConeOuterRadiusAtHeight(LocalZ) - RadialInset, 0.0f);
	const FVector2D SamplePosition(
		FMath::Cos(SurfaceAngle) * SampleRadius,
		FMath::Sin(SurfaceAngle) * SampleRadius);
	const float FirstCenterX = ConeGridLocalMin.X + BlockDimensions.X * 0.5f;
	const float FirstCenterY = ConeGridLocalMin.Y + BlockDimensions.Y * 0.5f;
	const int32 EstimatedX = FMath::RoundToInt(
		(SamplePosition.X - FirstCenterX) / FMath::Max(PitchX, 1.0f));
	const int32 EstimatedY = FMath::RoundToInt(
		(SamplePosition.Y - FirstCenterY) / FMath::Max(PitchY, 1.0f));

	int32 ClosestX = INDEX_NONE;
	int32 ClosestY = INDEX_NONE;
	float ClosestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 OffsetY = -2; OffsetY <= 2; ++OffsetY)
	{
		for (int32 OffsetX = -2; OffsetX <= 2; ++OffsetX)
		{
			const int32 CandidateX = EstimatedX + OffsetX;
			const int32 CandidateY = EstimatedY + OffsetY;
			if (CandidateX < 0 || CandidateY < 0 ||
				CandidateX >= GridDiameterBlocks || CandidateY >= GridDiameterBlocks ||
				!IsConeShellCell(CandidateX, CandidateY, Z))
			{
				continue;
			}
			const FVector CandidateCenter = GetPhysicalCellLocalCenter(
				CandidateX, CandidateY, Z);
			const float DistanceSquared = FVector2D::DistSquared(
				SamplePosition, FVector2D(CandidateCenter.X, CandidateCenter.Y));
			if (DistanceSquared < ClosestDistanceSquared)
			{
				ClosestDistanceSquared = DistanceSquared;
				ClosestX = CandidateX;
				ClosestY = CandidateY;
			}
		}
	}
	return ClosestX != INDEX_NONE && CellExists3D(ClosestX, ClosestY, Z);
}

void AQuadPanelConeStructure::ConfigureChunkComponent(
	UDynamicMeshComponent *Component) const
{
	Super::ConfigureChunkComponent(Component);
	if (!Component)
	{
		return;
	}
	// Initial collision must be ready before the first shot, including panel-only cones.
	Component->bUseAsyncCooking = bUseAsyncConeCollisionCooking && bSpiralCollisionReadyForAsyncUpdates;

	UMaterialInterface *ResolvedConeMaterial = ResolveStructureMaterial(false);
	UMaterialInterface *ResolvedPlatformMaterial = ResolveStructureMaterial(true);

	Component->SetNumMaterials(2);
	Component->SetMaterial(0, ResolvedConeMaterial);
	Component->SetMaterial(1, ResolvedPlatformMaterial);
	if (MusicReactionMaterialInstance)
	{
		Component->SetMaterial(MusicMaterialSlot, MusicReactionMaterialInstance);
	}
	const UDynamicMesh *DynamicMesh = Component->GetDynamicMesh();
	const int32 TriangleCount = DynamicMesh ? DynamicMesh->GetTriangleCount() : 0;
	const bool bUseComplexCollisionForComponent =
		TriangleCount > 0 && TriangleCount <= FMath::Clamp(MaxSmoothCollisionTrianglesPerChunk, 10000, 240000);
	Component->SetComplexAsSimpleCollisionEnabled(bUseComplexCollisionForComponent, false);
}

void AQuadPanelConeStructure::AppendClippedSurfaceTriangle(
	FDynamicMesh3 &Mesh,
	int32 ChunkIndex,
	int32 LogicalZ,
	const FVector3d &A,
	const FVector3d &B,
	const FVector3d &C,
	const FVector3f &Normal,
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
	const float SafeUVScale = FMath::Max(UVWorldUnits, 1.0f);
	const double SealFraction = FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);
	const double VisualHalfX = BlockDimensions.X * 0.5 + BlockSpacing.X * 0.5 * SealFraction;
	const double VisualHalfY = BlockDimensions.Y * 0.5 + BlockSpacing.Y * 0.5 * SealFraction;
	const double VisualHalfZ = BlockDimensions.Z * 0.5 + BlockSpacing.Z * 0.5 * SealFraction;
	const FVector3d VisualHalfSize(VisualHalfX, VisualHalfY, VisualHalfZ);
	FIntVector ClipMinimum(0, 0, 0), ClipMaximum(GridDiameterBlocks, GridDiameterBlocks, GridHeightBlocks);
	if (ChunkIndex != INDEX_NONE && (!GetSpatialChunkBounds(ChunkIndex, ClipMinimum, ClipMaximum) ||
		LogicalZ < ClipMinimum.Z || LogicalZ >= ClipMaximum.Z)) { return; }
	const bool bOnGridPlane[3] = {
		FMath::IsNearlyEqual(TriangleMin.X, TriangleMax.X, QuadConeClipTolerance),
		FMath::IsNearlyEqual(TriangleMin.Y, TriangleMax.Y, QuadConeClipTolerance),
		FMath::IsNearlyEqual(TriangleMin.Z, TriangleMax.Z, QuadConeClipTolerance)};

	for (int32 Y = FMath::Max(MinimumY, ClipMinimum.Y); Y <= FMath::Min(MaximumY, ClipMaximum.Y - 1); ++Y)
	{
		for (int32 X = FMath::Max(MinimumX, ClipMinimum.X); X <= FMath::Min(MaximumX, ClipMaximum.X - 1); ++X)
		{
			const FIntPoint Cell = EncodeCell(X, Y, LogicalZ);
			const bool bBuildingSurfaceFlash = !SurfaceFlashCellFilter.IsEmpty();
			const bool bSelectedFlashCell = bBuildingSurfaceFlash && SurfaceFlashCellFilter.Contains(Cell);
			if (Cell == FIntPoint::NoneValue ||
				!Cells.IsValidIndex(ToCellIndex(Cell)) ||
				(bBuildingSurfaceFlash && !bSelectedFlashCell) ||
				(!Cells[ToCellIndex(Cell)].bExists && !bSelectedFlashCell))
			{
				continue;
			}

			const FVector CellCenter = GetCellLocalCenter(Cell);
			// A face on a shared cell plane belongs only to the cell on its solid side.
			// Inclusive clipping otherwise emits it twice, and leaves a floating face when
			// that solid-side cell is destroyed but the cell on the empty side survives.
			bool bOwnsSurface = true;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (bOnGridPlane[Axis] && FMath::Abs(Normal[Axis]) > .5f)
				{
					const double SolidSample = TriangleMin[Axis] - Normal[Axis] * QuadConeClipTolerance * 4;
					bOwnsSurface &= SolidSample > CellCenter[Axis] - VisualHalfSize[Axis] &&
						SolidSample < CellCenter[Axis] + VisualHalfSize[Axis];
				}
			}
			if (!bOwnsSurface) { continue; }
			const double CellMinX = CellCenter.X - VisualHalfX;
			const double CellMaxX = CellCenter.X + VisualHalfX;
			const double CellMinY = CellCenter.Y - VisualHalfY;
			const double CellMaxY = CellCenter.Y + VisualHalfY;
			const double CellMinZ = CellCenter.Z - VisualHalfZ;
			const double CellMaxZ = CellCenter.Z + VisualHalfZ;
			TArray<FVector3d> Polygon = {A, B, C};
			QuadClipSurfacePolygonToPlane(Polygon, 0, CellMinX, true);
			QuadClipSurfacePolygonToPlane(Polygon, 0, CellMaxX, false);
			QuadClipSurfacePolygonToPlane(Polygon, 1, CellMinY, true);
			QuadClipSurfacePolygonToPlane(Polygon, 1, CellMaxY, false);
			QuadClipSurfacePolygonToPlane(Polygon, 2, CellMinZ, true);
			QuadClipSurfacePolygonToPlane(Polygon, 2, CellMaxZ, false);
			QuadRemoveDuplicateSurfacePoints(Polygon);
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
				if (GeometricNormal.SquaredLength() <= FMath::Square(QuadConeClipTolerance))
				{
					continue;
				}
				if (FVector3d::DotProduct(GeometricNormal, FVector3d(Normal)) < 0.0)
				{
					Swap(PointB, PointC);
				}

				auto MakeUV = [Normal, SafeUVScale](const FVector3d &Point)
				{
					if (FMath::Abs(Normal.Z) > 0.75f)
					{
						return FVector2f(Point.X / SafeUVScale, Point.Y / SafeUVScale);
					}
					return FMath::Abs(Normal.X) > FMath::Abs(Normal.Y)
							   ? FVector2f(Point.Y / SafeUVScale, Point.Z / SafeUVScale)
							   : FVector2f(Point.X / SafeUVScale, Point.Z / SafeUVScale);
				};
				const int32 FirstTriangle = Mesh.MaxTriangleID();
				AppendMeshTriangle(
					Mesh, PointA, PointB, PointC, Normal, MaterialIndex,
					MakeUV(PointA), MakeUV(PointB), MakeUV(PointC));
				SetMusicTriangleCells(Mesh, FirstTriangle, Cell);
				// Record only the outer cone skin. Platform, caps, inner walls and cut faces
				// must not become part of a traveling square, even when they share a cell.
				if (IsMusicReactionEnabled() && ChunkIndex != INDEX_NONE && MaterialIndex == 0 &&
					Normal.X * (PointA.X + PointB.X + PointC.X) + Normal.Y * (PointA.Y + PointB.Y + PointC.Y) > 0.001)
				{
					for (int32 Triangle = FirstTriangle; Triangle < Mesh.MaxTriangleID(); ++Triangle)
					{
						if (Mesh.IsTriangle(Triangle)) { MusicChunkShellTriangles.FindOrAdd(ChunkIndex).Add(Triangle); }
					}
				}
			}
		}
	}
}

void AQuadPanelConeStructure::AppendClippedSurfaceTriangleAcrossLayers(
	FDynamicMesh3 &Mesh,
	int32 ChunkIndex,
	const FVector3d &A,
	const FVector3d &B,
	const FVector3d &C,
	const FVector3f &Normal,
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

void AQuadPanelConeStructure::AppendAnnularSurface(
	FDynamicMesh3 &Mesh,
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
	FIntVector ChunkMinimum;
	FIntVector ChunkMaximum;
	if (!GetSpatialChunkBounds(ChunkIndex, ChunkMinimum, ChunkMaximum))
	{
		return;
	}
	const int32 MinLogicalZ = ChunkMinimum.Z;
	const int32 MaxLogicalZ = ChunkMaximum.Z;
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

void AQuadPanelConeStructure::AppendConeSurfaces(
	FDynamicMesh3 &Mesh,
	int32 ChunkIndex) const
{
	// Only the base still uses the Cartesian grid. The shell is made of whole panels.
	if (!bGenerateConeBase) { return; }
	AppendAnnularSurface(Mesh, ChunkIndex, 0, 0.f, 0.f, GetConeOuterRadiusAtHeight(0.f), false, 0);
	const float Top = BlockDimensions.Z;
	AppendAnnularSurface(Mesh, ChunkIndex, 0, Top, 0.f, GetConeInnerRadiusAtHeight(Top), true, 0);
}

void AQuadPanelConeStructure::AppendConeGlowBandSurfaces(
	FDynamicMesh3 &Mesh,
	float BandBottomZ,
	float BandTopZ) const
{
	for (int32 Row = 0; Row < GridHeightBlocks; ++Row)
	{
		const double Height = Row * (BlockDimensions.Z + BlockSpacing.Z) + BlockDimensions.Z * .5;
		if (Height < BandBottomZ || Height >= BandTopZ) { continue; }
		for (int32 Column = 0; Column < WallPanelSegments; ++Column)
		{
			const FIntPoint Cell = PanelCell(Column, Row);
			if (Cells.IsValidIndex(ToCellIndex(Cell)) && Cells[ToCellIndex(Cell)].bExists)
				AppendPanel(Mesh, Cell, 0, .15f);
		}
	}
}

void AQuadPanelConeStructure::AppendConeSpiralGlowSurfaces(
	FDynamicMesh3 &Mesh,
	float PhaseOffsetRadians) const
{
	const int32 Strands = FMath::Clamp(SpiralGlowStrandCount, 1, 8);
	const double Width = FMath::DegreesToRadians(FMath::Clamp(SpiralGlowStripeWidthDegrees, 1.f, 350.f));
	for (int32 Row = 0; Row < GridHeightBlocks; ++Row)
	{
		const double Phase = PhaseOffsetRadians + FMath::DegreesToRadians(SpiralGlowStartAngleDegrees) +
			(bReverseSpiralGlowDirection ? -1. : 1.) * UE_DOUBLE_TWO_PI * SpiralGlowTurns * (Row + .5) / GridHeightBlocks;
		for (int32 Column = 0; Column < WallPanelSegments; ++Column)
		{
			const FIntPoint Cell = PanelCell(Column, Row);
			if (!Cells.IsValidIndex(ToCellIndex(Cell)) || !Cells[ToCellIndex(Cell)].bExists) { continue; }
			const double Angle = (Column + .5) * UE_DOUBLE_TWO_PI / WallPanelSegments;
			for (int32 Strand = 0; Strand < Strands; ++Strand)
			{
				if (FMath::Abs(FMath::UnwindRadians(Angle - Phase - Strand * UE_DOUBLE_TWO_PI / Strands)) <= Width * .5)
				{ AppendPanel(Mesh, Cell, 0, .15f); break; }
			}
		}
	}
}

void AQuadPanelConeStructure::AppendPlatformSurfaces(
	FDynamicMesh3 &Mesh,
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
	FIntVector ChunkMinimum;
	FIntVector ChunkMaximum;
	if (!GetSpatialChunkBounds(ChunkIndex, ChunkMinimum, ChunkMaximum))
	{
		return;
	}
	const int32 MinLogicalZ = ChunkMinimum.Z;
	const int32 MaxLogicalZ = ChunkMaximum.Z;
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
		const float OuterRadius = GetPlatformOuterRadiusAtHeight(PlatformReferenceZ);

		for (int32 Z = FMath::Max(PlatformBottomZ, MinLogicalZ);
			 Z <= FMath::Min(PlatformTopZ, MaxLogicalZ - 1); ++Z)
		{
			const float BottomZ = FMath::Max(Z * PitchZ - VerticalSeal, 0.0f);
			const float TopZ = FMath::Min(
				Z * PitchZ + BlockDimensions.Z + VerticalSeal,
				BlockDimensions.Z * GridHeightBlocks +
					BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0));
			const bool bSeparateLayer = BlockSpacing.Z > QuadConeSpacingTolerance &&
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

void AQuadPanelConeStructure::AppendSpiralSurfaces(
	FDynamicMesh3 &Mesh,
	int32 ChunkIndex, int32 OnlySlice) const
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
	const int32 SegmentCount = GetSpiralSegmentCount();
	if (OnlySlice < INDEX_NONE || OnlySlice >= SegmentCount * 2) { return; }
	auto MakeSection = [&](int32 SectionIndex,
						   FVector3d &OutInnerBottom,
						   FVector3d &OutOuterBottom,
						   FVector3d &OutInnerTop,
						   FVector3d &OutOuterTop)
	{
		const float Alpha = static_cast<float>(SectionIndex) / SegmentCount;
		const double Angle = StartAngle + Direction * TotalAngle * Alpha;
		const float BottomZ = FMath::Lerp(StartBottom, EndBottom, Alpha);
		const float TopZ = BottomZ + Thickness;
		const float ConeRadius = GetConeOuterRadiusAtHeight(TopZ);
		const float InnerRadius = FMath::Max(
			ConeRadius - SpiralWallOverlapBlocks * CachedRadialBlockUnit *
							 GetPlatformWidthScaleAtHeight(TopZ),
			0.0f);
		const float OuterRadius = GetPlatformOuterRadiusAtHeight(TopZ);
		const double CosAngle = FMath::Cos(Angle);
		const double SinAngle = FMath::Sin(Angle);
		OutInnerBottom = FVector3d(CosAngle * InnerRadius, SinAngle * InnerRadius, BottomZ);
		OutOuterBottom = FVector3d(CosAngle * OuterRadius, SinAngle * OuterRadius, BottomZ);
		OutInnerTop = FVector3d(CosAngle * InnerRadius, SinAngle * InnerRadius, TopZ);
		OutOuterTop = FVector3d(CosAngle * OuterRadius, SinAngle * OuterRadius, TopZ);
	};

	auto AppendOrientedTriangle = [&](FVector3d A, FVector3d B, FVector3d C, const FVector3d &DesiredNormal)
	{
		FVector3d GeometricNormal = FVector3d::CrossProduct(B - A, C - A);
		if (GeometricNormal.SquaredLength() <= FMath::Square(QuadConeSurfaceTolerance))
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
	auto IsTriangleSlicePresent = [this, SegmentCount](int32 SegmentIndex, int32 TriangleSliceIndex)
	{
		return SegmentIndex >= 0 && SegmentIndex < SegmentCount &&
			   !IsSpiralTriangleSliceDestroyed(SegmentIndex, TriangleSliceIndex);
	};

	const int32 FirstSegment = OnlySlice == INDEX_NONE ? 0 : OnlySlice / 2;
	const int32 EndSegment = OnlySlice == INDEX_NONE ? SegmentCount : FirstSegment + 1;
	for (int32 SegmentIndex = FirstSegment; SegmentIndex < EndSegment; ++SegmentIndex)
	{
		const bool bOuterTrianglePresent = IsTriangleSlicePresent(SegmentIndex, 0);
		const bool bInnerTrianglePresent = IsTriangleSlicePresent(SegmentIndex, 1);
		if (!bOuterTrianglePresent && !bInnerTrianglePresent)
		{
			continue;
		}

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
        // Rim is owned by the complete platform segment; removing either half removes its rim.
        if (bSpiralOuterRim && bOuterTrianglePresent && bInnerTrianglePresent && OnlySlice==INDEX_NONE)
        {
            const double WidthA=FMath::Min(double(SpiralRimWidth),(OuterTopA-InnerTopA).Length()*.35);
            const double WidthB=FMath::Min(double(SpiralRimWidth),(OuterTopB-InnerTopB).Length()*.35);
            const FVector3d IA=OuterTopA+(InnerTopA-OuterTopA).GetSafeNormal()*WidthA;
            const FVector3d IB=OuterTopB+(InnerTopB-OuterTopB).GetSafeNormal()*WidthB;
            const FVector3d ZA(0,0,GetSpiralRimHeight(OuterTopA.Z)),ZB(0,0,GetSpiralRimHeight(OuterTopB.Z));
            auto Quad=[&](FVector3d A,FVector3d B,FVector3d C,FVector3d D,FVector3d N)
            { AppendOrientedTriangle(A,B,C,N);AppendOrientedTriangle(A,C,D,N); };
            if (FMath::Min(WidthA,WidthB)>.001 && FMath::Max(ZA.Z,ZB.Z)>.001)
            {
                Quad(IA+ZA,OuterTopA+ZA,OuterTopB+ZB,IB+ZB,FVector3d::UnitZ());
                Quad(OuterTopA,OuterTopA+ZA,OuterTopB+ZB,OuterTopB,OutwardNormal);
                Quad(IA,IB,IB+ZB,IA+ZA,-OutwardNormal);
                const bool Prev=IsTriangleSlicePresent(SegmentIndex-1,0)&&IsTriangleSlicePresent(SegmentIndex-1,1);
                const bool Next=IsTriangleSlicePresent(SegmentIndex+1,0)&&IsTriangleSlicePresent(SegmentIndex+1,1);
                if (!Prev) Quad(IA,IA+ZA,OuterTopA+ZA,OuterTopA,-TangentNormal);
                if (!Next) Quad(IB,OuterTopB,OuterTopB+ZB,IB+ZB,TangentNormal);
            }
        }
		const FVector3d OuterTriangleCenter =
			(InnerTopA + OuterTopA + OuterTopB) / 3.0;
		const FVector3d InnerTriangleCenter =
			(InnerTopA + OuterTopB + InnerTopB) / 3.0;
		const FVector3d DiagonalNormal =
			(InnerTriangleCenter - OuterTriangleCenter).GetSafeNormal();

		if (bOuterTrianglePresent && (OnlySlice == INDEX_NONE || OnlySlice % 2 == 0))
		{
			AppendOrientedTriangle(
				InnerTopA, OuterTopA, OuterTopB, FVector3d::UnitZ());
			AppendOrientedTriangle(
				InnerBottomA, OuterBottomB, OuterBottomA, -FVector3d::UnitZ());
			AppendOrientedTriangle(
				OuterBottomA, OuterTopB, OuterTopA, OutwardNormal);
			AppendOrientedTriangle(
				OuterBottomA, OuterBottomB, OuterTopB, OutwardNormal);

			// The outer triangle owns the complete radial face at the start of the segment.
			if (!IsTriangleSlicePresent(SegmentIndex - 1, 1))
			{
				AppendOrientedTriangle(
					InnerBottomA, OuterBottomA, OuterTopA, -TangentNormal);
				AppendOrientedTriangle(
					InnerBottomA, OuterTopA, InnerTopA, -TangentNormal);
			}
			if (!bInnerTrianglePresent)
			{
				AppendOrientedTriangle(
					InnerBottomA, InnerTopA, OuterTopB, DiagonalNormal);
				AppendOrientedTriangle(
					InnerBottomA, OuterTopB, OuterBottomB, DiagonalNormal);
			}
		}

		if (bInnerTrianglePresent && (OnlySlice == INDEX_NONE || OnlySlice % 2 == 1))
		{
			AppendOrientedTriangle(
				InnerTopA, OuterTopB, InnerTopB, FVector3d::UnitZ());
			AppendOrientedTriangle(
				InnerBottomA, InnerBottomB, OuterBottomB, -FVector3d::UnitZ());
			AppendOrientedTriangle(
				InnerBottomA, InnerTopA, InnerTopB, -OutwardNormal);
			AppendOrientedTriangle(
				InnerBottomA, InnerTopB, InnerBottomB, -OutwardNormal);

			// The inner triangle owns the complete radial face at the end of the segment.
			if (!IsTriangleSlicePresent(SegmentIndex + 1, 0))
			{
				AppendOrientedTriangle(
					InnerBottomB, OuterTopB, OuterBottomB, TangentNormal);
				AppendOrientedTriangle(
					InnerBottomB, InnerTopB, OuterTopB, TangentNormal);
			}
			if (!bOuterTrianglePresent)
			{
				AppendOrientedTriangle(
					InnerBottomA, InnerTopA, OuterTopB, -DiagonalNormal);
				AppendOrientedTriangle(
					InnerBottomA, OuterTopB, OuterBottomB, -DiagonalNormal);
			}
		}
	}
}

bool AQuadPanelConeStructure::IsPointInsideRenderedStructure(
	const FVector3d &LocalPoint) const
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
	if (bGenerateConeBase && IsInsideRenderedLayer(0))
	{
		const double SafeZ = FMath::Clamp(LocalPoint.Z, 0.0, TotalHeight);
		const double BaseOuterRadius = GetFacetedRadius(
			GetConeOuterRadiusAtHeight(SafeZ), Angle, CircularSegmentCount);
		if (Radius <= BaseOuterRadius + BoundaryTolerance)
		{
			return true;
		}
	}
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
				GetPlatformOuterRadiusAtHeight(ReferenceZ),
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
	const int32 SpiralSegmentCount = GetSpiralSegmentCount();
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
			LocalPoint.Z > TopZ + GetSpiralRimHeight(TopZ) + BoundaryTolerance)
		{
			continue;
		}

		const double AngleA = StartAngle + Direction * ProgressA;
		const double AngleB = StartAngle + Direction * ProgressB;
		const double OuterRadiusA = GetPlatformOuterRadiusAtHeight(BottomA + Thickness);
		const double OuterRadiusB = GetPlatformOuterRadiusAtHeight(BottomB + Thickness);
		const double InnerRadiusA = FMath::Max(
			GetConeOuterRadiusAtHeight(BottomA + Thickness) -
				SpiralWallOverlapBlocks * CachedRadialBlockUnit *
					GetPlatformWidthScaleAtHeight(BottomA + Thickness),
			0.0f);
		const double InnerRadiusB = FMath::Max(
			GetConeOuterRadiusAtHeight(BottomB + Thickness) -
				SpiralWallOverlapBlocks * CachedRadialBlockUnit *
					GetPlatformWidthScaleAtHeight(BottomB + Thickness),
			0.0f);
		const double OuterRadius = IntersectPolarEdgeRadius(
			OuterRadiusA, AngleA, OuterRadiusB, AngleB, Angle);
		const double InnerRadius = IntersectPolarEdgeRadius(
			InnerRadiusA, AngleA, InnerRadiusB, AngleB, Angle);
		if (LocalPoint.Z>TopZ+BoundaryTolerance)
        {
            if (bSpiralOuterRim && Radius<=OuterRadius+BoundaryTolerance &&
                Radius>=OuterRadius-FMath::Min(double(SpiralRimWidth),(OuterRadius-InnerRadius)*.35)-BoundaryTolerance &&
                !IsSpiralTriangleSliceDestroyed(SegmentIndex,0) && !IsSpiralTriangleSliceDestroyed(SegmentIndex,1)) return true;
            continue;
        }
        const double MinimumRadius = FMath::Min(InnerRadius, OuterRadius);
		const double MaximumRadius = FMath::Max(InnerRadius, OuterRadius);
		if (Radius >= MinimumRadius - BoundaryTolerance &&
			Radius <= MaximumRadius + BoundaryTolerance)
		{
			const double RadialAlpha = FMath::Clamp(
				(Radius - MinimumRadius) /
					FMath::Max(MaximumRadius - MinimumRadius, UE_DOUBLE_SMALL_NUMBER),
				0.0,
				1.0);
			const double SliceTolerance = BoundaryTolerance /
										  FMath::Max(MaximumRadius - MinimumRadius, 1.0);
			const bool bInsideOuterTriangle =
				RadialAlpha + SliceTolerance >= SegmentAlpha;
			const bool bInsideInnerTriangle =
				RadialAlpha - SliceTolerance <= SegmentAlpha;
			if ((bInsideOuterTriangle &&
				 !IsSpiralTriangleSliceDestroyed(SegmentIndex, 0)) ||
				(bInsideInnerTriangle &&
				 !IsSpiralTriangleSliceDestroyed(SegmentIndex, 1)))
			{
				return true;
			}
		}
	}
	return false;
}

void AQuadPanelConeStructure::AppendClippedDestructionCutFace(
	FDynamicMesh3 &Mesh,
	const FVector3d &A,
	const FVector3d &B,
	const FVector3d &C,
	const FVector3d &D,
	const FVector3f &Normal,
	int32 MaterialIndex) const
{
	const FVector3d FaceCorners[] = {A, B, C, D};
	FVector3d FaceMinimum = A;
	FVector3d FaceMaximum = A;
	for (const FVector3d &Corner : FaceCorners)
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
	TArray<FGeneralPolygon2d> CutRegions[2];
	auto EmitCutRegions = [&]()
	{
		// Merge adjacent sections before triangulating. A shared wall/platform cell contains
		// both solids, regardless of its material label. Emit their union exactly once;
		// retain the cell's preferred material only where the two solids overlap.
		for (auto& Regions : CutRegions)
		{
			if (Regions.IsEmpty()) { continue; }
			TArray<FGeneralPolygon2d> Merged;
			if (ensure(PolygonsUnion(Regions, Merged, false))) { Regions = MoveTemp(Merged); }
		}
		const int32 Preferred = FMath::Clamp(MaterialIndex, 0, 1);
		const int32 Other = 1 - Preferred;
		if (!CutRegions[Preferred].IsEmpty() && !CutRegions[Other].IsEmpty())
		{
			TArray<FGeneralPolygon2d> Difference;
			if (ensure(PolygonsDifference(CutRegions[Other], CutRegions[Preferred], Difference)))
			{
				CutRegions[Other] = MoveTemp(Difference);
			}
		}
		for (int32 SurfaceMaterial = 0; SurfaceMaterial < 2; ++SurfaceMaterial)
		{
			for (FGeneralPolygon2d Region : CutRegions[SurfaceMaterial])
			{
				// Adjacent sections can leave coincident/collinear vertices after the union.
				// Clean at the same precision as surface clipping, then triangulate the outline
				// directly. Only sections with an actual hole need constrained triangulation.
				Region.Simplify(QuadConeSurfaceTolerance, QuadConeSurfaceTolerance);
				Region.FilterHoles([](const FPolygon2d& Hole) { return Hole.VertexCount() < 3 || Hole.Area() <= QuadConeSurfaceTolerance; });
				if (Region.GetOuter().VertexCount() < 3 || Region.GetOuter().Area() <= QuadConeSurfaceTolerance) { continue; }
				TArray<FVector2d> Vertices;
				TArray<FIndex3i> Triangles;
				if (!Region.HasHoles())
				{
					Vertices = Region.GetOuter().GetVertices();
					PolygonTriangulation::TriangulateSimplePolygon(Vertices, Triangles, false);
				}
				else
				{
					FConstrainedDelaunay2d Triangulation;
					Triangulation.Add(Region);
					if (!ensure(Triangulation.Triangulate())) { continue; }
					Vertices = MoveTemp(Triangulation.Vertices);
					Triangles = MoveTemp(Triangulation.Triangles);
				}
				auto ToLocal = [&](int32 Index)
				{
					const FVector2d& Point = Vertices[Index];
					return A + SortAxisU * Point.X + SortAxisV * Point.Y;
				};
				auto MakeCutUV = [&](const FVector3d& Point)
				{
					if (FMath::Abs(Normal.Z) > .75f) { return FVector2f(Point.X / SafeUVScale, Point.Y / SafeUVScale); }
					return FMath::Abs(Normal.X) > FMath::Abs(Normal.Y)
						? FVector2f(Point.Y / SafeUVScale, Point.Z / SafeUVScale)
						: FVector2f(Point.X / SafeUVScale, Point.Z / SafeUVScale);
				};
				for (const FIndex3i& Triangle : Triangles)
				{
					FVector3d P = ToLocal(Triangle.A), Q = ToLocal(Triangle.B), R = ToLocal(Triangle.C);
					const FVector3d Cross = (Q - P).Cross(R - P);
					if (Cross.SquaredLength() <= FMath::Square(QuadConeSurfaceTolerance)) { continue; }
					if (Cross.Dot(FVector3d(Normal)) < 0) { Swap(Q, R); }
					AppendMeshTriangle(Mesh, P, Q, R, Normal, SurfaceMaterial, MakeCutUV(P), MakeCutUV(Q), MakeCutUV(R));
				}
			}
		}
	};
	static const FIntPoint HexahedronEdges[] = {
		FIntPoint(0, 1), FIntPoint(0, 2), FIntPoint(1, 3), FIntPoint(2, 3),
		FIntPoint(4, 5), FIntPoint(4, 6), FIntPoint(5, 7), FIntPoint(6, 7),
		FIntPoint(0, 4), FIntPoint(1, 5), FIntPoint(2, 6), FIntPoint(3, 7)};
	// Spiral slices are triangular prisms: three bottom vertices followed by three top
	// vertices. Their cut faces must use all nine prism edges, including the diagonal
	// shared by the two slices. Treating them as eight-vertex elements left cuts open.
	static const FIntPoint TriangularPrismEdges[] = {
		FIntPoint(0, 1), FIntPoint(1, 2), FIntPoint(2, 0),
		FIntPoint(3, 4), FIntPoint(4, 5), FIntPoint(5, 3),
		FIntPoint(0, 3), FIntPoint(1, 4), FIntPoint(2, 5)};

	auto AppendElementIntersection = [&](const TArray<FVector3d, TInlineAllocator<8>> &Vertices,
		TConstArrayView<FIntPoint> ElementEdges, int32 SurfaceMaterial)
	{
		FVector3d ElementMinimum = Vertices[0];
		FVector3d ElementMaximum = Vertices[0];
		for (const FVector3d &Vertex : Vertices)
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
		// The surviving solid is behind the outward normal. Merely touching a plane
		// from its removed side cannot create a cap (or revive a deleted layer).
		if ((Normal[PlaneAxis] > 0 && ElementMinimum[PlaneAxis] >= Plane - IntersectionTolerance) ||
			(Normal[PlaneAxis] < 0 && ElementMaximum[PlaneAxis] <= Plane + IntersectionTolerance)) { return; }
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
		auto AddUniquePoint = [&Polygon](const FVector3d &Point)
		{
			for (const FVector3d &Existing : Polygon)
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
			const FVector3d &Start = Vertices[Edge.X];
			const FVector3d &End = Vertices[Edge.Y];
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
		for (const FVector3d &Point : Polygon)
		{
			Center += Point;
		}
		Center /= Polygon.Num();
		Polygon.Sort([&](const FVector3d &Left, const FVector3d &Right)
					 {
			const FVector3d LeftDelta = Left - Center;
			const FVector3d RightDelta = Right - Center;
			const double LeftAngle = FMath::Atan2(
				FVector3d::DotProduct(LeftDelta, SortAxisV),
				FVector3d::DotProduct(LeftDelta, SortAxisU));
			const double RightAngle = FMath::Atan2(
				FVector3d::DotProduct(RightDelta, SortAxisV),
				FVector3d::DotProduct(RightDelta, SortAxisU));
			return LeftAngle < RightAngle; });

		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Axis == PlaneAxis)
			{
				continue;
			}
			QuadClipSurfacePolygonToPlane(Polygon, Axis, FaceMinimum[Axis], true);
			QuadClipSurfacePolygonToPlane(Polygon, Axis, FaceMaximum[Axis], false);
		}
		QuadRemoveDuplicateSurfacePoints(Polygon);
		if (Polygon.Num() < 3)
		{
			return;
		}

		FPolygon2d Region;
		for (const FVector3d& Point : Polygon)
		{
			const FVector3d Relative = Point - A;
			Region.AppendVertex(FVector2d(Relative.Dot(SortAxisU), Relative.Dot(SortAxisV)));
		}
		if (Region.Area() <= QuadConeSurfaceTolerance) { return; }
		if (Region.IsClockwise()) { Region.Reverse(); }
		CutRegions[SurfaceMaterial].Emplace(Region);
	};

	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
							  BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const float VerticalSeal = BlockSpacing.Z * 0.5f *
							   FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);

	// Always intersect the shell; a material label cannot exclude part of the volume.
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
			const float InnerBottomRadius = bGenerateConeBase && Z == 0 ? 0.0f : GetConeInnerRadiusAtHeight(BottomZ);
			const float InnerTopRadius = bGenerateConeBase && Z == 0 ? 0.0f : GetConeInnerRadiusAtHeight(TopZ);
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
					FVector3d(FMath::Cos(AngleB) * OuterTopRadius, FMath::Sin(AngleB) * OuterTopRadius, TopZ)};
				AppendElementIntersection(Vertices, HexahedronEdges, 0);
			}
		}
	}
	if (!bGeneratePlatforms)
	{
		EmitCutRegions();
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
			const float OuterRadius = GetPlatformOuterRadiusAtHeight(ReferenceZ);
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
					FVector3d(FMath::Cos(AngleB) * OuterRadius, FMath::Sin(AngleB) * OuterRadius, TopZ)};
				AppendElementIntersection(Vertices, HexahedronEdges, 1);
			}
		}
		EmitCutRegions();
		return;
	}

	const float StartBottom = GetSpiralBottomHeight(false);
	const float EndBottom = GetSpiralBottomHeight(true);
	const float Thickness = GetSpiralThickness();
	const float TotalAngle = UE_TWO_PI * PlatformSpiralTurns;
	const float StartAngle = FMath::DegreesToRadians(SpiralStartAngleDegrees);
	const float Direction = bPlatformSpiralClockwise ? -1.0f : 1.0f;
	const int32 SegmentCount = GetSpiralSegmentCount();
	auto MakeSection = [&](int32 SectionIndex, FVector3d &InnerBottom, FVector3d &OuterBottom,
						   FVector3d &InnerTop, FVector3d &OuterTop)
	{
		const float Alpha = static_cast<float>(SectionIndex) / SegmentCount;
		const double Angle = StartAngle + Direction * TotalAngle * Alpha;
		const float BottomZ = FMath::Lerp(StartBottom, EndBottom, Alpha);
		const float TopZ = BottomZ + Thickness;
		const float ConeOuterRadius = GetConeOuterRadiusAtHeight(TopZ);
		const float InnerRadius = FMath::Max(
			ConeOuterRadius - SpiralWallOverlapBlocks * CachedRadialBlockUnit *
								  GetPlatformWidthScaleAtHeight(TopZ),
			0.0f);
		const float OuterRadius = GetPlatformOuterRadiusAtHeight(TopZ);
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
        if (bSpiralOuterRim && !IsSpiralTriangleSliceDestroyed(SegmentIndex,0) && !IsSpiralTriangleSliceDestroyed(SegmentIndex,1))
        {
            const double WA=FMath::Min(double(SpiralRimWidth),(OuterTopA-InnerTopA).Length()*.35);
            const double WB=FMath::Min(double(SpiralRimWidth),(OuterTopB-InnerTopB).Length()*.35);
            const FVector3d IA=OuterTopA+(InnerTopA-OuterTopA).GetSafeNormal()*WA;
            const FVector3d IB=OuterTopB+(InnerTopB-OuterTopB).GetSafeNormal()*WB;
            const FVector3d ZA(0,0,GetSpiralRimHeight(OuterTopA.Z)),ZB(0,0,GetSpiralRimHeight(OuterTopB.Z));
            if (FMath::Min(WA,WB)>.001 && FMath::Max(ZA.Z,ZB.Z)>.001)
            {
                // The cut routine uses section pairs (inner/outer, bottom/top).
                TArray<FVector3d,TInlineAllocator<8>> Rim={IA,OuterTopA,IA+ZA,OuterTopA+ZA,IB,OuterTopB,IB+ZB,OuterTopB+ZB};
                AppendElementIntersection(Rim,HexahedronEdges,1);
            }
        }
		if (!IsSpiralTriangleSliceDestroyed(SegmentIndex, 0))
		{
			TArray<FVector3d, TInlineAllocator<8>> OuterTriangleVertices = {
				InnerBottomA, OuterBottomA, OuterBottomB,
				InnerTopA, OuterTopA, OuterTopB};
			AppendElementIntersection(OuterTriangleVertices, TriangularPrismEdges, 1);
		}
		if (!IsSpiralTriangleSliceDestroyed(SegmentIndex, 1))
		{
			TArray<FVector3d, TInlineAllocator<8>> InnerTriangleVertices = {
				InnerBottomA, OuterBottomB, InnerBottomB,
				InnerTopA, OuterTopB, InnerTopB};
			AppendElementIntersection(InnerTriangleVertices, TriangularPrismEdges, 1);
		}
	}
	EmitCutRegions();
}

bool AQuadPanelConeStructure::ShouldAppendPlanarCutFace(
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

void AQuadPanelConeStructure::AppendDestroyedCellCutFaces(
	FDynamicMesh3 &Mesh,
	FIntPoint Cell) const
{
	if (!Cells.IsValidIndex(ToCellIndex(Cell)))
	{
		return;
	}
	const FFloorCellData &CellData = Cells[ToCellIndex(Cell)];
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
	const double Seal = FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);
	const FVector VisualHalfSize = (BlockDimensions + BlockSpacing * Seal) * .5;
	const double MinX = Center.X - VisualHalfSize.X;
	const double MaxX = Center.X + VisualHalfSize.X;
	const double MinY = Center.Y - VisualHalfSize.Y;
	const double MaxY = Center.Y + VisualHalfSize.Y;
	const double MinZ = Center.Z - VisualHalfSize.Z;
	const double MaxZ = Center.Z + VisualHalfSize.Z;
	const int32 MaterialIndex = CellData.MaterialIndex;

	if (BlockSpacing.X * (1.0 - Seal) <= QuadConeSpacingTolerance && CellExists3D(X - 1, Y, Z))
	{
		const int32 FirstTriangle = Mesh.MaxTriangleID();
		AppendClippedDestructionCutFace(Mesh,
										FVector3d(MinX, MaxY, MinZ), FVector3d(MinX, MaxY, MaxZ),
										FVector3d(MinX, MinY, MaxZ), FVector3d(MinX, MinY, MinZ),
										FVector3f::UnitX(), MaterialIndex);
		SetMusicTriangleCells(Mesh, FirstTriangle, EncodeCell(X - 1, Y, Z));
	}
	if (BlockSpacing.X * (1.0 - Seal) <= QuadConeSpacingTolerance && CellExists3D(X + 1, Y, Z))
	{
		const int32 FirstTriangle = Mesh.MaxTriangleID();
		AppendClippedDestructionCutFace(Mesh,
										FVector3d(MaxX, MinY, MinZ), FVector3d(MaxX, MinY, MaxZ),
										FVector3d(MaxX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MinZ),
										-FVector3f::UnitX(), MaterialIndex);
		SetMusicTriangleCells(Mesh, FirstTriangle, EncodeCell(X + 1, Y, Z));
	}
	if (BlockSpacing.Y * (1.0 - Seal) <= QuadConeSpacingTolerance && CellExists3D(X, Y - 1, Z))
	{
		const int32 FirstTriangle = Mesh.MaxTriangleID();
		AppendClippedDestructionCutFace(Mesh,
										FVector3d(MinX, MinY, MinZ), FVector3d(MinX, MinY, MaxZ),
										FVector3d(MaxX, MinY, MaxZ), FVector3d(MaxX, MinY, MinZ),
										FVector3f::UnitY(), MaterialIndex);
		SetMusicTriangleCells(Mesh, FirstTriangle, EncodeCell(X, Y - 1, Z));
	}
	if (BlockSpacing.Y * (1.0 - Seal) <= QuadConeSpacingTolerance && CellExists3D(X, Y + 1, Z))
	{
		const int32 FirstTriangle = Mesh.MaxTriangleID();
		AppendClippedDestructionCutFace(Mesh,
										FVector3d(MaxX, MaxY, MinZ), FVector3d(MaxX, MaxY, MaxZ),
										FVector3d(MinX, MaxY, MaxZ), FVector3d(MinX, MaxY, MinZ),
										-FVector3f::UnitY(), MaterialIndex);
		SetMusicTriangleCells(Mesh, FirstTriangle, EncodeCell(X, Y + 1, Z));
	}
	if (BlockSpacing.Z * (1.0 - Seal) <= QuadConeSpacingTolerance && CellExists3D(X, Y, Z - 1))
	{
		const int32 FirstTriangle = Mesh.MaxTriangleID();
		AppendClippedDestructionCutFace(Mesh,
										FVector3d(MinX, MinY, MinZ), FVector3d(MaxX, MinY, MinZ),
										FVector3d(MaxX, MaxY, MinZ), FVector3d(MinX, MaxY, MinZ),
										FVector3f::UnitZ(), MaterialIndex);
		SetMusicTriangleCells(Mesh, FirstTriangle, EncodeCell(X, Y, Z - 1));
	}
	if (BlockSpacing.Z * (1.0 - Seal) <= QuadConeSpacingTolerance && CellExists3D(X, Y, Z + 1))
	{
		const int32 FirstTriangle = Mesh.MaxTriangleID();
		AppendClippedDestructionCutFace(Mesh,
										FVector3d(MinX, MaxY, MaxZ), FVector3d(MaxX, MaxY, MaxZ),
										FVector3d(MaxX, MinY, MaxZ), FVector3d(MinX, MinY, MaxZ),
										-FVector3f::UnitZ(), MaterialIndex);
		SetMusicTriangleCells(Mesh, FirstTriangle, EncodeCell(X, Y, Z + 1));
	}
}

void AQuadPanelConeStructure::AppendSurfaceFlashBoundaryFaces(
	FDynamicMesh3 &Mesh,
	const TSet<FIntPoint> &FlashCells) const
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

void AQuadPanelConeStructure::BuildChunkMesh(
	int32 ChunkIndex,
	FDynamicMesh3 &OutMesh) const
{
	MusicChunkShellTriangles.Remove(ChunkIndex);
	OutMesh.EnableVertexNormals(FVector3f::UnitZ());
	OutMesh.EnableAttributes();
	OutMesh.Attributes()->SetNumUVLayers(1);
	OutMesh.Attributes()->EnableMaterialID();
	if (IsMusicReactionEnabled()) { OutMesh.EnableTriangleGroups(INDEX_NONE); }

	if (ChunkIndex >= PanelChunkStart)
	{
		AppendPanelChunk(OutMesh, ChunkIndex);
		CacheMusicChunkMaterials(ChunkIndex, OutMesh);
		return;
	}
	AppendConeSurfaces(OutMesh, ChunkIndex);
	AppendPlatformSurfaces(OutMesh, ChunkIndex);

	// Only cell gaps, doorway reveals and newly exposed destruction cuts remain planar. The intact
	// exterior is emitted above from exact circular and conical surfaces, eliminating voxel steps.
	FIntVector ChunkMinimum;
	FIntVector ChunkMaximum;
	if (!GetSpatialChunkBounds(ChunkIndex, ChunkMinimum, ChunkMaximum))
	{
		return;
	}
	for (int32 Z = ChunkMinimum.Z; Z < ChunkMaximum.Z; ++Z)
	{
		for (int32 Y = ChunkMinimum.Y; Y < ChunkMaximum.Y; ++Y)
		{
			for (int32 X = ChunkMinimum.X; X < ChunkMaximum.X; ++X)
			{
				const FIntPoint Cell = EncodeCell(X, Y, Z);
				if (Cell == FIntPoint::NoneValue)
				{
					continue;
				}
				const FFloorCellData &CellData = Cells[ToCellIndex(Cell)];
				if (!CellData.bBelongsToGeneratedShape) { continue; }
				if (!CellData.bExists)
				{
					AppendDestroyedCellCutFaces(OutMesh, Cell);
					continue;
				}
				const int32 DecodedZ = Z;
				const double Seal = FMath::Clamp(SurfaceGapSealFraction, 0.0f, 1.0f);
				const FVector VisualHalfSize = (BlockDimensions + BlockSpacing * Seal) * .5;
				const int32 FirstCellTriangle = OutMesh.MaxTriangleID();
				const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
				const double MinX = Center.X - VisualHalfSize.X;
				const double MaxX = Center.X + VisualHalfSize.X;
				const double MinY = Center.Y - VisualHalfSize.Y;
				const double MaxY = Center.Y + VisualHalfSize.Y;
				const double MinCellZ = Center.Z - VisualHalfSize.Z;
				const double MaxCellZ = Center.Z + VisualHalfSize.Z;
				const int32 MaterialIndex = CellData.MaterialIndex;
				auto NeedsFace = [this](int32 NeighborX, int32 NeighborY, int32 NeighborZ, bool bAxisHasGap)
				{
					return ShouldAppendPlanarCutFace(NeighborX, NeighborY, NeighborZ) ||
						   (bAxisHasGap && CellExists3D(NeighborX, NeighborY, NeighborZ));
				};

				if (NeedsFace(X - 1, Y, DecodedZ, BlockSpacing.X * (1.0 - Seal) > QuadConeSpacingTolerance))
				{
					AppendClippedDestructionCutFace(OutMesh,
								   FVector3d(MinX, MinY, MinCellZ), FVector3d(MinX, MinY, MaxCellZ),
								   FVector3d(MinX, MaxY, MaxCellZ), FVector3d(MinX, MaxY, MinCellZ),
								   -FVector3f::UnitX(), MaterialIndex);
				}
				if (NeedsFace(X + 1, Y, DecodedZ, BlockSpacing.X * (1.0 - Seal) > QuadConeSpacingTolerance))
				{
					AppendClippedDestructionCutFace(OutMesh,
								   FVector3d(MaxX, MaxY, MinCellZ), FVector3d(MaxX, MaxY, MaxCellZ),
								   FVector3d(MaxX, MinY, MaxCellZ), FVector3d(MaxX, MinY, MinCellZ),
								   FVector3f::UnitX(), MaterialIndex);
				}
				if (NeedsFace(X, Y - 1, DecodedZ, BlockSpacing.Y * (1.0 - Seal) > QuadConeSpacingTolerance))
				{
					AppendClippedDestructionCutFace(OutMesh,
								   FVector3d(MaxX, MinY, MinCellZ), FVector3d(MaxX, MinY, MaxCellZ),
								   FVector3d(MinX, MinY, MaxCellZ), FVector3d(MinX, MinY, MinCellZ),
								   -FVector3f::UnitY(), MaterialIndex);
				}
				if (NeedsFace(X, Y + 1, DecodedZ, BlockSpacing.Y * (1.0 - Seal) > QuadConeSpacingTolerance))
				{
					AppendClippedDestructionCutFace(OutMesh,
								   FVector3d(MinX, MaxY, MinCellZ), FVector3d(MinX, MaxY, MaxCellZ),
								   FVector3d(MaxX, MaxY, MaxCellZ), FVector3d(MaxX, MaxY, MinCellZ),
								   FVector3f::UnitY(), MaterialIndex);
				}
				if (NeedsFace(X, Y, DecodedZ - 1, BlockSpacing.Z * (1.0 - Seal) > QuadConeSpacingTolerance))
				{
					AppendClippedDestructionCutFace(OutMesh,
								   FVector3d(MinX, MaxY, MinCellZ), FVector3d(MaxX, MaxY, MinCellZ),
								   FVector3d(MaxX, MinY, MinCellZ), FVector3d(MinX, MinY, MinCellZ),
								   -FVector3f::UnitZ(), MaterialIndex);
				}
				if (NeedsFace(X, Y, DecodedZ + 1, BlockSpacing.Z * (1.0 - Seal) > QuadConeSpacingTolerance))
				{
					AppendClippedDestructionCutFace(OutMesh,
								   FVector3d(MinX, MinY, MaxCellZ), FVector3d(MaxX, MinY, MaxCellZ),
								   FVector3d(MaxX, MaxY, MaxCellZ), FVector3d(MinX, MaxY, MaxCellZ),
								   FVector3f::UnitZ(), MaterialIndex);
				}
				SetMusicTriangleCells(OutMesh, FirstCellTriangle, Cell);
			}
		}
	}
	CacheMusicChunkMaterials(ChunkIndex, OutMesh);
}

void AQuadPanelConeStructure::BuildChunkSimpleCollision(
	int32 ChunkIndex,
	FKAggregateGeom &OutCollision) const
{
	OutCollision.EmptyElements();
	if (!bGenerateCollision || CanUseSmoothCollisionForChunk(ChunkIndex) || ChunkCount.X <= 0)
	{
		return;
	}
	FIntVector ChunkMinimum;
	FIntVector ChunkMaximum;
	if (!GetSpatialChunkBounds(ChunkIndex, ChunkMinimum, ChunkMaximum))
	{
		return;
	}
	TSet<int32> OccupiedPhysicalCells;
	TArray<FIntVector> SeedCells;
	auto PhysicalKey = [this](int32 X, int32 Y, int32 Z)
	{
		return X + Y * GridDiameterBlocks +
			   Z * GridDiameterBlocks * GridDiameterBlocks;
	};
	for (int32 Z = ChunkMinimum.Z; Z < ChunkMaximum.Z; ++Z)
	{
		for (int32 Y = ChunkMinimum.Y; Y < ChunkMaximum.Y; ++Y)
		{
			for (int32 X = ChunkMinimum.X; X < ChunkMaximum.X; ++X)
			{
				if (!CellExists3D(X, Y, Z))
				{
					continue;
				}
				OccupiedPhysicalCells.Add(PhysicalKey(X, Y, Z));
				SeedCells.Emplace(X, Y, Z);
			}
		}
	}

	SeedCells.Sort([](const FIntVector &A, const FIntVector &B)
				   {
		if (A.Z != B.Z)
		{
			return A.Z < B.Z;
		}
		if (A.Y != B.Y)
		{
			return A.Y < B.Y;
		}
		return A.X < B.X; });
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
	const bool bCanMergeX = BlockSpacing.X <= QuadConeSpacingTolerance;
	const bool bCanMergeY = BlockSpacing.Y <= QuadConeSpacingTolerance;
	const bool bCanMergeZ = BlockSpacing.Z <= QuadConeSpacingTolerance;

	for (const FIntVector &Seed : SeedCells)
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
		FKBoxElem &Box = OutCollision.BoxElems.Emplace_GetRef(
			static_cast<float>(BoxSize.X),
			static_cast<float>(BoxSize.Y),
			static_cast<float>(BoxSize.Z));
		Box.Center = (BoxMinimum + BoxMaximum) * 0.5;
	}
}

FTransform AQuadPanelConeStructure::GetCellFlashTransform(FIntPoint Cell) const
{
	if (IsPanelCell(Cell))
	{
		FVector3d Corners[8]; GetPanelCorners(Cell, Corners);
		const FVector Center = GetCellLocalCenter(Cell);
		const FQuat Rotation = FVector(Center.X, Center.Y, 0).Rotation().Quaternion();
		return FTransform(Rotation, Center, FVector((Corners[0] - Corners[4]).Length(),
			(Corners[0] - Corners[1]).Length(), Corners[2].Z - Corners[0].Z) / 100.);
	}
	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	}
	if (DestructionFlashStyle != EConeStructureDestructionFlashStyle::DetailedSurface)
	{
		const FVector Center = GetPhysicalCellLocalCenter(X, Y, Z);
		if (DestructionFlashStyle == EConeStructureDestructionFlashStyle::None)
		{
			return FTransform(FQuat::Identity, Center, FVector::ZeroVector);
		}
		const float CompactEdge = FMath::Max(
			FMath::Min3(
				static_cast<float>(BlockDimensions.X),
				static_cast<float>(BlockDimensions.Y),
				static_cast<float>(BlockDimensions.Z)) *
				0.34f,
			1.0f);
		return FTransform(
			FQuat::Identity,
			Center,
			FVector(CompactEdge / 100.0f) * FMath::Clamp(FlashScale, 0.01f, 1.0f));
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
	static constexpr float CandidateFractions[] = {-0.8f, -0.4f, 0.0f, 0.4f, 0.8f};
	static constexpr float ProbeFractions[] = {-1.0f, 0.0f, 1.0f};

	auto IsCubeInsideRenderedSolid = [this](const FVector &Center, float HalfEdge)
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

void AQuadPanelConeStructure::SpawnCellFlash(FIntPoint Cell, double CurrentTime)
{
	if (DestructionFlashStyle == EConeStructureDestructionFlashStyle::None)
	{
		return;
	}
	if (DestructionFlashStyle == EConeStructureDestructionFlashStyle::FastCompact)
	{
		Super::SpawnCellFlash(Cell, CurrentTime);
		return;
	}
	if (FlashDuration <= 0.0f || !GetWorld() ||
		!Cells.IsValidIndex(ToCellIndex(Cell)) || ChunkCount.X <= 0)
	{
		return;
	}

	// Cone platforms are curved solids clipped into rectangular logical cells. Reusing the floor's
	// cube ISM makes every occupied Z layer appear as a separate white box. Build one combined flash
	// from the same exact cone/ring/helix triangles as the visible structure. Cells processed in the
	// same destruction batch share a component, so a thick spiral flashes as one continuous shape.
	const bool bContinueCurrentBatch = CurrentSurfaceFlashComponent.IsValid() &&
									   FMath::IsNearlyEqual(CurrentSurfaceFlashTime, CurrentTime, UE_DOUBLE_SMALL_NUMBER);
	UDynamicMeshComponent *SurfaceFlash = bContinueCurrentBatch
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
			[](const TWeakObjectPtr<UDynamicMeshComponent> &Component)
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
				if (UDynamicMeshComponent *Component = WeakFlash.Get())
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
}

void AQuadPanelConeStructure::FinalizeDestructionBatch(double CurrentTime)
{
	if (SpiralPlatformGlowRuntime) { SpiralPlatformGlowRuntime->RefreshCurrentSlice(); }
	// The music surface is independent of the panel mesh and must disappear in
	// this destruction batch, without waiting for a beat or the throttled music tick.
	RemoveDestroyedMusicSurfaceTriangles();
	if (DestructionFlashStyle != EConeStructureDestructionFlashStyle::DetailedSurface ||
		!CurrentSurfaceFlashComponent.IsValid() || CurrentSurfaceFlashCells.IsEmpty() ||
		!FMath::IsNearlyEqual(CurrentSurfaceFlashTime, CurrentTime, UE_DOUBLE_SMALL_NUMBER))
	{
		return;
	}
	UDynamicMeshComponent *SurfaceFlash = CurrentSurfaceFlashComponent.Get();

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
		if (IsPanelCell(FlashCell)) { AppendPanel(SurfaceFlashMesh, FlashCell); continue; }
		if (Cells.IsValidIndex(ToCellIndex(FlashCell)) &&
			Cells[ToCellIndex(FlashCell)].MaterialIndex == 1)
		{
			bNeedsPlatformSurface = true;
		}
		else
		{
			bNeedsConeSurface = true;
		}
		int32 FlashX;
		int32 FlashY;
		int32 FlashZ;
		const int32 ChunkIndex = DecodeCell(FlashCell, FlashX, FlashY, FlashZ)
									 ? GetSpatialChunkIndex(FlashX, FlashY, FlashZ)
									 : INDEX_NONE;
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

	UDynamicMesh *DynamicMesh = NewObject<UDynamicMesh>(SurfaceFlash);
	DynamicMesh->SetMesh(MoveTemp(SurfaceFlashMesh));
	SurfaceFlash->SetDynamicMesh(DynamicMesh);
	SurfaceFlash->NotifyMeshUpdated();
	SurfaceFlash->SetVisibility(true, true);
	SurfaceFlash->MarkRenderStateDirty();
}

bool AQuadPanelConeStructure::ResolveExistingCellFromHit(
	const FHitResult &Hit,
	FIntPoint &OutCell) const
{
	// At an exposed top/side face, the exact boundary can belong to the empty
	// panel on the other side. Resolve just inside the struck surface first, so
	// firing down or sideways from a hole selects the solid panel consistently.
	const FVector InteriorPoint = GetInteriorImpactPoint(Hit);
	FIntPoint Panel;
	if (FindPanelAtLocalPoint(GetActorTransform().InverseTransformPosition(InteriorPoint), Panel, 1.0))
	{
		OutCell = Panel;
		return Cells.IsValidIndex(ToCellIndex(Panel)) && Cells[ToCellIndex(Panel)].bExists;
	}
	const FVector PanelImpact = GetActorTransform().InverseTransformPosition(Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint);
	if (FindPanelAtLocalPoint(PanelImpact, Panel, 1.0))
	{
		OutCell = Panel;
		return Cells.IsValidIndex(ToCellIndex(Panel)) && Cells[ToCellIndex(Panel)].bExists;
	}

	OutCell = FIntPoint::NoneValue;
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

int32 AQuadPanelConeStructure::SelectCellsInRadius(
	FVector WorldLocation,
	float Radius,
	EChunkedFloorBlastShape Shape,
	TFunctionRef<void(int32, float)> CellFunction)
{
	if (Radius <= 0.0f || Cells.IsEmpty() || GridDiameterBlocks <= 0 || GridHeightBlocks <= 0)
	{
		return 0;
	}
	const FVector LocalImpact = GetActorTransform().InverseTransformPosition(WorldLocation);
	const FVector Scale = GetActorScale3D().GetAbs();
	const FVector SafeScale(
		FMath::Max(Scale.X, KINDA_SMALL_NUMBER),
		FMath::Max(Scale.Y, KINDA_SMALL_NUMBER),
		FMath::Max(Scale.Z, KINDA_SMALL_NUMBER));
	const FVector LocalRadius(
		Radius / SafeScale.X,
		Radius / SafeScale.Y,
		Radius / SafeScale.Z);
	const FVector Pitch = BlockDimensions + BlockSpacing;
	const int32 MinX = FMath::Clamp(FMath::FloorToInt(
										(LocalImpact.X - LocalRadius.X - ConeGridLocalMin.X) / Pitch.X),
									0, GridDiameterBlocks - 1);
	const int32 MaxX = FMath::Clamp(FMath::FloorToInt(
										(LocalImpact.X + LocalRadius.X - ConeGridLocalMin.X) / Pitch.X),
									0, GridDiameterBlocks - 1);
	const int32 MinY = FMath::Clamp(FMath::FloorToInt(
										(LocalImpact.Y - LocalRadius.Y - ConeGridLocalMin.Y) / Pitch.Y),
									0, GridDiameterBlocks - 1);
	const int32 MaxY = FMath::Clamp(FMath::FloorToInt(
										(LocalImpact.Y + LocalRadius.Y - ConeGridLocalMin.Y) / Pitch.Y),
									0, GridDiameterBlocks - 1);
	const int32 MinZ = FMath::Clamp(FMath::FloorToInt(
										(LocalImpact.Z - LocalRadius.Z) / Pitch.Z),
									0, GridHeightBlocks - 1);
	const int32 MaxZ = FMath::Clamp(FMath::FloorToInt(
										(LocalImpact.Z + LocalRadius.Z) / Pitch.Z),
									0, GridHeightBlocks - 1);
	int32 SelectedCount = 0;
	AddPanelsInRadius(WorldLocation, Radius, Shape, CellFunction, SelectedCount);
	for (int32 Z = MinZ; Z <= MaxZ; ++Z)
	{
		if (!PhysicalCellsByLayer.IsValidIndex(Z))
		{
			continue;
		}
		const TArray<FIntPoint> &LayerCells = PhysicalCellsByLayer[Z];
		for (int32 Slot = 0; Slot < LayerCells.Num(); ++Slot)
		{
			const FIntPoint PhysicalCell = LayerCells[Slot];
			if (PhysicalCell.X < MinX || PhysicalCell.X > MaxX ||
				PhysicalCell.Y < MinY || PhysicalCell.Y > MaxY)
			{
				continue;
			}
			const FIntPoint Cell(Slot, Z);
			const int32 CellIndex = ToCellIndex(Cell);
			if (!Cells.IsValidIndex(CellIndex) || !Cells[CellIndex].bExists)
			{
				continue;
			}
			const FVector Center = GetPhysicalCellLocalCenter(
										   PhysicalCell.X, PhysicalCell.Y, Z);
			const FVector Point = Shape == EChunkedFloorBlastShape::Sphere
				? FBox(Center-BlockDimensions*.5, Center+BlockDimensions*.5).GetClosestPointTo(LocalImpact) : Center;
			const FVector LocalDelta = Point - LocalImpact;
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
	}
	return SelectedCount;
}

void AQuadPanelConeStructure::MarkCellChunksDirty(FIntPoint Cell)
{
	if (IsPanelCell(Cell)) { DirtyChunkIndices.Add(PanelChunk(Cell)); return; }

	int32 X;
	int32 Y;
	int32 Z;
	if (!DecodeCell(Cell, X, Y, Z))
	{
		return;
	}
	auto MarkPhysicalCellChunk = [this](int32 CellX, int32 CellY, int32 CellZ)
	{
		const int32 ChunkIndex = GetSpatialChunkIndex(CellX, CellY, CellZ);
		if (ChunkIndex == INDEX_NONE)
		{
			return;
		}
		DirtyChunkIndices.Add(ChunkIndex);
	};
	MarkPhysicalCellChunk(X, Y, Z);

	struct FNeighborDependency
	{
		FIntVector Offset;
		bool bHasSpacing;
	};
	const FNeighborDependency Dependencies[] = {
		{FIntVector(-1, 0, 0), BlockSpacing.X > QuadConeSpacingTolerance},
		{FIntVector(1, 0, 0), BlockSpacing.X > QuadConeSpacingTolerance},
		{FIntVector(0, -1, 0), BlockSpacing.Y > QuadConeSpacingTolerance},
		{FIntVector(0, 1, 0), BlockSpacing.Y > QuadConeSpacingTolerance},
		{FIntVector(0, 0, -1), BlockSpacing.Z > QuadConeSpacingTolerance},
		{FIntVector(0, 0, 1), BlockSpacing.Z > QuadConeSpacingTolerance}};
	for (const FNeighborDependency &Dependency : Dependencies)
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
			const FFloorCellData &NeighborData = Cells[ToCellIndex(Neighbor)];
			if (NeighborData.bBelongsToGeneratedShape && !NeighborData.bExists)
			{
				MarkPhysicalCellChunk(
					X + Dependency.Offset.X,
					Y + Dependency.Offset.Y,
					Z + Dependency.Offset.Z);
			}
		}
	}
}

bool AQuadPanelConeStructure::IsCharacterStandingOnPlatform(
	const ACharacter *Character) const
{
	if (!Character || !bGeneratePlatforms)
	{
		return false;
	}
	const UCharacterMovementComponent *Movement = Character->GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround() || !Movement->CurrentFloor.IsWalkableFloor())
	{
		return false;
	}
	const FHitResult &FloorHit = Movement->CurrentFloor.HitResult;
	const UPrimitiveComponent *FloorComponent = FloorHit.GetComponent();
	if (FloorHit.GetActor() != this && (!FloorComponent || FloorComponent->GetOwner() != this))
	{
		return false;
	}

	FIntPoint FloorCell;
	if (!ResolveExistingCellFromHit(FloorHit, FloorCell))
	{
		return false;
	}
	int32 X;
	int32 Y;
	int32 Z;
	return DecodeCell(FloorCell, X, Y, Z) && IsPlatformCell(X, Y, Z);
}

void AQuadPanelConeStructure::StartCharacterSpeedBoostMonitoring()
{
	if (!bBoostCharacterWalkSpeed || !GetWorld())
	{
		return;
	}
	CharacterWalkSpeedMultiplier = FMath::Clamp(CharacterWalkSpeedMultiplier, 1.0f, 20.0f);
	if (!GetWorld()->GetTimerManager().IsTimerActive(CharacterSpeedBoostTimerHandle))
	{
		GetWorld()->GetTimerManager().SetTimer(
			CharacterSpeedBoostTimerHandle,
			this,
			&AQuadPanelConeStructure::RefreshCharacterSpeedBoosts,
			0.05f,
			true);
	}
	RefreshCharacterSpeedBoosts();
}

void AQuadPanelConeStructure::RefreshCharacterSpeedBoosts()
{
	UWorld *World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!bBoostCharacterWalkSpeed)
	{
		StopCharacterSpeedBoostMonitoring();
		return;
	}

	const float SafeMultiplier = FMath::Clamp(CharacterWalkSpeedMultiplier, 1.0f, 20.0f);
	TSet<TWeakObjectPtr<ACharacter>> CharactersOnPlatforms;
	for (TActorIterator<ACharacter> CharacterIt(World); CharacterIt; ++CharacterIt)
	{
		ACharacter *Character = *CharacterIt;
		if (!IsCharacterStandingOnPlatform(Character))
		{
			continue;
		}
		UCharacterMovementComponent *Movement = Character->GetCharacterMovement();
		if (!Movement)
		{
			continue;
		}

		const TWeakObjectPtr<ACharacter> WeakCharacter(Character);
		CharactersOnPlatforms.Add(WeakCharacter);
		float *BaseSpeed = CharacterBaseWalkSpeeds.Find(WeakCharacter);
		float *AppliedSpeed = CharacterAppliedWalkSpeeds.Find(WeakCharacter);
		if (!BaseSpeed)
		{
			BaseSpeed = &CharacterBaseWalkSpeeds.Add(WeakCharacter, Movement->MaxWalkSpeed);
		}
		else if (AppliedSpeed &&
				 !FMath::IsNearlyEqual(Movement->MaxWalkSpeed, *AppliedSpeed, 0.1f))
		{
			// Respect sprinting or any other system that changes the character's speed while boosted.
			*BaseSpeed = Movement->MaxWalkSpeed;
		}

		const float NewAppliedSpeed = FMath::Max(*BaseSpeed * SafeMultiplier, 0.0f);
		Movement->MaxWalkSpeed = NewAppliedSpeed;
		CharacterAppliedWalkSpeeds.Add(WeakCharacter, NewAppliedSpeed);
	}

	TArray<TWeakObjectPtr<ACharacter>> CharactersToRestore;
	for (const TPair<TWeakObjectPtr<ACharacter>, float> &Pair : CharacterBaseWalkSpeeds)
	{
		if (!Pair.Key.IsValid() || !CharactersOnPlatforms.Contains(Pair.Key))
		{
			CharactersToRestore.Add(Pair.Key);
		}
	}
	for (const TWeakObjectPtr<ACharacter> Character : CharactersToRestore)
	{
		RestoreCharacterWalkSpeed(Character);
	}
}

void AQuadPanelConeStructure::RestoreCharacterWalkSpeed(
	TWeakObjectPtr<ACharacter> Character)
{
	const float *BaseSpeed = CharacterBaseWalkSpeeds.Find(Character);
	const float *AppliedSpeed = CharacterAppliedWalkSpeeds.Find(Character);
	if (BaseSpeed)
	{
		if (ACharacter *CharacterPtr = Character.Get())
		{
			if (UCharacterMovementComponent *Movement = CharacterPtr->GetCharacterMovement())
			{
				if (!AppliedSpeed ||
					FMath::IsNearlyEqual(Movement->MaxWalkSpeed, *AppliedSpeed, 0.1f))
				{
					Movement->MaxWalkSpeed = *BaseSpeed;
				}
			}
		}
	}
	CharacterBaseWalkSpeeds.Remove(Character);
	CharacterAppliedWalkSpeeds.Remove(Character);
}

void AQuadPanelConeStructure::StopCharacterSpeedBoostMonitoring()
{
	if (UWorld *World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CharacterSpeedBoostTimerHandle);
	}
	TArray<TWeakObjectPtr<ACharacter>> CharactersToRestore;
	CharacterBaseWalkSpeeds.GenerateKeyArray(CharactersToRestore);
	for (const TWeakObjectPtr<ACharacter> Character : CharactersToRestore)
	{
		RestoreCharacterWalkSpeed(Character);
	}
	CharacterBaseWalkSpeeds.Reset();
	CharacterAppliedWalkSpeeds.Reset();
}

void AQuadPanelConeStructure::CreateConeAnimationComponent()
{
	if (ConeAnimationComponent || !GetRootComponent())
	{
		return;
	}

	const FName ComponentName = MakeUniqueObjectName(
		this, UDynamicMeshComponent::StaticClass(), TEXT("ConeMaterialAnimation"));
	ConeAnimationComponent = NewObject<UDynamicMeshComponent>(this, ComponentName, RF_Transient);
	AddInstanceComponent(ConeAnimationComponent);
	ConeAnimationComponent->SetupAttachment(GetRootComponent());
	ConeAnimationComponent->SetRelativeTransform(FTransform::Identity);
	ConeAnimationComponent->SetMobility(EComponentMobility::Movable);
	ConeAnimationComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ConeAnimationComponent->SetGenerateOverlapEvents(false);
	ConeAnimationComponent->SetCastShadow(false);
	ConeAnimationComponent->SetCanEverAffectNavigation(false);
	ConeAnimationComponent->SetTranslucentSortPriority(10);
	ConeAnimationComponent->SetNumMaterials(1);
	ConeAnimationComponent->RegisterComponent();

	UDynamicMesh *AnimationMesh = NewObject<UDynamicMesh>(ConeAnimationComponent);
	ConeAnimationComponent->SetDynamicMesh(AnimationMesh);
	ConeAnimationMaterialInstance = CreateFixedAnimationMaterialInstance();
	if (ConeAnimationMaterialInstance)
	{
		ConeAnimationComponent->SetMaterial(0, ConeAnimationMaterialInstance);
	}
	else
	{
		ConeAnimationComponent->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
	}
}

void AQuadPanelConeStructure::CreateSineRowAnimationComponents()
{
	if (!ConeAnimationRowComponents.IsEmpty() || !GetRootComponent())
	{
		return;
	}
	const int32 ComponentCount = CalculateFixedSineComponentCount(GridHeightBlocks);
	ConeAnimationRowComponents.Reserve(ComponentCount);
	ConeAnimationRowMaterialInstances.Reserve(ComponentCount);
	ConeAnimationRowAssignments.Init(INDEX_NONE, ComponentCount);
	for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
	{
		const FName BaseName(*FString::Printf(TEXT("ConeSineRowAnimation_%d"), ComponentIndex));
		const FName ComponentName = MakeUniqueObjectName(
			this, UDynamicMeshComponent::StaticClass(), BaseName);
		UDynamicMeshComponent *RowComponent = NewObject<UDynamicMeshComponent>(
			this, ComponentName, RF_Transient);
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
		UMaterialInstanceDynamic *RowMaterial = CreateFixedAnimationMaterialInstance();
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
		ConeAnimationRowComponents.Add(RowComponent);
		ConeAnimationRowMaterialInstances.Add(RowMaterial);
	}
	bConeAnimationGeometryDirty = true;
}

void AQuadPanelConeStructure::RenderFixedAnimation(
	TConstArrayView<float> AnimationAlphas)
{
	if (AnimationAlphas.IsEmpty())
	{
		if (ConeAnimationComponent)
		{
			ConeAnimationComponent->SetVisibility(false, true);
		}
		for (UDynamicMeshComponent *RowComponent : ConeAnimationRowComponents)
		{
			if (RowComponent)
			{
				RowComponent->SetVisibility(false, true);
			}
		}
		return;
	}

	switch (ConeAnimationType)
	{
	case EConeStructureAnimationType::BottomToTopGlowWave:
		UpdateBottomToTopGlowWaves(AnimationAlphas);
		break;
	case EConeStructureAnimationType::BottomToTopSineRows:
		UpdateBottomToTopSineRows(AnimationAlphas);
		break;
	case EConeStructureAnimationType::RisingSpiralGlow:
		UpdateRisingSpiralGlowWaves(AnimationAlphas);
		break;
	case EConeStructureAnimationType::None:
	default:
		break;
	}
}

void AQuadPanelConeStructure::UpdateBottomToTopGlowWaves(
	TConstArrayView<float> AnimationAlphas)
{
	if (!ConeAnimationComponent || !ConeAnimationComponent->GetDynamicMesh())
	{
		return;
	}
	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
							  BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	const float BandHeight = FMath::Max(GlowWaveBandHeightBlocks * PitchZ, 1.0f);
	FDynamicMesh3 AnimationMesh;
	AnimationMesh.EnableVertexNormals(FVector3f::UnitZ());
	AnimationMesh.EnableAttributes();
	AnimationMesh.Attributes()->SetNumUVLayers(1);
	AnimationMesh.Attributes()->EnableMaterialID();
	for (const float AnimationAlpha : AnimationAlphas)
	{
		const float BandTop = FMath::Lerp(
			0.0f,
			TotalHeight + BandHeight,
			FMath::Clamp(AnimationAlpha, 0.0f, 1.0f));
		const float BandBottom = BandTop - BandHeight;
		AppendConeGlowBandSurfaces(AnimationMesh, BandBottom, BandTop);
	}
	const bool bHasVisibleWave = AnimationMesh.TriangleCount() > 0;
	ConeAnimationComponent->GetDynamicMesh()->SetMesh(MoveTemp(AnimationMesh));
	ConeAnimationComponent->NotifyMeshUpdated();
	ConeAnimationComponent->SetVisibility(bHasVisibleWave, true);
	ConeAnimationComponent->MarkRenderStateDirty();
}

void AQuadPanelConeStructure::UpdateRisingSpiralGlowWaves(
	TConstArrayView<float> AnimationAlphas)
{
	if (!ConeAnimationComponent || !ConeAnimationComponent->GetDynamicMesh() ||
		AnimationAlphas.IsEmpty())
	{
		return;
	}

	const int32 StrandCount = FMath::Clamp(SpiralGlowStrandCount, 1, 8);
	const float WindingDirection = bReverseSpiralGlowDirection ? -1.0f : 1.0f;
	const float PhaseOffset = -WindingDirection * static_cast<float>(UE_TWO_PI) *
							  FMath::Clamp(AnimationAlphas[0], 0.0f, 1.0f) / StrandCount;
	FDynamicMesh3 AnimationMesh;
	AnimationMesh.EnableVertexNormals(FVector3f::UnitZ());
	AnimationMesh.EnableAttributes();
	AnimationMesh.Attributes()->SetNumUVLayers(1);
	AnimationMesh.Attributes()->EnableMaterialID();
	AppendConeSpiralGlowSurfaces(AnimationMesh, PhaseOffset);
	const bool bHasVisibleSpiral = AnimationMesh.TriangleCount() > 0;
	ConeAnimationComponent->GetDynamicMesh()->SetMesh(MoveTemp(AnimationMesh));
	ConeAnimationComponent->NotifyMeshUpdated();
	ConeAnimationComponent->SetRelativeRotation(FRotator::ZeroRotator);
	ConeAnimationComponent->SetVisibility(bHasVisibleSpiral, true);
	ConeAnimationComponent->MarkRenderStateDirty();
	bConeAnimationGeometryDirty = false;
}

void AQuadPanelConeStructure::UpdateBottomToTopSineRows(
	TConstArrayView<float> AnimationAlphas)
{
	const int32 ComponentCount = ConeAnimationRowComponents.Num();
	if (ComponentCount <= 0 || GridHeightBlocks <= 0)
	{
		return;
	}
	if (bConeAnimationGeometryDirty || ConeAnimationRowAssignments.Num() != ComponentCount)
	{
		ConeAnimationRowAssignments.Init(INDEX_NONE, ComponentCount);
		bConeAnimationGeometryDirty = false;
	}
	for (int32 ComponentIndex = 0; ComponentIndex < ComponentCount; ++ComponentIndex)
	{
		if (UDynamicMeshComponent *Component = ConeAnimationRowComponents[ComponentIndex])
		{
			Component->SetVisibility(false, true);
		}
		if (ConeAnimationRowMaterialInstances.IsValidIndex(ComponentIndex))
		{
			ApplyFixedAnimationMaterialParameters(
				ConeAnimationRowMaterialInstances[ComponentIndex], 0.0f);
		}
	}

	const int32 RowsPerWave = FMath::Clamp(
		SineRowsSimultaneousRowCount,
		2,
		FMath::Max(GridHeightBlocks, 2));
	const float TravelSpan = FMath::Max(
		static_cast<float>(GridHeightBlocks - 1 + RowsPerWave), 1.0f);
	TArray<float> RowIntensities;
	RowIntensities.Init(0.0f, GridHeightBlocks);
	for (const float AnimationAlpha : AnimationAlphas)
	{
		const float WaveHead = FMath::Clamp(AnimationAlpha, 0.0f, 1.0f) * TravelSpan;
		const int32 LatestStartedRow = FMath::FloorToInt(WaveHead);
		const int32 FirstActiveRow = FMath::Max(LatestStartedRow - RowsPerWave + 1, 0);
		const int32 LastActiveRow = FMath::Min(LatestStartedRow, GridHeightBlocks - 1);
		for (int32 RowIndex = FirstActiveRow; RowIndex <= LastActiveRow; ++RowIndex)
		{
			const float RowAge = WaveHead - RowIndex;
			if (RowAge < 0.0f || RowAge > RowsPerWave)
			{
				continue;
			}
			const float SinePhase = FMath::Clamp(RowAge / RowsPerWave, 0.0f, 1.0f);
			const float WaveIntensity = FMath::Sin(UE_PI * SinePhase);
			if (WaveIntensity > KINDA_SMALL_NUMBER)
			{
				// Screen blending lets intersecting waves reinforce each other without
				// allowing the material intensity to exceed its configured maximum.
				const float ExistingIntensity = RowIntensities[RowIndex];
				RowIntensities[RowIndex] = 1.0f -
										   (1.0f - ExistingIntensity) * (1.0f - WaveIntensity);
			}
		}
	}

	TArray<int32> ActiveRows;
	TArray<bool> ActiveRowMask;
	ActiveRowMask.Init(false, GridHeightBlocks);
	for (int32 RowIndex = 0; RowIndex < GridHeightBlocks; ++RowIndex)
	{
		if (RowIntensities[RowIndex] > KINDA_SMALL_NUMBER)
		{
			ActiveRows.Add(RowIndex);
			ActiveRowMask[RowIndex] = true;
		}
	}

	const float PitchZ = BlockDimensions.Z + BlockSpacing.Z;
	const float TotalHeight = BlockDimensions.Z * GridHeightBlocks +
							  BlockSpacing.Z * FMath::Max(GridHeightBlocks - 1, 0);
	TArray<bool> UsedComponents;
	UsedComponents.Init(false, ComponentCount);
	for (const int32 RowIndex : ActiveRows)
	{
		int32 ComponentIndex = ConeAnimationRowAssignments.Find(RowIndex);
		if (!ConeAnimationRowComponents.IsValidIndex(ComponentIndex) ||
			!ConeAnimationRowComponents[ComponentIndex] || UsedComponents[ComponentIndex])
		{
			ComponentIndex = INDEX_NONE;
		}
		if (ComponentIndex == INDEX_NONE)
		{
			for (int32 CandidateIndex = 0; CandidateIndex < ComponentCount; ++CandidateIndex)
			{
				if (UsedComponents[CandidateIndex] ||
					!ConeAnimationRowComponents.IsValidIndex(CandidateIndex) ||
					!ConeAnimationRowComponents[CandidateIndex])
				{
					continue;
				}
				const int32 AssignedRow = ConeAnimationRowAssignments[CandidateIndex];
				if (AssignedRow == INDEX_NONE || !ActiveRowMask.IsValidIndex(AssignedRow) ||
					!ActiveRowMask[AssignedRow])
				{
					ComponentIndex = CandidateIndex;
					break;
				}
			}
		}
		if (ComponentIndex == INDEX_NONE)
		{
			for (int32 CandidateIndex = 0; CandidateIndex < ComponentCount; ++CandidateIndex)
			{
				if (!UsedComponents[CandidateIndex] &&
					ConeAnimationRowComponents.IsValidIndex(CandidateIndex) &&
					ConeAnimationRowComponents[CandidateIndex])
				{
					ComponentIndex = CandidateIndex;
					break;
				}
			}
		}
		if (!ConeAnimationRowComponents.IsValidIndex(ComponentIndex) ||
			!ConeAnimationRowComponents[ComponentIndex] ||
			!ConeAnimationRowComponents[ComponentIndex]->GetDynamicMesh())
		{
			continue;
		}
		UsedComponents[ComponentIndex] = true;
		UDynamicMeshComponent *RowComponent = ConeAnimationRowComponents[ComponentIndex];
		if (!ConeAnimationRowAssignments.IsValidIndex(ComponentIndex) ||
			ConeAnimationRowAssignments[ComponentIndex] != RowIndex)
		{
			FDynamicMesh3 RowMesh;
			RowMesh.EnableVertexNormals(FVector3f::UnitZ());
			RowMesh.EnableAttributes();
			RowMesh.Attributes()->SetNumUVLayers(1);
			RowMesh.Attributes()->EnableMaterialID();
			const float RowBottom = RowIndex * PitchZ;
			const float RowTop = FMath::Min(RowBottom + PitchZ, TotalHeight);
			AppendConeGlowBandSurfaces(RowMesh, RowBottom, RowTop);
			RowComponent->GetDynamicMesh()->SetMesh(MoveTemp(RowMesh));
			RowComponent->NotifyMeshUpdated();
			RowComponent->MarkRenderStateDirty();
			ConeAnimationRowAssignments[ComponentIndex] = RowIndex;
		}
		if (ConeAnimationRowMaterialInstances.IsValidIndex(ComponentIndex))
		{
			ApplyFixedAnimationMaterialParameters(
				ConeAnimationRowMaterialInstances[ComponentIndex], RowIntensities[RowIndex]);
		}
		RowComponent->SetVisibility(true, true);
	}
}

int32 AQuadPanelConeStructure::GetEffectiveConeChunkSideLength() const
{
	return FMath::Clamp(SpatialChunkSideLength, 4, 16);
}

int32 AQuadPanelConeStructure::GetEffectiveCircularSegments() const
{
	return FMath::Min(
		FMath::Clamp(CircularSegments, 32, 512),
		FMath::Clamp(MaxRuntimeCircularSegments, 32, 512));
}

int32 AQuadPanelConeStructure::GetEffectiveDebrisLimit() const
{
	const int32 ConfiguredLimit = FMath::Min(
		FMath::Clamp(MaxActiveDebrisPieces, 0, 1024),
		FMath::Clamp(MaxRuntimeDebrisPieces, 0, 1024));
	return bUseLightweightDebris ? FMath::Min(ConfiguredLimit, 24) : ConfiguredLimit;
}

bool AQuadPanelConeStructure::CanUseSmoothCollisionForChunk(int32 ChunkIndex) const
{
	const bool bNeedsSegmentCollision = bGeneratePlatforms &&
										PlatformStyle == EConeStructurePlatformStyle::RisingSpiral &&
										SpiralDestructionMode != EConeStructureSpiralDestructionMode::ExistingCellDestruction;
	if (!ChunkComponents.IsValidIndex(ChunkIndex) ||
		!ChunkComponents[ChunkIndex] || !ChunkComponents[ChunkIndex]->GetDynamicMesh())
	{
		return false;
	}
	return ChunkComponents[ChunkIndex]->GetDynamicMesh()->GetTriangleCount() <=
		   FMath::Clamp(MaxSmoothCollisionTrianglesPerChunk, 10000, 240000);
}

UMaterialInterface *AQuadPanelConeStructure::ResolveStructureMaterial(bool bPlatform) const
{
	UMaterialInterface *Material = bPlatform ? PlatformMaterial.Get() : ConeMaterial.Get();
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

void AQuadPanelConeStructure::HandleConeCellDestroyed(
	FIntPoint Cell,
	FVector WorldLocation,
	bool bRadialDestruction)
{
	bConeAnimationGeometryDirty = true;
	if (bScatterDestroyedPieces)
	{
		SpawnDebrisForCell(Cell);
	}
}

void AQuadPanelConeStructure::SpawnDebrisForCell(FIntPoint Cell)
{
	int32 X, Y, Z;
	if (!DecodeCell(Cell, X, Y, Z)) return;
	SpawnDebrisAtLocalPoint(GetCellFlashTransform(Cell).GetLocation(), IsPlatformCell(X, Y, Z));
}

void AQuadPanelConeStructure::SpawnDebrisAtLocalPoint(const FVector& LocalCenter, bool bPlatformCell)
{
	const int32 EffectiveDebrisLimit = GetEffectiveDebrisLimit();
	if (!DebrisCubeMesh || EffectiveDebrisLimit <= 0 || !GetWorld())
	{
		return;
	}
	ActiveDebrisComponents.RemoveAll(
		[](const TWeakObjectPtr<UStaticMeshComponent> &Component)
		{
			return !Component.IsValid();
		});
	if (ActiveDebrisComponents.Num() >= EffectiveDebrisLimit)
	{
		return;
	}

	UMaterialInterface *DebrisMaterial = ResolveStructureMaterial(bPlatformCell);
	const float MinimumScale = FMath::Clamp(
		static_cast<float>(FMath::Min(DebrisScaleRange.X, DebrisScaleRange.Y)), 0.05f, 1.0f);
	const float MaximumScale = FMath::Clamp(
		static_cast<float>(FMath::Max(DebrisScaleRange.X, DebrisScaleRange.Y)), MinimumScale, 1.0f);
	const int32 RequestedSpawnCount = bUseLightweightDebris
										  ? 1
										  : FMath::Clamp(DebrisPiecesPerCell, 1, 8);
	const int32 SpawnCount = FMath::Min(
		RequestedSpawnCount,
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

		UStaticMeshComponent *Debris = NewObject<UStaticMeshComponent>(this);
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
			ScatterDirection * DebrisImpulse + FVector::UpVector * DebrisUpwardImpulse
				+ GetDebrisBlastImpulse(FragmentWorldLocation),
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
		const float RequestedPhysicsDuration = bUseLightweightDebris
												   ? FMath::Min(DebrisPhysicsDuration, 0.35f)
												   : DebrisPhysicsDuration;
		const float SafePhysicsDuration = FMath::Clamp(
			RequestedPhysicsDuration, 0.05f, SafeDebrisLifetime);
		FTimerDelegate CleanupDelegate = FTimerDelegate::CreateWeakLambda(
			this,
			[this, WeakDebris]()
			{
				if (UStaticMeshComponent *Component = WeakDebris.Get())
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
			SafePhysicsDuration,
			false);
	}
}

void AQuadPanelConeStructure::ClearDebris()
{
	for (const TWeakObjectPtr<UStaticMeshComponent> &WeakDebris : ActiveDebrisComponents)
	{
		if (UStaticMeshComponent *Debris = WeakDebris.Get())
		{
			RemoveInstanceComponent(Debris);
			Debris->DestroyComponent();
		}
	}
	ActiveDebrisComponents.Reset();
}

float AQuadPanelConeStructure::GetSpiralRimHeight(float TopZ) const
{
    if (!bSpiralOuterRim) return 0;
    const float Height=BlockDimensions.Z*GridHeightBlocks+BlockSpacing.Z*FMath::Max(GridHeightBlocks-1,0);
    return FMath::Min(SpiralRimHeight,FMath::Max(0.f,Height-TopZ));
}
