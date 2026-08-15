// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleHouseActor14.h"

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
	constexpr int32 WindowRevealMaterialID = 1;
	constexpr int32 DoorRevealMaterialID = 2;
	constexpr int32 OpeningObjectRevealMaterialID = 1;

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

ASimpleHouseActor14::ASimpleHouseActor14(const FObjectInitializer& ObjectInitializer)
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

void ASimpleHouseActor14::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SynchronizeFloorWindowSettings();
	RebuildMesh();
}

void ASimpleHouseActor14::SynchronizeFloorWindowSettings()
{
	Building.FloorCount = FMath::Clamp(Building.FloorCount, 1, MaximumFloorCount);

	auto SynchronizeFacade = [&](FSimpleHouse14FacadeSettings& Facade)
	{
		Facade.FloorWindows.SetNum(Building.FloorCount);
		for (int32 FloorIndex = 0; FloorIndex < Facade.FloorWindows.Num(); ++FloorIndex)
		{
			FSimpleHouse14FloorWindowSettings& WindowSettings =
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

void ASimpleHouseActor14::FinalizeComponent(UDynamicMeshComponent* Component) const
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

void ASimpleHouseActor14::RebuildMesh()
{
	SynchronizeFloorWindowSettings();

	auto DestroyOpeningObjects = [&](TArray<TObjectPtr<UDynamicMeshComponent>>& Components)
	{
		for (UDynamicMeshComponent* Component : Components)
		{
			if (IsValid(Component))
			{
				RemoveInstanceComponent(Component);
				Component->DestroyComponent();
			}
		}
		Components.Reset();
	};
	DestroyOpeningObjects(WindowObjectComponents);
	DestroyOpeningObjects(DoorObjectComponents);

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
	const bool bPyramidRoof = Roof.RoofType == ESimpleHouse14RoofType::Pyramid;
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
			GroundCoverage == ESimpleHouse14GroundCoverage::FullBuildingFootprint;
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
		const FSimpleHouse14FacadeSettings& FacadeSettings,
		UMaterialInterface* FacadeWallMaterial,
		float FacadeSpan,
		const FVector& SurfaceCenter,
		bool bHorizontalAlongY)
	{
		if (!TargetWallMesh || !FacadeSettings.bEnableOpenings)
		{
			return;
		}

		auto CutOpening = [&](
			float CenterHorizontal,
			float CenterZ,
			float OpeningWidth,
			float OpeningHeight,
			bool bTouchesGround,
			ESimpleHouse14WindowType OpeningType,
			float RoundedCornerRadius,
			UMaterialInterface* OpeningObjectMaterial)
		{
			const float BooleanTolerance = 0.5f;
			const float CutterDepth = SafeThickness * 3.0f;

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

				if (OpeningType == ESimpleHouse14WindowType::Arched)
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
					OpeningType == ESimpleHouse14WindowType::Circular &&
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
						ESimpleHouse14WindowType::RoundedCorners &&
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

			const float OuterWidth =
				OpeningWidth + SafeOpeningObjectBorder * 2.0f;
			const float OuterHeight =
				OpeningHeight +
				SafeOpeningObjectBorder *
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
			UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
				TargetWallMesh,
				FTransform::Identity,
				WallCutter,
				FTransform(ObjectRotation, WallCutterLocation),
				EGeometryScriptBooleanOperation::Subtract,
				BooleanOptions);

			UDynamicMeshComponent* OpeningObjectComponent =
				CreateOpeningObjectComponent(
					bTouchesGround,
					ObjectLocation,
					ObjectRotation);
			UDynamicMesh* OpeningObjectMesh =
				OpeningObjectComponent->GetDynamicMesh();
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

			UMaterialInterface* SafeObjectMaterial =
				OpeningObjectMaterial
					? OpeningObjectMaterial
					: FacadeWallMaterial;
			UMaterialInterface* ObjectRevealMaterial =
				bTouchesGround
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

		const FSimpleHouse14DoorSettings& FacadeDoor = FacadeSettings.Door;
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
				FacadeDoor.DoorType == ESimpleHouse14DoorType::Arched;
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
					? ESimpleHouse14WindowType::Arched
					: ESimpleHouse14WindowType::Rectangular,
				0.0f,
				DoorObjectMaterial);
		}

		for (int32 FloorIndex = 0; FloorIndex < SafeFloorCount; ++FloorIndex)
		{
			const FSimpleHouse14FloorWindowSettings& WindowSettings =
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
				ESimpleHouse14WindowType::Circular;
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
				ESimpleHouse14WindowAlignment::Left)
			{
				WindowsPerSpan[0] = FMath::Min(
					ActualWindowCount,
					SpanCapacities[0]);
				WindowsPerSpan[1] =
					ActualWindowCount - WindowsPerSpan[0];
			}
			else if (
				WindowSettings.Alignment ==
				ESimpleHouse14WindowAlignment::Right)
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
					ESimpleHouse14WindowType::RoundedCorners
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
					ESimpleHouse14WindowAlignment::Right)
				{
					FirstWindowLeft = Span.Right - GroupWidth;
				}
				else if (
					WindowSettings.Alignment ==
					ESimpleHouse14WindowAlignment::Center)
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
						WindowObjectMaterial);
					++FloorWindowObjectIndex;
				}
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
		FVector(0.0f, -SafeDepth * 0.5f + SafeThickness * 0.5f, 0.0f),
		false);
	BuildFacadeOpenings(
		BackWallMesh,
		BackFacade,
		BackWallMaterial
			? BackWallMaterial.Get()
			: Material.Get(),
		SafeWidth,
		FVector(0.0f, SafeDepth * 0.5f - SafeThickness * 0.5f, 0.0f),
		false);
	BuildFacadeOpenings(
		SideWall1Mesh,
		SideWall1Facade,
		SideWall1Material
			? SideWall1Material.Get()
			: Material.Get(),
		SafeDepth,
		FVector(-SafeWidth * 0.5f + SafeThickness * 0.5f, 0.0f, 0.0f),
		true);
	BuildFacadeOpenings(
		SideWall2Mesh,
		SideWall2Facade,
		SideWall2Material
			? SideWall2Material.Get()
			: Material.Get(),
		SafeDepth,
		FVector(SafeWidth * 0.5f - SafeThickness * 0.5f, 0.0f, 0.0f),
		true);

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
			Component->SetCollisionEnabled(
				bEnableCollision
					? ECollisionEnabled::QueryAndPhysics
					: ECollisionEnabled::NoCollision);
			Component->SetComplexAsSimpleCollisionEnabled(
				bEnableCollision,
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

void ASimpleHouseActor14::ApplyPartVisibility()
{
	RoofComponent->SetVisibility(PartVisibility.bRoofVisible, true);
	CeilingComponent->SetVisibility(PartVisibility.bCeilingVisible, true);
	GroundComponent->SetVisibility(PartVisibility.bGroundVisible, true);
	FrontWallComponent->SetVisibility(PartVisibility.bFrontWallVisible, true);
	BackWallComponent->SetVisibility(PartVisibility.bBackWallVisible, true);
	SideWall1Component->SetVisibility(PartVisibility.bSideWall1Visible, true);
	SideWall2Component->SetVisibility(PartVisibility.bSideWall2Visible, true);
}

void ASimpleHouseActor14::SetPartVisibility(ESimpleHouse14Part Part, bool bVisible)
{
	switch (Part)
	{
	case ESimpleHouse14Part::Roof:
		PartVisibility.bRoofVisible = bVisible;
		break;
	case ESimpleHouse14Part::Ceiling:
		PartVisibility.bCeilingVisible = bVisible;
		break;
	case ESimpleHouse14Part::Ground:
		PartVisibility.bGroundVisible = bVisible;
		break;
	case ESimpleHouse14Part::FrontWall:
		PartVisibility.bFrontWallVisible = bVisible;
		break;
	case ESimpleHouse14Part::BackWall:
		PartVisibility.bBackWallVisible = bVisible;
		break;
	case ESimpleHouse14Part::LeftWall:
		PartVisibility.bSideWall1Visible = bVisible;
		break;
	case ESimpleHouse14Part::RightWall:
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

UDynamicMeshComponent* ASimpleHouseActor14::GetPartComponent(ESimpleHouse14Part Part) const
{
	switch (Part)
	{
	case ESimpleHouse14Part::Roof:
		return RoofComponent;
	case ESimpleHouse14Part::Ceiling:
		return CeilingComponent;
	case ESimpleHouse14Part::Ground:
		return GroundComponent;
	case ESimpleHouse14Part::FrontWall:
		return FrontWallComponent;
	case ESimpleHouse14Part::BackWall:
		return BackWallComponent;
	case ESimpleHouse14Part::LeftWall:
		return SideWall1Component;
	case ESimpleHouse14Part::RightWall:
		return SideWall2Component;
	default:
		return nullptr;
	}
}
