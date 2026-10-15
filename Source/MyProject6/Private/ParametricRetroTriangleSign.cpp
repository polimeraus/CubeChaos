// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParametricRetroTriangleSign.h"

#include "Algo/Reverse.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Font.h"
#include "Extensions/Text3DDefaultMaterialExtension.h"
#include "GeometryScript/MeshModelingFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Renderers/Text3DRendererBase.h"
#include "Text3DComponent.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr float MinimumSignDimension = 1.0f;

	void ConfigureSafeTextRenderer(UText3DComponent* Component)
	{
		if (!Component)
		{
			return;
		}

		// UE 5.8's legacy renderer creates one UStaticMeshComponent per glyph. Rebuilding four
		// text layers while the actor is selected can race the editor's bulk reregister path.
		// The dynamic renderer owns a single mesh component and avoids that lifecycle hazard.
		static UClass* DynamicMeshRendererClass = LoadClass<UText3DRendererBase>(
			nullptr,
			TEXT("/Script/Text3D.Text3DDynamicMeshRenderer"));
		if (DynamicMeshRendererClass)
		{
			// SetTextRendererClass is not exported by the Text3D module in UE 5.8. Assigning the
			// reflected class property before the deferred build is equivalent here: BeginBuild
			// detects the class change and safely creates the matching renderer.
			if (FClassProperty* RendererClassProperty = FindFProperty<FClassProperty>(
				UText3DComponent::StaticClass(),
				TEXT("TextRendererClass")))
			{
				RendererClassProperty->SetObjectPropertyValue_InContainer(
					Component,
					DynamicMeshRendererClass);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ParametricRetroTriangleSign could not load Text3D's dynamic mesh renderer."));
		}
	}

	void ConfigureGeneratedMeshComponent(UDynamicMeshComponent* Component)
	{
		if (!Component)
		{
			return;
		}
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	void RecalculateMeshNormals(UDynamicMesh* DynamicMesh)
	{
		if (!DynamicMesh)
		{
			return;
		}
		FGeometryScriptSplitNormalsOptions SplitOptions;
		SplitOptions.bSplitByOpeningAngle = true;
		SplitOptions.OpeningAngleDeg = 55.0f;
		SplitOptions.bSplitByFaceGroup = false;
		FGeometryScriptCalculateNormalsOptions NormalOptions;
		UGeometryScriptLibrary_MeshNormalsFunctions::ComputeSplitNormals(
			DynamicMesh,
			SplitOptions,
			NormalOptions);
	}
}

AParametricRetroTriangleSign::AParametricRetroTriangleSign()
{
	PrimaryActorTick.bCanEverTick = false;

	SignRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SignRoot"));
	SetRootComponent(SignRoot);
	SignRoot->SetMobility(EComponentMobility::Movable);

	PanelMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("PanelMesh"));
	PanelMesh->SetupAttachment(SignRoot);
	ConfigureGeneratedMeshComponent(PanelMesh);

	PrimaryFrameMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("PrimaryFrameMesh"));
	PrimaryFrameMesh->SetupAttachment(SignRoot);
	ConfigureGeneratedMeshComponent(PrimaryFrameMesh);

	AccentFrameMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("AccentFrameMesh"));
	AccentFrameMesh->SetupAttachment(SignRoot);
	ConfigureGeneratedMeshComponent(AccentFrameMesh);

	StripeMesh = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("StripeMesh"));
	StripeMesh->SetupAttachment(SignRoot);
	ConfigureGeneratedMeshComponent(StripeMesh);

	MainShadowComponent = CreateDefaultSubobject<UText3DComponent>(TEXT("MainTextShadow"));
	MainShadowComponent->SetupAttachment(SignRoot);
	MainTextComponent = CreateDefaultSubobject<UText3DComponent>(TEXT("MainText"));
	MainTextComponent->SetupAttachment(SignRoot);
	SubtitleShadowComponent = CreateDefaultSubobject<UText3DComponent>(TEXT("SubtitleTextShadow"));
	SubtitleShadowComponent->SetupAttachment(SignRoot);
	SubtitleTextComponent = CreateDefaultSubobject<UText3DComponent>(TEXT("SubtitleText"));
	SubtitleTextComponent->SetupAttachment(SignRoot);

	ConfigureSafeTextRenderer(MainTextComponent);
	ConfigureSafeTextRenderer(MainShadowComponent);
	ConfigureSafeTextRenderer(SubtitleTextComponent);
	ConfigureSafeTextRenderer(SubtitleShadowComponent);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterialFinder(
		TEXT("/Game/GameplayActors/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	if (GlowMaterialFinder.Succeeded())
	{
		MaterialAssignments.PanelMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.PrimaryFrameMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.AccentFrameMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.StripeMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.MainTextFrontMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.MainTextSideMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.MainTextShadowMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.SubtitleFrontMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.SubtitleSideMaterial = GlowMaterialFinder.Object;
		MaterialAssignments.SubtitleShadowMaterial = GlowMaterialFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UFont> DefaultFontFinder(
		TEXT("/Engine/EngineFonts/Roboto.Roboto"));

	MainText.Text = FText::FromString(TEXT("80s"));
	MainText.Font = DefaultFontFinder.Succeeded() ? DefaultFontFinder.Object : nullptr;
	MainText.FontSize = 180.0f;
	MainText.MaxWidth = 540.0f;
	MainText.Position = FVector2D(0.0, 45.0);
	MainText.Extrude = 18.0f;
	MainText.Bevel = 4.0f;
	MainText.BevelSegments = 5;
	MainText.SurfaceOffset = 8.0f;
	MainText.FrontColor = FLinearColor(0.02f, 0.85f, 1.0f, 1.0f);
	MainText.SideColor = FLinearColor(1.0f, 0.015f, 0.42f, 1.0f);
	MainText.ShadowColor = FLinearColor(0.28f, 0.0f, 0.62f, 1.0f);

	SubtitleText.Text = FText::FromString(TEXT("RETRO"));
	SubtitleText.Font = DefaultFontFinder.Succeeded() ? DefaultFontFinder.Object : nullptr;
	SubtitleText.FontSize = 76.0f;
	SubtitleText.MaxWidth = 430.0f;
	SubtitleText.Position = FVector2D(0.0, -105.0);
	SubtitleText.Extrude = 9.0f;
	SubtitleText.Bevel = 2.0f;
	SubtitleText.BevelSegments = 4;
	SubtitleText.SurfaceOffset = 11.0f;
	SubtitleText.FrontColor = FLinearColor(1.0f, 0.82f, 0.08f, 1.0f);
	SubtitleText.SideColor = FLinearColor(1.0f, 0.02f, 0.55f, 1.0f);
	SubtitleText.ShadowColor = FLinearColor(0.0f, 0.35f, 0.75f, 1.0f);
	SubtitleText.ShadowOffset = FVector2D(5.0, -5.0);
	SubtitleText.ShadowDepth = 2.0f;
}

void AParametricRetroTriangleSign::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildSign();
}

void AParametricRetroTriangleSign::PostLoad()
{
	Super::PostLoad();

	// Preserve material overrides saved before all selectors were consolidated.
	// New instances leave these hidden legacy fields null.
	if (PanelMaterial)
	{
		MaterialAssignments.PanelMaterial = PanelMaterial;
		PanelMaterial = nullptr;
	}
	if (PrimaryFrameMaterial)
	{
		MaterialAssignments.PrimaryFrameMaterial = PrimaryFrameMaterial;
		PrimaryFrameMaterial = nullptr;
	}
	if (AccentFrameMaterial)
	{
		MaterialAssignments.AccentFrameMaterial = AccentFrameMaterial;
		AccentFrameMaterial = nullptr;
	}
	if (StripeMaterial)
	{
		MaterialAssignments.StripeMaterial = StripeMaterial;
		StripeMaterial = nullptr;
	}
	if (MainText.FrontMaterial)
	{
		MaterialAssignments.MainTextFrontMaterial = MainText.FrontMaterial;
		MainText.FrontMaterial = nullptr;
	}
	if (MainText.SideMaterial)
	{
		MaterialAssignments.MainTextSideMaterial = MainText.SideMaterial;
		MainText.SideMaterial = nullptr;
	}
	if (MainText.ShadowMaterial)
	{
		MaterialAssignments.MainTextShadowMaterial = MainText.ShadowMaterial;
		MainText.ShadowMaterial = nullptr;
	}
	if (SubtitleText.FrontMaterial)
	{
		MaterialAssignments.SubtitleFrontMaterial = SubtitleText.FrontMaterial;
		SubtitleText.FrontMaterial = nullptr;
	}
	if (SubtitleText.SideMaterial)
	{
		MaterialAssignments.SubtitleSideMaterial = SubtitleText.SideMaterial;
		SubtitleText.SideMaterial = nullptr;
	}
	if (SubtitleText.ShadowMaterial)
	{
		MaterialAssignments.SubtitleShadowMaterial = SubtitleText.ShadowMaterial;
		SubtitleText.ShadowMaterial = nullptr;
	}
}

FVector2D AParametricRetroTriangleSign::RotatePoint2D(const FVector2D& Point, float Degrees)
{
	const double Radians = FMath::DegreesToRadians(static_cast<double>(Degrees));
	double SinAngle;
	double CosAngle;
	FMath::SinCos(&SinAngle, &CosAngle, Radians);
	return FVector2D(
		Point.X * CosAngle - Point.Y * SinAngle,
		Point.X * SinAngle + Point.Y * CosAngle);
}

void AParametricRetroTriangleSign::GetTriangleVertices(TArray<FVector2D>& OutVertices) const
{
	const double SafeWidth = FMath::Max(FMath::Abs(TriangleSize.X), MinimumSignDimension);
	const double SafeHeight = FMath::Max(FMath::Abs(TriangleSize.Y), MinimumSignDimension);
	const double HalfWidth = SafeWidth * 0.5;
	const double HalfHeight = SafeHeight * 0.5;
	const double SafeApexOffset = FMath::Clamp(ApexOffset, -1.0f, 1.0f);

	OutVertices = {
		FVector2D(-HalfWidth, -HalfHeight),
		FVector2D(HalfWidth, -HalfHeight),
		FVector2D(SafeApexOffset * HalfWidth, HalfHeight)
	};

	float DirectionRotation = 0.0f;
	switch (PointDirection)
	{
	case ERetroTrianglePointDirection::Right:
		DirectionRotation = -90.0f;
		break;
	case ERetroTrianglePointDirection::Down:
		DirectionRotation = 180.0f;
		break;
	case ERetroTrianglePointDirection::Left:
		DirectionRotation = 90.0f;
		break;
	case ERetroTrianglePointDirection::Up:
	default:
		break;
	}

	for (FVector2D& Vertex : OutVertices)
	{
		Vertex = RotatePoint2D(Vertex, DirectionRotation + TriangleRotationDegrees) + TriangleOffset;
	}
}

void AParametricRetroTriangleSign::GetSignOutlineVertices(TArray<FVector2D>& OutVertices) const
{
	if (SignType == ERetroSignType::NeonTriangle)
	{
		GetTriangleVertices(OutVertices);
		return;
	}

	OutVertices.Reset();
	auto TransformPoint = [](const FVector2D& Point, float Rotation, const FVector2D& Offset)
	{
		return RotatePoint2D(Point, Rotation) + Offset;
	};

	switch (SignType)
	{
	case ERetroSignType::SynthwaveSunsetDisc:
	{
		const double HalfWidth = FMath::Max(FMath::Abs(SynthwaveSunset.BadgeSize.X), MinimumSignDimension) * 0.5;
		const double HalfHeight = FMath::Max(FMath::Abs(SynthwaveSunset.BadgeSize.Y), MinimumSignDimension) * 0.5;
		const int32 SegmentCount = FMath::Clamp(SynthwaveSunset.OutlineSegments, 12, 96);
		OutVertices.Reserve(SegmentCount);
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			const double Angle = 2.0 * PI * static_cast<double>(Index) / SegmentCount;
			OutVertices.Add(TransformPoint(
				FVector2D(FMath::Cos(Angle) * HalfWidth, FMath::Sin(Angle) * HalfHeight),
				SynthwaveSunset.RotationDegrees,
				SynthwaveSunset.Offset));
		}
		break;
	}
	case ERetroSignType::ArcadeMarquee:
	{
		const double HalfWidth = FMath::Max(FMath::Abs(ArcadeMarquee.MarqueeSize.X), MinimumSignDimension) * 0.5;
		const double HalfHeight = FMath::Max(FMath::Abs(ArcadeMarquee.MarqueeSize.Y), MinimumSignDimension) * 0.5;
		const double TopInset = HalfWidth * FMath::Clamp(ArcadeMarquee.TopInsetRatio, 0.0f, 0.8f);
		const double BottomInset = HalfWidth * FMath::Clamp(ArcadeMarquee.BottomInsetRatio, 0.0f, 0.8f);
		const double SideY = HalfHeight * FMath::Clamp(ArcadeMarquee.SidePointHeight, -1.0f, 1.0f);
		OutVertices = {
			FVector2D(-HalfWidth + BottomInset, -HalfHeight),
			FVector2D(HalfWidth - BottomInset, -HalfHeight),
			FVector2D(HalfWidth, SideY),
			FVector2D(HalfWidth - TopInset, HalfHeight),
			FVector2D(-HalfWidth + TopInset, HalfHeight),
			FVector2D(-HalfWidth, SideY)
		};
		for (FVector2D& Vertex : OutVertices)
		{
			Vertex = TransformPoint(Vertex, ArcadeMarquee.RotationDegrees, ArcadeMarquee.Offset);
		}
		break;
	}
	case ERetroSignType::MemphisPopBadge:
	{
		const double HalfWidth = FMath::Max(FMath::Abs(MemphisPop.BadgeSize.X), MinimumSignDimension) * 0.5;
		const double HalfHeight = FMath::Max(FMath::Abs(MemphisPop.BadgeSize.Y), MinimumSignDimension) * 0.5;
		const double Cut = FMath::Min(HalfWidth, HalfHeight) * 2.0 *
			FMath::Clamp(MemphisPop.CornerCutRatio, 0.0f, 0.45f);
		const double Skew = HalfWidth * FMath::Clamp(MemphisPop.SkewRatio, -0.5f, 0.5f);
		OutVertices = {
			FVector2D(-HalfWidth + Cut - Skew, -HalfHeight),
			FVector2D(HalfWidth - Cut - Skew, -HalfHeight),
			FVector2D(HalfWidth - Skew, -HalfHeight + Cut),
			FVector2D(HalfWidth + Skew, HalfHeight - Cut),
			FVector2D(HalfWidth - Cut + Skew, HalfHeight),
			FVector2D(-HalfWidth + Cut + Skew, HalfHeight),
			FVector2D(-HalfWidth + Skew, HalfHeight - Cut),
			FVector2D(-HalfWidth - Skew, -HalfHeight + Cut)
		};
		for (FVector2D& Vertex : OutVertices)
		{
			Vertex = TransformPoint(Vertex, MemphisPop.RotationDegrees, MemphisPop.Offset);
		}
		break;
	}
	case ERetroSignType::NeonDiamondBadge:
	{
		const double HalfWidth = FMath::Max(FMath::Abs(NeonDiamond.BadgeSize.X), MinimumSignDimension) * 0.5;
		const double HalfHeight = FMath::Max(FMath::Abs(NeonDiamond.BadgeSize.Y), MinimumSignDimension) * 0.5;
		OutVertices = {
			FVector2D(0.0, -HalfHeight),
			FVector2D(HalfWidth, 0.0),
			FVector2D(0.0, HalfHeight),
			FVector2D(-HalfWidth, 0.0)
		};
		for (FVector2D& Vertex : OutVertices)
		{
			Vertex = TransformPoint(Vertex, NeonDiamond.RotationDegrees, NeonDiamond.Offset);
		}
		break;
	}
	case ERetroSignType::RoadsideArrow:
	{
		const double HalfWidth = FMath::Max(FMath::Abs(RoadsideArrow.ArrowSize.X), MinimumSignDimension) * 0.5;
		const double HalfHeight = FMath::Max(FMath::Abs(RoadsideArrow.ArrowSize.Y), MinimumSignDimension) * 0.5;
		const double HeadLength = HalfWidth * 2.0 * FMath::Clamp(RoadsideArrow.HeadLengthRatio, 0.15f, 0.75f);
		const double HeadBaseX = HalfWidth - HeadLength;
		const double ShaftHalfHeight = HalfHeight * FMath::Clamp(RoadsideArrow.ShaftHeightRatio, 0.1f, 1.0f);
		const double TailNotch = HalfWidth * 2.0 * FMath::Clamp(RoadsideArrow.TailNotchRatio, 0.0f, 0.6f);
		OutVertices = {
			FVector2D(-HalfWidth, -ShaftHalfHeight),
			FVector2D(HeadBaseX, -ShaftHalfHeight),
			FVector2D(HeadBaseX, -HalfHeight),
			FVector2D(HalfWidth, 0.0),
			FVector2D(HeadBaseX, HalfHeight),
			FVector2D(HeadBaseX, ShaftHalfHeight),
			FVector2D(-HalfWidth, ShaftHalfHeight),
			FVector2D(-HalfWidth + TailNotch, 0.0)
		};
		if (RoadsideArrow.Direction == ERetroHorizontalDirection::Left)
		{
			for (FVector2D& Vertex : OutVertices)
			{
				Vertex.X *= -1.0;
			}
			Algo::Reverse(OutVertices);
		}
		for (FVector2D& Vertex : OutVertices)
		{
			Vertex = TransformPoint(Vertex, RoadsideArrow.RotationDegrees, RoadsideArrow.Offset);
		}
		break;
	}
	case ERetroSignType::RoundedLightbox:
	{
		const double HalfWidth = FMath::Max(FMath::Abs(RoundedLightbox.LightboxSize.X), MinimumSignDimension) * 0.5;
		const double HalfHeight = FMath::Max(FMath::Abs(RoundedLightbox.LightboxSize.Y), MinimumSignDimension) * 0.5;
		const double Radius = FMath::Clamp(
			static_cast<double>(RoundedLightbox.CornerRadius),
			0.0,
			FMath::Min(HalfWidth, HalfHeight));
		if (Radius <= UE_KINDA_SMALL_NUMBER)
		{
			OutVertices = {
				FVector2D(-HalfWidth, -HalfHeight),
				FVector2D(HalfWidth, -HalfHeight),
				FVector2D(HalfWidth, HalfHeight),
				FVector2D(-HalfWidth, HalfHeight)
			};
			for (FVector2D& Vertex : OutVertices)
			{
				Vertex = TransformPoint(
					Vertex, RoundedLightbox.RotationDegrees, RoundedLightbox.Offset);
			}
			break;
		}
		const int32 CornerSteps = FMath::Clamp(RoundedLightbox.CornerSegments, 2, 16);
		const FVector2D CornerCenters[] = {
			FVector2D(HalfWidth - Radius, -HalfHeight + Radius),
			FVector2D(HalfWidth - Radius, HalfHeight - Radius),
			FVector2D(-HalfWidth + Radius, HalfHeight - Radius),
			FVector2D(-HalfWidth + Radius, -HalfHeight + Radius)
		};
		const double StartAngles[] = {-90.0, 0.0, 90.0, 180.0};
		OutVertices.Reserve(4 * (CornerSteps + 1));
		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
		{
			for (int32 Step = 0; Step <= CornerSteps; ++Step)
			{
				const double Angle = FMath::DegreesToRadians(
					StartAngles[CornerIndex] + 90.0 * static_cast<double>(Step) / CornerSteps);
				const FVector2D LocalPoint = CornerCenters[CornerIndex] +
					FVector2D(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
				OutVertices.Add(TransformPoint(
					LocalPoint, RoundedLightbox.RotationDegrees, RoundedLightbox.Offset));
			}
		}
		break;
	}
	case ERetroSignType::NeonShieldCrest:
	{
		const double HalfWidth = FMath::Max(FMath::Abs(NeonShield.BadgeSize.X), MinimumSignDimension) * 0.5;
		const double HalfHeight = FMath::Max(FMath::Abs(NeonShield.BadgeSize.Y), MinimumSignDimension) * 0.5;
		const double FullHeight = HalfHeight * 2.0;
		const double TopX = HalfWidth * (1.0 - FMath::Clamp(NeonShield.TopInsetRatio, 0.0f, 0.45f));
		const double PeakY = HalfHeight + FullHeight *
			FMath::Clamp(NeonShield.TopPeakRatio, 0.0f, 0.35f);
		const double ShoulderY = HalfHeight - FullHeight *
			FMath::Clamp(NeonShield.ShoulderDropRatio, 0.05f, 0.45f);
		const double SideLowerY = -HalfHeight + FullHeight *
			FMath::Clamp(NeonShield.PointDepthRatio, 0.1f, 0.45f);
		const double TaperY = FMath::Lerp(-HalfHeight, SideLowerY, 0.45);
		const double TaperX = HalfWidth * FMath::Clamp(NeonShield.BottomTaperRatio, 0.2f, 0.95f);
		OutVertices = {
			FVector2D(0.0, -HalfHeight),
			FVector2D(TaperX, TaperY),
			FVector2D(HalfWidth, SideLowerY),
			FVector2D(HalfWidth, ShoulderY),
			FVector2D(TopX, HalfHeight),
			FVector2D(0.0, PeakY),
			FVector2D(-TopX, HalfHeight),
			FVector2D(-HalfWidth, ShoulderY),
			FVector2D(-HalfWidth, SideLowerY),
			FVector2D(-TaperX, TaperY)
		};
		for (FVector2D& Vertex : OutVertices)
		{
			Vertex = TransformPoint(Vertex, NeonShield.RotationDegrees, NeonShield.Offset);
		}
		break;
	}
	default:
		GetTriangleVertices(OutVertices);
		break;
	}
}

UMaterialInstanceDynamic* AParametricRetroTriangleSign::CreateTintedMaterial(
	UMaterialInterface* RequestedParent,
	const FLinearColor& Color,
	float Intensity)
{
	UMaterialInterface* ParentMaterial = RequestedParent;
	if (!ParentMaterial)
	{
		ParentMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/GameplayActors/JumpPad/Assets/Materials/M_SimpleGlow.M_SimpleGlow"));
	}
	if (!ParentMaterial)
	{
		ParentMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(ParentMaterial, this);
	if (!Instance)
	{
		return nullptr;
	}

	const float SafeIntensity = FMath::Max(Intensity, 0.0f);
	const FLinearColor EmissiveColor = Color * SafeIntensity;
	Instance->SetVectorParameterValue(TEXT("Color"), EmissiveColor);
	Instance->SetVectorParameterValue(TEXT("BaseColor"), Color);
	Instance->SetVectorParameterValue(TEXT("GlowColor"), EmissiveColor);
	Instance->SetVectorParameterValue(TEXT("EmissiveColor"), EmissiveColor);
	Instance->SetScalarParameterValue(TEXT("Intensity"), SafeIntensity);
	Instance->SetScalarParameterValue(TEXT("GlowIntensity"), SafeIntensity);
	Instance->SetScalarParameterValue(TEXT("EmissiveIntensity"), SafeIntensity);
	Instance->SetScalarParameterValue(TEXT("Alpha"), Color.A);
	return Instance;
}

void AParametricRetroTriangleSign::BuildPanel(const TArray<FVector2D>& Vertices)
{
	UDynamicMesh* DynamicMesh = PanelMesh ? PanelMesh->GetDynamicMesh() : nullptr;
	if (!DynamicMesh || Vertices.Num() < 3)
	{
		return;
	}

	DynamicMesh->Reset();
	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSimpleExtrudePolygon(
		DynamicMesh,
		PrimitiveOptions,
		FTransform::Identity,
		Vertices,
		FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension),
		0,
		true,
		EGeometryScriptPrimitiveOriginMode::Center);

	const float SafeThickness = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension);
	FBox2D OutlineBounds(ForceInit);
	for (const FVector2D& Vertex : Vertices)
	{
		OutlineBounds += Vertex;
	}
	const FVector2D OutlineSize = OutlineBounds.GetSize();
	const float SafeBevel = FMath::Clamp(
		PanelBevel,
		0.0f,
		FMath::Min3(
			SafeThickness * 0.45f,
			static_cast<float>(FMath::Abs(OutlineSize.X) * 0.1),
			static_cast<float>(FMath::Abs(OutlineSize.Y) * 0.1)));
	if (SafeBevel > UE_KINDA_SMALL_NUMBER)
	{
		FGeometryScriptMeshBevelOptions BevelOptions;
		BevelOptions.BevelDistance = SafeBevel;
		BevelOptions.Subdivisions = FMath::Clamp(PanelBevelSegments, 1, 12);
		BevelOptions.RoundWeight = 1.0f;
		BevelOptions.bInferMaterialID = true;
		UGeometryScriptLibrary_MeshModelingFunctions::ApplyMeshPolygroupBevel(
			DynamicMesh,
			BevelOptions);
	}

	RecalculateMeshNormals(DynamicMesh);
	PanelMesh->NotifyMeshUpdated();
	PanelMesh->SetMaterial(0, PanelMaterialInstance);
	PanelMesh->SetCollisionEnabled(
		bEnablePanelCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	PanelMesh->SetComplexAsSimpleCollisionEnabled(bEnablePanelCollision, true);
}

void AParametricRetroTriangleSign::BuildTubeFrame(
	UDynamicMeshComponent* Component,
	const TArray<FVector2D>& SourceVertices,
	float Scale,
	float Radius,
	float RotationDegrees,
	FVector2D Offset,
	float SurfaceOffset)
{
	UDynamicMesh* DynamicMesh = Component ? Component->GetDynamicMesh() : nullptr;
	if (!DynamicMesh || SourceVertices.Num() < 3)
	{
		return;
	}
	DynamicMesh->Reset();

	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : SourceVertices)
	{
		Centroid += Vertex;
	}
	Centroid /= static_cast<double>(SourceVertices.Num());

	TArray<FVector2D> FrameVertices;
	FrameVertices.Reserve(SourceVertices.Num());
	const double SafeScale = FMath::Max(FMath::Abs(Scale), 0.1f);
	for (const FVector2D& Vertex : SourceVertices)
	{
		FrameVertices.Add(
			Centroid + RotatePoint2D((Vertex - Centroid) * SafeScale, RotationDegrees) + Offset);
	}

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;
	const float SafeRadius = FMath::Max(FMath::Abs(Radius), 0.5f);
	const int32 SafeRadialSegments = FMath::Clamp(FrameRadialSegments, 6, 32);
	const float FaceDepth = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension) * 0.5f + SurfaceOffset;

	for (int32 EdgeIndex = 0; EdgeIndex < FrameVertices.Num(); ++EdgeIndex)
	{
		const FVector2D& A = FrameVertices[EdgeIndex];
		const FVector2D& B = FrameVertices[(EdgeIndex + 1) % FrameVertices.Num()];
		const FVector2D Edge = B - A;
		const float EdgeLength = static_cast<float>(Edge.Size());
		if (EdgeLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		const FVector EdgeDirection(Edge.X / EdgeLength, Edge.Y / EdgeLength, 0.0f);
		const FQuat TubeRotation = FQuat::FindBetweenNormals(FVector::UpVector, EdgeDirection);
		const FVector2D Midpoint = (A + B) * 0.5;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
			DynamicMesh,
			PrimitiveOptions,
			FTransform(TubeRotation, FVector(Midpoint.X, Midpoint.Y, FaceDepth)),
			SafeRadius,
			EdgeLength,
			SafeRadialSegments,
			0,
			true,
			EGeometryScriptPrimitiveOriginMode::Center);
	}

	for (const FVector2D& Vertex : FrameVertices)
	{
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
			DynamicMesh,
			PrimitiveOptions,
			FTransform(FRotator::ZeroRotator, FVector(Vertex.X, Vertex.Y, FaceDepth)),
			SafeRadius,
			FMath::Max(SafeRadialSegments / 2, 4),
			SafeRadialSegments,
			EGeometryScriptPrimitiveOriginMode::Center);
	}

	Component->NotifyMeshUpdated();
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AParametricRetroTriangleSign::AppendTubeSegment(
	UDynamicMesh* DynamicMesh,
	const FVector2D& Start,
	const FVector2D& End,
	float Depth,
	float Radius) const
{
	if (!DynamicMesh)
	{
		return;
	}

	const FVector2D Segment = End - Start;
	const float Length = static_cast<float>(Segment.Size());
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;
	const FVector Direction(Segment.X / Length, Segment.Y / Length, 0.0f);
	const FQuat TubeRotation = FQuat::FindBetweenNormals(FVector::UpVector, Direction);
	const FVector2D Midpoint = (Start + End) * 0.5;
	UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendCylinder(
		DynamicMesh,
		PrimitiveOptions,
		FTransform(TubeRotation, FVector(Midpoint.X, Midpoint.Y, Depth)),
		FMath::Max(FMath::Abs(Radius), 0.5f),
		Length,
		FMath::Clamp(FrameRadialSegments, 6, 32),
		0,
		true,
		EGeometryScriptPrimitiveOriginMode::Center);
}

