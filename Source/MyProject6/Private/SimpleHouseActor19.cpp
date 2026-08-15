// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleHouseActor19.h"

#include "Components/SceneComponent.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/MaterialInterface.h"
#include "UDynamicMesh.h"

namespace
{
	constexpr float MinimumDimension = 10.0f;
	constexpr float MinimumOpeningDimension = 1.0f;
	constexpr int32 MaximumFloorCount = 100;
	constexpr int32 MaximumWindowCountPerFloor = 100;
	constexpr int32 ArchedWindowArcSegments = 12;
	constexpr int32 RoundedWindowCornerSegments = 8;
	constexpr int32 CircularWindowSegments = 32;
	constexpr int32 PaintContourSegments = 72;
	constexpr int32 PaintDropletSegments = 14;
	constexpr float PaintSurfaceDepth = 0.25f;
	constexpr float PaintSurfaceOffset = 0.05f;
	constexpr int32 WindowRevealMaterialID = 1;
	constexpr int32 DoorRevealMaterialID = 2;
	constexpr int32 OpeningObjectRevealMaterialID = 1;
	const FName PaintSplatterComponentTag(TEXT("PaintSplatter"));
	const FName GeneratedOpeningObjectComponentTag(
		TEXT("SimpleHouse19GeneratedOpeningObject"));

	struct FHorizontalSpan
	{
		float Left = 0.0f;
		float Right = 0.0f;

		float Length() const
		{
			return FMath::Max(0.0f, Right - Left);
		}
	};
}

ASimpleHouseActor19::ASimpleHouseActor19(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	HouseRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HouseRoot"));
	SetRootComponent(HouseRoot);

	RoofComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Roof"));
	CeilingComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Ceiling"));
	GroundComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("Ground"));
	FrontWallComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("FrontWall"));
	BackWallComponent = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("BackWall"));
	SideWall1Component = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("SideWall1"));
	SideWall2Component = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("SideWall2"));

	const TArray<UDynamicMeshComponent*> PartComponents = {
		RoofComponent,
		CeilingComponent,
		GroundComponent,
		FrontWallComponent,
		BackWallComponent,
		SideWall1Component,
		SideWall2Component
	};
	for (UDynamicMeshComponent* Component : PartComponents)
	{
		Component->SetupAttachment(HouseRoot);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetGenerateOverlapEvents(false);
		// Boolean cuts change the generated triangles and their UV interpolation.
		// Recalculate tangents from the final mesh so tangent-space materials
		// (especially materials with normal maps) remain stable after resizing.
		Component->SetTangentsType(
			EDynamicMeshComponentTangentsMode::AutoCalculated);
	}

	// Preserve Actor5's default: only the front facade starts with openings.
	FrontFacade.bEnableOpenings = true;
	SynchronizeFloorWindowSettings();
	FrontFacade.FloorWindows[0].bHasWindows = false;
}

void ASimpleHouseActor19::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SynchronizeFloorWindowSettings();
	RebuildMesh();
}

void ASimpleHouseActor19::SynchronizeFloorWindowSettings()
{
	Building.FloorCount = FMath::Clamp(Building.FloorCount, 1, MaximumFloorCount);

	auto SynchronizeFacade = [&](FSimpleHouse19FacadeSettings& Facade)
	{
		Facade.FloorWindows.SetNum(Building.FloorCount);
		for (int32 FloorIndex = 0; FloorIndex < Facade.FloorWindows.Num(); ++FloorIndex)
		{
			FSimpleHouse19FloorWindowSettings& WindowSettings =
				Facade.FloorWindows[FloorIndex];
			WindowSettings.FloorNumber = FloorIndex + 1;
			WindowSettings.WindowCount = FMath::Clamp(
				WindowSettings.WindowCount,
				0,
				MaximumWindowCountPerFloor);
			WindowSettings.WindowObjectMaterials.SetNum(
				WindowSettings.WindowCount);
		}
	};

	SynchronizeFacade(FrontFacade);
	SynchronizeFacade(BackFacade);
	SynchronizeFacade(SideWall1Facade);
	SynchronizeFacade(SideWall2Facade);
}

