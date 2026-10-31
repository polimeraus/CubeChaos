// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleDecorativeFloor.generated.h"

/** Selectable silhouette used by the unified destructible floor. */
UENUM(BlueprintType)
enum class EChunkedDecorativeFloorShape : uint8
{
	Grid UMETA(DisplayName = "Rectangle"),
	Triangle,
	FivePointStar UMETA(Hidden),
	SixPointStar UMETA(Hidden),
	RoundedBlob UMETA(DisplayName = "Rounded Blob"),
	Square,
	Circle,
	Star,
	Hexagon,
	LShape UMETA(DisplayName = "L / J"),
	TShape UMETA(DisplayName = "T"),
	Zigzag UMETA(DisplayName = "S / Z"),
	UShape UMETA(DisplayName = "U / C"),
	Cross UMETA(DisplayName = "Cross / Plus"),
	CustomBlocks UMETA(DisplayName = "Custom Blocks"),
	// Append only: existing serialized enum values must stay stable.
	DiagonalLink UMETA(DisplayName = "Diagonal Link"),
	OffsetLink UMETA(DisplayName = "Offset Link"),
	OffsetFork UMETA(DisplayName = "Offset Fork"),
	OffsetArch UMETA(DisplayName = "Offset Arch"),
	SteppedArch UMETA(DisplayName = "Stepped Arch"),
	SteppedBlock UMETA(DisplayName = "Stepped Block"),
	PiercedSteppedBlock UMETA(DisplayName = "Stepped Block with Hole"),
	NotchedBar UMETA(DisplayName = "Notched Bar"),
	FingerCorner UMETA(DisplayName = "Finger Corner"),
	ForkNotch UMETA(DisplayName = "Fork with Round Notch"),
	PiercedTrapezoid UMETA(DisplayName = "Trapezoid with Hole"),
	NotchedTrapezoid UMETA(DisplayName = "Notched Trapezoid"),
	PiercedCapsule UMETA(DisplayName = "Capsule with Hole"),
	RoundU UMETA(DisplayName = "Round Horseshoe"),
	CombArch UMETA(DisplayName = "Comb Arch"),
	InterlockTab UMETA(DisplayName = "Interlock Tabs with Hole"),
	InterlockSocket UMETA(DisplayName = "Interlock Sockets with Hole"),
	WaistedPier UMETA(DisplayName = "Waisted Pier"),
	Ring,
	HalfRing UMETA(DisplayName = "Half Ring / Arch"),
	HalfDisc UMETA(DisplayName = "Half Disc"),
	VNotchHalfDisc UMETA(DisplayName = "Half Disc with V Notch"),
	PiercedDiamond UMETA(DisplayName = "Diamond with Hole"),
	NotchedWedge UMETA(DisplayName = "Notched Wedge"),
	UnequalU UMETA(DisplayName = "Unequal Arm U"),
	SteppedBranch UMETA(DisplayName = "Stepped Branch"),
	ParametricShape UMETA(DisplayName = "Parametric Shape")
};

/** Material-layer and geometry controls shown only while Heart Motif is selected. */
USTRUCT(BlueprintType)
struct FChunkedHeartMotifSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0"))
	int32 BackgroundMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0"))
	int32 AccentMaterialIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0"))
	int32 InnerMaterialIndex = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D Size = FVector2D(2200.0, 1750.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0.05", ClampMax = "0.9"))
	float InnerScale = 0.32f;

	/** Keeps the motif bounds centered inside the selected outer shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Auto Center"))
	bool bAutoCenter = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (EditCondition = "!bAutoCenter", Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "32", ClampMax = "256"))
	int32 CurveResolution = 192;
};

/** Material-layer and geometry controls shown only while Star Motif is selected. */
USTRUCT(BlueprintType)
struct FChunkedStarMotifSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0"))
	int32 BackgroundMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0"))
	int32 AccentMaterialIndex = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0"))
	int32 InnerMaterialIndex = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D Size = FVector2D(2200.0, 2200.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "3", ClampMax = "32"))
	int32 PointCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0.1", ClampMax = "0.9"))
	float InnerRadius = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0.05", ClampMax = "0.9"))
	float InnerScale = 0.35f;

	/** Keeps the motif bounds centered inside the selected outer shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Auto Center"))
	bool bAutoCenter = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (EditCondition = "!bAutoCenter", Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;
};

/** Shape family used by the Flower floor motif. */
UENUM(BlueprintType)
enum class EChunkedFlowerType : uint8
{
	Daisy UMETA(DisplayName = "Small Daisy"),
	LargeRound UMETA(DisplayName = "Large Simple (Round Center)"),
	LargeHeart UMETA(DisplayName = "Large Simple (Heart Center)")
};