void AParametricRetroTriangleSign::BuildOriginalStripes(
	UDynamicMesh* DynamicMesh,
	const TArray<FVector2D>& Vertices)
{
	if (!DynamicMesh || Vertices.Num() < 3)
	{
		return;
	}

	const int32 SafeStripeCount = FMath::Clamp(StripeCount, 1, 20);
	const float SafeSpacing = FMath::Max(StripeSpacing, 1.0f);
	const float SafeRadius = FMath::Max(StripeTubeRadius, 0.5f);
	const float SafeWidthScale = FMath::Clamp(StripeWidthScale, 0.05f, 1.0f);
	const float FaceDepth = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension) * 0.5f + 2.0f;

	for (int32 StripeIndex = 0; StripeIndex < SafeStripeCount; ++StripeIndex)
	{
		const double LineY = StripeVerticalOffset +
			(static_cast<double>(StripeIndex) - (SafeStripeCount - 1) * 0.5) * SafeSpacing;
		TArray<double> Intersections;
		for (int32 EdgeIndex = 0; EdgeIndex < Vertices.Num(); ++EdgeIndex)
		{
			const FVector2D& A = Vertices[EdgeIndex];
			const FVector2D& B = Vertices[(EdgeIndex + 1) % Vertices.Num()];
			if ((A.Y <= LineY && B.Y > LineY) || (B.Y <= LineY && A.Y > LineY))
			{
				const double Alpha = (LineY - A.Y) / (B.Y - A.Y);
				Intersections.Add(FMath::Lerp(A.X, B.X, Alpha));
			}
		}
		if (Intersections.Num() < 2)
		{
			continue;
		}
		Intersections.Sort();
		const double FullMinX = Intersections[0];
		const double FullMaxX = Intersections.Last();
		const double CenterX = (FullMinX + FullMaxX) * 0.5;
		const double HalfLength = (FullMaxX - FullMinX) * 0.5 * SafeWidthScale;
		if (HalfLength <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		AppendTubeSegment(
			DynamicMesh,
			FVector2D(CenterX - HalfLength, LineY),
			FVector2D(CenterX + HalfLength, LineY),
			FaceDepth,
			SafeRadius);
	}
}

