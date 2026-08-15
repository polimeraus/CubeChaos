// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleHouseActor5.h"

#include "Components/SceneComponent.h"
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

ASimpleHouseActor5::ASimpleHouseActor5(const FObjectInitializer& ObjectInitializer)
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
	}

	SynchronizeFloorWindowSettings();
}

void ASimpleHouseActor5::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SynchronizeFloorWindowSettings();
	RebuildMesh();
}

void ASimpleHouseActor5::SynchronizeFloorWindowSettings()
{
	Building.FloorCount = FMath::Clamp(Building.FloorCount, 1, MaximumFloorCount);
	FloorWindows.SetNum(Building.FloorCount);

	for (int32 FloorIndex = 0; FloorIndex < FloorWindows.Num(); ++FloorIndex)
	{
		FloorWindows[FloorIndex].FloorNumber = FloorIndex + 1;
	}
}

void ASimpleHouseActor5::FinalizeComponent(UDynamicMeshComponent* Component) const
{
	if (!Component)
	{
		return;
	}

	Component->SetMaterial(0, Material);
	Component->NotifyMeshUpdated();
	Component->SetCollisionEnabled(
		bEnableCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	Component->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}

void ASimpleHouseActor5::RebuildMesh()
{
	SynchronizeFloorWindowSettings();

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
	const float SafeRoofHeight = FMath::Max(Roof.RoofHeight, MinimumDimension);
	const int32 SafeFloorCount = FMath::Clamp(Building.FloorCount, 1, MaximumFloorCount);
	const float TotalWallHeight = SafeFloorHeight * static_cast<float>(SafeFloorCount);
	const float MaximumThickness = 0.45f * FMath::Min3(SafeWidth, SafeDepth, SafeFloorHeight);
	const float SafeThickness = FMath::Clamp(Building.WallThickness, MinimumOpeningDimension, MaximumThickness);
	const float InnerWidth = SafeWidth - SafeThickness * 2.0f;
	const float InnerDepth = SafeDepth - SafeThickness * 2.0f;

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	// Front/back own the rectangular area between the side walls and the
	// triangular gables above them. Keeping the lower corners out of these
	// meshes prevents overlapping volumes and makes each side wall own its
	// complete outward-facing surface.
	const TArray<FVector2D> GableWallProfile = {
		FVector2D(-InnerWidth * 0.5f, 0.0f),
		FVector2D(InnerWidth * 0.5f, 0.0f),
		FVector2D(InnerWidth * 0.5f, TotalWallHeight),
		FVector2D(SafeWidth * 0.5f, TotalWallHeight),
		FVector2D(0.0f, TotalWallHeight + SafeRoofHeight),
		FVector2D(-SafeWidth * 0.5f, TotalWallHeight),
		FVector2D(-InnerWidth * 0.5f, TotalWallHeight)
	};
	const FTransform FrontWallTransform(
		FRotator(0.0f, 0.0f, -90.0f),
		FVector(0.0f, -SafeDepth * 0.5f + SafeThickness, 0.0f));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
		FrontWallMesh,
		PrimitiveOptions,
		FrontWallTransform,
		GableWallProfile,
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
		GableWallProfile,
		SafeThickness,
		0,
		true,
		EGeometryScriptPrimitiveOriginMode::Base);

	// Side walls span the complete depth and meet the narrower rectangular
	// portions of the front/back walls without overlapping them.
	const FTransform SideWall1Transform(
		FRotator::ZeroRotator,
		FVector(-SafeWidth * 0.5f + SafeThickness * 0.5f, 0.0f, TotalWallHeight * 0.5f));
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
		FVector(SafeWidth * 0.5f - SafeThickness * 0.5f, 0.0f, TotalWallHeight * 0.5f));
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

	// Both sloped panels belong to one independently accessible roof component.
	// Their depth stops at the inner faces of the gable walls. The gable wall's
	// own sloped edge covers the remaining end strip, so there are no coplanar
	// end caps competing for the same pixels on the outer facade.
	const float HalfWidth = SafeWidth * 0.5f;
	const float RoofSlopeLength = FMath::Sqrt(
		HalfWidth * HalfWidth + SafeRoofHeight * SafeRoofHeight);
	const float RoofPitchDegrees = FMath::RadiansToDegrees(
		FMath::Atan2(SafeRoofHeight, HalfWidth));
	const FVector LeftRoofOutwardNormal(
		-SafeRoofHeight / RoofSlopeLength,
		0.0f,
		HalfWidth / RoofSlopeLength);
	const FVector RightRoofOutwardNormal(
		SafeRoofHeight / RoofSlopeLength,
		0.0f,
		HalfWidth / RoofSlopeLength);
	const FVector LeftRoofCenter =
		FVector(-SafeWidth * 0.25f, 0.0f, TotalWallHeight + SafeRoofHeight * 0.5f) -
		LeftRoofOutwardNormal * SafeThickness * 0.5f;
	const FVector RightRoofCenter =
		FVector(SafeWidth * 0.25f, 0.0f, TotalWallHeight + SafeRoofHeight * 0.5f) -
		RightRoofOutwardNormal * SafeThickness * 0.5f;

	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
		RoofMesh,
		PrimitiveOptions,
		FTransform(FRotator(RoofPitchDegrees, 0.0f, 0.0f), LeftRoofCenter),
		RoofSlopeLength,
		InnerDepth,
		SafeThickness,
		0,
		0,
		0,
		EGeometryScriptPrimitiveOriginMode::Center);
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
		RoofMesh,
		PrimitiveOptions,
		FTransform(FRotator(-RoofPitchDegrees, 0.0f, 0.0f), RightRoofCenter),
		RoofSlopeLength,
		InnerDepth,
		SafeThickness,
		0,
		0,
		0,
		EGeometryScriptPrimitiveOriginMode::Center);

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
		const FTransform GroundTransform(
			FRotator::ZeroRotator,
			FVector(0.0f, 0.0f, SafeThickness * 0.5f));
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
			GroundMesh,
			PrimitiveOptions,
			GroundTransform,
			InnerWidth,
			InnerDepth,
			SafeThickness,
			0,
			0,
			0,
			EGeometryScriptPrimitiveOriginMode::Center);
	}

	FGeometryScriptMeshBooleanOptions BooleanOptions;
	BooleanOptions.bFillHoles = true;
	BooleanOptions.bSimplifyOutput = true;
	BooleanOptions.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;

	// Door and window booleans affect only the independent front-wall mesh.
	auto CutFrontOpening = [&](
		float CenterX,
		float CenterZ,
		float OpeningWidth,
		float OpeningHeight,
		bool bTouchesGround,
		ESimpleHouse5WindowType OpeningType)
	{
		const float BooleanTolerance = 0.5f;
		const float CutterHeight = OpeningHeight + (bTouchesGround ? BooleanTolerance * 2.0f : 0.0f);

		UDynamicMesh* Cutter = NewObject<UDynamicMesh>(this);
		FTransform CutterTransform = FTransform::Identity;
		if (OpeningType == ESimpleHouse5WindowType::Arched && !bTouchesGround)
		{
			const float HalfOpeningWidth = OpeningWidth * 0.5f;
			const float HalfOpeningHeight = OpeningHeight * 0.5f;
			// A normal-width window receives a true semicircular cap. If the
			// requested window is unusually wide and short, the cap safely
			// becomes a half ellipse while preserving the requested bounds.
			const float ArchHeight = FMath::Min(
				HalfOpeningWidth,
				OpeningHeight * 0.9f);
			const float ArchShoulderZ = HalfOpeningHeight - ArchHeight;

			TArray<FVector2D> ArchedProfile;
			ArchedProfile.Reserve(ArchedWindowArcSegments + 3);
			ArchedProfile.Add(FVector2D(-HalfOpeningWidth, -HalfOpeningHeight));
			ArchedProfile.Add(FVector2D(HalfOpeningWidth, -HalfOpeningHeight));
			ArchedProfile.Add(FVector2D(HalfOpeningWidth, ArchShoulderZ));
			for (int32 SegmentIndex = 1; SegmentIndex <= ArchedWindowArcSegments; ++SegmentIndex)
			{
				const float Angle =
					PI * static_cast<float>(SegmentIndex) /
					static_cast<float>(ArchedWindowArcSegments);
				ArchedProfile.Add(FVector2D(
					FMath::Cos(Angle) * HalfOpeningWidth,
					ArchShoulderZ + FMath::Sin(Angle) * ArchHeight));
			}

			const FTransform ArchedCutterTransform(
				FRotator(0.0f, 0.0f, -90.0f),
				FVector(
					CenterX,
					-SafeDepth * 0.5f + SafeThickness * 2.0f,
					CenterZ));
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
				Cutter,
				PrimitiveOptions,
				ArchedCutterTransform,
				ArchedProfile,
				SafeThickness * 3.0f,
				0,
				true,
				EGeometryScriptPrimitiveOriginMode::Base);
		}
		else
		{
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
				Cutter,
				PrimitiveOptions,
				FTransform::Identity,
				OpeningWidth,
				SafeThickness * 3.0f,
				CutterHeight,
				0,
				0,
				0,
				EGeometryScriptPrimitiveOriginMode::Center);

			CutterTransform = FTransform(
				FRotator::ZeroRotator,
				FVector(CenterX, -SafeDepth * 0.5f + SafeThickness * 0.5f, CenterZ));
		}

		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
			FrontWallMesh,
			FTransform::Identity,
			Cutter,
			CutterTransform,
			EGeometryScriptBooleanOperation::Subtract,
			BooleanOptions);
	};

	const float OpeningSideClearance = FMath::Max(
		MinimumOpeningDimension,
		SafeThickness * 0.25f);
	const float HorizontalEdgeMargin = FMath::Min(
		SafeThickness + OpeningSideClearance,
		SafeWidth * 0.45f);
	const float FacadeLeft = -SafeWidth * 0.5f + HorizontalEdgeMargin;
	const float FacadeRight = SafeWidth * 0.5f - HorizontalEdgeMargin;

	float SafeDoorWidth = 0.0f;
	float SafeDoorHeight = 0.0f;
	if (Door.bHasDoor)
	{
		const float MaximumDoorWidth = FMath::Max(
			MinimumOpeningDimension,
			FacadeRight - FacadeLeft);
		const float MaximumDoorHeight = FMath::Max(
			MinimumOpeningDimension,
			SafeFloorHeight - MinimumOpeningDimension);
		SafeDoorWidth = FMath::Clamp(
			Door.DoorWidth,
			MinimumOpeningDimension,
			MaximumDoorWidth);
		SafeDoorHeight = FMath::Clamp(
			Door.DoorHeight,
			MinimumOpeningDimension,
			MaximumDoorHeight);

		CutFrontOpening(
			0.0f,
			SafeDoorHeight * 0.5f,
			SafeDoorWidth,
			SafeDoorHeight,
			true,
			ESimpleHouse5WindowType::Rectangular);
	}

	for (int32 FloorIndex = 0; FloorIndex < SafeFloorCount; ++FloorIndex)
	{
		const FSimpleHouse5FloorWindowSettings& WindowSettings = FloorWindows[FloorIndex];
		if (!WindowSettings.bHasWindows || WindowSettings.WindowCount <= 0)
		{
			continue;
		}

		const float VerticalMargin = FMath::Min(
			FMath::Max(SafeThickness * 0.5f, MinimumOpeningDimension),
			SafeFloorHeight * 0.45f);
		const float MaximumWindowHeight = FMath::Max(
			MinimumOpeningDimension,
			SafeFloorHeight - VerticalMargin * 2.0f);
		const float SafeWindowHeight = FMath::Clamp(
			WindowSettings.WindowHeight,
			MinimumOpeningDimension,
			MaximumWindowHeight);
		const float WindowCenterZ =
			static_cast<float>(FloorIndex) * SafeFloorHeight + SafeFloorHeight * 0.5f;
		const float WindowBottom = WindowCenterZ - SafeWindowHeight * 0.5f;
		const float WindowTop = WindowCenterZ + SafeWindowHeight * 0.5f;

		TArray<FHorizontalSpan, TInlineAllocator<2>> AvailableSpans;
		AvailableSpans.Add({FacadeLeft, FacadeRight});

		const bool bOverlapsDoorVertically =
			FloorIndex == 0 &&
			Door.bHasDoor &&
			WindowBottom < SafeDoorHeight &&
			WindowTop > 0.0f;
		if (bOverlapsDoorVertically)
		{
			AvailableSpans.Reset();
			const float DoorClearance = FMath::Max(MinimumOpeningDimension, SafeThickness * 0.25f);
			const FHorizontalSpan LeftSpan{FacadeLeft, -SafeDoorWidth * 0.5f - DoorClearance};
			const FHorizontalSpan RightSpan{SafeDoorWidth * 0.5f + DoorClearance, FacadeRight};
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
		for (int32 SpanIndex = 0; SpanIndex < AvailableSpans.Num(); ++SpanIndex)
		{
			SpanCapacities[SpanIndex] = FMath::FloorToInt(
				AvailableSpans[SpanIndex].Length() / MinimumOpeningDimension);
			TotalCapacity += SpanCapacities[SpanIndex];
		}

		const int32 ActualWindowCount = FMath::Min(
			FMath::Clamp(WindowSettings.WindowCount, 0, MaximumWindowCountPerFloor),
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
		else if (WindowSettings.Alignment == ESimpleHouse5WindowAlignment::Left)
		{
			WindowsPerSpan[0] = FMath::Min(ActualWindowCount, SpanCapacities[0]);
			WindowsPerSpan[1] = ActualWindowCount - WindowsPerSpan[0];
		}
		else if (WindowSettings.Alignment == ESimpleHouse5WindowAlignment::Right)
		{
			WindowsPerSpan[1] = FMath::Min(ActualWindowCount, SpanCapacities[1]);
			WindowsPerSpan[0] = ActualWindowCount - WindowsPerSpan[1];
		}
		else
		{
			WindowsPerSpan[0] = FMath::Min(
				(ActualWindowCount + 1) / 2,
				SpanCapacities[0]);
			WindowsPerSpan[1] = ActualWindowCount - WindowsPerSpan[0];
			if (WindowsPerSpan[1] > SpanCapacities[1])
			{
				const int32 Overflow = WindowsPerSpan[1] - SpanCapacities[1];
				WindowsPerSpan[1] -= Overflow;
				WindowsPerSpan[0] += Overflow;
			}
		}

		float MaximumSpacing = TNumericLimits<float>::Max();
		bool bHasSpacingSlot = false;
		for (int32 SpanIndex = 0; SpanIndex < AvailableSpans.Num(); ++SpanIndex)
		{
			const int32 SpanWindowCount = WindowsPerSpan[SpanIndex];
			if (SpanWindowCount > 1)
			{
				bHasSpacingSlot = true;
				MaximumSpacing = FMath::Min(
					MaximumSpacing,
					(AvailableSpans[SpanIndex].Length() -
						static_cast<float>(SpanWindowCount) * MinimumOpeningDimension) /
						static_cast<float>(SpanWindowCount - 1));
			}
		}
		const float SafeWindowSpacing = bHasSpacingSlot
			? FMath::Clamp(WindowSettings.WindowSpacing, 0.0f, FMath::Max(0.0f, MaximumSpacing))
			: 0.0f;

		float MaximumWindowWidth = TNumericLimits<float>::Max();
		for (int32 SpanIndex = 0; SpanIndex < AvailableSpans.Num(); ++SpanIndex)
		{
			const int32 SpanWindowCount = WindowsPerSpan[SpanIndex];
			if (SpanWindowCount > 0)
			{
				MaximumWindowWidth = FMath::Min(
					MaximumWindowWidth,
					(AvailableSpans[SpanIndex].Length() -
						static_cast<float>(SpanWindowCount - 1) * SafeWindowSpacing) /
						static_cast<float>(SpanWindowCount));
			}
		}
		const float SafeWindowWidth = FMath::Clamp(
			WindowSettings.WindowWidth,
			MinimumOpeningDimension,
			FMath::Max(MinimumOpeningDimension, MaximumWindowWidth));

		for (int32 SpanIndex = 0; SpanIndex < AvailableSpans.Num(); ++SpanIndex)
		{
			const int32 SpanWindowCount = WindowsPerSpan[SpanIndex];
			if (SpanWindowCount <= 0)
			{
				continue;
			}

			const FHorizontalSpan& Span = AvailableSpans[SpanIndex];
			const float GroupWidth =
				static_cast<float>(SpanWindowCount) * SafeWindowWidth +
				static_cast<float>(SpanWindowCount - 1) * SafeWindowSpacing;
			float FirstWindowLeft = Span.Left;

			if (WindowSettings.Alignment == ESimpleHouse5WindowAlignment::Right)
			{
				FirstWindowLeft = Span.Right - GroupWidth;
			}
			else if (WindowSettings.Alignment == ESimpleHouse5WindowAlignment::Center)
			{
				if (AvailableSpans.Num() == 1)
				{
					FirstWindowLeft = (Span.Left + Span.Right - GroupWidth) * 0.5f;
				}
				else
				{
					FirstWindowLeft = SpanIndex == 0
						? Span.Right - GroupWidth
						: Span.Left;
				}
			}

			for (int32 WindowIndex = 0; WindowIndex < SpanWindowCount; ++WindowIndex)
			{
				const float WindowCenterX =
					FirstWindowLeft +
					SafeWindowWidth * 0.5f +
					static_cast<float>(WindowIndex) * (SafeWindowWidth + SafeWindowSpacing);
				CutFrontOpening(
					WindowCenterX,
					WindowCenterZ,
					SafeWindowWidth,
					SafeWindowHeight,
					false,
					WindowSettings.WindowType);
			}
		}
	}

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

void ASimpleHouseActor5::ApplyPartVisibility()
{
	RoofComponent->SetVisibility(PartVisibility.bRoofVisible, true);
	CeilingComponent->SetVisibility(PartVisibility.bCeilingVisible, true);
	GroundComponent->SetVisibility(PartVisibility.bGroundVisible, true);
	FrontWallComponent->SetVisibility(PartVisibility.bFrontWallVisible, true);
	BackWallComponent->SetVisibility(PartVisibility.bBackWallVisible, true);
	SideWall1Component->SetVisibility(PartVisibility.bSideWall1Visible, true);
	SideWall2Component->SetVisibility(PartVisibility.bSideWall2Visible, true);
}

void ASimpleHouseActor5::SetPartVisibility(ESimpleHouse5Part Part, bool bVisible)
{
	switch (Part)
	{
	case ESimpleHouse5Part::Roof:
		PartVisibility.bRoofVisible = bVisible;
		break;
	case ESimpleHouse5Part::Ceiling:
		PartVisibility.bCeilingVisible = bVisible;
		break;
	case ESimpleHouse5Part::Ground:
		PartVisibility.bGroundVisible = bVisible;
		break;
	case ESimpleHouse5Part::FrontWall:
		PartVisibility.bFrontWallVisible = bVisible;
		break;
	case ESimpleHouse5Part::BackWall:
		PartVisibility.bBackWallVisible = bVisible;
		break;
	case ESimpleHouse5Part::LeftWall:
		PartVisibility.bSideWall1Visible = bVisible;
		break;
	case ESimpleHouse5Part::RightWall:
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

UDynamicMeshComponent* ASimpleHouseActor5::GetPartComponent(ESimpleHouse5Part Part) const
{
	switch (Part)
	{
	case ESimpleHouse5Part::Roof:
		return RoofComponent;
	case ESimpleHouse5Part::Ceiling:
		return CeilingComponent;
	case ESimpleHouse5Part::Ground:
		return GroundComponent;
	case ESimpleHouse5Part::FrontWall:
		return FrontWallComponent;
	case ESimpleHouse5Part::BackWall:
		return BackWallComponent;
	case ESimpleHouse5Part::LeftWall:
		return SideWall1Component;
	case ESimpleHouse5Part::RightWall:
		return SideWall2Component;
	default:
		return nullptr;
	}
}
