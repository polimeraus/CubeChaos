// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleHouseActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInterface.h"
#include "UDynamicMesh.h"

ASimpleHouseActor::ASimpleHouseActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetGenerateOverlapEvents(false);
}

void ASimpleHouseActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildMesh();
}

void ASimpleHouseActor::RebuildMesh()
{
	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	UDynamicMesh* HouseMesh = MeshComponent ? MeshComponent->GetDynamicMesh() : nullptr;
	if (!HouseMesh)
	{
		return;
	}

	const float SafeWidth = FMath::Max(Width, 10.0f);
	const float SafeDepth = FMath::Max(Depth, 10.0f);
	const float SafeWallHeight = FMath::Max(WallHeight, 10.0f);
	const float SafeRoofHeight = FMath::Max(RoofHeight, 10.0f);
	const float MaxThickness = 0.45f * FMath::Min3(SafeWidth, SafeDepth, SafeWallHeight);
	const float SafeThickness = FMath::Clamp(WallThickness, 2.0f, MaxThickness);

	HouseMesh->Reset();

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	// The 2D silhouette is extruded along local Z. A -90 degree roll maps profile Y
	// to actor Z and extrusion Z to actor -Y, producing a classic gable-front house.
	// FRotator's constructor order is Pitch, Yaw, Roll, so the roll is the third value.
	const FTransform OuterTransform(FRotator(0.0f, 0.0f, -90.0f), FVector(0.0f, SafeDepth * 0.5f, 0.0f));
	const TArray<FVector2D> OuterProfile = {
		FVector2D(-SafeWidth * 0.5f, 0.0f),
		FVector2D(SafeWidth * 0.5f, 0.0f),
		FVector2D(SafeWidth * 0.5f, SafeWallHeight),
		FVector2D(0.0f, SafeWallHeight + SafeRoofHeight),
		FVector2D(-SafeWidth * 0.5f, SafeWallHeight)
	};
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
		HouseMesh, PrimitiveOptions, OuterTransform, OuterProfile, SafeDepth, 0, true,
		EGeometryScriptPrimitiveOriginMode::Base);

	// Remove a smaller matching prism. This leaves walls, floor and roof as one hollow shell.
	UDynamicMesh* InteriorTool = NewObject<UDynamicMesh>(this);
	const float InnerWidth = SafeWidth - SafeThickness * 2.0f;
	const float InnerDepth = SafeDepth - SafeThickness * 2.0f;
	// Offset both sloping roof planes inward by exactly SafeThickness. Simply lowering
	// their vertices by that amount would make a steep roof progressively thicker.
	const float RoofSlope = (2.0f * SafeRoofHeight) / SafeWidth;
	const float RoofNormalScale = FMath::Sqrt(1.0f + RoofSlope * RoofSlope);
	const float InnerShoulderHeight = SafeWallHeight - SafeThickness * (RoofNormalScale - RoofSlope);
	const float InnerApexHeight = SafeWallHeight + SafeRoofHeight - SafeThickness * RoofNormalScale;
	const TArray<FVector2D> InnerProfile = {
		FVector2D(-InnerWidth * 0.5f, SafeThickness),
		FVector2D(InnerWidth * 0.5f, SafeThickness),
		FVector2D(InnerWidth * 0.5f, InnerShoulderHeight),
		FVector2D(0.0f, InnerApexHeight),
		FVector2D(-InnerWidth * 0.5f, InnerShoulderHeight)
	};
	const FTransform InnerTransform(FRotator(0.0f, 0.0f, -90.0f), FVector(0.0f, SafeDepth * 0.5f - SafeThickness, 0.0f));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
		InteriorTool, PrimitiveOptions, InnerTransform, InnerProfile, InnerDepth, 0, true,
		EGeometryScriptPrimitiveOriginMode::Base);

	FGeometryScriptMeshBooleanOptions BooleanOptions;
	BooleanOptions.bFillHoles = true;
	BooleanOptions.bSimplifyOutput = true;
	BooleanOptions.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		HouseMesh, FTransform::Identity, InteriorTool, FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract, BooleanOptions);

	// Each cutter reaches past the facade and into the hollow interior, so the openings
	// are actual passages rather than dark material patches.
	auto CutFacadeOpening = [&](float CenterX, float CenterZ, float OpeningWidth, float OpeningHeight)
	{
		UDynamicMesh* Cutter = NewObject<UDynamicMesh>(this);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			Cutter, PrimitiveOptions, FTransform::Identity, OpeningWidth,
			SafeThickness * 2.5f, OpeningHeight, 0, 0, 0,
			EGeometryScriptPrimitiveOriginMode::Center);

		const FTransform CutterTransform(FRotator::ZeroRotator,
			FVector(CenterX, -SafeDepth * 0.5f, CenterZ));
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
			HouseMesh, FTransform::Identity, Cutter, CutterTransform,
			EGeometryScriptBooleanOperation::Subtract, BooleanOptions);
	};

	const float SafeDoorWidth = FMath::Clamp(DoorWidth, 10.0f, SafeWidth - SafeThickness * 2.0f);
	const float SafeDoorHeight = FMath::Clamp(DoorHeight, 10.0f, SafeWallHeight - SafeThickness);
	CutFacadeOpening(0.0f, SafeDoorHeight * 0.5f, SafeDoorWidth, SafeDoorHeight);

	const float SafeWindowWidth = FMath::Clamp(WindowWidth, 10.0f, SafeWidth * 0.28f);
	const float SafeWindowHeight = FMath::Clamp(WindowHeight, 10.0f, SafeWallHeight * 0.35f);
	const float WindowCenterZ = SafeWallHeight * 0.63f;
	const float WindowCenterX = FMath::Min(SafeWidth * 0.31f, SafeWidth * 0.5f - SafeWindowWidth * 0.6f - SafeThickness);
	CutFacadeOpening(-WindowCenterX, WindowCenterZ, SafeWindowWidth, SafeWindowHeight);
	CutFacadeOpening(WindowCenterX, WindowCenterZ, SafeWindowWidth, SafeWindowHeight);

	FGeometryScriptSplitNormalsOptions SplitOptions;
	SplitOptions.bSplitByOpeningAngle = true;
	SplitOptions.OpeningAngleDeg = 60.0f;
	FGeometryScriptCalculateNormalsOptions NormalOptions;
	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(HouseMesh, SplitOptions, NormalOptions);

	MeshComponent->SetMaterial(0, Material);
	MeshComponent->NotifyMeshUpdated();
	MeshComponent->SetCollisionEnabled(
		bEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	MeshComponent->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}