void ASimpleHouseActor19::FinalizeComponent(UDynamicMeshComponent* Component) const
{
	if (!Component)
	{
		return;
	}

	UMaterialInterface* ComponentMaterial = Material.Get();
	if (Component == RoofComponent.Get() && RoofMaterial)
	{
		ComponentMaterial = RoofMaterial.Get();
	}
	else if (Component == CeilingComponent.Get() && CeilingMaterial)
	{
		ComponentMaterial = CeilingMaterial.Get();
	}
	else if (Component == GroundComponent.Get() && GroundMaterial)
	{
		ComponentMaterial = GroundMaterial.Get();
	}
	else if (Component == FrontWallComponent.Get() && FrontWallMaterial)
	{
		ComponentMaterial = FrontWallMaterial.Get();
	}
	else if (Component == BackWallComponent.Get() && BackWallMaterial)
	{
		ComponentMaterial = BackWallMaterial.Get();
	}
	else if (Component == SideWall1Component.Get() && SideWall1Material)
	{
		ComponentMaterial = SideWall1Material.Get();
	}
	else if (Component == SideWall2Component.Get() && SideWall2Material)
	{
		ComponentMaterial = SideWall2Material.Get();
	}

	Component->SetMaterial(0, ComponentMaterial);
	const bool bIsWallComponent =
		Component == FrontWallComponent.Get() ||
		Component == BackWallComponent.Get() ||
		Component == SideWall1Component.Get() ||
		Component == SideWall2Component.Get();
	if (bIsWallComponent)
	{
		Component->SetMaterial(
			WindowRevealMaterialID,
			WindowRevealMaterial
				? WindowRevealMaterial.Get()
				: ComponentMaterial);
		Component->SetMaterial(
			DoorRevealMaterialID,
			DoorRevealMaterial
				? DoorRevealMaterial.Get()
				: ComponentMaterial);
	}
	Component->InvalidateAutoCalculatedTangents();
	Component->NotifyMeshUpdated();
	Component->SetCollisionEnabled(
		bEnableCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	Component->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}

void ASimpleHouseActor19::RebuildMesh()
{
	SynchronizeFloorWindowSettings();

	// Construction reruns can deserialize the transient tracking arrays without
	// every previously-created instance component. Find generated opening
	// objects on the actor itself as well, so lowering the floor/window count
	// cannot leave orphaned frames or paint objects behind.
	TSet<UDynamicMeshComponent*> OpeningObjectsToDestroy;
	auto TrackOpeningObjects = [&](const TArray<TObjectPtr<UDynamicMeshComponent>>& Components)
	{
		for (UDynamicMeshComponent* Component : Components)
		{
			if (IsValid(Component))
			{
				OpeningObjectsToDestroy.Add(Component);
			}
		}
	};
	TrackOpeningObjects(WindowObjectComponents);
	TrackOpeningObjects(DoorObjectComponents);

	TInlineComponentArray<UDynamicMeshComponent*> OwnedDynamicMeshComponents;
	GetComponents(OwnedDynamicMeshComponents);
	for (UDynamicMeshComponent* Component : OwnedDynamicMeshComponents)
	{
		if (!IsValid(Component))
		{
			continue;
		}

		const FString ComponentName = Component->GetName();
		const bool bHasGeneratedTag = Component->ComponentTags.Contains(
			GeneratedOpeningObjectComponentTag);
		const bool bHasLegacyGeneratedName =
			ComponentName.StartsWith(TEXT("WindowObject_")) ||
			ComponentName.StartsWith(TEXT("DoorObject_"));
		if (bHasGeneratedTag || bHasLegacyGeneratedName)
		{
			OpeningObjectsToDestroy.Add(Component);
		}
	}

	WindowObjectComponents.Reset();
	DoorObjectComponents.Reset();
	for (UDynamicMeshComponent* Component : OpeningObjectsToDestroy)
	{
		if (IsValid(Component))
		{
			RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}

	UDynamicMesh* RoofMesh = RoofComponent ? RoofComponent->GetDynamicMesh() : nullptr;
	UDynamicMesh* CeilingMesh = CeilingComponent ? CeilingComponent->GetDynamicMesh() : nullptr;
	UDynamicMesh* GroundMesh = GroundComponent ? GroundComponent->GetDynamicMesh() : nullptr;
	UDynamicMesh* FrontWallMesh = FrontWallComponent ? FrontWallComponent->GetDynamicMesh() : nullptr;
	UDynamicMesh* BackWallMesh = BackWallComponent ? BackWallComponent->GetDynamicMesh() : nullptr;
	UDynamicMesh* SideWall1Mesh = SideWall1Component ? SideWall1Component->GetDynamicMesh() : nullptr;
	UDynamicMesh* SideWall2Mesh = SideWall2Component ? SideWall2Component->GetDynamicMesh() : nullptr;
	if (!RoofMesh || !CeilingMesh || !GroundMesh || !FrontWallMesh ||
		!BackWallMesh || !SideWall1Mesh || !SideWall2Mesh)
	{
		return;
	}

	const TArray<UDynamicMesh*> PartMeshes = {
		RoofMesh,
		CeilingMesh,
		GroundMesh,
		FrontWallMesh,
		BackWallMesh,
		SideWall1Mesh,
		SideWall2Mesh
	};
	for (UDynamicMesh* PartMesh : PartMeshes)
	{
		PartMesh->Reset();
	}

	const float SafeWidth = FMath::Max(Building.BaseWidth, MinimumDimension);
	const float SafeDepth = FMath::Max(Building.BaseDepth, MinimumDimension);
	const float SafeFloorHeight = FMath::Max(Building.FloorHeight, MinimumDimension);
	const float SafeGableHeight = FMath::Max(Roof.RoofHeight, MinimumDimension);
	const float SafePyramidHeight = FMath::Max(Roof.PyramidHeight, MinimumDimension);
	const float SafeRoofFrontBackOverhang = FMath::Max(Roof.FrontBackOverhang, 0.0f);
	const bool bPyramidRoof = Roof.RoofType == ESimpleHouse19RoofType::Pyramid;
	const int32 SafeFloorCount = FMath::Clamp(Building.FloorCount, 1, MaximumFloorCount);
	const float TotalWallHeight = SafeFloorHeight * static_cast<float>(SafeFloorCount);
	const float MaximumThickness = 0.45f * FMath::Min3(SafeWidth, SafeDepth, SafeFloorHeight);
	const float SafeThickness = FMath::Clamp(Building.WallThickness, MinimumOpeningDimension, MaximumThickness);
	const float SafeOpeningObjectBorder = FMath::Max(
		OpeningObjectBorder,
		0.1f);
	const float SafeGroundThickness = FMath::Max(GroundThickness, MinimumOpeningDimension);
	const float InnerWidth = SafeWidth - SafeThickness * 2.0f;
	const float InnerDepth = SafeDepth - SafeThickness * 2.0f;

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	// Gable roofs retain the triangular front/back wall sections from Actor6.
	// Pyramid roofs terminate every wall at the common horizontal eave.
	TArray<FVector2D> FrontBackWallProfile;
	if (bPyramidRoof)
	{
		FrontBackWallProfile = {
			FVector2D(-InnerWidth * 0.5f, 0.0f),
			FVector2D(InnerWidth * 0.5f, 0.0f),
			FVector2D(InnerWidth * 0.5f, TotalWallHeight),
			FVector2D(-InnerWidth * 0.5f, TotalWallHeight)
		};
	}
	else
	{
		const float HalfWidth = SafeWidth * 0.5f;
		const float RoofSlopeLength = FMath::Sqrt(
			HalfWidth * HalfWidth + SafeGableHeight * SafeGableHeight);
		const float InnerRidgeZ =
			TotalWallHeight + SafeGableHeight -
			SafeThickness * RoofSlopeLength / HalfWidth;
		const float InnerWallEdgeZ =
			InnerRidgeZ -
			SafeGableHeight / HalfWidth * (InnerWidth * 0.5f);

		// Stop the gable wall at the roof's inner surface. Extending it to the
		// outer roof slope makes the two solids intersect and exposes the wall
		// material through the roof when viewed from an angle.
		FrontBackWallProfile = {
			FVector2D(-InnerWidth * 0.5f, 0.0f),
			FVector2D(InnerWidth * 0.5f, 0.0f),
			FVector2D(InnerWidth * 0.5f, InnerWallEdgeZ),
			FVector2D(0.0f, InnerRidgeZ),
			FVector2D(-InnerWidth * 0.5f, InnerWallEdgeZ)
		};
	}
	const FTransform FrontWallTransform(
		FRotator(0.0f, 0.0f, -90.0f),
		FVector(0.0f, -SafeDepth * 0.5f + SafeThickness, 0.0f));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
		FrontWallMesh,
		PrimitiveOptions,
		FrontWallTransform,
		FrontBackWallProfile,
		SafeThickness,
		0,
		true,
		EGeometryScriptPrimitiveOriginMode::Base);

	const FTransform BackWallTransform(
		FRotator(0.0f, 0.0f, -90.0f),
		FVector(0.0f, SafeDepth * 0.5f, 0.0f));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
		BackWallMesh,
		PrimitiveOptions,
		BackWallTransform,
		FrontBackWallProfile,
		SafeThickness,
		0,
		true,
		EGeometryScriptPrimitiveOriginMode::Base);

	// Side walls span the complete depth and meet the narrower front/back
	// walls without overlapping them.
	if (bPyramidRoof)
	{
		const FTransform SideWall1Transform(
			FRotator::ZeroRotator,
			FVector(
				-SafeWidth * 0.5f + SafeThickness * 0.5f,
				0.0f,
				TotalWallHeight * 0.5f));
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			SideWall1Mesh,
			PrimitiveOptions,
			SideWall1Transform,
			SafeThickness,
			SafeDepth,
			TotalWallHeight,
			0,
			0,
			0,
			EGeometryScriptPrimitiveOriginMode::Center);

		const FTransform SideWall2Transform(
			FRotator::ZeroRotator,
			FVector(
				SafeWidth * 0.5f - SafeThickness * 0.5f,
				0.0f,
				TotalWallHeight * 0.5f));
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			SideWall2Mesh,
			PrimitiveOptions,
			SideWall2Transform,
			SafeThickness,
			SafeDepth,
			TotalWallHeight,
			0,
			0,
			0,
			EGeometryScriptPrimitiveOriginMode::Center);
	}
	else
	{
		const float HalfWidth = SafeWidth * 0.5f;
		const float HalfInnerWidth = InnerWidth * 0.5f;
		const float RoofSlopeLength = FMath::Sqrt(
			HalfWidth * HalfWidth + SafeGableHeight * SafeGableHeight);
		const float InnerEaveHorizontalInset =
			SafeThickness * SafeGableHeight / RoofSlopeLength;
		const float InnerEaveVerticalOffset =
			SafeThickness * HalfWidth / RoofSlopeLength;
		const float InnerRidgeZ =
			TotalWallHeight + SafeGableHeight -
			SafeThickness * RoofSlopeLength / HalfWidth;
		const float InnerWallEdgeZ =
			InnerRidgeZ -
			SafeGableHeight / HalfWidth * HalfInnerWidth;
		const float InnerEaveX =
			HalfWidth - InnerEaveHorizontalInset;
		const float InnerEaveZ =
			TotalWallHeight - InnerEaveVerticalOffset;

		// Follow both the inner roof slope and the closed eave face so the
		// wall volume only touches the roof shell at a shared boundary.
		const TArray<FVector2D> SideWall1Profile = {
			FVector2D(-HalfWidth, 0.0f),
			FVector2D(-HalfInnerWidth, 0.0f),
			FVector2D(-HalfInnerWidth, InnerWallEdgeZ),
			FVector2D(-InnerEaveX, InnerEaveZ),
			FVector2D(-HalfWidth, TotalWallHeight)
		};
		const TArray<FVector2D> SideWall2Profile = {
			FVector2D(HalfInnerWidth, 0.0f),
			FVector2D(HalfWidth, 0.0f),
			FVector2D(HalfWidth, TotalWallHeight),
			FVector2D(InnerEaveX, InnerEaveZ),
			FVector2D(HalfInnerWidth, InnerWallEdgeZ)
		};
		const FTransform SideWallTransform(
			FRotator(0.0f, 0.0f, -90.0f),
			FVector(0.0f, SafeDepth * 0.5f, 0.0f));

		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
			SideWall1Mesh,
			PrimitiveOptions,
			SideWallTransform,
			SideWall1Profile,
			SafeDepth,
			0,
			true,
			EGeometryScriptPrimitiveOriginMode::Base);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
			SideWall2Mesh,
			PrimitiveOptions,
			SideWallTransform,
			SideWall2Profile,
			SafeDepth,
			0,
			true,
			EGeometryScriptPrimitiveOriginMode::Base);
	}

	if (bPyramidRoof)
	{
		// Build one watertight shell with shared ridge vertices. Four separate
		// extruded panels overlap at high pitches and visibly fork at the apex.
		const float HalfWidth = SafeWidth * 0.5f;
		const float HalfDepth =
			SafeDepth * 0.5f + SafeRoofFrontBackOverhang;
		const float FrontBackSlopeLength = FMath::Sqrt(
			HalfDepth * HalfDepth + SafePyramidHeight * SafePyramidHeight);
		const float SideSlopeLength = FMath::Sqrt(
			HalfWidth * HalfWidth + SafePyramidHeight * SafePyramidHeight);
		const float FrontBackHorizontalInset =
			SafePyramidHeight / FrontBackSlopeLength;
		const float FrontBackEaveVerticalOffset =
			HalfDepth / FrontBackSlopeLength;
		const float SideHorizontalInset =
			SafePyramidHeight / SideSlopeLength;
		const float SideEaveVerticalOffset =
			HalfWidth / SideSlopeLength;
		const float FrontBackRidgeVerticalOffset =
			FrontBackSlopeLength / HalfDepth;
		const float SideRidgeVerticalOffset =
			SideSlopeLength / HalfWidth;
		const float EaveZ = TotalWallHeight;
		const float OuterApexZ = EaveZ + SafePyramidHeight;
		const float InnerHalfWidth = FMath::Max(
			MinimumOpeningDimension,
			HalfWidth - SafeThickness * SideHorizontalInset);
		const float InnerHalfDepth = FMath::Max(
			MinimumOpeningDimension,
			HalfDepth - SafeThickness * FrontBackHorizontalInset);
		const float InnerEaveZ =
			EaveZ -
			SafeThickness * FMath::Max(
				FrontBackEaveVerticalOffset,
				SideEaveVerticalOffset);
		const float DesiredInnerApexZ =
			OuterApexZ -
			SafeThickness * FMath::Max(
				FrontBackRidgeVerticalOffset,
				SideRidgeVerticalOffset);
		const float InnerApexZ = FMath::Max(
			InnerEaveZ + MinimumOpeningDimension,
			DesiredInnerApexZ);

		FGeometryScriptSimpleMeshBuffers PyramidBuffers;
		PyramidBuffers.Vertices = {
			// Outer base, counter-clockwise when viewed from above.
			FVector(-HalfWidth, -HalfDepth, EaveZ),
			FVector(HalfWidth, -HalfDepth, EaveZ),
			FVector(HalfWidth, HalfDepth, EaveZ),
			FVector(-HalfWidth, HalfDepth, EaveZ),
			FVector(0.0f, 0.0f, OuterApexZ),
			// Inner base and the single shared inner apex.
			FVector(-InnerHalfWidth, -InnerHalfDepth, InnerEaveZ),
			FVector(InnerHalfWidth, -InnerHalfDepth, InnerEaveZ),
			FVector(InnerHalfWidth, InnerHalfDepth, InnerEaveZ),
			FVector(-InnerHalfWidth, InnerHalfDepth, InnerEaveZ),
			FVector(0.0f, 0.0f, InnerApexZ)
		};
		PyramidBuffers.Triangles = {
			// Unreal's left-handed winding points these faces outwards.
			FIntVector(0, 4, 1),
			FIntVector(1, 4, 2),
			FIntVector(2, 4, 3),
			FIntVector(3, 4, 0),
			// Inner faces point down into the house interior.
			FIntVector(5, 6, 9),
			FIntVector(6, 7, 9),
			FIntVector(7, 8, 9),
			FIntVector(8, 5, 9),
			// Closed eave rim between the outer and inner surfaces.
			FIntVector(1, 5, 0),
			FIntVector(1, 6, 5),
			FIntVector(2, 6, 1),
			FIntVector(2, 7, 6),
			FIntVector(3, 7, 2),
			FIntVector(3, 8, 7),
			FIntVector(0, 8, 3),
			FIntVector(0, 5, 8)
		};
		FGeometryScriptIndexList NewPyramidTriangles;
		UGeometryScriptLibrary_MeshBasicEditFunctions::AppendBuffersToMesh(
			RoofMesh,
			PyramidBuffers,
			NewPyramidTriangles);
	}
	else
	{
		// Build the gable as one closed, mitered cross-section instead of two
		// intersecting boxes. This keeps the ridge clean at every roof height.
		const float HalfWidth = SafeWidth * 0.5f;
		const float RoofSlopeLength = FMath::Sqrt(
			HalfWidth * HalfWidth + SafeGableHeight * SafeGableHeight);
		const float InnerEaveHorizontalInset =
			SafeThickness * SafeGableHeight / RoofSlopeLength;
		const float InnerEaveVerticalOffset =
			SafeThickness * HalfWidth / RoofSlopeLength;
		const float InnerRidgeVerticalOffset =
			SafeThickness * RoofSlopeLength / HalfWidth;
		const float EaveZ = TotalWallHeight;
		const float RidgeZ = EaveZ + SafeGableHeight;

		// Counter-clockwise outline of the union of both roof panels. The
		// central inner point is their exact miter intersection.
		const TArray<FVector2D> GableRoofProfile = {
			FVector2D(
				-HalfWidth + InnerEaveHorizontalInset,
				EaveZ - InnerEaveVerticalOffset),
			FVector2D(0.0f, RidgeZ - InnerRidgeVerticalOffset),
			FVector2D(
				HalfWidth - InnerEaveHorizontalInset,
				EaveZ - InnerEaveVerticalOffset),
			FVector2D(HalfWidth, EaveZ),
			FVector2D(0.0f, RidgeZ),
			FVector2D(-HalfWidth, EaveZ)
		};

		// Roll maps the 2D profile's Y axis to world Z and extrusion to -Y.
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
			RoofMesh,
			PrimitiveOptions,
			FTransform(
				FRotator(0.0f, 0.0f, -90.0f),
				FVector(
					0.0f,
					SafeDepth * 0.5f + SafeRoofFrontBackOverhang,
					0.0f)),
			GableRoofProfile,
			SafeDepth + SafeRoofFrontBackOverhang * 2.0f,
			0,
			true,
			EGeometryScriptPrimitiveOriginMode::Base);
	}

	if (Roof.bHasCeiling)
	{
		const FTransform CeilingTransform(
			FRotator::ZeroRotator,
			FVector(0.0f, 0.0f, TotalWallHeight - SafeThickness * 0.5f));
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			CeilingMesh,
			PrimitiveOptions,
			CeilingTransform,
			InnerWidth,
			InnerDepth,
			SafeThickness,
			0,
			0,
			0,
			EGeometryScriptPrimitiveOriginMode::Center);
	}

	if (bHasGroundFloor)
	{
		const bool bCoverFullBuildingFootprint =
			GroundCoverage == ESimpleHouse19GroundCoverage::FullBuildingFootprint;
		const float GroundWidth =
			bCoverFullBuildingFootprint ? SafeWidth : InnerWidth;
		const float GroundDepth =
			bCoverFullBuildingFootprint ? SafeDepth : InnerDepth;
		const FTransform GroundTransform(
			FRotator::ZeroRotator,
			FVector(0.0f, 0.0f, SafeGroundThickness * 0.5f));
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			GroundMesh,
			PrimitiveOptions,
			GroundTransform,
			GroundWidth,
			GroundDepth,
			SafeGroundThickness,
			0,
			0,
			0,
			EGeometryScriptPrimitiveOriginMode::Center);
	}

	FGeometryScriptMeshBooleanOptions BooleanOptions;
	BooleanOptions.bFillHoles = true;
	BooleanOptions.bSimplifyOutput = true;
	BooleanOptions.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;

	int32 WindowObjectCounter = 0;
	int32 DoorObjectCounter = 0;
	auto CreateOpeningObjectComponent = [&](
		bool bDoorObject,
		const FVector& RelativeLocation,
		const FRotator& RelativeRotation)
	{
		int32& ObjectCounter = bDoorObject
			? DoorObjectCounter
			: WindowObjectCounter;
		++ObjectCounter;
		const FName BaseName(*FString::Printf(
			TEXT("%s_%03d"),
			bDoorObject ? TEXT("DoorObject") : TEXT("WindowObject"),
			ObjectCounter));
		const FName ComponentName = MakeUniqueObjectName(
			this,
			UDynamicMeshComponent::StaticClass(),
			BaseName);
		UDynamicMeshComponent* Component =
			NewObject<UDynamicMeshComponent>(
				this,
				ComponentName,
				RF_Transactional);
		Component->SetupAttachment(HouseRoot);
		Component->SetRelativeLocationAndRotation(
			RelativeLocation,
			RelativeRotation);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetGenerateOverlapEvents(false);
		Component->SetTangentsType(
			EDynamicMeshComponentTangentsMode::AutoCalculated);
		Component->ComponentTags.AddUnique(
			GeneratedOpeningObjectComponentTag);
		AddInstanceComponent(Component);
		Component->RegisterComponent();

		if (bDoorObject)
		{
			DoorObjectComponents.Add(Component);
		}
		else
		{
			WindowObjectComponents.Add(Component);
		}
		return Component;
	};

	// A single placement system serves every facade. Cutters are generated in
	// canonical local space, then rotated onto the requested wall.
	auto BuildFacadeOpenings = [&](
		UDynamicMesh* TargetWallMesh,
		const FSimpleHouse19FacadeSettings& FacadeSettings,
		UMaterialInterface* FacadeWallMaterial,
		float FacadeSpan,
		float PaintableFacadeSpan,
		const FVector& SurfaceCenter,
		bool bHorizontalAlongY,
		float ExteriorLocalYSign)
	{
		if (!TargetWallMesh || !FacadeSettings.bEnableOpenings)
		{
			return;
		}

		struct FFacadeOpeningCutout
		{
			UDynamicMesh* CutterMesh = nullptr;
			FVector2D Center = FVector2D::ZeroVector;
			FVector2D HalfExtents = FVector2D::ZeroVector;
			FVector CutterScale = FVector::OneVector;
		};

		struct FPendingPaintPiece
		{
			UDynamicMesh* Mesh = nullptr;
			FVector2D BoundsMin = FVector2D::ZeroVector;
			FVector2D BoundsMax = FVector2D::ZeroVector;
		};

		struct FPendingPaintObject
		{
			UDynamicMeshComponent* Component = nullptr;
			FVector2D Center = FVector2D::ZeroVector;
			TArray<FPendingPaintPiece> Pieces;
		};

		TArray<FFacadeOpeningCutout> FacadeOpeningCutouts;
		TArray<FPendingPaintObject> PendingPaintObjects;

		auto CutOpening = [&](
			float CenterHorizontal,
			float CenterZ,
			float OpeningWidth,
			float OpeningHeight,
			bool bTouchesGround,
			ESimpleHouse19WindowType OpeningType,
			float RoundedCornerRadius,
			ESimpleHouse19WindowBorderStyle WindowBorderStyle,
			float PaintSpread,
			float PaintSplatterAmount,
			int32 PaintSplatterSeed,
			UMaterialInterface* OpeningObjectMaterial)
		{
			const float BooleanTolerance = 0.5f;
			const float CutterDepth = SafeThickness * 3.0f;
			const bool bPaintSplatter =
				!bTouchesGround &&
				WindowBorderStyle ==
					ESimpleHouse19WindowBorderStyle::PaintSplatter;
			const float SafePaintSpread = FMath::Max(
				PaintSpread,
				MinimumOpeningDimension);
			const float SafePaintSplatterAmount =
				FMath::Clamp(PaintSplatterAmount, 0.0f, 100.0f) /
				100.0f;
			TArray<FPendingPaintPiece> CurrentPaintPieces;

			auto AppendOpeningCutter = [&](
				UDynamicMesh* CutterMesh,
				const FGeometryScriptPrimitiveOptions& CutterOptions,
				float BottomExtension)
			{
				const float MaximumRoundedCornerRadius = FMath::Max(
					0.0f,
					FMath::Min(OpeningWidth, OpeningHeight) * 0.5f -
						0.01f);
				const float SafeRoundedCornerRadius = FMath::Clamp(
					RoundedCornerRadius,
					0.0f,
					MaximumRoundedCornerRadius);

				if (OpeningType == ESimpleHouse19WindowType::Arched)
				{
					const float HalfOpeningWidth = OpeningWidth * 0.5f;
					const float HalfOpeningHeight = OpeningHeight * 0.5f;
					const float OpeningBottomZ =
						-HalfOpeningHeight - BottomExtension;
					const float ArchHeight = bTouchesGround
						? HalfOpeningWidth
						: FMath::Min(
							HalfOpeningWidth,
							OpeningHeight * 0.9f);
					const float ArchShoulderZ =
						HalfOpeningHeight - ArchHeight;

					TArray<FVector2D> ArchedProfile;
					ArchedProfile.Reserve(ArchedWindowArcSegments + 3);
					ArchedProfile.Add(FVector2D(
						-HalfOpeningWidth,
						OpeningBottomZ));
					ArchedProfile.Add(FVector2D(
						HalfOpeningWidth,
						OpeningBottomZ));
					ArchedProfile.Add(FVector2D(
						HalfOpeningWidth,
						ArchShoulderZ));
					for (int32 SegmentIndex = 1;
						 SegmentIndex <= ArchedWindowArcSegments;
						 ++SegmentIndex)
					{
						const float Angle =
							PI * static_cast<float>(SegmentIndex) /
							static_cast<float>(ArchedWindowArcSegments);
						ArchedProfile.Add(FVector2D(
							FMath::Cos(Angle) * HalfOpeningWidth,
							ArchShoulderZ +
								FMath::Sin(Angle) * ArchHeight));
					}

					const FTransform CanonicalArchedTransform(
						FRotator(0.0f, 0.0f, -90.0f),
						FVector(0.0f, CutterDepth * 0.5f, 0.0f));
					UGeometryScriptLibrary_MeshPrimitiveFunctions::
						AppendSimpleExtrudePolygon(
							CutterMesh,
							CutterOptions,
							CanonicalArchedTransform,
							ArchedProfile,
							CutterDepth,
							0,
							true,
							EGeometryScriptPrimitiveOriginMode::Base);
				}
				else if (
					OpeningType == ESimpleHouse19WindowType::Circular &&
					!bTouchesGround)
				{
					const float Radius = OpeningWidth * 0.5f;
					TArray<FVector2D> CircularProfile;
					CircularProfile.Reserve(CircularWindowSegments);
					for (int32 SegmentIndex = 0;
						 SegmentIndex < CircularWindowSegments;
						 ++SegmentIndex)
					{
						const float Angle =
							2.0f * PI *
							static_cast<float>(SegmentIndex) /
							static_cast<float>(CircularWindowSegments);
						CircularProfile.Add(FVector2D(
							FMath::Cos(Angle) * Radius,
							FMath::Sin(Angle) * Radius));
					}

					const FTransform CanonicalCircularTransform(
						FRotator(0.0f, 0.0f, -90.0f),
						FVector(0.0f, CutterDepth * 0.5f, 0.0f));
					UGeometryScriptLibrary_MeshPrimitiveFunctions::
						AppendSimpleExtrudePolygon(
							CutterMesh,
							CutterOptions,
							CanonicalCircularTransform,
							CircularProfile,
							CutterDepth,
							0,
							true,
							EGeometryScriptPrimitiveOriginMode::Base);
				}
				else if (
					OpeningType ==
						ESimpleHouse19WindowType::RoundedCorners &&
					SafeRoundedCornerRadius > KINDA_SMALL_NUMBER)
				{
					const float HalfOpeningWidth = OpeningWidth * 0.5f;
					const float HalfOpeningHeight = OpeningHeight * 0.5f;
					const FVector2D CornerCenters[4] = {
						FVector2D(
							HalfOpeningWidth - SafeRoundedCornerRadius,
							-HalfOpeningHeight + SafeRoundedCornerRadius),
						FVector2D(
							HalfOpeningWidth - SafeRoundedCornerRadius,
							HalfOpeningHeight - SafeRoundedCornerRadius),
						FVector2D(
							-HalfOpeningWidth + SafeRoundedCornerRadius,
							HalfOpeningHeight - SafeRoundedCornerRadius),
						FVector2D(
							-HalfOpeningWidth + SafeRoundedCornerRadius,
							-HalfOpeningHeight + SafeRoundedCornerRadius)
					};

					TArray<FVector2D> RoundedProfile;
					RoundedProfile.Reserve(
						4 * (RoundedWindowCornerSegments + 1));
					for (int32 CornerIndex = 0;
						 CornerIndex < 4;
						 ++CornerIndex)
					{
						const float StartAngle =
							-0.5f * PI +
							static_cast<float>(CornerIndex) * 0.5f * PI;
						for (int32 SegmentIndex = 0;
							 SegmentIndex <= RoundedWindowCornerSegments;
							 ++SegmentIndex)
						{
							const float Angle =
								StartAngle +
								0.5f * PI *
									static_cast<float>(SegmentIndex) /
									static_cast<float>(
										RoundedWindowCornerSegments);
							RoundedProfile.Add(
								CornerCenters[CornerIndex] +
								FVector2D(
									FMath::Cos(Angle),
									FMath::Sin(Angle)) *
									SafeRoundedCornerRadius);
						}
					}

					const FTransform CanonicalRoundedTransform(
						FRotator(0.0f, 0.0f, -90.0f),
						FVector(0.0f, CutterDepth * 0.5f, 0.0f));
					UGeometryScriptLibrary_MeshPrimitiveFunctions::
						AppendSimpleExtrudePolygon(
							CutterMesh,
							CutterOptions,
							CanonicalRoundedTransform,
							RoundedProfile,
							CutterDepth,
							0,
							true,
							EGeometryScriptPrimitiveOriginMode::Base);
				}
				else
				{
					const FTransform CutterTransform(
						FRotator::ZeroRotator,
						FVector(
							0.0f,
							0.0f,
							-BottomExtension * 0.5f));
					UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
						CutterMesh,
						CutterOptions,
						CutterTransform,
						OpeningWidth,
						CutterDepth,
						OpeningHeight + BottomExtension,
						0,
						0,
						0,
					EGeometryScriptPrimitiveOriginMode::Center);
				}
			};

			// Paint exists on a single facade, so clip every generated polygon
			// against that facade before creating geometry. Bounds are expressed
			// relative to this opening object's center.
			const float PaintWallBoundaryInset = 0.05f;
			const float PaintWallMinX =
				-PaintableFacadeSpan * 0.5f -
				CenterHorizontal +
				PaintWallBoundaryInset;
			const float PaintWallMaxX =
				PaintableFacadeSpan * 0.5f -
				CenterHorizontal -
				PaintWallBoundaryInset;
			const float PaintWallMinZ =
				-CenterZ + PaintWallBoundaryInset;
			const float PaintWallMaxZ =
				TotalWallHeight -
				CenterZ -
				PaintWallBoundaryInset;

			auto ClipPaintProfileToWall = [&](
				const TArray<FVector2D>& SourceProfile,
				const FVector2D& Offset)
			{
				TArray<FVector2D> ClippedProfile;
				ClippedProfile.Reserve(SourceProfile.Num());
				for (const FVector2D& Point : SourceProfile)
				{
					ClippedProfile.Add(Point + Offset);
				}

				auto ClipAgainstBoundary = [](
					const TArray<FVector2D>& InputProfile,
					auto IsInside,
					auto FindIntersection)
				{
					TArray<FVector2D> OutputProfile;
					if (InputProfile.IsEmpty())
					{
						return OutputProfile;
					}

					OutputProfile.Reserve(InputProfile.Num() + 4);
					FVector2D PreviousPoint = InputProfile.Last();
					bool bPreviousInside = IsInside(PreviousPoint);
					for (const FVector2D& CurrentPoint : InputProfile)
					{
						const bool bCurrentInside = IsInside(CurrentPoint);
						if (bCurrentInside)
						{
							if (!bPreviousInside)
							{
								OutputProfile.Add(FindIntersection(
									PreviousPoint,
									CurrentPoint));
							}
							OutputProfile.Add(CurrentPoint);
						}
						else if (bPreviousInside)
						{
							OutputProfile.Add(FindIntersection(
								PreviousPoint,
								CurrentPoint));
						}

						PreviousPoint = CurrentPoint;
						bPreviousInside = bCurrentInside;
					}
					return OutputProfile;
				};

				auto IntersectVertical = [](
					const FVector2D& Start,
					const FVector2D& End,
					float BoundaryX)
				{
					const float DeltaX = End.X - Start.X;
					const float Alpha = FMath::IsNearlyZero(DeltaX)
						? 0.0f
						: FMath::Clamp(
							(BoundaryX - Start.X) / DeltaX,
							0.0f,
							1.0f);
					FVector2D Intersection =
						Start + (End - Start) * Alpha;
					Intersection.X = BoundaryX;
					return Intersection;
				};
				auto IntersectHorizontal = [](
					const FVector2D& Start,
					const FVector2D& End,
					float BoundaryZ)
				{
					const float DeltaZ = End.Y - Start.Y;
					const float Alpha = FMath::IsNearlyZero(DeltaZ)
						? 0.0f
						: FMath::Clamp(
							(BoundaryZ - Start.Y) / DeltaZ,
							0.0f,
							1.0f);
					FVector2D Intersection =
						Start + (End - Start) * Alpha;
					Intersection.Y = BoundaryZ;
					return Intersection;
				};

				ClippedProfile = ClipAgainstBoundary(
					ClippedProfile,
					[&](const FVector2D& Point)
					{
						return Point.X >= PaintWallMinX;
					},
					[&](const FVector2D& Start, const FVector2D& End)
					{
						return IntersectVertical(
							Start,
							End,
							PaintWallMinX);
					});
				ClippedProfile = ClipAgainstBoundary(
					ClippedProfile,
					[&](const FVector2D& Point)
					{
						return Point.X <= PaintWallMaxX;
					},
					[&](const FVector2D& Start, const FVector2D& End)
					{
						return IntersectVertical(
							Start,
							End,
							PaintWallMaxX);
					});
				ClippedProfile = ClipAgainstBoundary(
					ClippedProfile,
					[&](const FVector2D& Point)
					{
						return Point.Y >= PaintWallMinZ;
					},
					[&](const FVector2D& Start, const FVector2D& End)
					{
						return IntersectHorizontal(
							Start,
							End,
							PaintWallMinZ);
					});
				ClippedProfile = ClipAgainstBoundary(
					ClippedProfile,
					[&](const FVector2D& Point)
					{
						return Point.Y <= PaintWallMaxZ;
					},
					[&](const FVector2D& Start, const FVector2D& End)
					{
						return IntersectHorizontal(
							Start,
							End,
							PaintWallMaxZ);
					});
				return ClippedProfile;
			};

			auto AppendPaintSplatter = [&](
				const FGeometryScriptPrimitiveOptions& PaintOptions)
			{
				// Use one continuous, rounded impact mass as the visual anchor.
				// Low-frequency lobes make the silhouette feel hand-thrown, while
				// narrow integrated bursts keep it from reading as a noisy frame.
				const float CoverageMultiplier =
					0.72f + SafePaintSplatterAmount * 0.22f;
				const float BaseHalfWidth =
					OpeningWidth * 0.5f +
					SafePaintSpread * CoverageMultiplier;
				const float BaseHalfHeight =
					OpeningHeight * 0.5f +
					SafePaintSpread *
						(0.62f + SafePaintSplatterAmount * 0.2f);
				FRandomStream PaintRandom(PaintSplatterSeed);
				const float NoisePhase1 =
					PaintRandom.FRandRange(0.0f, 2.0f * PI);
				const float NoisePhase2 =
					PaintRandom.FRandRange(0.0f, 2.0f * PI);
				const float NoisePhase3 =
					PaintRandom.FRandRange(0.0f, 2.0f * PI);
				const FVector2D ImpactCenter(
					SafePaintSpread *
						PaintRandom.FRandRange(-0.055f, 0.055f),
					SafePaintSpread *
						PaintRandom.FRandRange(-0.075f, 0.025f));

				auto AppendPaintPolygon = [&](
					const TArray<FVector2D>& Profile,
					const FVector2D& Offset)
				{
					const TArray<FVector2D> WallClippedProfile =
						ClipPaintProfileToWall(Profile, Offset);
					if (WallClippedProfile.Num() < 3)
					{
						return;
					}

					UDynamicMesh* PaintPieceMesh =
						NewObject<UDynamicMesh>(this);
					const FTransform PaintTransform(
						FRotator(0.0f, 0.0f, -90.0f),
						FVector(
							0.0f,
							PaintSurfaceDepth * 0.5f,
							0.0f));
					UGeometryScriptLibrary_MeshPrimitiveFunctions::
						AppendSimpleExtrudePolygon(
							PaintPieceMesh,
							PaintOptions,
							PaintTransform,
							WallClippedProfile,
							PaintSurfaceDepth,
							0,
							true,
							EGeometryScriptPrimitiveOriginMode::Base);

					FVector2D BoundsMin(
						TNumericLimits<float>::Max(),
						TNumericLimits<float>::Max());
					FVector2D BoundsMax(
						TNumericLimits<float>::Lowest(),
						TNumericLimits<float>::Lowest());
					for (const FVector2D& Point : WallClippedProfile)
					{
						BoundsMin.X = FMath::Min(BoundsMin.X, Point.X);
						BoundsMin.Y = FMath::Min(BoundsMin.Y, Point.Y);
						BoundsMax.X = FMath::Max(BoundsMax.X, Point.X);
						BoundsMax.Y = FMath::Max(BoundsMax.Y, Point.Y);
					}
					CurrentPaintPieces.Add({
						PaintPieceMesh,
						BoundsMin,
						BoundsMax
					});
				};

				struct FPaintContourBurst
				{
					float Angle = 0.0f;
					float Length = 0.0f;
					float HalfWidth = 0.0f;
				};

				TArray<FPaintContourBurst> ContourBursts;
				const int32 ContourBurstCount = FMath::RoundToInt(
					FMath::Lerp(3.0f, 8.0f, SafePaintSplatterAmount));
				ContourBursts.Reserve(ContourBurstCount);
				for (int32 BurstIndex = 0;
					 BurstIndex < ContourBurstCount;
					 ++BurstIndex)
				{
					float BurstAngle =
						PaintRandom.FRandRange(0.0f, 2.0f * PI);
					// Gravity gets its own rounded runs below. Bias impact spikes
					// toward the sides and upper edge to keep the lower silhouette
					// from becoming a dense row of triangles.
					if (FMath::Sin(BurstAngle) < -0.45f &&
						PaintRandom.FRand() < 0.7f)
					{
						BurstAngle = -BurstAngle;
					}
					ContourBursts.Add({
						BurstAngle,
						static_cast<float>(
							SafePaintSpread *
							PaintRandom.FRandRange(0.18f, 0.82f) *
							(0.45f + SafePaintSplatterAmount * 0.7f)),
						static_cast<float>(
							PaintRandom.FRandRange(0.085f, 0.22f))
					});
				}

				auto GetAngularDistance = [](
					float FirstAngle,
					float SecondAngle)
				{
					const float Delta = FirstAngle - SecondAngle;
					return FMath::Abs(FMath::Atan2(
						FMath::Sin(Delta),
						FMath::Cos(Delta)));
				};

				TArray<float> ContourRadii;
				ContourRadii.Reserve(PaintContourSegments);
				TArray<FVector2D> PaintPatch;
				PaintPatch.Reserve(PaintContourSegments);
				for (int32 SegmentIndex = 0;
					 SegmentIndex < PaintContourSegments;
					 ++SegmentIndex)
				{
					const float Angle =
						2.0f * PI *
						static_cast<float>(SegmentIndex) /
						static_cast<float>(PaintContourSegments);
					const FVector2D Direction(
						FMath::Cos(Angle),
						FMath::Sin(Angle));
					const float RectangleRadius = FMath::Min(
						BaseHalfWidth /
							FMath::Max(FMath::Abs(Direction.X), 0.001f),
						BaseHalfHeight /
							FMath::Max(FMath::Abs(Direction.Y), 0.001f));
					const float EllipseRadius =
						1.0f /
						FMath::Sqrt(
							FMath::Square(Direction.X / BaseHalfWidth) +
							FMath::Square(Direction.Y / BaseHalfHeight));
					float Radius = FMath::Lerp(
						EllipseRadius,
						RectangleRadius,
						0.47f);

					const float OrganicNoise =
						FMath::Sin(Angle * 2.0f + NoisePhase1) * 0.07f +
						FMath::Sin(Angle * 5.0f + NoisePhase2) * 0.035f +
						FMath::Sin(Angle * 9.0f + NoisePhase3) * 0.018f +
						PaintRandom.FRandRange(-0.035f, 0.045f);
					Radius +=
						SafePaintSpread *
						OrganicNoise *
						(0.45f + SafePaintSplatterAmount * 0.75f);

					for (const FPaintContourBurst& Burst : ContourBursts)
					{
						const float AngularDistance =
							GetAngularDistance(Angle, Burst.Angle);
						if (AngularDistance >= Burst.HalfWidth)
						{
							continue;
						}
						const float BurstAlpha =
							1.0f - AngularDistance / Burst.HalfWidth;
						Radius +=
							Burst.Length *
							FMath::Square(BurstAlpha) *
							(3.0f - 2.0f * BurstAlpha);
					}

					Radius = FMath::Max(
						Radius,
						FMath::Min(BaseHalfWidth, BaseHalfHeight) * 0.78f);
					ContourRadii.Add(Radius);
					PaintPatch.Add(ImpactCenter + Direction * Radius);
				}
				AppendPaintPolygon(
					PaintPatch,
					FVector2D::ZeroVector);

				auto GetContourPoint = [&](
					float Angle,
					float ExtraDistance)
				{
					float WrappedAngle = FMath::Fmod(
						Angle,
						2.0f * PI);
					if (WrappedAngle < 0.0f)
					{
						WrappedAngle += 2.0f * PI;
					}
					const int32 SegmentIndex =
						FMath::RoundToInt(
							WrappedAngle /
							(2.0f * PI) *
							static_cast<float>(PaintContourSegments)) %
						PaintContourSegments;
					const FVector2D Direction(
						FMath::Cos(Angle),
						FMath::Sin(Angle));
					return ImpactCenter +
						Direction *
							(ContourRadii[SegmentIndex] + ExtraDistance);
				};

				// Detached tapered flicks create readable negative space between
				// the main stain and the fastest paint thrown from the impact.
				const int32 FlickCount = FMath::RoundToInt(
					FMath::Lerp(5.0f, 19.0f, SafePaintSplatterAmount));
				for (int32 FlickIndex = 0;
					 FlickIndex < FlickCount;
					 ++FlickIndex)
				{
					float Angle =
						PaintRandom.FRandRange(0.0f, 2.0f * PI);
					if (FMath::Sin(Angle) < -0.55f &&
						PaintRandom.FRand() < 0.6f)
					{
						Angle = -Angle;
					}
					const FVector2D Direction(
						FMath::Cos(Angle),
						FMath::Sin(Angle));
					const FVector2D Tangent(
						-Direction.Y,
						Direction.X);
					const float Gap =
						SafePaintSpread *
						PaintRandom.FRandRange(0.045f, 0.16f);
					const FVector2D FlickStart =
						GetContourPoint(Angle, Gap);
					const float FlickLength =
						SafePaintSpread *
						PaintRandom.FRandRange(0.18f, 1.3f) *
						(0.5f + SafePaintSplatterAmount * 0.65f);
					const float StartHalfWidth =
						SafePaintSpread *
						PaintRandom.FRandRange(0.018f, 0.065f);
					const float TipHalfWidth =
						StartHalfWidth *
						PaintRandom.FRandRange(0.08f, 0.3f);
					const FVector2D FlickEnd =
						FlickStart + Direction * FlickLength;
					const TArray<FVector2D> FlickProfile = {
						FlickStart - Tangent * StartHalfWidth,
						FlickEnd - Tangent * TipHalfWidth,
						FlickEnd + Tangent * TipHalfWidth,
						FlickStart + Tangent * StartHalfWidth
					};
					AppendPaintPolygon(
						FlickProfile,
						FVector2D::ZeroVector);
				}

				// Irregular, directionally stretched satellites look much less
				// procedural than identical circular droplets.
				const int32 DropletCount = FMath::RoundToInt(
					FMath::Lerp(7.0f, 31.0f, SafePaintSplatterAmount));
				for (int32 DropletIndex = 0;
					 DropletIndex < DropletCount;
					 ++DropletIndex)
				{
					const float Angle =
						PaintRandom.FRandRange(0.0f, 2.0f * PI);
					const FVector2D Direction(
						FMath::Cos(Angle),
						FMath::Sin(Angle));
					const FVector2D Tangent(
						-Direction.Y,
						Direction.X);
					const float Gap =
						SafePaintSpread *
						PaintRandom.FRandRange(0.2f, 1.75f);
					FVector2D DropletCenter =
						GetContourPoint(Angle, Gap);
					const float BaseRadius =
						SafePaintSpread *
						PaintRandom.FRandRange(0.025f, 0.115f);
					float RadialRadius =
						BaseRadius *
						PaintRandom.FRandRange(0.8f, 1.75f);
					float TangentRadius =
						BaseRadius *
						PaintRandom.FRandRange(0.62f, 1.05f);
					if (Direction.Y < -0.25f &&
						PaintRandom.FRand() < 0.55f)
					{
						RadialRadius *=
							PaintRandom.FRandRange(1.35f, 2.4f);
						TangentRadius *= 0.72f;
						DropletCenter.Y -= RadialRadius * 0.2f;
					}

					const float DropletPhase =
						PaintRandom.FRandRange(0.0f, 2.0f * PI);
					TArray<FVector2D> DropletProfile;
					DropletProfile.Reserve(PaintDropletSegments);
					for (int32 DropletSegmentIndex = 0;
						 DropletSegmentIndex < PaintDropletSegments;
						 ++DropletSegmentIndex)
					{
						const float DropletAngle =
							2.0f * PI *
							static_cast<float>(DropletSegmentIndex) /
							static_cast<float>(PaintDropletSegments);
						const float EdgeVariation =
							1.0f +
							FMath::Sin(
								DropletAngle * 3.0f +
								DropletPhase) *
								0.08f +
							PaintRandom.FRandRange(-0.055f, 0.055f);
						DropletProfile.Add(
							Direction *
								(FMath::Cos(DropletAngle) *
								 RadialRadius *
								 EdgeVariation) +
							Tangent *
								(FMath::Sin(DropletAngle) *
								 TangentRadius *
								 EdgeVariation));
					}
					AppendPaintPolygon(
						DropletProfile,
						DropletCenter);
				}

				// A few hero drips replace the former uniform comb. Each run has a
				// soft shoulder, narrow neck, slight gravity wobble, and heavy end.
				const int32 DripCount = FMath::RoundToInt(
					FMath::Lerp(2.0f, 7.0f, SafePaintSplatterAmount));
				for (int32 DripIndex = 0;
					 DripIndex < DripCount;
					 ++DripIndex)
				{
					const float DripAngle =
						-PI * 0.5f +
						PaintRandom.FRandRange(-0.58f, 0.58f);
					const FVector2D DripAnchor =
						GetContourPoint(
							DripAngle,
							-SafePaintSpread * 0.025f);
					const float DripLength =
						SafePaintSpread *
						PaintRandom.FRandRange(0.55f, 2.8f) *
						(0.58f + SafePaintSplatterAmount * 0.52f);
					const float TopHalfWidth =
						SafePaintSpread *
						PaintRandom.FRandRange(0.035f, 0.09f);
					const float NeckHalfWidth =
						TopHalfWidth *
						PaintRandom.FRandRange(0.28f, 0.58f);
					const float BulbRadius =
						TopHalfWidth *
						PaintRandom.FRandRange(0.5f, 0.88f);
					const float EndOffsetX =
						SafePaintSpread *
						PaintRandom.FRandRange(-0.075f, 0.075f);
					const float TopZ = DripAnchor.Y;
					const float NeckZ =
						TopZ - DripLength * 0.64f;
					const float BulbCenterZ =
						TopZ - DripLength;
					const float EndX =
						DripAnchor.X + EndOffsetX;
					const float NeckX =
						FMath::Lerp(
							DripAnchor.X,
							EndX,
							0.62f);
					const TArray<FVector2D> DripProfile = {
						FVector2D(
							EndX,
							BulbCenterZ - BulbRadius),
						FVector2D(
							EndX + BulbRadius * 0.78f,
							BulbCenterZ - BulbRadius * 0.45f),
						FVector2D(
							EndX + BulbRadius,
							BulbCenterZ + BulbRadius * 0.25f),
						FVector2D(
							NeckX + NeckHalfWidth,
							NeckZ),
						FVector2D(
							DripAnchor.X + TopHalfWidth,
							TopZ),
						FVector2D(
							DripAnchor.X - TopHalfWidth,
							TopZ),
						FVector2D(
							NeckX - NeckHalfWidth,
							NeckZ),
						FVector2D(
							EndX - BulbRadius,
							BulbCenterZ + BulbRadius * 0.25f),
						FVector2D(
							EndX - BulbRadius * 0.78f,
							BulbCenterZ - BulbRadius * 0.45f)
					};
					AppendPaintPolygon(
						DripProfile,
						FVector2D::ZeroVector);
				}
			};
			const float OuterWidth =
				OpeningWidth +
				(bPaintSplatter
					? SafePaintSpread
					: SafeOpeningObjectBorder) *
					2.0f;
			const float OuterHeight =
				OpeningHeight +
				(bPaintSplatter
					? SafePaintSpread
					: SafeOpeningObjectBorder) *
					(bTouchesGround ? 1.0f : 2.0f);
			const float OuterCenterZ =
				CenterZ +
				(bTouchesGround
					? SafeOpeningObjectBorder * 0.5f
					: 0.0f);

			FVector ObjectLocation = SurfaceCenter;
			if (bHorizontalAlongY)
			{
				ObjectLocation.Y += CenterHorizontal;
			}
			else
			{
				ObjectLocation.X += CenterHorizontal;
			}
			ObjectLocation.Z = OuterCenterZ;

			const FRotator ObjectRotation(
				0.0f,
				bHorizontalAlongY ? 90.0f : 0.0f,
				0.0f);
			FVector WallCutterLocation = ObjectLocation;
			float WallCutterHeight = OuterHeight;
			if (bTouchesGround)
			{
				WallCutterHeight += BooleanTolerance;
				WallCutterLocation.Z -= BooleanTolerance * 0.5f;
			}

			UDynamicMesh* WallCutter = NewObject<UDynamicMesh>(this);
			FGeometryScriptPrimitiveOptions WallCutterOptions =
				PrimitiveOptions;
			WallCutterOptions.MaterialID = bTouchesGround
				? DoorRevealMaterialID
				: WindowRevealMaterialID;
			if (bPaintSplatter)
			{
				// Paint sits on the wall instead of filling a larger rectangular
				// frame cut, so only remove the actual window profile.
				AppendOpeningCutter(
					WallCutter,
					WallCutterOptions,
					0.0f);
			}
			else
			{
				UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
					WallCutter,
					WallCutterOptions,
					FTransform::Identity,
					OuterWidth,
					CutterDepth,
					WallCutterHeight,
					0,
					0,
					0,
					EGeometryScriptPrimitiveOriginMode::Center);
			}
			UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
				TargetWallMesh,
				FTransform::Identity,
				WallCutter,
				FTransform(ObjectRotation, WallCutterLocation),
				EGeometryScriptBooleanOperation::Subtract,
				BooleanOptions);

			FVector OpeningObjectLocation = ObjectLocation;
			if (bPaintSplatter)
			{
				const FVector LocalPaintOffset(
					0.0f,
					ExteriorLocalYSign *
						(SafeThickness * 0.5f +
							PaintSurfaceDepth * 0.5f +
							PaintSurfaceOffset),
					0.0f);
				OpeningObjectLocation +=
					ObjectRotation.RotateVector(LocalPaintOffset);
			}

			UDynamicMeshComponent* OpeningObjectComponent =
				CreateOpeningObjectComponent(
					bTouchesGround,
					OpeningObjectLocation,
					ObjectRotation);
			UDynamicMesh* OpeningObjectMesh =
				OpeningObjectComponent->GetDynamicMesh();
			if (bPaintSplatter)
			{
				FGeometryScriptPrimitiveOptions PaintOptions =
					PrimitiveOptions;
				PaintOptions.MaterialID = 0;
				AppendPaintSplatter(PaintOptions);
				OpeningObjectComponent->ComponentTags.AddUnique(
					PaintSplatterComponentTag);
				OpeningObjectComponent->SetCastShadow(false);
			}
			else
			{
				UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
					OpeningObjectMesh,
					PrimitiveOptions,
					FTransform::Identity,
					OuterWidth,
					SafeThickness,
					OuterHeight,
					0,
					0,
					0,
					EGeometryScriptPrimitiveOriginMode::Center);
			}

			UDynamicMesh* OpeningCutter = NewObject<UDynamicMesh>(this);
			FGeometryScriptPrimitiveOptions OpeningCutterOptions =
				PrimitiveOptions;
			OpeningCutterOptions.MaterialID =
				OpeningObjectRevealMaterialID;
			AppendOpeningCutter(
				OpeningCutter,
				OpeningCutterOptions,
				bTouchesGround ? BooleanTolerance * 2.0f : 0.0f);
			const float LocalOpeningCenterZ =
				CenterZ - OuterCenterZ;
			if (!bPaintSplatter)
			{
				UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
					OpeningObjectMesh,
					FTransform::Identity,
					OpeningCutter,
					FTransform(FVector(
						0.0f,
						0.0f,
						LocalOpeningCenterZ)),
					EGeometryScriptBooleanOperation::Subtract,
					BooleanOptions);
			}

			const float OpeningSafetyClearance = FMath::Min(
				0.75f,
				FMath::Min(OpeningWidth, OpeningHeight) * 0.05f);
			FFacadeOpeningCutout OpeningCutout;
			OpeningCutout.CutterMesh = OpeningCutter;
			OpeningCutout.Center = FVector2D(
				CenterHorizontal,
				CenterZ);
			OpeningCutout.HalfExtents = FVector2D(
				OpeningWidth * 0.5f + OpeningSafetyClearance,
				OpeningHeight * 0.5f +
					OpeningSafetyClearance +
					(bTouchesGround ? BooleanTolerance * 2.0f : 0.0f));
			OpeningCutout.CutterScale = FVector(
				(OpeningWidth + OpeningSafetyClearance * 2.0f) /
					OpeningWidth,
				1.0f,
				(OpeningHeight + OpeningSafetyClearance * 2.0f) /
					OpeningHeight);
			FacadeOpeningCutouts.Add(OpeningCutout);

			if (bPaintSplatter)
			{
				FPendingPaintObject PendingPaintObject;
				PendingPaintObject.Component = OpeningObjectComponent;
				PendingPaintObject.Center = FVector2D(
					CenterHorizontal,
					CenterZ);
				PendingPaintObject.Pieces = MoveTemp(CurrentPaintPieces);
				PendingPaintObjects.Add(MoveTemp(PendingPaintObject));
			}

			UMaterialInterface* SafeObjectMaterial =
				OpeningObjectMaterial
					? OpeningObjectMaterial
					: FacadeWallMaterial;
			UMaterialInterface* ObjectRevealMaterial =
				bPaintSplatter
					? SafeObjectMaterial
					: bTouchesGround
					? DoorRevealMaterial.Get()
					: WindowRevealMaterial.Get();
			OpeningObjectComponent->SetMaterial(
				0,
				SafeObjectMaterial);
			OpeningObjectComponent->SetMaterial(
				OpeningObjectRevealMaterialID,
				ObjectRevealMaterial
					? ObjectRevealMaterial
					: SafeObjectMaterial);
		};

		const float OpeningSideClearance = FMath::Max(
			MinimumOpeningDimension,
			SafeThickness * 0.25f);
		const float HorizontalEdgeMargin = FMath::Min(
			FMath::Max(
				SafeThickness + OpeningSideClearance,
				SafeOpeningObjectBorder),
			FacadeSpan * 0.45f);
		const float FacadeLeft = -FacadeSpan * 0.5f + HorizontalEdgeMargin;
		const float FacadeRight = FacadeSpan * 0.5f - HorizontalEdgeMargin;

		const FSimpleHouse19DoorSettings& FacadeDoor = FacadeSettings.Door;
		float SafeDoorWidth = 0.0f;
		float SafeDoorHeight = 0.0f;
		if (FacadeDoor.bHasDoor)
		{
			const float MaximumDoorHeight = FMath::Max(
				MinimumOpeningDimension,
				SafeFloorHeight - SafeOpeningObjectBorder);
			SafeDoorHeight = FMath::Clamp(
				FacadeDoor.DoorHeight,
				MinimumOpeningDimension,
				MaximumDoorHeight);
			const bool bArchedDoor =
				FacadeDoor.DoorType == ESimpleHouse19DoorType::Arched;
			const float MaximumDoorWidthForShape = bArchedDoor
				? FMath::Max(
					MinimumOpeningDimension,
					2.0f * (SafeDoorHeight - MinimumOpeningDimension))
				: FacadeRight - FacadeLeft;
			const float MaximumDoorWidth = FMath::Max(
				MinimumOpeningDimension,
				FMath::Min(
					FacadeRight - FacadeLeft,
					MaximumDoorWidthForShape));
			SafeDoorWidth = FMath::Clamp(
				FacadeDoor.DoorWidth,
				MinimumOpeningDimension,
				MaximumDoorWidth);

			UMaterialInterface* DoorObjectMaterial =
				FacadeDoor.DoorObjectMaterial
					? FacadeDoor.DoorObjectMaterial.Get()
					: FacadeWallMaterial;
			CutOpening(
				0.0f,
				SafeDoorHeight * 0.5f,
				SafeDoorWidth,
				SafeDoorHeight,
				true,
				bArchedDoor
					? ESimpleHouse19WindowType::Arched
					: ESimpleHouse19WindowType::Rectangular,
				0.0f,
				ESimpleHouse19WindowBorderStyle::ParametricFrame,
				0.0f,
				0.0f,
				0,
				DoorObjectMaterial);
		}

		for (int32 FloorIndex = 0; FloorIndex < SafeFloorCount; ++FloorIndex)
		{
			const FSimpleHouse19FloorWindowSettings& WindowSettings =
				FacadeSettings.FloorWindows[FloorIndex];
			if (!WindowSettings.bHasWindows || WindowSettings.WindowCount <= 0)
			{
				continue;
			}

			const float VerticalMargin = FMath::Min(
				FMath::Max3(
					SafeThickness * 0.5f,
					MinimumOpeningDimension,
					SafeOpeningObjectBorder),
				SafeFloorHeight * 0.45f);
			const float MaximumWindowHeight = FMath::Max(
				MinimumOpeningDimension,
				SafeFloorHeight - VerticalMargin * 2.0f);
			const bool bCircularWindow =
				WindowSettings.WindowType ==
				ESimpleHouse19WindowType::Circular;
			const float RequestedWindowHeight = bCircularWindow
				? WindowSettings.WindowWidth
				: WindowSettings.WindowHeight;
			const float SafeWindowHeight = FMath::Clamp(
				RequestedWindowHeight,
				MinimumOpeningDimension,
				MaximumWindowHeight);
			const float WindowCenterZ =
				static_cast<float>(FloorIndex) * SafeFloorHeight +
				SafeFloorHeight * 0.5f;
			const float WindowBottom =
				WindowCenterZ - SafeWindowHeight * 0.5f;
			const float WindowTop =
				WindowCenterZ + SafeWindowHeight * 0.5f;

			TArray<FHorizontalSpan, TInlineAllocator<2>> AvailableSpans;
			AvailableSpans.Add({FacadeLeft, FacadeRight});

			const bool bOverlapsDoorVertically =
				FloorIndex == 0 &&
				FacadeDoor.bHasDoor &&
				WindowBottom < SafeDoorHeight &&
				WindowTop > 0.0f;
			if (bOverlapsDoorVertically)
			{
				AvailableSpans.Reset();
				const float DoorClearance = FMath::Max(
					FMath::Max(
						MinimumOpeningDimension,
						SafeThickness * 0.25f),
					SafeOpeningObjectBorder * 2.0f);
				const FHorizontalSpan LeftSpan{
					FacadeLeft,
					-SafeDoorWidth * 0.5f - DoorClearance};
				const FHorizontalSpan RightSpan{
					SafeDoorWidth * 0.5f + DoorClearance,
					FacadeRight};
				if (LeftSpan.Length() >= MinimumOpeningDimension)
				{
					AvailableSpans.Add(LeftSpan);
				}
				if (RightSpan.Length() >= MinimumOpeningDimension)
				{
					AvailableSpans.Add(RightSpan);
				}
			}

			if (AvailableSpans.IsEmpty())
			{
				continue;
			}

			TArray<int32, TInlineAllocator<2>> SpanCapacities;
			SpanCapacities.SetNumZeroed(AvailableSpans.Num());
			int32 TotalCapacity = 0;
			for (int32 SpanIndex = 0;
				 SpanIndex < AvailableSpans.Num();
				 ++SpanIndex)
			{
				SpanCapacities[SpanIndex] = FMath::FloorToInt(
					AvailableSpans[SpanIndex].Length() /
					MinimumOpeningDimension);
				TotalCapacity += SpanCapacities[SpanIndex];
			}

			const int32 ActualWindowCount = FMath::Min(
				FMath::Clamp(
					WindowSettings.WindowCount,
					0,
					MaximumWindowCountPerFloor),
				TotalCapacity);
			if (ActualWindowCount <= 0)
			{
				continue;
			}

			TArray<int32, TInlineAllocator<2>> WindowsPerSpan;
			WindowsPerSpan.SetNumZeroed(AvailableSpans.Num());
			if (AvailableSpans.Num() == 1)
			{
				WindowsPerSpan[0] = ActualWindowCount;
			}
			else if (
				WindowSettings.Alignment ==
				ESimpleHouse19WindowAlignment::Left)
			{
				WindowsPerSpan[0] = FMath::Min(
					ActualWindowCount,
					SpanCapacities[0]);
				WindowsPerSpan[1] =
					ActualWindowCount - WindowsPerSpan[0];
			}
			else if (
				WindowSettings.Alignment ==
				ESimpleHouse19WindowAlignment::Right)
			{
				WindowsPerSpan[1] = FMath::Min(
					ActualWindowCount,
					SpanCapacities[1]);
				WindowsPerSpan[0] =
					ActualWindowCount - WindowsPerSpan[1];
			}
			else
			{
				WindowsPerSpan[0] = FMath::Min(
					(ActualWindowCount + 1) / 2,
					SpanCapacities[0]);
				WindowsPerSpan[1] =
					ActualWindowCount - WindowsPerSpan[0];
				if (WindowsPerSpan[1] > SpanCapacities[1])
				{
					const int32 Overflow =
						WindowsPerSpan[1] - SpanCapacities[1];
					WindowsPerSpan[1] -= Overflow;
					WindowsPerSpan[0] += Overflow;
				}
			}

			float MaximumSpacing = TNumericLimits<float>::Max();
			bool bHasSpacingSlot = false;
			for (int32 SpanIndex = 0;
				 SpanIndex < AvailableSpans.Num();
				 ++SpanIndex)
			{
				const int32 SpanWindowCount =
					WindowsPerSpan[SpanIndex];
				if (SpanWindowCount > 1)
				{
					bHasSpacingSlot = true;
					MaximumSpacing = FMath::Min(
						MaximumSpacing,
						(AvailableSpans[SpanIndex].Length() -
							static_cast<float>(SpanWindowCount) *
							MinimumOpeningDimension) /
							static_cast<float>(SpanWindowCount - 1));
				}
			}
			const float SafeWindowSpacing = bHasSpacingSlot
				? FMath::Clamp(
					WindowSettings.WindowSpacing,
					FMath::Min(
						SafeOpeningObjectBorder * 2.0f,
						FMath::Max(0.0f, MaximumSpacing)),
					FMath::Max(0.0f, MaximumSpacing))
				: 0.0f;

			float MaximumWindowWidth = TNumericLimits<float>::Max();
			for (int32 SpanIndex = 0;
				 SpanIndex < AvailableSpans.Num();
				 ++SpanIndex)
			{
				const int32 SpanWindowCount =
					WindowsPerSpan[SpanIndex];
				if (SpanWindowCount > 0)
				{
					MaximumWindowWidth = FMath::Min(
						MaximumWindowWidth,
						(AvailableSpans[SpanIndex].Length() -
							static_cast<float>(SpanWindowCount - 1) *
							SafeWindowSpacing) /
							static_cast<float>(SpanWindowCount));
				}
			}
			const float MaximumWindowWidthForType = bCircularWindow
				? FMath::Min(MaximumWindowWidth, MaximumWindowHeight)
				: MaximumWindowWidth;
			const float SafeWindowWidth = FMath::Clamp(
				WindowSettings.WindowWidth,
				MinimumOpeningDimension,
				FMath::Max(
					MinimumOpeningDimension,
					MaximumWindowWidthForType));
			const float SafeRoundedDegree = FMath::Clamp(
				WindowSettings.RoundedDegree,
				0.0f,
				100.0f);
			const float MaximumRoundedCornerRadius = FMath::Max(
				0.0f,
				FMath::Min(SafeWindowWidth, SafeWindowHeight) * 0.5f -
					0.01f);
			const float RoundedCornerRadius =
				WindowSettings.WindowType ==
					ESimpleHouse19WindowType::RoundedCorners
					? MaximumRoundedCornerRadius *
						(SafeRoundedDegree / 100.0f)
					: 0.0f;
			int32 FloorWindowObjectIndex = 0;

			for (int32 SpanIndex = 0;
				 SpanIndex < AvailableSpans.Num();
				 ++SpanIndex)
			{
				const int32 SpanWindowCount =
					WindowsPerSpan[SpanIndex];
				if (SpanWindowCount <= 0)
				{
					continue;
				}

				const FHorizontalSpan& Span =
					AvailableSpans[SpanIndex];
				const float GroupWidth =
					static_cast<float>(SpanWindowCount) *
					SafeWindowWidth +
					static_cast<float>(SpanWindowCount - 1) *
					SafeWindowSpacing;
				float FirstWindowLeft = Span.Left;

				if (
					WindowSettings.Alignment ==
					ESimpleHouse19WindowAlignment::Right)
				{
					FirstWindowLeft = Span.Right - GroupWidth;
				}
				else if (
					WindowSettings.Alignment ==
					ESimpleHouse19WindowAlignment::Center)
				{
					if (AvailableSpans.Num() == 1)
					{
						FirstWindowLeft =
							(Span.Left + Span.Right - GroupWidth) *
							0.5f;
					}
					else
					{
						FirstWindowLeft = SpanIndex == 0
							? Span.Right - GroupWidth
							: Span.Left;
					}
				}

				for (int32 WindowIndex = 0;
					 WindowIndex < SpanWindowCount;
					 ++WindowIndex)
				{
					const float WindowCenterHorizontal =
						FirstWindowLeft +
						SafeWindowWidth * 0.5f +
						static_cast<float>(WindowIndex) *
						(SafeWindowWidth + SafeWindowSpacing);
					UMaterialInterface* WindowObjectMaterial =
						FacadeWallMaterial;
					if (WindowSettings.WindowObjectMaterials.IsValidIndex(
						FloorWindowObjectIndex) &&
						WindowSettings.WindowObjectMaterials[
							FloorWindowObjectIndex])
					{
						WindowObjectMaterial =
							WindowSettings.WindowObjectMaterials[
								FloorWindowObjectIndex].Get();
					}
					CutOpening(
						WindowCenterHorizontal,
						WindowCenterZ,
						SafeWindowWidth,
						bCircularWindow
							? SafeWindowWidth
							: SafeWindowHeight,
						false,
						WindowSettings.WindowType,
						RoundedCornerRadius,
						WindowSettings.WindowBorderStyle,
						WindowSettings.PaintSpread,
						WindowSettings.PaintSplatterAmount,
						WindowSettings.PaintSplatterSeed +
							FloorIndex * 7919 +
							WindowObjectCounter * 104729,
						WindowObjectMaterial);
					++FloorWindowObjectIndex;
				}
			}
		}

		// All openings on this facade are now known. Cut every potentially
		// overlapping door and window from every individual paint solid before
		// combining the solids into their component. Processing clean pieces
		// avoids the unreliable result of boolean-cutting one self-overlapping
		// splash mesh.
		FGeometryScriptMeshBooleanOptions PaintBooleanOptions =
			BooleanOptions;
		PaintBooleanOptions.bSimplifyOutput = false;
		// A spray or drip can sit completely inside an opening. Geometry Script
		// keeps the original target when an empty boolean result is disallowed,
		// which leaves those small paint pieces visible in windows and doors.
		PaintBooleanOptions.bAllowEmptyResult = true;
		for (FPendingPaintObject& PaintObject : PendingPaintObjects)
		{
			if (!IsValid(PaintObject.Component))
			{
				continue;
			}

			UDynamicMesh* FinalPaintMesh =
				PaintObject.Component->GetDynamicMesh();
			FinalPaintMesh->Reset();
			for (FPendingPaintPiece& PaintPiece : PaintObject.Pieces)
			{
				if (!PaintPiece.Mesh)
				{
					continue;
				}

				for (const FFacadeOpeningCutout& Opening :
					 FacadeOpeningCutouts)
				{
					if (!Opening.CutterMesh)
					{
						continue;
					}

					const FVector2D LocalOpeningCenter =
						Opening.Center - PaintObject.Center;
					const FVector2D OpeningBoundsMin =
						LocalOpeningCenter - Opening.HalfExtents;
					const FVector2D OpeningBoundsMax =
						LocalOpeningCenter + Opening.HalfExtents;
					const bool bBoundsOverlap =
						PaintPiece.BoundsMax.X >= OpeningBoundsMin.X &&
						PaintPiece.BoundsMin.X <= OpeningBoundsMax.X &&
						PaintPiece.BoundsMax.Y >= OpeningBoundsMin.Y &&
						PaintPiece.BoundsMin.Y <= OpeningBoundsMax.Y;
					if (!bBoundsOverlap)
					{
						continue;
					}

					UGeometryScriptLibrary_MeshBooleanFunctions::
						ApplyMeshBoolean(
							PaintPiece.Mesh,
							FTransform::Identity,
							Opening.CutterMesh,
							FTransform(
								FQuat::Identity,
								FVector(
									LocalOpeningCenter.X,
									0.0f,
									LocalOpeningCenter.Y),
								Opening.CutterScale),
							EGeometryScriptBooleanOperation::Subtract,
							PaintBooleanOptions);
				}

				UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(
					FinalPaintMesh,
					PaintPiece.Mesh,
					FTransform::Identity);
			}
		}
	};

	BuildFacadeOpenings(
		FrontWallMesh,
		FrontFacade,
		FrontWallMaterial
			? FrontWallMaterial.Get()
			: Material.Get(),
		SafeWidth,
		InnerWidth,
		FVector(0.0f, -SafeDepth * 0.5f + SafeThickness * 0.5f, 0.0f),
		false,
		-1.0f);
	BuildFacadeOpenings(
		BackWallMesh,
		BackFacade,
		BackWallMaterial
			? BackWallMaterial.Get()
			: Material.Get(),
		SafeWidth,
		InnerWidth,
		FVector(0.0f, SafeDepth * 0.5f - SafeThickness * 0.5f, 0.0f),
		false,
		1.0f);
	BuildFacadeOpenings(
		SideWall1Mesh,
		SideWall1Facade,
		SideWall1Material
			? SideWall1Material.Get()
			: Material.Get(),
		SafeDepth,
		SafeDepth,
		FVector(-SafeWidth * 0.5f + SafeThickness * 0.5f, 0.0f, 0.0f),
		true,
		1.0f);
	BuildFacadeOpenings(
		SideWall2Mesh,
		SideWall2Facade,
		SideWall2Material
			? SideWall2Material.Get()
			: Material.Get(),
		SafeDepth,
		SafeDepth,
		FVector(SafeWidth * 0.5f - SafeThickness * 0.5f, 0.0f, 0.0f),
		true,
		-1.0f);

	FGeometryScriptSplitNormalsOptions SplitOptions;
	SplitOptions.bSplitByOpeningAngle = true;
	SplitOptions.OpeningAngleDeg = 60.0f;
	FGeometryScriptCalculateNormalsOptions NormalOptions;
	for (UDynamicMesh* PartMesh : PartMeshes)
	{
		UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(
			PartMesh,
			SplitOptions,
			NormalOptions);
	}
	auto FinalizeOpeningObjects = [&](const TArray<TObjectPtr<UDynamicMeshComponent>>& Components)
	{
		for (UDynamicMeshComponent* Component : Components)
		{
			if (!IsValid(Component))
			{
				continue;
			}
			UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(
				Component->GetDynamicMesh(),
				SplitOptions,
				NormalOptions);
			Component->InvalidateAutoCalculatedTangents();
			Component->NotifyMeshUpdated();
			const bool bEnableObjectCollision =
				bEnableCollision &&
				!Component->ComponentTags.Contains(
					PaintSplatterComponentTag);
			Component->SetCollisionEnabled(
				bEnableObjectCollision
					? ECollisionEnabled::QueryAndPhysics
					: ECollisionEnabled::NoCollision);
			Component->SetComplexAsSimpleCollisionEnabled(
				bEnableObjectCollision,
				true);
		}
	};
	FinalizeOpeningObjects(WindowObjectComponents);
	FinalizeOpeningObjects(DoorObjectComponents);

	const TArray<UDynamicMeshComponent*> PartComponents = {
		RoofComponent,
		CeilingComponent,
		GroundComponent,
		FrontWallComponent,
		BackWallComponent,
		SideWall1Component,
		SideWall2Component
	};
	for (UDynamicMeshComponent* Component : PartComponents)
	{
		FinalizeComponent(Component);
	}

	ApplyPartVisibility();
}

