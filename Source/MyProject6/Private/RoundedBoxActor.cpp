// Copyright Epic Games, Inc. All Rights Reserved.

#include "RoundedBoxActor.h"

#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInterface.h"
#include "UDynamicMesh.h"

ARoundedBoxActor::ARoundedBoxActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetGenerateOverlapEvents(false);
}

void ARoundedBoxActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildMesh();
}

void ARoundedBoxActor::RebuildMesh()
{
	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	UDynamicMesh* DynamicMesh = MeshComponent ? MeshComponent->GetDynamicMesh() : nullptr;
	if (!DynamicMesh)
	{
		return;
	}

	const float SafeWidth = FMath::Max(Width, 1.0f);
	const float SafeDepth = FMath::Max(Depth, 1.0f);
	const float SafeHeight = FMath::Max(Height, 1.0f);
	const float MaxRadius = 0.499f * FMath::Min3(SafeWidth, SafeDepth, SafeHeight);
	const float SafeRadius = FMath::Clamp(CornerRadius, 0.0f, MaxRadius);

	// Geometry Script emits a change for each stage. Keep collision synchronous at the
	// end, but do not cook the discarded reset/primitive/bevel intermediate meshes.
	MeshComponent->SetTransientDeferCollisionUpdates(true);
	DynamicMesh->Reset();

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
		DynamicMesh,
		PrimitiveOptions,
		FTransform::Identity,
		SafeWidth,
		SafeDepth,
		SafeHeight,
		0,
		0,
		0,
		EGeometryScriptPrimitiveOriginMode::Base);

	if (SafeRadius > UE_KINDA_SMALL_NUMBER)
	{
		FGeometryScriptMeshBevelOptions BevelOptions;
		BevelOptions.BevelDistance = SafeRadius;
		BevelOptions.Subdivisions = FMath::Clamp(CornerSegments, 1, 16);
		BevelOptions.RoundWeight = FMath::Clamp(Roundness, 0.0f, 1.0f);
		BevelOptions.bInferMaterialID = true;

		UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshPolygroupBevel(
			DynamicMesh,
			BevelOptions);
	}

	// Recalculate shading while retaining only genuinely sharp transitions.
	FGeometryScriptSplitNormalsOptions SplitOptions;
	SplitOptions.bSplitByOpeningAngle = true;
	SplitOptions.OpeningAngleDeg = 60.0f;
	SplitOptions.bSplitByFaceGroup = false;

	FGeometryScriptCalculateNormalsOptions NormalOptions;
	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(
		DynamicMesh,
		SplitOptions,
		NormalOptions);

	MeshComponent->SetMaterial(0, Material);

	MeshComponent->NotifyMeshUpdated();
	MeshComponent->SetCollisionEnabled(
		bEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	MeshComponent->SetTransientDeferCollisionUpdates(false);
	MeshComponent->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}

void ARoundedBoxActor::CarveSphereAtWorldLocation(FVector WorldLocation, float Radius, float DepthFraction)
{
	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	UDynamicMesh* TargetMesh = MeshComponent ? MeshComponent->GetDynamicMesh() : nullptr;
	const float SafeRadius = FMath::Max(Radius, 1.0f);
	if (!TargetMesh)
	{
		return;
	}

	UDynamicMesh* ToolMesh = GetOrCreateDamageCutter(SafeRadius);

	const float SafeDepthFraction = FMath::Clamp(DepthFraction, 0.0f, 1.0f);
	const FVector CutCenter = WorldLocation;
	const FTransform ToolTransform(FRotator::ZeroRotator, CutCenter);

	FGeometryScriptMeshBooleanOptions BooleanOptions;
	BooleanOptions.bFillHoles = true;
	BooleanOptions.bSimplifyOutput = true;
	BooleanOptions.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;

	// Move the sphere inward toward the block's centre to create a visible cavity.
	const FVector ToActorCenter = (GetActorLocation() - WorldLocation).GetSafeNormal();
	const FTransform InsetToolTransform(FRotator::ZeroRotator, CutCenter + ToActorCenter * SafeRadius * SafeDepthFraction);
	// The boolean and normal pass both emit mesh-change events. Defer their intermediate
	// collision cooks, then synchronously publish the final collision below.
	MeshComponent->SetTransientDeferCollisionUpdates(true);
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		TargetMesh,
		MeshComponent->GetComponentTransform(),
		ToolMesh,
		InsetToolTransform,
		EGeometryScriptBooleanOperation::Subtract,
		BooleanOptions);

	FGeometryScriptSplitNormalsOptions SplitOptions;
	SplitOptions.bSplitByOpeningAngle = true;
	SplitOptions.OpeningAngleDeg = 60.0f;
	FGeometryScriptCalculateNormalsOptions NormalOptions;
	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(TargetMesh, SplitOptions, NormalOptions);
	MeshComponent->SetTransientDeferCollisionUpdates(false);

	MeshComponent->NotifyMeshUpdated();
	MeshComponent->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}

UDynamicMesh* ARoundedBoxActor::GetOrCreateDamageCutter(float Radius)
{
	if (!CachedDamageCutterMesh)
	{
		CachedDamageCutterMesh = NewObject<UDynamicMesh>(this);
	}
	if (CachedDamageCutterRadius != Radius)
	{
		CachedDamageCutterMesh->Reset();
		FGeometryScriptPrimitiveOptions PrimitiveOptions;
		PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
			CachedDamageCutterMesh,
			PrimitiveOptions,
			FTransform::Identity,
			Radius,
			12,
			24,
			EGeometryScriptPrimitiveOriginMode::Center);
		CachedDamageCutterRadius = Radius;
	}
	return CachedDamageCutterMesh;
}
