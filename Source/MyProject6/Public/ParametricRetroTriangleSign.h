// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Text3DTypes.h"
#include "ParametricRetroTriangleSign.generated.h"

class UDynamicMeshComponent;
class UFont;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UText3DComponent;

/** Direction in which the triangle's apex points before the optional in-plane rotation. */
UENUM(BlueprintType)
enum class ERetroTrianglePointDirection : uint8
{
	Up,
	Down,
	Left,
	Right
};

/** Research-derived 1980s sign families. The original triangle remains the default. */
UENUM(BlueprintType)
enum class ERetroSignType : uint8
{
	NeonTriangle UMETA(DisplayName = "Neon Triangle (Original)"),
	SynthwaveSunsetDisc UMETA(DisplayName = "Synthwave Sunset Disc"),
	ArcadeMarquee UMETA(DisplayName = "Arcade Marquee"),
	MemphisPopBadge UMETA(DisplayName = "Memphis Pop Badge"),
	NeonDiamondBadge UMETA(DisplayName = "Neon Diamond Badge"),
	RoadsideArrow UMETA(DisplayName = "Roadside Arrow Sign"),
	RoundedLightbox UMETA(DisplayName = "Rounded Neon Lightbox"),
	NeonShieldCrest UMETA(DisplayName = "Neon Shield Crest")
};

UENUM(BlueprintType)
enum class ERetroHorizontalDirection : uint8
{
	Left,
	Right
};

