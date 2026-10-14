#pragma once
#include "CoreMinimal.h"
#include "RailgunVisualSettings.generated.h"

/** Appearance of a shot, also exposed in the player's Blueprint Class Defaults. */
USTRUCT(BlueprintType)
struct FRailgunVisualSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(HideAlphaChannel))
    FLinearColor Color = FLinearColor(.02f, .55f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="30", UIMax="12"))
    float Intensity = 4.f;
    /** Optical density; zero makes the trail invisible. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="5"))
    float Density = 1.f;
    /** Full width of the soft outer glow. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.5", ClampMax="50", Units="cm"))
    float Width = 15.f;

    /** Independent core/spiral colors, like Quake's color1/color2. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(HideAlphaChannel))
    FLinearColor SpiralColor = FLinearColor(.02f, .3f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun")
    bool bSpiral = true;
    /** Quake units are converted at 2.5 cm per unit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="50", Units="cm"))
    float SpiralRadius = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="50", ClampMax="1000", Units="cm"))
    float SpiralPitch = 450.f;
};
