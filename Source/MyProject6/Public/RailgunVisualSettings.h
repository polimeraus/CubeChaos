#pragma once
#include "CoreMinimal.h"
#include "RailgunVisualSettings.generated.h"

/** Appearance of a shot, also exposed in the player's Blueprint Class Defaults. */
USTRUCT(BlueprintType)
struct FRailgunVisualSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(HideAlphaChannel))
    FLinearColor Color = FLinearColor(.008f, .22f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="30", UIMax="12"))
    float Intensity = 7.f;
    /** Optical density; zero makes the trail invisible. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="5"))
    float Density = 1.f;
    /** Full width of the soft outer glow. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.5", ClampMax="50", Units="cm"))
    float Width = 18.f;

    /** Independent core/spiral colors, like Quake's color1/color2. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(HideAlphaChannel))
    FLinearColor SpiralColor = FLinearColor(.008f, .5f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun")
    bool bSpiral = true;
    /** A continuous, soft filament instead of the original separated particle dots. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun")
    bool bSmoothSpiral = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.5", ClampMax="10", Units="cm"))
    float SpiralWidth = 3.8f;
    /** Quake units are converted at 2.5 cm per unit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="50", Units="cm"))
    float SpiralRadius = 7.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="50", ClampMax="1000", Units="cm"))
    float SpiralPitch = 300.f;
};