void AParametricRetroTriangleSign::BuildSunsetStripes(UDynamicMesh* DynamicMesh)
{
	if (!DynamicMesh)
	{
		return;
	}

	const double BadgeWidth = FMath::Max(FMath::Abs(SynthwaveSunset.BadgeSize.X), MinimumSignDimension);
	const double BadgeHeight = FMath::Max(FMath::Abs(SynthwaveSunset.BadgeSize.Y), MinimumSignDimension);
	const double SunRadius = FMath::Min(BadgeWidth, BadgeHeight) * 0.5 *
		FMath::Clamp(SynthwaveSunset.SunRadiusScale, 0.05f, 1.0f);
	const int32 StripeTotal = FMath::Clamp(SynthwaveSunset.SunStripeCount, 1, 24);
	const double StripeSpacingValue = FMath::Max(SynthwaveSunset.SunStripeSpacing, 1.0f);
	const double WidthScale = FMath::Clamp(SynthwaveSunset.SunStripeWidthScale, 0.05f, 1.0f);
	const float FaceDepth = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension) * 0.5f + 3.0f;

	for (int32 StripeIndex = 0; StripeIndex < StripeTotal; ++StripeIndex)
	{
		const double RelativeY =
			(static_cast<double>(StripeIndex) - (StripeTotal - 1) * 0.5) * StripeSpacingValue;
		if (FMath::Abs(RelativeY) >= SunRadius)
		{
			continue;
		}

		const double HalfWidth = FMath::Sqrt(
			FMath::Max(SunRadius * SunRadius - RelativeY * RelativeY, 0.0)) * WidthScale;
		const double LineY = SynthwaveSunset.SunVerticalOffset + RelativeY;
		const FVector2D Start = RotatePoint2D(
			FVector2D(-HalfWidth, LineY), SynthwaveSunset.RotationDegrees) + SynthwaveSunset.Offset;
		const FVector2D End = RotatePoint2D(
			FVector2D(HalfWidth, LineY), SynthwaveSunset.RotationDegrees) + SynthwaveSunset.Offset;
		AppendTubeSegment(
			DynamicMesh,
			Start,
			End,
			FaceDepth,
			SynthwaveSunset.SunStripeTubeRadius);
	}
}