/** Elliptical retro-futurist badge with a horizontally segmented sun motif. */
USTRUCT(BlueprintType)
struct FRetroSynthwaveSunsetSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D BadgeSize = FVector2D(680.0, 560.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "12", ClampMax = "96"))
	int32 OutlineSegments = 48;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	/** X is horizontal and Y is vertical on the sign face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	/** Sun radius relative to half of the badge's shortest dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Motif", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float SunRadiusScale = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Motif", meta = (Units = "cm"))
	float SunVerticalOffset = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Motif")
	bool bShowSunStripes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Motif", meta = (EditCondition = "bShowSunStripes", ClampMin = "1", ClampMax = "24"))
	int32 SunStripeCount = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Motif", meta = (EditCondition = "bShowSunStripes", ClampMin = "1.0", Units = "cm"))
	float SunStripeSpacing = 29.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Motif", meta = (EditCondition = "bShowSunStripes", ClampMin = "0.5", Units = "cm"))
	float SunStripeTubeRadius = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Motif", meta = (EditCondition = "bShowSunStripes", ClampMin = "0.05", ClampMax = "1.0"))
	float SunStripeWidthScale = 0.92f;
};

/** Wide arcade-cabinet header with sloped corners and perimeter chase bulbs. */
USTRUCT(BlueprintType)
struct FRetroArcadeMarqueeSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D MarqueeSize = FVector2D(820.0, 360.0);

	/** Horizontal inset of the two upper corners, relative to half-width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "0.8"))
	float TopInsetRatio = 0.16f;

	/** Horizontal inset of the two lower corners, relative to half-width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "0.8"))
	float BottomInsetRatio = 0.30f;

	/** Height of the widest side points: -1 is bottom and +1 is top. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float SidePointHeight = -0.10f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	/** X is horizontal and Y is vertical on the sign face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs")
	bool bShowChaseBulbs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs", meta = (EditCondition = "bShowChaseBulbs", ClampMin = "10.0", Units = "cm"))
	float BulbSpacing = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs", meta = (EditCondition = "bShowChaseBulbs", ClampMin = "0.5", Units = "cm"))
	float BulbRadius = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs", meta = (EditCondition = "bShowChaseBulbs", ClampMin = "0.1", ClampMax = "1.2"))
	float BulbInsetScale = 0.90f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs", meta = (EditCondition = "bShowChaseBulbs", Units = "cm"))
	float BulbSurfaceOffset = 5.0f;
};

/** Skewed clipped-corner badge with deterministic Memphis-inspired geometric confetti. */
USTRUCT(BlueprintType)
struct FRetroMemphisPopSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D BadgeSize = FVector2D(720.0, 480.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float CornerCutRatio = 0.16f;

	/** Horizontal shear applied from the lower edge to the upper edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "-0.5", ClampMax = "0.5"))
	float SkewRatio = 0.14f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "deg"))
	float RotationDegrees = -3.0f;

	/** X is horizontal and Y is vertical on the sign face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometric Pattern")
	bool bShowPattern = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometric Pattern", meta = (EditCondition = "bShowPattern"))
	int32 PatternSeed = 1981;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometric Pattern", meta = (EditCondition = "bShowPattern", ClampMin = "1", ClampMax = "48"))
	int32 PatternCount = 14;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometric Pattern", meta = (EditCondition = "bShowPattern", ClampMin = "2.0", Units = "cm"))
	float MotifSize = 24.0f;

	/** Fraction of the panel extents used for motif placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometric Pattern", meta = (EditCondition = "bShowPattern", ClampMin = "0.1", ClampMax = "1.0"))
	float PatternSpread = 0.84f;

	/** Keeps the central text area clear; zero allows motifs everywhere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometric Pattern", meta = (EditCondition = "bShowPattern", ClampMin = "0.0", ClampMax = "0.9"))
	float CenterClearance = 0.48f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometric Pattern", meta = (EditCondition = "bShowPattern", Units = "cm"))
	float PatternSurfaceOffset = 4.0f;
};

/** Rotated rhombus logo plate with paired lateral speed streaks. */
USTRUCT(BlueprintType)
struct FRetroNeonDiamondSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D BadgeSize = FVector2D(680.0, 680.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed Lines")
	bool bShowSpeedLines = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed Lines", meta = (EditCondition = "bShowSpeedLines", ClampMin = "2", ClampMax = "16"))
	int32 SpeedLineCount = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed Lines", meta = (EditCondition = "bShowSpeedLines", ClampMin = "5.0", Units = "cm"))
	float SpeedLineLength = 105.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed Lines", meta = (EditCondition = "bShowSpeedLines", ClampMin = "1.0", Units = "cm"))
	float SpeedLineSpacing = 24.0f;

	/** Horizontal distance from center relative to the diamond half-width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed Lines", meta = (EditCondition = "bShowSpeedLines", ClampMin = "0.1", ClampMax = "1.5"))
	float SpeedLineSideOffsetRatio = 0.76f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed Lines", meta = (EditCondition = "bShowSpeedLines", Units = "deg"))
	float SpeedLineSlantDegrees = -7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed Lines", meta = (EditCondition = "bShowSpeedLines", ClampMin = "0.5", Units = "cm"))
	float SpeedLineTubeRadius = 2.5f;
};

/** Concave roadside arrow plate inspired by stacked neon wayfinding signs. */
USTRUCT(BlueprintType)
struct FRetroRoadsideArrowSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D ArrowSize = FVector2D(820.0, 360.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	ERetroHorizontalDirection Direction = ERetroHorizontalDirection::Right;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.15", ClampMax = "0.75"))
	float HeadLengthRatio = 0.34f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float ShaftHeightRatio = 0.58f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "0.6"))
	float TailNotchRatio = 0.16f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "deg"))
	float RotationDegrees = -3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs")
	bool bShowChaseBulbs = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs", meta = (EditCondition = "bShowChaseBulbs", ClampMin = "10.0", Units = "cm"))
	float BulbSpacing = 58.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs", meta = (EditCondition = "bShowChaseBulbs", ClampMin = "0.5", Units = "cm"))
	float BulbRadius = 6.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs", meta = (EditCondition = "bShowChaseBulbs", ClampMin = "0.1", ClampMax = "1.2"))
	float BulbInsetScale = 0.88f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chase Bulbs", meta = (EditCondition = "bShowChaseBulbs", Units = "cm"))
	float BulbSurfaceOffset = 5.0f;
};

