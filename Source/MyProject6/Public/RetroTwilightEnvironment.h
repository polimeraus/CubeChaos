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

/** One lightweight, editable twilight rig. Color and power affect all ambient lighting. */
UCLASS(Blueprintable, meta=(DisplayName="Retro Twilight Environment"))
class MYPROJECT6_API ARetroTwilightEnvironment : public AActor
{
    GENERATED_BODY()
public:
    ARetroTwilightEnvironment();
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

    /** Stylized angular diameter; controls the sky disc and soft shadow source together. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Twilight|Moon", meta=(ClampMin="0.1", ClampMax="15", Units="deg"))
    float MoonDiameter = 4.0f;

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
