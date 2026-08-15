// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParametricSpiralRamp.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GeometryScript/GeometryScriptSelectionTypes.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshUVFunctions.h"
#include "Materials/MaterialInterface.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

AParametricSpiralRamp::AParametricSpiralRamp(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	if (UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent())
	{
		MeshComponent->SetMobility(EComponentMobility::Movable);
		MeshComponent->SetGenerateOverlapEvents(false);
		MeshComponent->SetCanEverAffectNavigation(false);
		MeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultRoadMaterialFinder(
		TEXT("/Game/LevelPrototyping/Materials/MI_PrototypeGrid_Gray.MI_PrototypeGrid_Gray"));
	if (DefaultRoadMaterialFinder.Succeeded())
	{
		RoadMaterial = DefaultRoadMaterialFinder.Object;
	}
}

void AParametricSpiralRamp::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRamp();
}

void AParametricSpiralRamp::GetSafeSettings(
	float& OutRadius,
	float& OutWidth,
	float& OutTurns,
	float& OutRise,
	float& OutThickness,
	int32& OutSegments) const
{
	OutWidth = FMath::Max(FMath::Abs(RoadWidth), 10.0f);
	// Keep the inner edge away from the revolve axis so the generated solid cannot self-intersect.
	OutRadius = FMath::Max(FMath::Abs(CenterRadius), OutWidth * 0.5f + MinimumRampDimension);
	OutTurns = FMath::Clamp(FMath::Abs(TurnCount), 0.05f, 20.0f);
	OutRise = FMath::Max(TotalRise, 0.0f);
	OutThickness = FMath::Max(FMath::Abs(RoadThickness), MinimumRampDimension);
	OutSegments = FMath::Clamp(
		FMath::CeilToInt(OutTurns * FMath::Clamp(SegmentsPerTurn, 8, 256)),
		2,
		4096);
}

void AParametricSpiralRamp::RebuildRamp()
{
	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	UDynamicMesh* RampMesh = MeshComponent ? MeshComponent->GetDynamicMesh() : nullptr;
	if (!RampMesh)
	{
		return;
	}

	float SafeRadius;
	float SafeWidth;
	float SafeTurns;
	float SafeRise;
	float SafeThickness;
	int32 SafeSegments;
	GetSafeSettings(
		SafeRadius,
		SafeWidth,
		SafeTurns,
		SafeRise,
		SafeThickness,
		SafeSegments);

	RampMesh->Reset();

	// Geometry Script interprets profile X as radial/outward and profile Y as vertical. This
	// counter-clockwise rectangle places the walkable surface at Z=0 and its thickness below it.
	const float HalfWidth = SafeWidth * 0.5f;
	const TArray<FVector2D> RoadProfile = {
		FVector2D(-HalfWidth, -SafeThickness),
		FVector2D(HalfWidth, -SafeThickness),
		FVector2D(HalfWidth, 0.0),
		FVector2D(-HalfWidth, 0.0)
	};

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;
	PrimitiveOptions.UVMode = EGeometryScriptPrimitiveUVMode::Uniform;
	PrimitiveOptions.MaterialID = 0;

	FGeometryScriptRevolveOptions RevolveOptions;
	RevolveOptions.RevolveDegrees = SafeTurns * 360.0f;
	RevolveOptions.DegreeOffset = StartAngleDegrees;
	RevolveOptions.bReverseDirection = bClockwise;
	RevolveOptions.bHardNormals = true;
	RevolveOptions.HardNormalAngle = 45.0f;
	RevolveOptions.bProfileAtMidpoint = false;
	RevolveOptions.bFillPartialRevolveEndcaps = bCapRoadEnds;

	const float RisePerTurn = SafeRise / SafeTurns;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSpiralRevolvePolygon(
		RampMesh,
		PrimitiveOptions,
		FTransform::Identity,
		RoadProfile,
		RevolveOptions,
		SafeRadius,
		SafeSegments,
		RisePerTurn);

	FGeometryScriptMeshSelection EmptySelection;
	UGeometryScriptLibrary_MeshUVFunctions::ScaleMeshUVs(
		RampMesh,
		0,
		FVector2D(FMath::Max(UVTiling.X, 0.01), FMath::Max(UVTiling.Y, 0.01)),
		FVector2D::ZeroVector,
		EmptySelection);

	MeshComponent->SetMaterial(0, RoadMaterial);
	MeshComponent->SetCastShadow(bCastRoadShadow);
	MeshComponent->SetCanEverAffectNavigation(bEnableCollision && bAffectNavigation);
	MeshComponent->SetCollisionEnabled(
		bEnableCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	MeshComponent->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
	MeshComponent->NotifyMeshUpdated();
}

void AParametricSpiralRamp::ConfigureRamp(
	float NewCenterRadius,
	float NewRoadWidth,
	float NewTurnCount,
	float NewTotalRise,
	float NewRoadThickness)
{
	CenterRadius = NewCenterRadius;
	RoadWidth = NewRoadWidth;
	TurnCount = NewTurnCount;
	TotalRise = NewTotalRise;
	RoadThickness = NewRoadThickness;
	RebuildRamp();
}

FParametricSpiralRampMetrics AParametricSpiralRamp::GetRampMetrics() const
{
	float SafeRadius;
	float SafeWidth;
	float SafeTurns;
	float SafeRise;
	float SafeThickness;
	int32 SafeSegments;
	GetSafeSettings(
		SafeRadius,
		SafeWidth,
		SafeTurns,
		SafeRise,
		SafeThickness,
		SafeSegments);

	FParametricSpiralRampMetrics Metrics;
	Metrics.InnerRadius = SafeRadius - SafeWidth * 0.5f;
	Metrics.OuterRadius = SafeRadius + SafeWidth * 0.5f;
	Metrics.HorizontalArcLength = UE_TWO_PI * SafeRadius * SafeTurns;
	Metrics.SurfaceLength = FMath::Sqrt(
		FMath::Square(Metrics.HorizontalArcLength) + FMath::Square(SafeRise));
	Metrics.SlopeAngleDegrees = FMath::RadiansToDegrees(FMath::Atan2(
		SafeRise,
		FMath::Max(Metrics.HorizontalArcLength, MinimumRampDimension)));
	Metrics.EndHeight = SafeRise;
	Metrics.SegmentCount = SafeSegments;
	return Metrics;
}

FTransform AParametricSpiralRamp::GetEndLocalTransform() const
{
	float SafeRadius;
	float SafeWidth;
	float SafeTurns;
	float SafeRise;
	float SafeThickness;
	int32 SafeSegments;
	GetSafeSettings(
		SafeRadius,
		SafeWidth,
		SafeTurns,
		SafeRise,
		SafeThickness,
		SafeSegments);

	const float DirectionSign = bClockwise ? -1.0f : 1.0f;
	const float EndAngleDegrees = StartAngleDegrees + DirectionSign * SafeTurns * 360.0f;
	const float EndAngleRadians = FMath::DegreesToRadians(EndAngleDegrees);
	const FVector EndLocation(
		FMath::Cos(EndAngleRadians) * SafeRadius,
		FMath::Sin(EndAngleRadians) * SafeRadius,
		SafeRise);
	const float TangentYaw = EndAngleDegrees + DirectionSign * 90.0f;
	return FTransform(FRotator(0.0f, TangentYaw, 0.0f), EndLocation);
}