void ASimpleHouseActor::CarveSphereAtWorldLocation(FVector WorldLocation, float Radius, float DepthFraction)
{
	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	UDynamicMesh* HouseMesh = MeshComponent ? MeshComponent->GetDynamicMesh() : nullptr;
	if (!HouseMesh)
	{
		return;
	}

	const float SafeRadius = FMath::Max(Radius, 1.0f);
	UDynamicMesh* Cutter = NewObject<UDynamicMesh>(this);
	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
		Cutter,
		PrimitiveOptions,
		FTransform::Identity,
		SafeRadius,
		12,
		24,
		EGeometryScriptPrimitiveOriginMode::Center);

	// Push the cutting sphere toward the approximate centre of the hollow house.
	// This guarantees that a projectile impact crosses the shell instead of only
	// shaving the outer surface, including when the projectile hits the roof.
	const FVector LocalInteriorCentre(0.0f, 0.0f, WallHeight * 0.5f);
	const FVector WorldInteriorCentre = GetActorTransform().TransformPosition(LocalInteriorCentre);
	const FVector InwardDirection = (WorldInteriorCentre - WorldLocation).GetSafeNormal();
	const float SafeDepthFraction = FMath::Clamp(DepthFraction, 0.0f, 1.0f);
	const FVector CutterCentre = WorldLocation + InwardDirection * SafeRadius * SafeDepthFraction;

	FGeometryScriptMeshBooleanOptions BooleanOptions;
	BooleanOptions.bFillHoles = true;
	BooleanOptions.bSimplifyOutput = true;
	BooleanOptions.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		HouseMesh,
		MeshComponent->GetComponentTransform(),
		Cutter,
		FTransform(FRotator::ZeroRotator, CutterCentre),
		EGeometryScriptBooleanOperation::Subtract,
		BooleanOptions);

	FGeometryScriptSplitNormalsOptions SplitOptions;
	SplitOptions.bSplitByOpeningAngle = true;
	SplitOptions.OpeningAngleDeg = 60.0f;
	FGeometryScriptCalculateNormalsOptions NormalOptions;
	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(HouseMesh, SplitOptions, NormalOptions);

	MeshComponent->NotifyMeshUpdated();
	MeshComponent->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}
