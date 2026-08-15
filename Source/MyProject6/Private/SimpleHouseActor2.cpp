// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleHouseActor2.h"

#include "Components/DynamicMeshComponent.h"
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

ASimpleHouseActor2::ASimpleHouseActor2(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetGenerateOverlapEvents(false);

	SynchronizeFloorWindowSettings();
}

void ASimpleHouseActor2::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	SynchronizeFloorWindowSettings();
	RebuildMesh();
}

void ASimpleHouseActor2::SynchronizeFloorWindowSettings()
{
	Building.FloorCount = FMath::Clamp(Building.FloorCount, 1, MaximumFloorCount);
	FloorWindows.SetNum(Building.FloorCount);

	for (int32 FloorIndex = 0; FloorIndex < FloorWindows.Num(); ++FloorIndex)
	{
		FloorWindows[FloorIndex].FloorNumber = FloorIndex + 1;
	}
}

void ASimpleHouseActor2::RebuildMesh()
{
	SynchronizeFloorWindowSettings();

	UDynamicMeshComponent* MeshComponent = GetDynamicMeshComponent();
	UDynamicMesh* HouseMesh = MeshComponent ? MeshComponent->GetDynamicMesh() : nullptr;
	if (!HouseMesh)
	{
		return;
	}

	const float SafeWidth = FMath::Max(Building.BaseWidth, MinimumDimension);
	const float SafeDepth = FMath::Max(Building.BaseDepth, MinimumDimension);
	const float SafeFloorHeight = FMath::Max(Building.FloorHeight, MinimumDimension);
	const int32 SafeFloorCount = FMath::Clamp(Building.FloorCount, 1, MaximumFloorCount);
	const float TotalHeight = SafeFloorHeight * static_cast<float>(SafeFloorCount);
	const float MaximumThickness = 0.45f * FMath::Min3(SafeWidth, SafeDepth, SafeFloorHeight);
	const float SafeThickness = FMath::Clamp(Building.WallThickness, MinimumOpeningDimension, MaximumThickness);

	HouseMesh->Reset();

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	// Begin with a solid block that follows the rectangular footprint.
	const FTransform OuterTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, 0.0f, TotalHeight * 0.5f));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
		HouseMesh,
		PrimitiveOptions,
		OuterTransform,
		SafeWidth,
		SafeDepth,
		TotalHeight,
		0,
		0,
		0,
		EGeometryScriptPrimitiveOriginMode::Center);

	FGeometryScriptMeshBooleanOptions BooleanOptions;
	BooleanOptions.bFillHoles = true;
	BooleanOptions.bSimplifyOutput = true;
	BooleanOptions.OutputTransformSpace = EGeometryScriptBooleanOutputSpace::TargetTransformSpace;

	// Remove the interior. Extending the tool below Z=0 omits the ground slab,
	// while stopping it below the top always leaves a flat roof.
	const float InnerBottom = bHasGroundFloor ? SafeThickness : -SafeThickness;
	const float InnerTop = TotalHeight - SafeThickness;
	const float InnerHeight = FMath::Max(MinimumOpeningDimension, InnerTop - InnerBottom);
	UDynamicMesh* InteriorTool = NewObject<UDynamicMesh>(this);
	const FTransform InteriorTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, 0.0f, (InnerBottom + InnerTop) * 0.5f));
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(
		InteriorTool,
		PrimitiveOptions,
		InteriorTransform,
		SafeWidth - SafeThickness * 2.0f,
		SafeDepth - SafeThickness * 2.0f,
		InnerHeight,
		0,
		0,
		0,
		EGeometryScriptPrimitiveOriginMode::Center);
	UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
		HouseMesh,
		FTransform::Identity,
		InteriorTool,
		FTransform::Identity,
		EGeometryScriptBooleanOperation::Subtract,
		BooleanOptions);

	// All openings are cut into the front face at local -Y.
	auto CutFrontOpening = [&](float CenterX, float CenterZ, float OpeningWidth, float OpeningHeight, bool bTouchesGround)
	{
		const float BooleanTolerance = 0.5f;
		const float CutterHeight = OpeningHeight + (bTouchesGround ? BooleanTolerance * 2.0f : 0.0f);

		UDynamicMesh* Cutter = NewObject<UDynamicMesh>(this);
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

		const FTransform CutterTransform(
			FRotator::ZeroRotator,
			FVector(CenterX, -SafeDepth * 0.5f, CenterZ));
		UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
			HouseMesh,
			FTransform::Identity,
			Cutter,
			CutterTransform,
			EGeometryScriptBooleanOperation::Subtract,
			BooleanOptions);
	};

	// Keep every opening inside the facade's inner side edges.
	const float HorizontalEdgeMargin = FMath::Min(
		FMath::Max(SafeThickness, MinimumOpeningDimension),
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
			true);
	}

	for (int32 FloorIndex = 0; FloorIndex < SafeFloorCount; ++FloorIndex)
	{
		const FSimpleHouse2FloorWindowSettings& WindowSettings = FloorWindows[FloorIndex];
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

		// When ground-storey windows overlap the door vertically, reserve the
		// door's horizontal area so separate openings never merge.
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
		else if (WindowSettings.Alignment == ESimpleHouse2WindowAlignment::Left)
		{
			WindowsPerSpan[0] = FMath::Min(ActualWindowCount, SpanCapacities[0]);
			WindowsPerSpan[1] = ActualWindowCount - WindowsPerSpan[0];
		}
		else if (WindowSettings.Alignment == ESimpleHouse2WindowAlignment::Right)
		{
			WindowsPerSpan[1] = FMath::Min(ActualWindowCount, SpanCapacities[1]);
			WindowsPerSpan[0] = ActualWindowCount - WindowsPerSpan[1];
		}
		else
		{
			// A centred group grows evenly outwards from the centred door.
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

			if (WindowSettings.Alignment == ESimpleHouse2WindowAlignment::Right)
			{
				FirstWindowLeft = Span.Right - GroupWidth;
			}
			else if (WindowSettings.Alignment == ESimpleHouse2WindowAlignment::Center)
			{
				if (AvailableSpans.Num() == 1)
				{
					FirstWindowLeft = (Span.Left + Span.Right - GroupWidth) * 0.5f;
				}
				else
				{
					// With a centred door, pack both window groups toward it.
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
					false);
			}
		}
	}

	FGeometryScriptSplitNormalsOptions SplitOptions;
	SplitOptions.bSplitByOpeningAngle = true;
	SplitOptions.OpeningAngleDeg = 60.0f;
	FGeometryScriptCalculateNormalsOptions NormalOptions;
	UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(
		HouseMesh,
		SplitOptions,
		NormalOptions);

	MeshComponent->SetMaterial(0, Material);
	MeshComponent->NotifyMeshUpdated();
	MeshComponent->SetCollisionEnabled(
		bEnableCollision
			? ECollisionEnabled::QueryAndPhysics
			: ECollisionEnabled::NoCollision);
	MeshComponent->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}