/** Single, hand-drawn flower controls shown only while Flower is selected. */
USTRUCT(BlueprintType)
struct FChunkedFlowerMotifSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif")
	EChunkedFlowerType FlowerType = EChunkedFlowerType::Daisy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0"))
	int32 BackgroundMaterialIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "0"))
	int32 OutlineMaterialIndex = 2;

	/** Shared fill material for the center and the inside of every petal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Petal + Center Material Index", ClampMin = "0"))
	int32 CenterMaterialIndex = 1;

	/** Exact base width and height of the centered flower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "50.0", Units = "cm"))
	FVector2D FlowerSize = FVector2D(2200.0, 2200.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Daisy Petal Count", EditCondition = "FlowerType == EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "3", ClampMax = "24"))
	int32 PetalCount = 8;

	/** Half-length of a petal relative to the flower radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Daisy Petal Length", EditCondition = "FlowerType == EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "0.15", ClampMax = "0.75"))
	float PetalLengthRatio = 0.42f;

	/** Half-width of a petal relative to the flower radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Daisy Petal Width", EditCondition = "FlowerType == EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "0.08", ClampMax = "0.65"))
	float PetalWidthRatio = 0.27f;

	/** Center-disc radius relative to the flower radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Daisy Center Size", EditCondition = "FlowerType == EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "0.05", ClampMax = "0.55"))
	float CenterRadiusRatio = 0.22f;

	/** Makes both simple variants substantially larger than the daisy at the same base size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Large Type Scale", EditCondition = "FlowerType != EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "1.0", ClampMax = "4.0"))
	float LargeTypeScale = 1.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Simple Petal Count", EditCondition = "FlowerType != EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "3", ClampMax = "12"))
	int32 SimplePetalCount = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Simple Petal Length", EditCondition = "FlowerType != EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "0.3", ClampMax = "0.8"))
	float SimplePetalLengthRatio = 0.56f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Simple Petal Width", EditCondition = "FlowerType != EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "0.2", ClampMax = "0.8"))
	float SimplePetalWidthRatio = 0.50f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Simple Center Size", EditCondition = "FlowerType != EChunkedFlowerType::Daisy", EditConditionHides, ClampMin = "0.15", ClampMax = "0.65"))
	float SimpleCenterRadiusRatio = 0.38f;

	/** Thickness of the visible outline around petals and the center shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Line Thickness", ClampMin = "0.0", Units = "cm"))
	float OutlineWidth = 28.0f;

	/** Keeps the flower centered inside the selected outer shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (DisplayName = "Auto Center"))
	bool bAutoCenter = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (EditCondition = "!bAutoCenter", Units = "cm"))
	FVector2D Offset = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (Units = "deg"))
	float RotationDegrees = 0.0f;

	/** Curve samples used by every petal and center disc. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Motif", meta = (ClampMin = "8", ClampMax = "64"))
	int32 CurveResolution = 24;
};

/**
 * A selectable, chunked floor silhouette with optional borders and smooth overlay motifs.
 *
 * This remains as a compatibility class for existing assets. New floors should use AChunkedFloor,
 * which exposes this implementation under the shorter unified actor name. Every visible tile is
 * still part of the inherited destructible grid, so damage, explosions, ripple, flash, sound and
 * chunk-local rebuilding all share the same implementation.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API AChunkedDestructibleDecorativeFloor : public AChunkedDestructibleFloor
{
	GENERATED_BODY()

public:
	AChunkedDestructibleDecorativeFloor();
	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Changes the outer silhouette and rebuilds the floor. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	void SetDecorativeFloorShape(
		EChunkedDecorativeFloorShape NewShape,
		FVector2D NewShapeSize,
		bool bPreserveCellState = true);

	/** Configures the silhouette, cell dimensions and spacing in one rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	virtual void ConfigureDecorativeFloor(
		EChunkedDecorativeFloorShape NewShape,
		FVector2D NewShapeSize,
		FVector NewCellDimensions,
		FVector2D NewCellSpacing,
		bool bPreserveCellState = true);

	/** Selects the parametric star silhouette and rebuilds the floor. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Shape")
	void ConfigureStarShape(
		int32 NewPointCount,
		float NewInnerRadius,
		float NewRotationDegrees,
		bool bPreserveCellState = true);

	/** Selects the organic silhouette and applies its deterministic outline parameters. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Shape")
	void ConfigureRoundedBlobShape(
		int32 NewLobeCount,
		float NewLobeStrength,
		float NewEdgeRoundness,
		int32 NewShapeSeed,
		bool bPreserveCellState = true);

	/** Configures the two nested hearts. InnerHeartScale is relative to the large heart. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	void ConfigureHeartMotif(
		bool bNewHeartMotifEnabled,
		FVector2D NewHeartSize,
		float NewInnerHeartScale,
		FVector2D NewHeartOffset,
		float NewHeartRotationDegrees);

	/** Selects and configures the two-layer parametric star motif. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	void ConfigureStarMotif(
		FVector2D NewStarSize,
		int32 NewPointCount,
		float NewInnerRadius,
		float NewInnerStarScale,
		FVector2D NewStarOffset,
		float NewStarRotationDegrees);

	/** Selects and configures the single parametric flower motif. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Materials")
	void ConfigureFlowerMotif(
		EChunkedFlowerType NewFlowerType,
		FVector2D NewFlowerSize,
		int32 NewPetalCount,
		FVector2D NewOffset,
		float NewRotationDegrees);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Shape")
	EChunkedDecorativeFloorShape OuterShape = EChunkedDecorativeFloorShape::Grid;

	/** Exact bounding size. Square and Circle use X as their side length / diameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Shape", meta = (ClampMin = "1.0", Units = "cm"))
	FVector2D ShapeSize = FVector2D(6000.0, 4500.0);

	/** Horizontal triangle apex: -1 is left, 0 centered and +1 right. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Shape", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::Triangle", EditConditionHides, ClampMin = "-1.0", ClampMax = "1.0"))
	float TriangleApexOffset = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Shape", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::Star", EditConditionHides, ClampMin = "3", ClampMax = "32"))
	int32 StarPointCount = 5;

	/** Radius of the inward star corners relative to the outside radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Shape", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::Star || OuterShape == EChunkedDecorativeFloorShape::FivePointStar || OuterShape == EChunkedDecorativeFloorShape::SixPointStar", EditConditionHides, ClampMin = "0.1", ClampMax = "0.9"))
	float StarInnerRadius = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Shape", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::Star || OuterShape == EChunkedDecorativeFloorShape::FivePointStar || OuterShape == EChunkedDecorativeFloorShape::SixPointStar", EditConditionHides, Units = "deg"))
	float StarRotationDegrees = 0.0f;

	/** More segments produce a smoother circular edge while retaining exact straight segment cuts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Shape", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::Circle", EditConditionHides, ClampMin = "16", ClampMax = "512"))
	int32 CircleEdgeSegments = 96;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Rounded Blob", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides, ClampMin = "3", ClampMax = "16"))
	int32 BlobLobeCount = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Rounded Blob", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides, ClampMin = "0.0", ClampMax = "0.45"))
	float BlobLobeStrength = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Rounded Blob", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides, ClampMin = "0.0", ClampMax = "1.0"))
	float BlobEdgeRoundness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Rounded Blob", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides))
	int32 BlobShapeSeed = 1337;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Rounded Blob", meta = (EditCondition = "OuterShape == EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides, ClampMin = "4", ClampMax = "32"))
	int32 BlobOutlineSamplesPerLobe = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Bright Border")
	bool bUseBrightBorder = true;

	/** The inner edge is an inset copy of the selected outer shape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Bright Border", meta = (EditCondition = "bUseBrightBorder", ClampMin = "0.0", Units = "cm"))
	float BorderWidth = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Bright Border", meta = (EditCondition = "bUseBrightBorder", ClampMin = "0"))
	int32 BorderMaterialIndex = 1;

	/** Deprecated compatibility state. Heart Motif is now selected exclusively through FloorPattern. */
	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Select Heart Motif in Floor Pattern instead."))
	bool bUseHeartMotif = false;

	/** Native expandable group, ordered directly below Floor Pattern without an editor customization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DisplayName = "Heart", EditCondition = "FloorPattern == EChunkedFloorPattern::HeartMotif", EditConditionHides, DisplayAfter = "FloorPattern", DisplayPriority = "0", FullyExpand = "true"))
	FChunkedHeartMotifSettings HeartMotifSettings;

	/** Native expandable group, ordered directly below Floor Pattern without an editor customization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DisplayName = "Star", EditCondition = "FloorPattern == EChunkedFloorPattern::StarMotif", EditConditionHides, DisplayAfter = "FloorPattern", DisplayPriority = "0", FullyExpand = "true"))
	FChunkedStarMotifSettings StarMotifSettings;

	/** Native expandable group, ordered directly below Floor Pattern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DisplayName = "Flower", EditCondition = "FloorPattern == EChunkedFloorPattern::FlowerMotif", EditConditionHides, DisplayAfter = "FloorPattern", DisplayPriority = "0", FullyExpand = "true"))
	FChunkedFlowerMotifSettings FlowerMotifSettings;

	/** Legacy serialized fields retained for automatic asset migration and Blueprint compatibility. */
	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Heart Motif settings."))
	FVector2D HeartSize = FVector2D(2200.0, 1750.0);

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Heart Motif settings."))
	float InnerHeartScale = 0.32f;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Heart Motif settings."))
	FVector2D HeartOffset = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Heart Motif settings."))
	float HeartRotationDegrees = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Heart Motif settings."))
	int32 HeartCurveResolution = 192;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Heart Motif settings."))
	int32 HeartAccentMaterialIndex = 1;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Heart Motif settings."))
	int32 InnerHeartMaterialIndex = 2;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Star Motif settings."))
	FVector2D MotifStarSize = FVector2D(2200.0, 2200.0);

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Star Motif settings."))
	int32 MotifStarPointCount = 5;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Star Motif settings."))
	float MotifStarInnerRadius = 0.45f;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Star Motif settings."))
	float InnerStarScale = 0.35f;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Star Motif settings."))
	FVector2D MotifStarOffset = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Star Motif settings."))
	float MotifStarRotationDegrees = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Star Motif settings."))
	int32 StarAccentMaterialIndex = 1;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use Star Motif settings."))
	int32 InnerStarMaterialIndex = 2;

	UPROPERTY(BlueprintReadWrite, Category = "Chunked Floor|Materials", meta = (DeprecatedProperty, DeprecationMessage = "Use the selected motif's Background Material Index."))
	int32 BackgroundMaterialIndex = 0;

	/** False only on assets saved before motif settings became native structs. */
	UPROPERTY()
	bool bMotifSettingsMigrated = false;

	virtual bool CalculateLayout() override;
	virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
	virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
	virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& OutMesh) const override;
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const override;
	virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;