void AParametricRetroTriangleSign::BuildArcadeBulbs(
	UDynamicMesh* DynamicMesh,
	const TArray<FVector2D>& Vertices)
{
	AppendPerimeterBulbs(
		DynamicMesh,
		Vertices,
		ArcadeMarquee.BulbSpacing,
		ArcadeMarquee.BulbRadius,
		ArcadeMarquee.BulbInsetScale,
		ArcadeMarquee.BulbSurfaceOffset);
}

void AParametricRetroTriangleSign::AppendPerimeterBulbs(
	UDynamicMesh* DynamicMesh,
	const TArray<FVector2D>& Vertices,
	float Spacing,
	float Radius,
	float InsetScale,
	float SurfaceOffset) const
{
	if (!DynamicMesh || Vertices.Num() < 3)
	{
		return;
	}

	FVector2D Centroid = FVector2D::ZeroVector;
	for (const FVector2D& Vertex : Vertices)
	{
		Centroid += Vertex;
	}
	Centroid /= static_cast<double>(Vertices.Num());

	TArray<FVector2D> BulbPath;
	BulbPath.Reserve(Vertices.Num());
	const double SafeInsetScale = FMath::Clamp(InsetScale, 0.1f, 1.2f);
	for (const FVector2D& Vertex : Vertices)
	{
		BulbPath.Add(Centroid + (Vertex - Centroid) * SafeInsetScale);
	}

	double Perimeter = 0.0;
	TArray<double> EdgeLengths;
	EdgeLengths.Reserve(BulbPath.Num());
	for (int32 EdgeIndex = 0; EdgeIndex < BulbPath.Num(); ++EdgeIndex)
	{
		const double EdgeLength = FVector2D::Distance(
			BulbPath[EdgeIndex], BulbPath[(EdgeIndex + 1) % BulbPath.Num()]);
		EdgeLengths.Add(EdgeLength);
		Perimeter += EdgeLength;
	}
	if (Perimeter <= UE_KINDA_SMALL_NUMBER)
	{
		return;
	}

	const int32 BulbCount = FMath::Clamp(
		FMath::RoundToInt(Perimeter / FMath::Max(Spacing, 10.0f)),
		4,
		128);
	const float FaceDepth = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension) * 0.5f +
		SurfaceOffset;
	const float SafeRadius = FMath::Max(FMath::Abs(Radius), 0.5f);
	const int32 LongitudeSteps = FMath::Clamp(FrameRadialSegments, 6, 32);
	const int32 LatitudeSteps = FMath::Max(LongitudeSteps / 2, 4);
	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	int32 EdgeIndex = 0;
	double EdgeStartDistance = 0.0;
	for (int32 BulbIndex = 0; BulbIndex < BulbCount; ++BulbIndex)
	{
		const double TargetDistance = Perimeter * static_cast<double>(BulbIndex) / BulbCount;
		while (EdgeIndex < EdgeLengths.Num() - 1 &&
			TargetDistance > EdgeStartDistance + EdgeLengths[EdgeIndex])
		{
			EdgeStartDistance += EdgeLengths[EdgeIndex];
			++EdgeIndex;
		}

		const double EdgeLength = FMath::Max(EdgeLengths[EdgeIndex], UE_KINDA_SMALL_NUMBER);
		const double Alpha = FMath::Clamp((TargetDistance - EdgeStartDistance) / EdgeLength, 0.0, 1.0);
		const FVector2D Position = FMath::Lerp(
			BulbPath[EdgeIndex], BulbPath[(EdgeIndex + 1) % BulbPath.Num()], Alpha);
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
			DynamicMesh,
			PrimitiveOptions,
			FTransform(FRotator::ZeroRotator, FVector(Position.X, Position.Y, FaceDepth)),
			SafeRadius,
			LatitudeSteps,
			LongitudeSteps,
			EGeometryScriptPrimitiveOriginMode::Center);
	}
}

