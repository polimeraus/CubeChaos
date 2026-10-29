#pragma once

#include "CoreMinimal.h"
#include "PlatformPadPathPattern.generated.h"

class UMaterialInterface;

UENUM(BlueprintType)
enum class EPlatformPadPaint : uint8 { Disc, Ring };

/** A named circle or path anchor, in normalized platform coordinates. */
USTRUCT(BlueprintType)
struct FPlatformPatternPad
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle") FName Id = NAME_None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle") bool bEnabled = true;
    /** Hidden circles remain usable as path endpoints. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle") bool bShowCircle = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(ClampMin="-1", ClampMax="1"))
    FVector2D Position = FVector2D::ZeroVector;
    /** Fraction of the shorter platform half extent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(ClampMin="0.001", ClampMax="0.95", EditCondition="bShowCircle"))
    float Radius = .22f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(EditCondition="bShowCircle"))
    EPlatformPadPaint Paint = EPlatformPadPaint::Disc;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circle", meta=(ClampMin="0.01", ClampMax="1", EditCondition="bShowCircle && Paint == EPlatformPadPaint::Ring"))
    float RingWidth = .14f;
};

USTRUCT(BlueprintType)
struct FPlatformPatternPath
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path") FName FromPad;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path") FName ToPad;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path") bool bEnabled = true;
    /** Intermediate points in the same -1..1 coordinates as the circles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path") TArray<FVector2D> Waypoints;
    /** Fraction of the shorter full platform dimension. Joins and ends are rounded. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path", meta=(ClampMin="0.001", ClampMax="0.5"))
    float Width = .055f;
    /** Extend the final segment toward the boundary when To Pad has Show Circle disabled. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path") bool bExtendToEdge = true;
    /** Minimum clearance of the rounded endpoint from the platform boundary. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path", meta=(ClampMin="0", Units="cm", EditCondition="bExtendToEdge"))
    float EdgeInset = 65.f;
};

/** Surface painting only; no spawning or level actor references. */
USTRUCT(BlueprintType)
struct FPlatformPadPathPattern
{
    GENERATED_BODY()
    FPlatformPadPathPattern();
    /** First 16 entries; every ID must be unique, including disabled entries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths", meta=(TitleProperty="Id"))
    TArray<FPlatformPatternPad> Pads;
    /** Up to 32 line segments, including waypoint segments. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths", meta=(TitleProperty="FromPad"))
    TArray<FPlatformPatternPath> Paths;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths") bool bFitPadsInsidePlatform = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths") bool bShowLayout = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths", meta=(HideAlphaChannel))
    FLinearColor BackgroundColor = FLinearColor(.105f, .003f, .46f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths", meta=(HideAlphaChannel))
    FLinearColor PatternColor = FLinearColor(1.f, .87f, .006f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths", meta=(ClampMin="0", ClampMax="20"))
    float BackgroundEmission = .18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths", meta=(ClampMin="0", ClampMax="20"))
    float PatternEmission = .55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths", meta=(ClampMin="0.02", ClampMax="1"))
    float Roughness = .19f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths", meta=(ClampMin="0", ClampMax="1"))
    float Metallic = .32f;
    /** Analytic material supplied with this pattern. Other materials must expose the same pad/path parameters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Circles & Paths")
    TObjectPtr<UMaterialInterface> SurfaceMaterial;
};