void ASimpleHouseActor19::ApplyPartVisibility()
{
	RoofComponent->SetVisibility(PartVisibility.bRoofVisible, true);
	CeilingComponent->SetVisibility(PartVisibility.bCeilingVisible, true);
	GroundComponent->SetVisibility(PartVisibility.bGroundVisible, true);
	FrontWallComponent->SetVisibility(PartVisibility.bFrontWallVisible, true);
	BackWallComponent->SetVisibility(PartVisibility.bBackWallVisible, true);
	SideWall1Component->SetVisibility(PartVisibility.bSideWall1Visible, true);
	SideWall2Component->SetVisibility(PartVisibility.bSideWall2Visible, true);
}

void ASimpleHouseActor19::SetPartVisibility(ESimpleHouse19Part Part, bool bVisible)
{
	switch (Part)
	{
	case ESimpleHouse19Part::Roof:
		PartVisibility.bRoofVisible = bVisible;
		break;
	case ESimpleHouse19Part::Ceiling:
		PartVisibility.bCeilingVisible = bVisible;
		break;
	case ESimpleHouse19Part::Ground:
		PartVisibility.bGroundVisible = bVisible;
		break;
	case ESimpleHouse19Part::FrontWall:
		PartVisibility.bFrontWallVisible = bVisible;
		break;
	case ESimpleHouse19Part::BackWall:
		PartVisibility.bBackWallVisible = bVisible;
		break;
	case ESimpleHouse19Part::LeftWall:
		PartVisibility.bSideWall1Visible = bVisible;
		break;
	case ESimpleHouse19Part::RightWall:
		PartVisibility.bSideWall2Visible = bVisible;
		break;
	default:
		break;
	}

	if (UDynamicMeshComponent* Component = GetPartComponent(Part))
	{
		Component->SetVisibility(bVisible, true);
	}
}