void AParametricRetroTriangleSign::BuildMemphisPattern(UDynamicMesh* DynamicMesh)
{
	if (!DynamicMesh)
	{
		return;
	}

	const double HalfWidth = FMath::Max(FMath::Abs(MemphisPop.BadgeSize.X), MinimumSignDimension) * 0.5;
	const double HalfHeight = FMath::Max(FMath::Abs(MemphisPop.BadgeSize.Y), MinimumSignDimension) * 0.5;
	const double Spread = FMath::Clamp(MemphisPop.PatternSpread, 0.1f, 1.0f);
	const double Clearance = FMath::Clamp(MemphisPop.CenterClearance, 0.0f, 0.9f);
	const double MaxX = HalfWidth * Spread;
	const double MaxY = HalfHeight * Spread;
	const float MotifSize = FMath::Max(FMath::Abs(MemphisPop.MotifSize), 2.0f);
	const float FaceDepth = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension) * 0.5f +
		MemphisPop.PatternSurfaceOffset;
	const int32 MotifCount = FMath::Clamp(MemphisPop.PatternCount, 1, 48);
	FRandomStream Random(MemphisPop.PatternSeed);
	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	auto TransformMotifPoint = [this, HalfWidth, HalfHeight](const FVector2D& Point)
	{
		const double SafeHalfHeight = FMath::Max(HalfHeight, 0.5);
		const double Shear = HalfWidth * FMath::Clamp(MemphisPop.SkewRatio, -0.5f, 0.5f) *
			(Point.Y / SafeHalfHeight);
		return RotatePoint2D(
			FVector2D(Point.X + Shear, Point.Y), MemphisPop.RotationDegrees) + MemphisPop.Offset;
	};

	int32 AddedMotifs = 0;
	const int32 MaxAttempts = MotifCount * 12;
	for (int32 Attempt = 0; Attempt < MaxAttempts && AddedMotifs < MotifCount; ++Attempt)
	{
		const FVector2D LocalPosition(
			Random.FRandRange(-MaxX, MaxX),
			Random.FRandRange(-MaxY, MaxY));
		if (Clearance > 0.0 &&
			FMath::Abs(LocalPosition.X) < HalfWidth * Clearance &&
			FMath::Abs(LocalPosition.Y) < HalfHeight * Clearance * 0.55)
		{
			continue;
		}

		const float Angle = Random.FRandRange(0.0f, 360.0f);
		const FVector2D Direction = RotatePoint2D(FVector2D(1.0, 0.0), Angle);
		const int32 MotifType = Random.RandRange(0, 2);
		if (MotifType == 0)
		{
			const FVector2D Position = TransformMotifPoint(LocalPosition);
			UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
				DynamicMesh,
				PrimitiveOptions,
				FTransform(FRotator::ZeroRotator, FVector(Position.X, Position.Y, FaceDepth)),
				MotifSize * 0.28f,
				FMath::Max(FrameRadialSegments / 2, 4),
				FMath::Clamp(FrameRadialSegments, 6, 32),
				EGeometryScriptPrimitiveOriginMode::Center);
		}
		else if (MotifType == 1)
		{
			const FVector2D HalfSegment = Direction * (MotifSize * 0.65f);
			AppendTubeSegment(
				DynamicMesh,
				TransformMotifPoint(LocalPosition - HalfSegment),
				TransformMotifPoint(LocalPosition + HalfSegment),
				FaceDepth,
				MotifSize * 0.13f);
		}
		else
		{
			TArray<FVector2D, TInlineAllocator<3>> Triangle;
			for (int32 VertexIndex = 0; VertexIndex < 3; ++VertexIndex)
			{
				const float VertexAngle = Angle + 90.0f + VertexIndex * 120.0f;
				Triangle.Add(TransformMotifPoint(
					LocalPosition + RotatePoint2D(FVector2D(MotifSize * 0.62f, 0.0f), VertexAngle)));
			}
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				AppendTubeSegment(
					DynamicMesh,
					Triangle[Edge],
					Triangle[(Edge + 1) % 3],
					FaceDepth,
					MotifSize * 0.10f);
			}
		}
		++AddedMotifs;
	}
}

void AParametricRetroTriangleSign::BuildDiamondSpeedLines(UDynamicMesh* DynamicMesh)
{
	if (!DynamicMesh)
	{
		return;
	}

	const double HalfWidth = FMath::Max(FMath::Abs(NeonDiamond.BadgeSize.X), MinimumSignDimension) * 0.5;
	const int32 LineCount = FMath::Clamp(NeonDiamond.SpeedLineCount, 2, 16);
	const double SideOffset = HalfWidth * FMath::Clamp(NeonDiamond.SpeedLineSideOffsetRatio, 0.1f, 1.5f);
	const double Spacing = FMath::Max(NeonDiamond.SpeedLineSpacing, 1.0f);
	const float BaseLength = FMath::Max(FMath::Abs(NeonDiamond.SpeedLineLength), 5.0f);
	const FVector2D Direction = RotatePoint2D(
		FVector2D(1.0, 0.0), NeonDiamond.SpeedLineSlantDegrees);
	const float FaceDepth = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension) * 0.5f + 3.0f;

	for (int32 LineIndex = 0; LineIndex < LineCount; ++LineIndex)
	{
		const double Y = (static_cast<double>(LineIndex) - (LineCount - 1) * 0.5) * Spacing;
		const float LengthScale = 1.0f - 0.10f * (LineIndex % 3);
		const FVector2D HalfSegment = Direction * (BaseLength * LengthScale * 0.5f);
		for (const double Side : {-1.0, 1.0})
		{
			const FVector2D Center(Side * SideOffset, Y);
			const FVector2D Start = RotatePoint2D(
				Center - HalfSegment, NeonDiamond.RotationDegrees) + NeonDiamond.Offset;
			const FVector2D End = RotatePoint2D(
				Center + HalfSegment, NeonDiamond.RotationDegrees) + NeonDiamond.Offset;
			AppendTubeSegment(
				DynamicMesh,
				Start,
				End,
				FaceDepth,
				NeonDiamond.SpeedLineTubeRadius);
		}
	}
}

void AParametricRetroTriangleSign::BuildRoadsideArrowBulbs(
	UDynamicMesh* DynamicMesh,
	const TArray<FVector2D>& Vertices)
{
	AppendPerimeterBulbs(
		DynamicMesh,
		Vertices,
		RoadsideArrow.BulbSpacing,
		RoadsideArrow.BulbRadius,
		RoadsideArrow.BulbInsetScale,
		RoadsideArrow.BulbSurfaceOffset);
}

void AParametricRetroTriangleSign::BuildLightboxMedallions(UDynamicMesh* DynamicMesh)
{
	if (!DynamicMesh)
	{
		return;
	}

	const double HalfHeight = FMath::Max(FMath::Abs(RoundedLightbox.LightboxSize.Y), MinimumSignDimension) * 0.5;
	const int32 MedallionCount = FMath::Clamp(RoundedLightbox.HeaderMedallionCount, 1, 12);
	const double MedallionSpacing = FMath::Max(RoundedLightbox.HeaderMedallionSpacing, 1.0f);
	const float SafeRadius = FMath::Max(FMath::Abs(RoundedLightbox.HeaderMedallionRadius), 0.5f);
	const float FaceDepth = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension) * 0.5f +
		RoundedLightbox.HeaderSurfaceOffset;
	const int32 LongitudeSteps = FMath::Clamp(FrameRadialSegments, 6, 32);
	const int32 LatitudeSteps = FMath::Max(LongitudeSteps / 2, 4);
	FGeometryScriptPrimitiveOptions PrimitiveOptions;
	PrimitiveOptions.PolygroupMode = EGeometryScriptPrimitivePolygroupMode::PerFace;

	for (int32 Index = 0; Index < MedallionCount; ++Index)
	{
		const double X = (static_cast<double>(Index) - (MedallionCount - 1) * 0.5) * MedallionSpacing;
		const double Y = HalfHeight + RoundedLightbox.HeaderVerticalOffset;
		const FVector2D Position = RotatePoint2D(
			FVector2D(X, Y), RoundedLightbox.RotationDegrees) + RoundedLightbox.Offset;
		UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendSphereLatLong(
			DynamicMesh,
			PrimitiveOptions,
			FTransform(FRotator::ZeroRotator, FVector(Position.X, Position.Y, FaceDepth)),
			SafeRadius,
			LatitudeSteps,
			LongitudeSteps,
			EGeometryScriptPrimitiveOriginMode::Center);
	}
}

void AParametricRetroTriangleSign::BuildShieldUnderlines(UDynamicMesh* DynamicMesh)
{
	if (!DynamicMesh)
	{
		return;
	}

	const int32 LineCount = FMath::Clamp(NeonShield.UnderlineCount, 1, 6);
	const double Spacing = FMath::Max(NeonShield.UnderlineSpacing, 1.0f);
	const float BaseLength = FMath::Max(FMath::Abs(NeonShield.UnderlineLength), 5.0f);
	const FVector2D Direction = RotatePoint2D(
		FVector2D(1.0, 0.0), NeonShield.UnderlineSlantDegrees);
	const float FaceDepth = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension) * 0.5f + 3.0f;

	for (int32 LineIndex = 0; LineIndex < LineCount; ++LineIndex)
	{
		const double Y = NeonShield.UnderlineVerticalOffset -
			(static_cast<double>(LineIndex) - (LineCount - 1) * 0.5) * Spacing;
		const float LengthScale = FMath::Max(1.0f - LineIndex * 0.13f, 0.45f);
		const FVector2D HalfSegment = Direction * (BaseLength * LengthScale * 0.5f);
		const FVector2D LocalCenter(0.0, Y);
		const FVector2D Start = RotatePoint2D(
			LocalCenter - HalfSegment, NeonShield.RotationDegrees) + NeonShield.Offset;
		const FVector2D End = RotatePoint2D(
			LocalCenter + HalfSegment, NeonShield.RotationDegrees) + NeonShield.Offset;
		AppendTubeSegment(
			DynamicMesh,
			Start,
			End,
			FaceDepth,
			NeonShield.UnderlineTubeRadius);
	}
}