protected:
	struct FShapeTriangle
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		FVector2D C = FVector2D::ZeroVector;
		FBox2D Bounds = FBox2D(EForceInit::ForceInit);
	};

	struct FShapeOutlineSegment
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
	};

	struct FDecorativeCellGeometryCache
	{
		bool bBelongsToShape = false;
		bool bFullyInside = false;
		TArray<TArray<FVector2D>> Fragments;
		TArray<FShapeOutlineSegment> OutlineSegments;
	};

	struct FPatternSourceTriangle
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		FVector2D C = FVector2D::ZeroVector;
		FVector2D Min = FVector2D::ZeroVector;
		FVector2D Max = FVector2D::ZeroVector;
	};

	struct FCachedPatternTriangle
	{
		FVector2D A = FVector2D::ZeroVector;
		FVector2D B = FVector2D::ZeroVector;
		FVector2D C = FVector2D::ZeroVector;
	};

	struct FPatternSurfaceCache
	{
		TArray<FVector2D> Outline;
		int32 MaterialIndex = 0;
		double SurfaceZ = 0.0;
		FVector2D BoundsMin = FVector2D::ZeroVector;
		FVector2D BoundsMax = FVector2D::ZeroVector;
		TArray<FPatternSourceTriangle> SourceTriangles;
		TArray<TArray<FCachedPatternTriangle>> CellTriangles;
		TBitArray<> CellBuilt;
	};

	TArray<float> CachedBlobRadii;
	TArray<FVector2D> CachedShapeOutline;
	// Clockwise interior loops; shared by clipping, collision and boundary walls.
	TArray<TArray<FVector2D>> CachedShapeHoles;
	void ForEachShapeBoundary(TFunctionRef<void(const TArray<FVector2D>&)> Function) const;
	TArray<FShapeTriangle> CachedShapeTriangles;
	FVector2D CachedShapeFanCenter = FVector2D::ZeroVector;
	TArray<FDecorativeCellGeometryCache> CellGeometryCache;
	mutable TArray<FPatternSurfaceCache> PatternSurfaceCaches;

	virtual void RebuildBlobCache();
	virtual void RebuildShapeOutline();
	void CenterMotifsInShape();
	virtual bool IsPointInsideOuterShape(const FVector2D& LocalPoint, float Inset = 0.0f) const;
	bool IsPointInsideNormalizedShape(const FVector2D& Point) const;
	bool IsPointInsidePolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon) const;
	bool IsPointInsideGeneratedShape(const FVector2D& Point, double Tolerance = 0.01) const;
	bool DoesShapeOutlineIntersectCell(FIntPoint Cell) const;
	bool ComputeCellFullyInsideGeneratedShape(FIntPoint Cell) const;
	bool IsCellFullyInsideGeneratedShape(FIntPoint Cell) const;
	bool ComputeClippedCellFragments(FIntPoint Cell, TArray<TArray<FVector2D>>& OutFragments) const;
	bool GetClippedCellFragments(FIntPoint Cell, TArray<TArray<FVector2D>>& OutFragments) const;
	void RebuildCellGeometryCache();
	FPatternSurfaceCache& FindOrAddPatternSurfaceCache(
		const TArray<FVector2D>& Outline,
		int32 MaterialIndex,
		double SurfaceZ) const;
	void GetCellBounds(FIntPoint Cell, double& OutMinX, double& OutMinY, double& OutMaxX, double& OutMaxY) const;
	virtual float SampleBlobRadius(float Angle) const;
	void AppendDecorativeSurfacePatterns(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& Mesh) const;
	int32 ClampMaterialIndex(int32 MaterialIndex) const;
	void SyncMotifSettingsFromLegacyFields();
	void SyncLegacyFieldsFromMotifSettings();
	void AppendGreedyFullHorizontalFaces(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 MinCellX,
		int32 MinCellY,
		int32 MaxCellX,
		int32 MaxCellY,
		bool bTopFace) const;
	void AppendPolygonHorizontalFace(
		UE::Geometry::FDynamicMesh3& Mesh,
		const TArray<FVector2D>& Polygon,
		bool bTopFace,
		int32 MaterialIndex) const;
	void AppendHeartSurface(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 ChunkIndex,
		float Scale,
		int32 MaterialIndex,
		double SurfaceZ) const;
	void AppendStarSurface(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 ChunkIndex,
		float Scale,
		int32 MaterialIndex,
		double SurfaceZ) const;
	void AppendFlowerPattern(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 ChunkIndex) const;
	virtual void AppendPolygonSurface(
		UE::Geometry::FDynamicMesh3& Mesh,
		int32 ChunkIndex,
		const TArray<FVector2D>& Outline,
		int32 MaterialIndex,
		double SurfaceZ) const;
};
