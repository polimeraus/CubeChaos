#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RetroTwilightEnvironment.generated.h"

class UDirectionalLightComponent;
class USkyLightComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

UENUM(BlueprintType)
enum class ERetroMoonPattern : uint8
{
    // Saved selections use names; retain the photographic and retro material IDs.
    Smooth = 0 UMETA(DisplayName="Smooth"),
    RetroBands = 4 UMETA(DisplayName="Retro Bands (Soft Gradient)"),
    Realistic = 5 UMETA(DisplayName="Realistic (NASA Photo Texture)")
};

UENUM(BlueprintType)
enum class ERetroMoonColor : uint8
{
    White UMETA(DisplayName="White"),
    PaleYellow UMETA(DisplayName="Pale Yellow"),
    PaleOrange UMETA(DisplayName="Pale Orange"),
    Custom UMETA(DisplayName="Custom")
};

/** One lightweight, editable twilight rig. Color and power affect all ambient lighting. */
UCLASS(Blueprintable, meta=(DisplayName="Retro Twilight Environment"))
class MYPROJECT6_API ARetroTwilightEnvironment : public AActor
{
    GENERATED_BODY()
public:
    ARetroTwilightEnvironment();
    virtual void PostLoad() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
    virtual void BeginPlay() override;

    /** Single dominant hue for sky, haze, ambient fill and moonlight. Linear RGB. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Main", meta=(HideAlphaChannel))
    FLinearColor DominantColor = FLinearColor(0.32f, 0.22f, 0.65f);

    /** Master multiplier for moon and ambient illumination. Does not alter neon materials. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Main", meta=(ClampMin="0", UIMax="4"))
    float LightIntensity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Main", meta=(ClampMin="0", UIMax="3"))
    float SkyBrightness = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Lighting", meta=(ClampMin="0", UIMax="3"))
    float AmbientIntensity = 0.45f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Lighting", meta=(ClampMin="0", UIMax="5"))
    float MoonLightIntensity = 1.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon")
    bool bShowMoon = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon", meta=(ClampMin="1", ClampMax="85", Units="deg"))
    float MoonElevation = 24.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon", meta=(UIMin="-180", UIMax="180", Units="deg"))
    float MoonAzimuth = 145.0f;

    /** Apparent angular diameter. 4 = original size, 10 = large stylized moon. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon", meta=(ClampMin="0.1", ClampMax="30", Units="deg"))
    float MoonDiameter = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon")
    ERetroMoonPattern MoonPattern = ERetroMoonPattern::Realistic;

    /** NASA LROC photographic surface map; equirectangular, centered on the near side. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Realistic", meta=(EditCondition="MoonPattern == ERetroMoonPattern::Realistic"))
    TObjectPtr<UTexture2D> MoonTexture;

    /** Emissive multiplier for the photographic moon. 1.35 gives a restrained glow without losing craters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Realistic", meta=(ClampMin="0", UIMax="5", EditCondition="MoonPattern == ERetroMoonPattern::Realistic"))
    float MoonEmissiveIntensity = 1.35f;

    /** Rotation of the photographic disc, independent of the crescent's orientation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Realistic", meta=(UIMin="-180", UIMax="180", Units="deg", EditCondition="MoonPattern == ERetroMoonPattern::Realistic"))
    float MoonTextureRotation = 0.0f;

    /** Warm cream at the upper edge, fading smoothly into the lower pink. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Retro", meta=(HideAlphaChannel, EditCondition="MoonPattern == ERetroMoonPattern::RetroBands"))
    FLinearColor MoonRetroTopColor = FLinearColor(1.0f, 0.97f, 0.64f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Retro", meta=(HideAlphaChannel, EditCondition="MoonPattern == ERetroMoonPattern::RetroBands"))
    FLinearColor MoonRetroBottomColor = FLinearColor(1.0f, 0.16f, 0.58f);

    /** Scales the progressively wider transparent horizontal cuts in the lower disc. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Retro", meta=(ClampMin="0", ClampMax="2", EditCondition="MoonPattern == ERetroMoonPattern::RetroBands"))
    float MoonRetroBandGap = 1.0f;

    /** 0 = full, 50 = half, 75 = crescent, 90 = thin crescent, 100 = new moon.
     * Visual phase only; Moon Light Intensity remains independent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon", meta=(ClampMin="0", ClampMax="100", Units="Percent"))
    float MoonCrescentAmount = 0.0f;

    /** Rotates the crescent: 0 = right-lit, 180 = left-lit, 90/270 = horizontal horns. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon", meta=(ClampMin="0", ClampMax="360", Units="deg"))
    float MoonPhaseRotation = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon")
    ERetroMoonColor MoonColorPreset = ERetroMoonColor::PaleYellow;

    /** Disc color in linear RGB when Color Preset is Custom. Does not recolor the environment. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon", meta=(HideAlphaChannel, EditCondition="MoonColorPreset == ERetroMoonColor::Custom"))
    FLinearColor MoonCustomColor = FLinearColor(1.0f, 0.88f, 0.60f);

    /** Visible disc brightness, independent of directional Moon Light Intensity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon", meta=(ClampMin="0", UIMax="3"))
    float MoonBrightness = 1.0f;

    /** Surface contrast for the photographic and smooth moons. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Surface", meta=(ClampMin="0", UIMax="2"))
    float MoonSurfaceContrast = 1.15f;

    /** Faint earthshine on the unlit side; 0 makes it disappear into the sky. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Surface", meta=(ClampMin="0", ClampMax="0.2"))
    float MoonEarthshine = 0.025f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Glow", meta=(HideAlphaChannel))
    FLinearColor MoonGlowColor = FLinearColor(0.36f, 0.48f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Glow", meta=(ClampMin="0", UIMax="3"))
    float MoonGlowIntensity = 0.65f;

    /** Halo reach in moon radii. Independent of global scene bloom. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Glow", meta=(ClampMin="0.1", ClampMax="8"))
    float MoonGlowWidth = 2.4f;

    /** Higher values make the inner-to-outer glow transition more gradual. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon Glow", meta=(ClampMin="0.25", ClampMax="4"))
    float MoonGlowSoftness = 1.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora")
    bool bShowAurora = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(HideAlphaChannel, EditCondition="bShowAurora"))
    FLinearColor AuroraLowerColor = FLinearColor(0.07f, 1.0f, 0.49f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(HideAlphaChannel, EditCondition="bShowAurora"))
    FLinearColor AuroraUpperColor = FLinearColor(0.22f, 0.38f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(HideAlphaChannel, EditCondition="bShowAurora"))
    FLinearColor AuroraAccentColor = FLinearColor(0.75f, 0.14f, 0.82f);

    /** Emissive sky brightness. Does not change ambient or directional illumination. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(ClampMin="0", UIMax="5", EditCondition="bShowAurora"))
    float AuroraIntensity = 0.85f;

    /** Amount of luminous cloud within each curtain, independent of brightness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(ClampMin="0", ClampMax="2", EditCondition="bShowAurora"))
    float AuroraDensity = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(ClampMin="1", ClampMax="40", Units="deg", EditCondition="bShowAurora"))
    float AuroraWidth = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(ClampMin="5", ClampMax="75", Units="deg", EditCondition="bShowAurora"))
    float AuroraElevation = 34.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(UIMin="-180", UIMax="180", Units="deg", EditCondition="bShowAurora"))
    float AuroraAzimuth = 145.0f;

    /** Horizontal extent; 360 wraps around the whole sky. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora", meta=(ClampMin="20", ClampMax="360", Units="deg", EditCondition="bShowAurora"))
    float AuroraCoverage = 220.0f;

    /** Signed animation speed. Zero freezes the aurora, negative reverses it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora Motion", meta=(UIMin="-1", UIMax="1", EditCondition="bShowAurora"))
    float AuroraFlowSpeed = 0.12f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora Motion", meta=(ClampMin="0", ClampMax="2", EditCondition="bShowAurora"))
    float AuroraTurbulence = 0.65f;

    /** Scale of curtain folds and cloud details. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora Motion", meta=(ClampMin="0.25", ClampMax="4", EditCondition="bShowAurora"))
    float AuroraDetailScale = 1.0f;

    /** Deterministic variation; also chooses a frozen pose when Flow Speed is zero. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Aurora Motion", meta=(ClampMin="0", ClampMax="1000", EditCondition="bShowAurora"))
    float AuroraSeed = 7.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Sky", meta=(ClampMin="0", UIMax="2"))
    float StarBrightness = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Fog", meta=(ClampMin="0", ClampMax="0.05", UIMax="0.02"))
    float FogDensity = 0.003f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Fog", meta=(ClampMin="0", Units="cm"))
    float FogStartDistance = 1800.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Fog", meta=(Units="cm"))
    float FogHeight = -2000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Neon", meta=(ClampMin="0", UIMax="3"))
    float BloomIntensity = 0.65f;

    /** Fixed exposure prevents looking at a bright neon from darkening the whole scene. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Neon", meta=(ClampMin="-5", ClampMax="5"))
    float ExposureCompensation = 0.0f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Twilight|Sky")
    TObjectPtr<UMaterialInterface> SkyMaterial;

    /** Editor changes apply immediately; call after runtime Blueprint property edits. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Twilight")
    void ApplyEnvironment();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Twilight|Components")
    TObjectPtr<UDirectionalLightComponent> MoonLight;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Twilight|Components")
    TObjectPtr<USkyLightComponent> AmbientLight;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Twilight|Components")
    TObjectPtr<UExponentialHeightFogComponent> Haze;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Twilight|Components")
    TObjectPtr<UPostProcessComponent> NeonPostProcess;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Twilight|Components")
    TObjectPtr<UStaticMeshComponent> SkyDome;

private:
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> SkyMID;
};