void AParametricRetroTriangleSign::BuildDecorations(const TArray<FVector2D>& Vertices)
{
	UDynamicMesh* DynamicMesh = StripeMesh ? StripeMesh->GetDynamicMesh() : nullptr;
	if (!DynamicMesh)
	{
		return;
	}

	DynamicMesh->Reset();
	bool bShowDecorations = false;
	switch (SignType)
	{
	case ERetroSignType::NeonTriangle:
		bShowDecorations = bShowHorizontalStripes;
		if (bShowDecorations)
		{
			BuildOriginalStripes(DynamicMesh, Vertices);
		}
		break;
	case ERetroSignType::SynthwaveSunsetDisc:
		bShowDecorations = SynthwaveSunset.bShowSunStripes;
		if (bShowDecorations)
		{
			BuildSunsetStripes(DynamicMesh);
		}
		break;
	case ERetroSignType::ArcadeMarquee:
		bShowDecorations = ArcadeMarquee.bShowChaseBulbs;
		if (bShowDecorations)
		{
			BuildArcadeBulbs(DynamicMesh, Vertices);
		}
		break;
	case ERetroSignType::MemphisPopBadge:
		bShowDecorations = MemphisPop.bShowPattern;
		if (bShowDecorations)
		{
			BuildMemphisPattern(DynamicMesh);
		}
		break;
	case ERetroSignType::NeonDiamondBadge:
		bShowDecorations = NeonDiamond.bShowSpeedLines;
		if (bShowDecorations)
		{
			BuildDiamondSpeedLines(DynamicMesh);
		}
		break;
	case ERetroSignType::RoadsideArrow:
		bShowDecorations = RoadsideArrow.bShowChaseBulbs;
		if (bShowDecorations)
		{
			BuildRoadsideArrowBulbs(DynamicMesh, Vertices);
		}
		break;
	case ERetroSignType::RoundedLightbox:
		bShowDecorations = RoundedLightbox.bShowHeaderMedallions;
		if (bShowDecorations)
		{
			BuildLightboxMedallions(DynamicMesh);
		}
		break;
	case ERetroSignType::NeonShieldCrest:
		bShowDecorations = NeonShield.bShowUnderlineStrokes;
		if (bShowDecorations)
		{
			BuildShieldUnderlines(DynamicMesh);
		}
		break;
	default:
		break;
	}

	StripeMesh->NotifyMeshUpdated();
	StripeMesh->SetVisibility(bShowDecorations, true);
	StripeMesh->SetHiddenInGame(!bShowDecorations, true);
	StripeMesh->SetMaterial(0, StripeMaterialInstance);
	StripeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AParametricRetroTriangleSign::ConfigureTextLayer(
	UText3DComponent* TextComponent,
	UText3DComponent* ShadowComponent,
	const FRetroTriangleSignTextSettings& Settings,
	UMaterialInterface* FrontMaterial,
	UMaterialInterface* SideMaterial,
	UMaterialInterface* ShadowMaterial)
{
	if (!TextComponent || !ShadowComponent)
	{
		return;
	}

	// Also enforce this for serialized actors whose component data may still name the legacy
	// renderer even though the native constructor now defaults to the dynamic renderer.
	ConfigureSafeTextRenderer(TextComponent);
	ConfigureSafeTextRenderer(ShadowComponent);

	const bool bTextVisible = Settings.bVisible && !Settings.Text.IsEmpty();
	TextComponent->SetVisibility(bTextVisible, true);
	TextComponent->SetHiddenInGame(!bTextVisible, true);
	ShadowComponent->SetVisibility(bTextVisible && Settings.bUseOffsetShadow, true);
	ShadowComponent->SetHiddenInGame(!bTextVisible || !Settings.bUseOffsetShadow, true);

	auto ConfigureGeometry = [&Settings](UText3DComponent* Component, float Extrude)
	{
		Component->SetText(Settings.Text);
		if (Settings.Font)
		{
			Component->SetFont(Settings.Font);
		}
		if (!Settings.Typeface.IsNone())
		{
			Component->SetTypeface(Settings.Typeface);
		}
		Component->SetFontSize(FMath::Max(Settings.FontSize, 1.0f));
		Component->SetExtrude(FMath::Max(Extrude, 0.0f));
		Component->SetBevel(FMath::Max(Settings.Bevel, 0.0f));
		Component->SetBevelType(Settings.BevelType);
		Component->SetBevelSegments(FMath::Clamp(Settings.BevelSegments, 1, 12));
		Component->SetHasOutline(Settings.bUseOutline);
		Component->SetOutlineExpand(FMath::Max(Settings.OutlineExpand, 0.0f));
		Component->SetKerning(FMath::Clamp(Settings.Kerning, -100.0f, 100.0f));
		Component->SetLineSpacing(FMath::Clamp(Settings.LineSpacing, -100.0f, 500.0f));
		Component->SetWordSpacing(FMath::Clamp(Settings.WordSpacing, -100.0f, 500.0f));
		Component->SetHorizontalAlignment(EText3DHorizontalTextAlignment::Center);
		Component->SetVerticalAlignment(EText3DVerticalTextAlignment::Center);
		Component->SetHasMaxWidth(Settings.MaxWidth > 0.0f);
		Component->SetMaxWidth(FMath::Max(Settings.MaxWidth, 1.0f));
		Component->SetMaxWidthHandling(EText3DMaxWidthHandling::Scale);
		Component->SetScaleProportionally(true);
	};

	ConfigureGeometry(TextComponent, Settings.Extrude);
	ConfigureGeometry(ShadowComponent, FMath::Max(Settings.ShadowDepth, 0.5f));

	UMaterialInstanceDynamic* FrontInstance = CreateTintedMaterial(
		FrontMaterial,
		Settings.FrontColor,
		Settings.EmissiveIntensity);
	UMaterialInstanceDynamic* SideInstance = CreateTintedMaterial(
		SideMaterial,
		Settings.SideColor,
		Settings.EmissiveIntensity);
	UMaterialInstanceDynamic* ShadowInstance = CreateTintedMaterial(
		ShadowMaterial,
		Settings.ShadowColor,
		Settings.EmissiveIntensity);
	TextMaterialInstances.Add(FrontInstance);
	TextMaterialInstances.Add(SideInstance);
	TextMaterialInstances.Add(ShadowInstance);

	// UE 5.8's default Text3D material extension starts in Solid mode. Merely
	// calling SetFrontMaterial does not change that mode, so its next async
	// material pass replaces custom slots with the built-in white material.
	// Switch both layers to Custom before assigning our material instances.
	auto EnableCustomMaterials = [](UText3DComponent* Component)
	{
		if (UText3DDefaultMaterialExtension* MaterialExtension =
			Component->GetCastedMaterialExtension<UText3DDefaultMaterialExtension>())
		{
			MaterialExtension->SetUseSingleMaterial(false);
			MaterialExtension->SetStyle(EText3DMaterialStyle::Custom);
		}
	};
	EnableCustomMaterials(TextComponent);
	EnableCustomMaterials(ShadowComponent);

	TextComponent->SetFrontMaterial(FrontInstance);
	TextComponent->SetBevelMaterial(SideInstance);
	TextComponent->SetExtrudeMaterial(SideInstance);
	TextComponent->SetBackMaterial(SideInstance);
	ShadowComponent->SetFrontMaterial(ShadowInstance);
	ShadowComponent->SetBevelMaterial(ShadowInstance);
	ShadowComponent->SetExtrudeMaterial(ShadowInstance);
	ShadowComponent->SetBackMaterial(ShadowInstance);

	const float SafeThickness = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension);
	const float FrontX = SafeThickness * 0.5f + FMath::Max(Settings.SurfaceOffset, 0.0f);
	const FQuat FaceTowardPositiveX(FVector::UpVector, PI);
	const FQuat InPlaneRotation(
		FVector::ForwardVector,
		FMath::DegreesToRadians(Settings.RotationDegrees));
	const FQuat TextRotation = InPlaneRotation * FaceTowardPositiveX;
	TextComponent->SetRelativeLocation(FVector(FrontX, Settings.Position.X, Settings.Position.Y));
	TextComponent->SetRelativeRotation(TextRotation);
	TextComponent->SetRelativeScale3D(FVector(
		1.0f,
		FMath::Max(Settings.HorizontalScale, 0.05f),
		FMath::Max(Settings.VerticalScale, 0.05f)));

	ShadowComponent->SetRelativeLocation(FVector(
		FrontX - FMath::Max(Settings.ShadowDepth, 0.0f),
		Settings.Position.X + Settings.ShadowOffset.X,
		Settings.Position.Y + Settings.ShadowOffset.Y));
	ShadowComponent->SetRelativeRotation(TextRotation);
	ShadowComponent->SetRelativeScale3D(FVector(
		1.0f,
		FMath::Max(Settings.HorizontalScale, 0.05f),
		FMath::Max(Settings.VerticalScale, 0.05f)));

	TextComponent->SetCastShadow(Settings.bCastShadow);
	ShadowComponent->SetCastShadow(Settings.bCastShadow);
	// Text3D setters above already coalesce their flags in the build system. Do not force an
	// immediate rebuild from OnConstruction: it can overlap editor component reregistration.
}