UDynamicMeshComponent* ASimpleHouseActor19::GetPartComponent(ESimpleHouse19Part Part) const
{
	switch (Part)
	{
	case ESimpleHouse19Part::Roof:
		return RoofComponent;
	case ESimpleHouse19Part::Ceiling:
		return CeilingComponent;
	case ESimpleHouse19Part::Ground:
		return GroundComponent;
	case ESimpleHouse19Part::FrontWall:
		return FrontWallComponent;
	case ESimpleHouse19Part::BackWall:
		return BackWallComponent;
	case ESimpleHouse19Part::LeftWall:
		return SideWall1Component;
	case ESimpleHouse19Part::RightWall:
		return SideWall2Component;
	default:
		return nullptr;
	}
}

void ASimpleHouseActor19::CarveSphereAtWorldLocation(
	FVector WorldLocation,
	float Radius,
	float DepthFraction)
{
	if (!bEnableDamage)
	{
		return;
	}

	const float SafeRadius = FMath::Max(Radius, 1.0f);
	const float SafeFloorHeight = FMath::Max(
		Building.FloorHeight,
		MinimumDimension);
	const int32 SafeFloorCount = FMath::Clamp(
		Building.FloorCount,
		1,
		MaximumFloorCount);
	const FVector LocalInteriorCenter(
		0.0f,
		0.0f,
		SafeFloorHeight * static_cast<float>(SafeFloorCount) * 0.5f);
	const FVector WorldInteriorCenter =
		GetActorTransform().TransformPosition(LocalInteriorCenter);
	const FVector InwardDirection =
		(WorldInteriorCenter - WorldLocation).GetSafeNormal();
	const float SafeDepthFraction = FMath::Clamp(
		DepthFraction,
		0.0f,
		1.0f);
	const FVector CutterCenter =
		WorldLocation +
		InwardDirection * SafeRadius * SafeDepthFraction;

	UDynamicMesh* CutterMesh = NewObject<UDynamicMesh>(this);
	FGeometryScriptPrimitiveOptions CutterOptions;
	CutterOptions.PolygroupMode =
		EGeometryScriptPrimitivePolygroupMode::PerFace;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
		CutterMesh,
		CutterOptions,
		FTransform::Identity,
		SafeRadius,
		12,
		24,
		EGeometryScriptPrimitiveOriginMode::Center);

	FGeometryScriptMeshBooleanOptions DamageBooleanOptions;
	DamageBooleanOptions.bFillHoles = true;
	DamageBooleanOptions.bSimplifyOutput = true;
	DamageBooleanOptions.bAllowEmptyResult = true;
	DamageBooleanOptions.OutputTransformSpace =
		EGeometryScriptBooleanOutputSpace::TargetTransformSpace;

	FGeometryScriptSplitNormalsOptions SplitOptions;
	SplitOptions.bSplitByOpeningAngle = true;
	SplitOptions.OpeningAngleDeg = 60.0f;
	FGeometryScriptCalculateNormalsOptions NormalOptions;

	TArray<UDynamicMeshComponent*> DamageableComponents = {
		RoofComponent,
		CeilingComponent,
		GroundComponent,
		FrontWallComponent,
		BackWallComponent,
		SideWall1Component,
		SideWall2Component
	};
	for (UDynamicMeshComponent* Component : WindowObjectComponents)
	{
		DamageableComponents.Add(Component);
	}
	for (UDynamicMeshComponent* Component : DoorObjectComponents)
	{
		DamageableComponents.Add(Component);
	}

	const FTransform CutterTransform(
		FRotator::ZeroRotator,
		CutterCenter);
	for (UDynamicMeshComponent* Component : DamageableComponents)
	{
		if (!IsValid(Component))
		{
			continue;
		}

		UDynamicMesh* TargetMesh = Component->GetDynamicMesh();
		if (!TargetMesh)
		{
			continue;
		}

		const FBox WorldBounds = Component->Bounds.GetBox();
		if (WorldBounds.ComputeSquaredDistanceToPoint(CutterCenter) >
			FMath::Square(SafeRadius))
		{
			continue;
		}

		const bool bHadCollision =
			Component->GetCollisionEnabled() !=
			ECollisionEnabled::NoCollision;
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
			TargetMesh,
			Component->GetComponentTransform(),
			CutterMesh,
			CutterTransform,
			EGeometryScriptBooleanOperation::Subtract,
			DamageBooleanOptions);
		UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(
			TargetMesh,
			SplitOptions,
			NormalOptions);

		Component->InvalidateAutoCalculatedTangents();
		Component->NotifyMeshUpdated();
		Component->SetComplexAsSimpleCollisionEnabled(
			bHadCollision,
			true);
	}
}
