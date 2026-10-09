#pragma once
#include "CoreMinimal.h"
#include "RailgunVisualSettings.generated.h"

/** Appearance of a shot, also exposed in the player's Blueprint Class Defaults. */
USTRUCT(BlueprintType)
struct FRailgunVisualSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(HideAlphaChannel))
    FLinearColor Color = FLinearColor(1.f, .008f, .002f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="30", UIMax="12"))
    float Intensity = 5.f;
    /** Optical density; zero makes the trail invisible. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0", ClampMax="5"))
    float Density = 1.2f;
    /** Full width of the soft outer glow. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Railgun", meta=(ClampMin="0.5", ClampMax="50", Units="cm"))
    float Width = 9.f;
};