void AParametricRetroTriangleSign::RebuildSign()
{
	TriangleSize = FVector2D(
		FMath::Max(FMath::Abs(TriangleSize.X), MinimumSignDimension),
		FMath::Max(FMath::Abs(TriangleSize.Y), MinimumSignDimension));
	SynthwaveSunset.BadgeSize = FVector2D(
		FMath::Max(FMath::Abs(SynthwaveSunset.BadgeSize.X), MinimumSignDimension),
		FMath::Max(FMath::Abs(SynthwaveSunset.BadgeSize.Y), MinimumSignDimension));
	SynthwaveSunset.OutlineSegments = FMath::Clamp(SynthwaveSunset.OutlineSegments, 12, 96);
	SynthwaveSunset.SunRadiusScale = FMath::Clamp(SynthwaveSunset.SunRadiusScale, 0.05f, 1.0f);
	SynthwaveSunset.SunStripeCount = FMath::Clamp(SynthwaveSunset.SunStripeCount, 1, 24);
	ArcadeMarquee.MarqueeSize = FVector2D(
		FMath::Max(FMath::Abs(ArcadeMarquee.MarqueeSize.X), MinimumSignDimension),
		FMath::Max(FMath::Abs(ArcadeMarquee.MarqueeSize.Y), MinimumSignDimension));
	ArcadeMarquee.TopInsetRatio = FMath::Clamp(ArcadeMarquee.TopInsetRatio, 0.0f, 0.8f);
	ArcadeMarquee.BottomInsetRatio = FMath::Clamp(ArcadeMarquee.BottomInsetRatio, 0.0f, 0.8f);
	ArcadeMarquee.SidePointHeight = FMath::Clamp(ArcadeMarquee.SidePointHeight, -1.0f, 1.0f);
	ArcadeMarquee.BulbInsetScale = FMath::Clamp(ArcadeMarquee.BulbInsetScale, 0.1f, 1.2f);
	MemphisPop.BadgeSize = FVector2D(
		FMath::Max(FMath::Abs(MemphisPop.BadgeSize.X), MinimumSignDimension),
		FMath::Max(FMath::Abs(MemphisPop.BadgeSize.Y), MinimumSignDimension));
	MemphisPop.CornerCutRatio = FMath::Clamp(MemphisPop.CornerCutRatio, 0.0f, 0.45f);
	MemphisPop.SkewRatio = FMath::Clamp(MemphisPop.SkewRatio, -0.5f, 0.5f);
	MemphisPop.PatternCount = FMath::Clamp(MemphisPop.PatternCount, 1, 48);
	MemphisPop.PatternSpread = FMath::Clamp(MemphisPop.PatternSpread, 0.1f, 1.0f);
	MemphisPop.CenterClearance = FMath::Clamp(MemphisPop.CenterClearance, 0.0f, 0.9f);
	NeonDiamond.BadgeSize = FVector2D(
		FMath::Max(FMath::Abs(NeonDiamond.BadgeSize.X), MinimumSignDimension),
		FMath::Max(FMath::Abs(NeonDiamond.BadgeSize.Y), MinimumSignDimension));
	NeonDiamond.SpeedLineCount = FMath::Clamp(NeonDiamond.SpeedLineCount, 2, 16);
	NeonDiamond.SpeedLineSideOffsetRatio = FMath::Clamp(
		NeonDiamond.SpeedLineSideOffsetRatio, 0.1f, 1.5f);
	RoadsideArrow.ArrowSize = FVector2D(
		FMath::Max(FMath::Abs(RoadsideArrow.ArrowSize.X), MinimumSignDimension),
		FMath::Max(FMath::Abs(RoadsideArrow.ArrowSize.Y), MinimumSignDimension));
	RoadsideArrow.HeadLengthRatio = FMath::Clamp(RoadsideArrow.HeadLengthRatio, 0.15f, 0.75f);
	RoadsideArrow.ShaftHeightRatio = FMath::Clamp(RoadsideArrow.ShaftHeightRatio, 0.1f, 1.0f);
	RoadsideArrow.TailNotchRatio = FMath::Clamp(RoadsideArrow.TailNotchRatio, 0.0f, 0.6f);
	RoadsideArrow.BulbInsetScale = FMath::Clamp(RoadsideArrow.BulbInsetScale, 0.1f, 1.2f);
	RoundedLightbox.LightboxSize = FVector2D(
		FMath::Max(FMath::Abs(RoundedLightbox.LightboxSize.X), MinimumSignDimension),
		FMath::Max(FMath::Abs(RoundedLightbox.LightboxSize.Y), MinimumSignDimension));
	RoundedLightbox.CornerRadius = FMath::Clamp(
		RoundedLightbox.CornerRadius,
		0.0f,
		static_cast<float>(FMath::Min(
			RoundedLightbox.LightboxSize.X,
			RoundedLightbox.LightboxSize.Y) * 0.5));
	RoundedLightbox.CornerSegments = FMath::Clamp(RoundedLightbox.CornerSegments, 2, 16);
	RoundedLightbox.HeaderMedallionCount = FMath::Clamp(
		RoundedLightbox.HeaderMedallionCount, 1, 12);
	NeonShield.BadgeSize = FVector2D(
		FMath::Max(FMath::Abs(NeonShield.BadgeSize.X), MinimumSignDimension),
		FMath::Max(FMath::Abs(NeonShield.BadgeSize.Y), MinimumSignDimension));
	NeonShield.TopInsetRatio = FMath::Clamp(NeonShield.TopInsetRatio, 0.0f, 0.45f);
	NeonShield.TopPeakRatio = FMath::Clamp(NeonShield.TopPeakRatio, 0.0f, 0.35f);
	NeonShield.ShoulderDropRatio = FMath::Clamp(NeonShield.ShoulderDropRatio, 0.05f, 0.45f);
	NeonShield.BottomTaperRatio = FMath::Clamp(NeonShield.BottomTaperRatio, 0.2f, 0.95f);
	NeonShield.PointDepthRatio = FMath::Clamp(NeonShield.PointDepthRatio, 0.1f, 0.45f);
	NeonShield.UnderlineCount = FMath::Clamp(NeonShield.UnderlineCount, 1, 6);
	PanelThickness = FMath::Max(FMath::Abs(PanelThickness), MinimumSignDimension);
	ApexOffset = FMath::Clamp(ApexOffset, -1.0f, 1.0f);
	FrameRadialSegments = FMath::Clamp(FrameRadialSegments, 6, 32);

	const FRotator MeshFaceRotation = FRotationMatrix::MakeFromXY(
		FVector::YAxisVector,
		FVector::ZAxisVector).Rotator();
	PanelMesh->SetRelativeRotation(MeshFaceRotation);
	PrimaryFrameMesh->SetRelativeRotation(MeshFaceRotation);
	AccentFrameMesh->SetRelativeRotation(MeshFaceRotation);
	StripeMesh->SetRelativeRotation(MeshFaceRotation);

	PanelMaterialInstance = CreateTintedMaterial(
		MaterialAssignments.PanelMaterial,
		PanelColor,
		PanelEmissiveIntensity);
	PrimaryFrameMaterialInstance = CreateTintedMaterial(
		MaterialAssignments.PrimaryFrameMaterial,
		PrimaryFrameColor,
		FrameEmissiveIntensity);
	AccentFrameMaterialInstance = CreateTintedMaterial(
		MaterialAssignments.AccentFrameMaterial,
		AccentFrameColor,
		FrameEmissiveIntensity);
	StripeMaterialInstance = CreateTintedMaterial(
		MaterialAssignments.StripeMaterial,
		StripeColor,
		StripeEmissiveIntensity);

	TArray<FVector2D> Vertices;
	GetSignOutlineVertices(Vertices);
	BuildPanel(Vertices);

	PrimaryFrameMesh->SetVisibility(bShowPrimaryFrame, true);
	if (bShowPrimaryFrame)
	{
		BuildTubeFrame(
			PrimaryFrameMesh,
			Vertices,
			PrimaryFrameScale,
			PrimaryFrameTubeRadius,
			0.0f,
			FVector2D::ZeroVector,
			PrimaryFrameSurfaceOffset);
	}
	else if (UDynamicMesh* DynamicMesh = PrimaryFrameMesh->GetDynamicMesh())
	{
		DynamicMesh->Reset();
		PrimaryFrameMesh->NotifyMeshUpdated();
	}
	PrimaryFrameMesh->SetMaterial(0, PrimaryFrameMaterialInstance);

	AccentFrameMesh->SetVisibility(bShowAccentFrame, true);
	if (bShowAccentFrame)
	{
		BuildTubeFrame(
			AccentFrameMesh,
			Vertices,
			AccentFrameScale,
			AccentFrameTubeRadius,
			AccentFrameRotationDegrees,
			AccentFrameOffset,
			AccentFrameSurfaceOffset);
	}
	else if (UDynamicMesh* DynamicMesh = AccentFrameMesh->GetDynamicMesh())
	{
		DynamicMesh->Reset();
		AccentFrameMesh->NotifyMeshUpdated();
	}
	AccentFrameMesh->SetMaterial(0, AccentFrameMaterialInstance);

	BuildDecorations(Vertices);
	TextMaterialInstances.Reset();
	ConfigureTextLayer(
		MainTextComponent,
		MainShadowComponent,
		MainText,
		MaterialAssignments.MainTextFrontMaterial,
		MaterialAssignments.MainTextSideMaterial,
		MaterialAssignments.MainTextShadowMaterial);
	ConfigureTextLayer(
		SubtitleTextComponent,
		SubtitleShadowComponent,
		SubtitleText,
		MaterialAssignments.SubtitleFrontMaterial,
		MaterialAssignments.SubtitleSideMaterial,
		MaterialAssignments.SubtitleShadowMaterial);
}