/** Rounded rectangular lightbox with a row of circular header lamps. */
USTRUCT(BlueprintType)
struct FRetroRoundedLightboxSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D LightboxSize = FVector2D(700.0, 500.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", Units = "cm"))
	float CornerRadius = 65.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "2", ClampMax = "16"))
	int32 CornerSegments = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Header Medallions")
	bool bShowHeaderMedallions = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Header Medallions", meta = (EditCondition = "bShowHeaderMedallions", ClampMin = "1", ClampMax = "12"))
	int32 HeaderMedallionCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Header Medallions", meta = (EditCondition = "bShowHeaderMedallions", ClampMin = "1.0", Units = "cm"))
	float HeaderMedallionSpacing = 78.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Header Medallions", meta = (EditCondition = "bShowHeaderMedallions", ClampMin = "0.5", Units = "cm"))
	float HeaderMedallionRadius = 20.0f;

	/** Distance above the panel's top edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Header Medallions", meta = (EditCondition = "bShowHeaderMedallions", Units = "cm"))
	float HeaderVerticalOffset = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Header Medallions", meta = (EditCondition = "bShowHeaderMedallions", Units = "cm"))
	float HeaderSurfaceOffset = 5.0f;
};

/** Tall shield-shaped crest with stacked-wordmark-friendly proportions and underline strokes. */
USTRUCT(BlueprintType)
struct FRetroNeonShieldSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D BadgeSize = FVector2D(560.0, 720.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "0.45"))
	float TopInsetRatio = 0.15f;

	/** Height of the centered roof peak as a ratio of the full badge height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "0.35"))
	float TopPeakRatio = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.05", ClampMax = "0.45"))
	float ShoulderDropRatio = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.2", ClampMax = "0.95"))
	float BottomTaperRatio = 0.62f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.1", ClampMax = "0.45"))
	float PointDepthRatio = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underline Strokes")
	bool bShowUnderlineStrokes = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underline Strokes", meta = (EditCondition = "bShowUnderlineStrokes", ClampMin = "1", ClampMax = "6"))
	int32 UnderlineCount = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underline Strokes", meta = (EditCondition = "bShowUnderlineStrokes", ClampMin = "5.0", Units = "cm"))
	float UnderlineLength = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underline Strokes", meta = (EditCondition = "bShowUnderlineStrokes", ClampMin = "1.0", Units = "cm"))
	float UnderlineSpacing = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underline Strokes", meta = (EditCondition = "bShowUnderlineStrokes", Units = "cm"))
	float UnderlineVerticalOffset = -235.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underline Strokes", meta = (EditCondition = "bShowUnderlineStrokes", Units = "deg"))
	float UnderlineSlantDegrees = -8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Underline Strokes", meta = (EditCondition = "bShowUnderlineStrokes", ClampMin = "0.5", Units = "cm"))
	float UnderlineTubeRadius = 3.0f;
};

/** Complete geometry, layout and retro-material configuration for one line of 3D text. */
USTRUCT(BlueprintType)
struct FRetroTriangleSignTextSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content")
	bool bVisible = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Content", meta = (MultiLine = "true"))
	FText Text;

	/** Optional font asset. Null uses Text 3D's project fallback font. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	TObjectPtr<UFont> Font = nullptr;

	/** Optional face name inside a composite font, for example Bold or Italic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font")
	FName Typeface = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font", meta = (ClampMin = "1.0", Units = "cm"))
	float FontSize = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float HorizontalScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font", meta = (ClampMin = "0.05", ClampMax = "10.0"))
	float VerticalScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Font", meta = (ClampMin = "-100.0", ClampMax = "100.0"))
	float Kerning = 0.0f;

	/** Extra distance between baselines. Negative values tighten stacked text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "-100.0", ClampMax = "500.0", Units = "cm"))
	float LineSpacing = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "-100.0", ClampMax = "500.0", Units = "cm"))
	float WordSpacing = 0.0f;

	/** Zero disables automatic fitting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float MaxWidth = 500.0f;

	/** X is horizontal and Y is vertical on the sign face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Units = "cm"))
	FVector2D Position = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	/** Distance in front of the panel's front surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float SurfaceOffset = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "0.0", Units = "cm"))
	float Extrude = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "0.0", Units = "cm"))
	float Bevel = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	EText3DBevelType BevelType = EText3DBevelType::Convex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (ClampMin = "1", ClampMax = "12"))
	int32 BevelSegments = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry")
	bool bUseOutline = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geometry", meta = (EditCondition = "bUseOutline", ClampMin = "0.0", Units = "cm"))
	float OutlineExpand = 1.5f;

	/** Legacy serialized field. Material selection now lives on the actor. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FrontMaterial = nullptr;

	/** Used by the bevel, extrusion and back faces. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> SideMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	FLinearColor FrontColor = FLinearColor(0.05f, 0.85f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials")
	FLinearColor SideColor = FLinearColor(1.0f, 0.02f, 0.45f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Materials", meta = (ClampMin = "0.0"))
	float EmissiveIntensity = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset Shadow")
	bool bUseOffsetShadow = true;

	/** X is horizontal and Y is vertical on the sign face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset Shadow", meta = (EditCondition = "bUseOffsetShadow", Units = "cm"))
	FVector2D ShadowOffset = FVector2D(8.0, -8.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset Shadow", meta = (EditCondition = "bUseOffsetShadow", ClampMin = "0.0", Units = "cm"))
	float ShadowDepth = 3.0f;

	/** Legacy serialized field. Material selection now lives on the actor. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ShadowMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Offset Shadow", meta = (EditCondition = "bUseOffsetShadow"))
	FLinearColor ShadowColor = FLinearColor(0.35f, 0.0f, 0.75f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rendering")
	bool bCastShadow = false;
};

/** Every material asset used by the sign, collected into one Details panel group. */
USTRUCT(BlueprintType)
struct FRetroTriangleSignMaterialAssignments
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Panel Material"))
	TObjectPtr<UMaterialInterface> PanelMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Primary Frame Material"))
	TObjectPtr<UMaterialInterface> PrimaryFrameMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Accent Frame Material"))
	TObjectPtr<UMaterialInterface> AccentFrameMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Decoration Material"))
	TObjectPtr<UMaterialInterface> StripeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Main Text Front Material"))
	TObjectPtr<UMaterialInterface> MainTextFrontMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Main Text Side Material"))
	TObjectPtr<UMaterialInterface> MainTextSideMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Main Text Shadow Material"))
	TObjectPtr<UMaterialInterface> MainTextShadowMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Subtitle Front Material"))
	TObjectPtr<UMaterialInterface> SubtitleFrontMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Subtitle Side Material"))
	TObjectPtr<UMaterialInterface> SubtitleSideMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material Assignments", meta = (DisplayName = "Subtitle Shadow Material"))
	TObjectPtr<UMaterialInterface> SubtitleShadowMaterial = nullptr;
};

