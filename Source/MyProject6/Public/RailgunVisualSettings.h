#pragma once
#include "CoreMinimal.h"
#include "RailgunVisualSettings.generated.h"

/** Appearance of a shot, also exposed in the player's Blueprint Class Defaults. */
USTRUCT(BlueprintType)
struct FRailgunVisualSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(HideAlphaChannel))
    FLinearColor Color = FLinearColor(.025f, .52f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="30", UIMax="20"))
    float Intensity = 7.f;
    /** Optical density; zero makes the trail invisible. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="5"))
    float Density = 1.f;
    /** Full width of the soft outer glow. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.5", ClampMax="50", Units="cm"))
    float Width = 16.f;
    /** Amount of white light concentrated on the shot axis. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="1"))
    float WhiteCore = .95f;

    /** Independent core/spiral colors, like Quake's color1/color2. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(HideAlphaChannel))
    FLinearColor SpiralColor = FLinearColor(.001f, .55f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun")
    bool bSpiral = true;
    /** A continuous, soft filament instead of the original separated particle dots. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun")
    bool bSmoothSpiral = true;
    /** Diameter of the filament or of each luminous bead. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.5", ClampMax="32", Units="cm"))
    float SpiralWidth = 6.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="50", Units="cm"))
    float SpiralRadius = 22.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="50", ClampMax="1000", Units="cm"))
    float SpiralPitch = 245.f;
};