void AParametricRetroTriangleSign::SetMainText(const FText& NewText)
{
	MainText.Text = NewText;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetSubtitleText(const FText& NewText)
{
	SubtitleText.Text = NewText;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetSignTexts(
	const FText& NewMainText,
	const FText& NewSubtitleText)
{
	MainText.Text = NewMainText;
	SubtitleText.Text = NewSubtitleText;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetSignType(ERetroSignType NewSignType)
{
	SignType = NewSignType;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetSynthwaveSunsetSettings(
	const FRetroSynthwaveSunsetSettings& NewSettings)
{
	SignType = ERetroSignType::SynthwaveSunsetDisc;
	SynthwaveSunset = NewSettings;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetArcadeMarqueeSettings(
	const FRetroArcadeMarqueeSettings& NewSettings)
{
	SignType = ERetroSignType::ArcadeMarquee;
	ArcadeMarquee = NewSettings;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetMemphisPopSettings(
	const FRetroMemphisPopSettings& NewSettings)
{
	SignType = ERetroSignType::MemphisPopBadge;
	MemphisPop = NewSettings;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetNeonDiamondSettings(
	const FRetroNeonDiamondSettings& NewSettings)
{
	SignType = ERetroSignType::NeonDiamondBadge;
	NeonDiamond = NewSettings;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetRoadsideArrowSettings(
	const FRetroRoadsideArrowSettings& NewSettings)
{
	SignType = ERetroSignType::RoadsideArrow;
	RoadsideArrow = NewSettings;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetRoundedLightboxSettings(
	const FRetroRoundedLightboxSettings& NewSettings)
{
	SignType = ERetroSignType::RoundedLightbox;
	RoundedLightbox = NewSettings;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetNeonShieldSettings(
	const FRetroNeonShieldSettings& NewSettings)
{
	SignType = ERetroSignType::NeonShieldCrest;
	NeonShield = NewSettings;
	RebuildSign();
}

void AParametricRetroTriangleSign::ApplyReferenceShieldPreset()
{
	SignType = ERetroSignType::NeonShieldCrest;

	NeonShield.BadgeSize = FVector2D(600.0, 700.0);
	NeonShield.TopInsetRatio = 0.18f;
	NeonShield.TopPeakRatio = 0.13f;
	NeonShield.ShoulderDropRatio = 0.09f;
	NeonShield.BottomTaperRatio = 0.78f;
	NeonShield.PointDepthRatio = 0.30f;
	NeonShield.RotationDegrees = 0.0f;
	NeonShield.Offset = FVector2D::ZeroVector;
	NeonShield.bShowUnderlineStrokes = true;
	NeonShield.UnderlineCount = 3;
	NeonShield.UnderlineLength = 300.0f;
	NeonShield.UnderlineSpacing = 18.0f;
	NeonShield.UnderlineVerticalOffset = -285.0f;
	NeonShield.UnderlineSlantDegrees = -10.0f;
	NeonShield.UnderlineTubeRadius = 3.5f;

	PanelThickness = 22.0f;
	PanelBevel = 4.0f;
	PanelBevelSegments = 4;
	PanelColor = FLinearColor(0.012f, 0.004f, 0.045f, 1.0f);
	PanelEmissiveIntensity = 0.3f;

	bShowPrimaryFrame = true;
	PrimaryFrameScale = 0.94f;
	PrimaryFrameTubeRadius = 5.0f;
	PrimaryFrameSurfaceOffset = 5.0f;
	PrimaryFrameColor = FLinearColor(1.0f, 0.76f, 0.98f, 1.0f);
	bShowAccentFrame = true;
	AccentFrameScale = 1.035f;
	AccentFrameTubeRadius = 3.2f;
	AccentFrameRotationDegrees = 0.0f;
	AccentFrameOffset = FVector2D::ZeroVector;
	AccentFrameSurfaceOffset = 1.0f;
	AccentFrameColor = FLinearColor(1.0f, 0.0f, 0.72f, 1.0f);
	FrameRadialSegments = 16;
	FrameEmissiveIntensity = 13.0f;
	StripeColor = FLinearColor(1.0f, 0.15f, 0.82f, 1.0f);
	StripeEmissiveIntensity = 15.0f;

	if (UFont* BlockFont = LoadObject<UFont>(
		nullptr,
		TEXT("/Game/Structures/DynamicMesh/RetroTriangleSign/Fonts/RetroBlock_RussoOne_Font.RetroBlock_RussoOne_Font")))
	{
		MainText.Font = BlockFont;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Purple neon shield preset could not load its Russo One font asset."));
	}
	MainText.bVisible = true;
	MainText.Text = FText::FromString(TEXT("SEME\nSTA88"));
	MainText.Typeface = NAME_None;
	MainText.FontSize = 172.0f;
	MainText.HorizontalScale = 0.92f;
	MainText.VerticalScale = 1.0f;
	MainText.Kerning = -3.0f;
	MainText.LineSpacing = -38.0f;
	MainText.WordSpacing = 0.0f;
	MainText.MaxWidth = 535.0f;
	MainText.Position = FVector2D(0.0, 45.0);
	MainText.RotationDegrees = 0.0f;
	MainText.SurfaceOffset = 13.0f;
	MainText.Extrude = 22.0f;
	MainText.Bevel = 3.2f;
	MainText.BevelType = EText3DBevelType::Convex;
	MainText.BevelSegments = 5;
	MainText.bUseOutline = false;
	MainText.FrontColor = FLinearColor(0.91f, 0.91f, 1.0f, 1.0f);
	MainText.SideColor = FLinearColor(0.28f, 0.02f, 0.56f, 1.0f);
	MainText.EmissiveIntensity = 1.4f;
	MainText.bUseOffsetShadow = true;
	MainText.ShadowOffset = FVector2D(10.0, -11.0);
	MainText.ShadowDepth = 4.0f;
	MainText.ShadowColor = FLinearColor(0.24f, 0.0f, 0.48f, 1.0f);
	MainText.bCastShadow = false;

	if (UFont* ScriptFont = LoadObject<UFont>(
		nullptr,
		TEXT("/Game/Structures/DynamicMesh/RetroTriangleSign/Fonts/RetroScript_Yellowtail_Font.RetroScript_Yellowtail_Font")))
	{
		SubtitleText.Font = ScriptFont;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Purple neon shield preset could not load its Yellowtail font asset."));
	}
	SubtitleText.bVisible = true;
	SubtitleText.Text = FText::FromString(TEXT("Gacor"));
	SubtitleText.Typeface = NAME_None;
	SubtitleText.FontSize = 132.0f;
	SubtitleText.HorizontalScale = 1.0f;
	SubtitleText.VerticalScale = 1.0f;
	SubtitleText.Kerning = 0.0f;
	SubtitleText.LineSpacing = 0.0f;
	SubtitleText.WordSpacing = 0.0f;
	SubtitleText.MaxWidth = 500.0f;
	SubtitleText.Position = FVector2D(0.0, -215.0);
	SubtitleText.RotationDegrees = -5.0f;
	SubtitleText.SurfaceOffset = 30.0f;
	SubtitleText.Extrude = 12.0f;
	SubtitleText.Bevel = 2.0f;
	SubtitleText.BevelType = EText3DBevelType::Convex;
	SubtitleText.BevelSegments = 4;
	SubtitleText.bUseOutline = false;
	SubtitleText.FrontColor = FLinearColor(1.0f, 0.82f, 0.98f, 1.0f);
	SubtitleText.SideColor = FLinearColor(1.0f, 0.0f, 0.68f, 1.0f);
	SubtitleText.EmissiveIntensity = 9.0f;
	SubtitleText.bUseOffsetShadow = true;
	SubtitleText.ShadowOffset = FVector2D(7.0, -7.0);
	SubtitleText.ShadowDepth = 3.0f;
	SubtitleText.ShadowColor = FLinearColor(0.28f, 0.0f, 0.55f, 1.0f);
	SubtitleText.bCastShadow = false;

	RebuildSign();
}

void AParametricRetroTriangleSign::ConfigureTriangle(
	FVector2D NewTriangleSize,
	float NewPanelThickness,
	float NewApexOffset,
	ERetroTrianglePointDirection NewPointDirection,
	float NewRotationDegrees,
	FVector2D NewTriangleOffset)
{
	SignType = ERetroSignType::NeonTriangle;
	TriangleSize = NewTriangleSize;
	PanelThickness = NewPanelThickness;
	ApexOffset = NewApexOffset;
	PointDirection = NewPointDirection;
	TriangleRotationDegrees = NewRotationDegrees;
	TriangleOffset = NewTriangleOffset;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetMainTextSettings(
	const FRetroTriangleSignTextSettings& NewSettings)
{
	MainText = NewSettings;
	RebuildSign();
}

void AParametricRetroTriangleSign::SetSubtitleTextSettings(
	const FRetroTriangleSignTextSettings& NewSettings)
{
	SubtitleText = NewSettings;
	RebuildSign();
}