/**
 * Fully parametric family of 1980s-inspired signs with real 3D text.
 *
 * The sign faces local +X. Its width runs along local Y and its height along local Z, making it
 * convenient to place against a wall or use as a freestanding stage prop.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API AParametricRetroTriangleSign : public AActor
{
	GENERATED_BODY()

public:
	AParametricRetroTriangleSign();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostLoad() override;

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Retro Sign")
	void RebuildSign();

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Text")
	void SetMainText(const FText& NewText);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Text")
	void SetSubtitleText(const FText& NewText);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Text")
	void SetSignTexts(const FText& NewMainText, const FText& NewSubtitleText);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Style")
	void SetSignType(ERetroSignType NewSignType);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Synthwave Sunset Disc")
	void SetSynthwaveSunsetSettings(const FRetroSynthwaveSunsetSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Arcade Marquee")
	void SetArcadeMarqueeSettings(const FRetroArcadeMarqueeSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Memphis Pop Badge")
	void SetMemphisPopSettings(const FRetroMemphisPopSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Neon Diamond Badge")
	void SetNeonDiamondSettings(const FRetroNeonDiamondSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Roadside Arrow")
	void SetRoadsideArrowSettings(const FRetroRoadsideArrowSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Rounded Neon Lightbox")
	void SetRoundedLightboxSettings(const FRetroRoundedLightboxSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Neon Shield Crest")
	void SetNeonShieldSettings(const FRetroNeonShieldSettings& NewSettings);

	/** Applies the supplied purple double-neon shield reference as a complete editable preset. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Retro Sign|Presets", meta = (DisplayName = "Apply Purple Neon Shield Reference"))
	void ApplyReferenceShieldPreset();

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Triangle")
	void ConfigureTriangle(
		FVector2D NewTriangleSize,
		float NewPanelThickness,
		float NewApexOffset,
		ERetroTrianglePointDirection NewPointDirection,
		float NewRotationDegrees,
		FVector2D NewTriangleOffset);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Text")
	void SetMainTextSettings(const FRetroTriangleSignTextSettings& NewSettings);

	UFUNCTION(BlueprintCallable, Category = "Retro Sign|Text")
	void SetSubtitleTextSettings(const FRetroTriangleSignTextSettings& NewSettings);

	/** Returns the panel outline in sign-face coordinates: X is horizontal, Y is vertical. */
	UFUNCTION(BlueprintPure, Category = "Retro Sign|Shape")
	void GetSignOutlineVertices(TArray<FVector2D>& OutVertices) const;

	/** Returns the legacy triangle outline regardless of the selected sign type. */
	UFUNCTION(BlueprintPure, Category = "Retro Sign|Triangle")
	void GetTriangleVertices(TArray<FVector2D>& OutVertices) const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Retro Sign|Components")
	TObjectPtr<USceneComponent> SignRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Retro Sign|Components")
	TObjectPtr<UDynamicMeshComponent> PanelMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Retro Sign|Components")
	TObjectPtr<UDynamicMeshComponent> PrimaryFrameMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Retro Sign|Components")
	TObjectPtr<UDynamicMeshComponent> AccentFrameMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Retro Sign|Components")
	TObjectPtr<UDynamicMeshComponent> StripeMesh;

	// Kept out of the actor Details tree because Text3D owns several instanced editor-only
	// helper objects. Showing four copies there can confuse UE 5.8's property-node mapper.
	UPROPERTY(Transient)
	TObjectPtr<UText3DComponent> MainTextComponent;

	UPROPERTY(Transient)
	TObjectPtr<UText3DComponent> MainShadowComponent;

	UPROPERTY(Transient)
	TObjectPtr<UText3DComponent> SubtitleTextComponent;

	UPROPERTY(Transient)
	TObjectPtr<UText3DComponent> SubtitleShadowComponent;

	/** All material asset assignments are intentionally exposed in this single group. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Material Assignments", meta = (ShowOnlyInnerProperties))
	FRetroTriangleSignMaterialAssignments MaterialAssignments;

	/** Selects the generated silhouette and its research-derived decorative system. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Style")
	ERetroSignType SignType = ERetroSignType::NeonTriangle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle", EditConditionHides, ClampMin = "1.0", Units = "cm"))
	FVector2D TriangleSize = FVector2D(650.0, 520.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Common Geometry", meta = (ClampMin = "1.0", Units = "cm"))
	float PanelThickness = 18.0f;

	/** Normalized apex shift along the opposite edge: -1 to +1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle", EditConditionHides, ClampMin = "-1.0", ClampMax = "1.0"))
	float ApexOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle", EditConditionHides))
	ERetroTrianglePointDirection PointDirection = ERetroTrianglePointDirection::Down;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle", EditConditionHides, Units = "deg"))
	float TriangleRotationDegrees = -4.0f;

	/** X is horizontal and Y is vertical on the sign face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle", EditConditionHides, Units = "cm"))
	FVector2D TriangleOffset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Common Geometry", meta = (ClampMin = "0.0", Units = "cm"))
	float PanelBevel = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Common Geometry", meta = (ClampMin = "1", ClampMax = "12"))
	int32 PanelBevelSegments = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Synthwave Sunset Disc", meta = (EditCondition = "SignType == ERetroSignType::SynthwaveSunsetDisc", EditConditionHides, ShowOnlyInnerProperties))
	FRetroSynthwaveSunsetSettings SynthwaveSunset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Arcade Marquee", meta = (EditCondition = "SignType == ERetroSignType::ArcadeMarquee", EditConditionHides, ShowOnlyInnerProperties))
	FRetroArcadeMarqueeSettings ArcadeMarquee;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Memphis Pop Badge", meta = (EditCondition = "SignType == ERetroSignType::MemphisPopBadge", EditConditionHides, ShowOnlyInnerProperties))
	FRetroMemphisPopSettings MemphisPop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Neon Diamond Badge", meta = (EditCondition = "SignType == ERetroSignType::NeonDiamondBadge", EditConditionHides, ShowOnlyInnerProperties))
	FRetroNeonDiamondSettings NeonDiamond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Roadside Arrow", meta = (EditCondition = "SignType == ERetroSignType::RoadsideArrow", EditConditionHides, ShowOnlyInnerProperties))
	FRetroRoadsideArrowSettings RoadsideArrow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Rounded Neon Lightbox", meta = (EditCondition = "SignType == ERetroSignType::RoundedLightbox", EditConditionHides, ShowOnlyInnerProperties))
	FRetroRoundedLightboxSettings RoundedLightbox;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Neon Shield Crest", meta = (EditCondition = "SignType == ERetroSignType::NeonShieldCrest", EditConditionHides, ShowOnlyInnerProperties))
	FRetroNeonShieldSettings NeonShield;

	/** Legacy serialized field; migrated to MaterialAssignments in PostLoad. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PanelMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Panel Appearance")
	FLinearColor PanelColor = FLinearColor(0.75f, 0.015f, 0.32f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Panel Appearance", meta = (ClampMin = "0.0"))
	float PanelEmissiveIntensity = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Primary Frame")
	bool bShowPrimaryFrame = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Primary Frame", meta = (EditCondition = "bShowPrimaryFrame", ClampMin = "0.1"))
	float PrimaryFrameScale = 0.88f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Primary Frame", meta = (EditCondition = "bShowPrimaryFrame", ClampMin = "0.5", Units = "cm"))
	float PrimaryFrameTubeRadius = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Primary Frame", meta = (EditCondition = "bShowPrimaryFrame", Units = "cm"))
	float PrimaryFrameSurfaceOffset = 4.0f;

	/** Legacy serialized field; migrated to MaterialAssignments in PostLoad. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> PrimaryFrameMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Primary Frame", meta = (EditCondition = "bShowPrimaryFrame"))
	FLinearColor PrimaryFrameColor = FLinearColor(0.0f, 0.9f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Accent Frame")
	bool bShowAccentFrame = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Accent Frame", meta = (EditCondition = "bShowAccentFrame", ClampMin = "0.1"))
	float AccentFrameScale = 1.14f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Accent Frame", meta = (EditCondition = "bShowAccentFrame", ClampMin = "0.5", Units = "cm"))
	float AccentFrameTubeRadius = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Accent Frame", meta = (EditCondition = "bShowAccentFrame", Units = "deg"))
	float AccentFrameRotationDegrees = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Accent Frame", meta = (EditCondition = "bShowAccentFrame", Units = "cm"))
	FVector2D AccentFrameOffset = FVector2D(-20.0, 15.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Accent Frame", meta = (EditCondition = "bShowAccentFrame", Units = "cm"))
	float AccentFrameSurfaceOffset = -6.0f;

	/** Legacy serialized field; migrated to MaterialAssignments in PostLoad. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> AccentFrameMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Accent Frame", meta = (EditCondition = "bShowAccentFrame"))
	FLinearColor AccentFrameColor = FLinearColor(1.0f, 0.02f, 0.65f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Frames", meta = (ClampMin = "6", ClampMax = "32"))
	int32 FrameRadialSegments = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Frames", meta = (ClampMin = "0.0"))
	float FrameEmissiveIntensity = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle Stripes", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle", EditConditionHides))
	bool bShowHorizontalStripes = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle Stripes", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle && bShowHorizontalStripes", EditConditionHides, ClampMin = "1", ClampMax = "20"))
	int32 StripeCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle Stripes", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle && bShowHorizontalStripes", EditConditionHides, ClampMin = "1.0", Units = "cm"))
	float StripeSpacing = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle Stripes", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle && bShowHorizontalStripes", EditConditionHides, ClampMin = "0.5", Units = "cm"))
	float StripeTubeRadius = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle Stripes", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle && bShowHorizontalStripes", EditConditionHides, ClampMin = "0.05", ClampMax = "1.0"))
	float StripeWidthScale = 0.82f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Original Neon Triangle Stripes", meta = (EditCondition = "SignType == ERetroSignType::NeonTriangle && bShowHorizontalStripes", EditConditionHides, Units = "cm"))
	float StripeVerticalOffset = 0.0f;

	/** Legacy serialized field; migrated to MaterialAssignments in PostLoad. */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> StripeMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Decoration Appearance", meta = (DisplayName = "Decoration Color"))
	FLinearColor StripeColor = FLinearColor(1.0f, 0.65f, 0.02f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Decoration Appearance", meta = (DisplayName = "Decoration Emissive Intensity", ClampMin = "0.0"))
	float StripeEmissiveIntensity = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Main Text")
	FRetroTriangleSignTextSettings MainText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Subtitle Text")
	FRetroTriangleSignTextSettings SubtitleText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Retro Sign|Collision")
	bool bEnablePanelCollision = false;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PanelMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PrimaryFrameMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> AccentFrameMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StripeMaterialInstance;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> TextMaterialInstances;

	void BuildPanel(const TArray<FVector2D>& Vertices);
	void BuildTubeFrame(
		UDynamicMeshComponent* Component,
		const TArray<FVector2D>& SourceVertices,
		float Scale,
		float Radius,
		float RotationDegrees,
		FVector2D Offset,
		float SurfaceOffset);
	void BuildDecorations(const TArray<FVector2D>& Vertices);
	void BuildOriginalStripes(UDynamicMesh* DynamicMesh, const TArray<FVector2D>& Vertices);
	void BuildSunsetStripes(UDynamicMesh* DynamicMesh);
	void BuildArcadeBulbs(UDynamicMesh* DynamicMesh, const TArray<FVector2D>& Vertices);
	void BuildMemphisPattern(UDynamicMesh* DynamicMesh);
	void BuildDiamondSpeedLines(UDynamicMesh* DynamicMesh);
	void BuildRoadsideArrowBulbs(UDynamicMesh* DynamicMesh, const TArray<FVector2D>& Vertices);
	void BuildLightboxMedallions(UDynamicMesh* DynamicMesh);
	void BuildShieldUnderlines(UDynamicMesh* DynamicMesh);
	void AppendPerimeterBulbs(
		UDynamicMesh* DynamicMesh,
		const TArray<FVector2D>& Vertices,
		float Spacing,
		float Radius,
		float InsetScale,
		float SurfaceOffset) const;
	void AppendTubeSegment(
		UDynamicMesh* DynamicMesh,
		const FVector2D& Start,
		const FVector2D& End,
		float Depth,
		float Radius) const;
	void ConfigureTextLayer(
		UText3DComponent* TextComponent,
		UText3DComponent* ShadowComponent,
		const FRetroTriangleSignTextSettings& Settings,
		UMaterialInterface* FrontMaterial,
		UMaterialInterface* SideMaterial,
		UMaterialInterface* ShadowMaterial);
	UMaterialInstanceDynamic* CreateTintedMaterial(
		UMaterialInterface* RequestedParent,
		const FLinearColor& Color,
		float Intensity);
	static FVector2D RotatePoint2D(const FVector2D& Point, float Degrees);
};
