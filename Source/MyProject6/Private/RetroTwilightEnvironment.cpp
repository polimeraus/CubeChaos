#include "RetroTwilightEnvironment.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
ERetroMoonPattern SupportedMoonPattern(ERetroMoonPattern Pattern)
{
    switch (Pattern)
    {
    case ERetroMoonPattern::Smooth:
    case ERetroMoonPattern::RetroBands:
    case ERetroMoonPattern::Realistic: return Pattern;
    default: return ERetroMoonPattern::Realistic;
    }
}
}

ARetroTwilightEnvironment::ARetroTwilightEnvironment()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("EnvironmentRoot"));
    RootComponent->SetMobility(EComponentMobility::Movable);
    MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
    MoonLight->SetupAttachment(RootComponent);
    MoonLight->SetMobility(EComponentMobility::Movable);
    MoonLight->bAtmosphereSunLight = false;
    MoonLight->DynamicShadowDistanceMovableLight = 18000.0f;
    MoonLight->DynamicShadowCascades = 2;
    AmbientLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("AmbientLight"));
    AmbientLight->SetupAttachment(RootComponent);
    AmbientLight->SetMobility(EComponentMobility::Movable);
    AmbientLight->SourceType = SLS_SpecifiedCubemap;
    AmbientLight->bRealTimeCapture = false;
    AmbientLight->CastShadows = false;
    AmbientLight->bLowerHemisphereIsBlack = true;
    static ConstructorHelpers::FObjectFinder<UTextureCube> AmbientCube(
        TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap"));
    if (AmbientCube.Succeeded()) AmbientLight->Cubemap = AmbientCube.Object;
    Haze = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Haze"));
    Haze->SetupAttachment(RootComponent);
    Haze->SetVolumetricFog(false);
    NeonPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("NeonPostProcess"));
    NeonPostProcess->SetupAttachment(RootComponent);
    NeonPostProcess->bUnbound = true;
    NeonPostProcess->Priority = 50.0f;
    SkyDome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SkyDome"));
    SkyDome->SetupAttachment(RootComponent);
    SkyDome->SetMobility(EComponentMobility::Movable);
    SkyDome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SkyDome->SetGenerateOverlapEvents(false);
    SkyDome->SetCanEverAffectNavigation(false);
    SkyDome->SetCastShadow(false);
    SkyDome->bReceivesDecals = false;
    SkyDome->bAffectDistanceFieldLighting = false;
    SkyDome->bVisibleInRayTracing = false;
    SkyDome->SetRelativeScale3D(FVector(40000.0));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (Sphere.Succeeded()) SkyDome->SetStaticMesh(Sphere.Object);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SkyFinder(
        TEXT("/Game/Environment/RetroTwilight/M_RetroTwilightSky.M_RetroTwilightSky"));
    if (SkyFinder.Succeeded()) SkyMaterial = SkyFinder.Object;
    static ConstructorHelpers::FObjectFinder<UTexture2D> MoonFinder(
        TEXT("/Game/Environment/RetroTwilight/T_Moon_NASA_LRO_8K.T_Moon_NASA_LRO_8K"));
    if (MoonFinder.Succeeded()) MoonTexture = MoonFinder.Object;
}

void ARetroTwilightEnvironment::PostLoad()
{
    Super::PostLoad();
    MoonPattern = SupportedMoonPattern(MoonPattern);
}

void ARetroTwilightEnvironment::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ApplyEnvironment();
}

void ARetroTwilightEnvironment::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    if (!IsTemplate()) ApplyEnvironment();
}

void ARetroTwilightEnvironment::BeginPlay()
{
    Super::BeginPlay();
    ApplyEnvironment();
}

void ARetroTwilightEnvironment::ApplyEnvironment()
{
    MoonPattern = SupportedMoonPattern(MoonPattern);
    if (IsTemplate() || !MoonLight || !SkyDome) return;
    const FLinearColor Tint = DominantColor.GetClamped(0.0f, 1.0f);
    const float Power = FMath::Max(0.0f, LightIntensity);
    FLinearColor DiscColor = FLinearColor::White;
    switch (MoonColorPreset)
    {
    case ERetroMoonColor::PaleYellow: DiscColor = FLinearColor(1.0f, 0.88f, 0.60f); break;
    case ERetroMoonColor::PaleOrange: DiscColor = FLinearColor(1.0f, 0.65f, 0.38f); break;
    case ERetroMoonColor::Custom: DiscColor = MoonCustomColor.GetClamped(0.0f, 1.0f); break;
    default: break;
    }
    const FVector MoonDirection = FRotator(FMath::Clamp(MoonElevation, 1.0f, 85.0f), MoonAzimuth, 0).Vector();
    // Light rays travel toward the scene, exactly opposite the visible disc.
    MoonLight->SetWorldRotation((-MoonDirection).Rotation());
    MoonLight->SetLightColor(FMath::Lerp(Tint, FLinearColor::White, 0.65f));
    MoonLight->SetIntensity(bShowMoon ? Power * FMath::Max(0.0f, MoonLightIntensity) : 0.0f);
    MoonLight->SetLightSourceAngle(FMath::Clamp(MoonDiameter, 0.1f, 15.0f));
    AmbientLight->SetLightColor(FMath::Lerp(Tint, FLinearColor::White, 0.28f));
    AmbientLight->SetIntensity(Power * FMath::Max(0.0f, AmbientIntensity));
    AmbientLight->SetLowerHemisphereColor(Tint * 0.015f);
    Haze->SetWorldLocation(FVector(GetActorLocation().X, GetActorLocation().Y, FogHeight));
    Haze->SetFogDensity(FMath::Clamp(FogDensity, 0.0f, 0.05f));
    Haze->SetFogHeightFalloff(0.18f);
    Haze->SetFogMaxOpacity(0.6f);
    Haze->SetStartDistance(FMath::Max(0.0f, FogStartDistance));
    Haze->SetFogInscatteringColor(Tint * 0.055f * FMath::Max(0.0f, SkyBrightness));
    auto& Post = NeonPostProcess->Settings;
    Post.bOverride_AutoExposureMethod = true;
    Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Post.AutoExposureApplyPhysicalCameraExposure = false;
    Post.bOverride_AutoExposureBias = true;
    Post.AutoExposureBias = FMath::Clamp(ExposureCompensation, -5.0f, 5.0f);
    Post.bOverride_BloomIntensity = true;
    Post.BloomIntensity = FMath::Max(0.0f, BloomIntensity);
    Post.bOverride_BloomMethod = true;
    Post.BloomMethod = BM_SOG;
    Post.bOverride_BloomGaussianIntensity = true;
    Post.BloomGaussianIntensity = 1.0f;
    Post.bOverride_BloomThreshold = true;
    // A physical, threshold-free bloom also preserves halos on the existing low-range neon colors.
    Post.BloomThreshold = -1.0f;
    Post.bOverride_VignetteIntensity = true;
    Post.VignetteIntensity = 0.18f;
    Post.bOverride_MotionBlurAmount = true;
    Post.MotionBlurAmount = 0.0f;
    Post.bOverride_LensFlareIntensity = true;
    Post.LensFlareIntensity = 0.0f;
    if (SkyMaterial)
    {
        if (!SkyMID || SkyMID->Parent != SkyMaterial)
        {
            SkyMID = UMaterialInstanceDynamic::Create(SkyMaterial, this);
            SkyMID->SetFlags(RF_Transient);
        }
        SkyDome->SetMaterial(0, SkyMID);
        SkyMID->SetVectorParameterValue(TEXT("DominantColor"), Tint);
        SkyMID->SetVectorParameterValue(TEXT("MoonDirection"), FLinearColor(MoonDirection.X, MoonDirection.Y, MoonDirection.Z));
        SkyMID->SetScalarParameterValue(TEXT("SkyBrightness"), FMath::Max(0.0f, SkyBrightness));
        SkyMID->SetScalarParameterValue(TEXT("MoonVisibility"), bShowMoon ? 1.0f : 0.0f);
        SkyMID->SetScalarParameterValue(TEXT("MoonRadius"), FMath::DegreesToRadians(FMath::Clamp(MoonDiameter, 0.1f, 30.0f) * 0.5f));
        SkyMID->SetVectorParameterValue(TEXT("MoonColor"), DiscColor);
        SkyMID->SetScalarParameterValue(TEXT("MoonPattern"), static_cast<float>(MoonPattern));
        // A missing optional texture falls back to the material's bundled NASA map.
        UTexture* PhotoTexture = MoonTexture;
        if (!PhotoTexture) SkyMaterial->GetTextureParameterValue(FMaterialParameterInfo(TEXT("MoonTexture")), PhotoTexture);
        if (PhotoTexture) SkyMID->SetTextureParameterValue(TEXT("MoonTexture"), PhotoTexture);
        SkyMID->SetScalarParameterValue(TEXT("MoonEmissiveIntensity"), FMath::Max(0.0f, MoonEmissiveIntensity));
        SkyMID->SetScalarParameterValue(TEXT("MoonTextureRotation"), FMath::DegreesToRadians(MoonTextureRotation));
        SkyMID->SetVectorParameterValue(TEXT("MoonRetroTopColor"), MoonRetroTopColor.GetClamped(0.0f, 1.0f));
        SkyMID->SetVectorParameterValue(TEXT("MoonRetroBottomColor"), MoonRetroBottomColor.GetClamped(0.0f, 1.0f));
        SkyMID->SetScalarParameterValue(TEXT("MoonRetroBandGap"), FMath::Clamp(MoonRetroBandGap, 0.0f, 2.0f));
        SkyMID->SetScalarParameterValue(TEXT("MoonPhase"), FMath::Clamp(MoonCrescentAmount / 100.0f, 0.0f, 1.0f));
        SkyMID->SetScalarParameterValue(TEXT("MoonPhaseRotation"), FMath::DegreesToRadians(MoonPhaseRotation));
        SkyMID->SetScalarParameterValue(TEXT("MoonBrightness"), FMath::Max(0.0f, MoonBrightness));
        SkyMID->SetScalarParameterValue(TEXT("MoonSurfaceContrast"), FMath::Max(0.0f, MoonSurfaceContrast));
        SkyMID->SetScalarParameterValue(TEXT("MoonEarthshine"), FMath::Clamp(MoonEarthshine, 0.0f, 0.2f));
        SkyMID->SetVectorParameterValue(TEXT("MoonGlowColor"), MoonGlowColor.GetClamped(0.0f, 1.0f));
        SkyMID->SetScalarParameterValue(TEXT("MoonGlowIntensity"), FMath::Max(0.0f, MoonGlowIntensity));
        SkyMID->SetScalarParameterValue(TEXT("MoonGlowWidth"), FMath::Clamp(MoonGlowWidth, 0.1f, 8.0f));
        SkyMID->SetScalarParameterValue(TEXT("MoonGlowSoftness"), FMath::Clamp(MoonGlowSoftness, 0.25f, 4.0f));
        SkyMID->SetScalarParameterValue(TEXT("AuroraVisibility"), bShowAurora ? 1.0f : 0.0f);
        SkyMID->SetVectorParameterValue(TEXT("AuroraLowerColor"), AuroraLowerColor.GetClamped(0.0f, 1.0f));
        SkyMID->SetVectorParameterValue(TEXT("AuroraUpperColor"), AuroraUpperColor.GetClamped(0.0f, 1.0f));
        SkyMID->SetVectorParameterValue(TEXT("AuroraAccentColor"), AuroraAccentColor.GetClamped(0.0f, 1.0f));
        SkyMID->SetScalarParameterValue(TEXT("AuroraIntensity"), FMath::Max(0.0f, AuroraIntensity));
        SkyMID->SetScalarParameterValue(TEXT("AuroraDensity"), FMath::Clamp(AuroraDensity, 0.0f, 2.0f));
        SkyMID->SetScalarParameterValue(TEXT("AuroraWidth"), FMath::DegreesToRadians(FMath::Clamp(AuroraWidth, 1.0f, 40.0f)));
        SkyMID->SetScalarParameterValue(TEXT("AuroraElevation"), FMath::DegreesToRadians(FMath::Clamp(AuroraElevation, 5.0f, 75.0f)));
        SkyMID->SetScalarParameterValue(TEXT("AuroraAzimuth"), FMath::DegreesToRadians(AuroraAzimuth));
        SkyMID->SetScalarParameterValue(TEXT("AuroraCoverage"), FMath::DegreesToRadians(FMath::Clamp(AuroraCoverage, 20.0f, 360.0f)));
        SkyMID->SetScalarParameterValue(TEXT("AuroraFlowSpeed"), AuroraFlowSpeed);
        SkyMID->SetScalarParameterValue(TEXT("AuroraTurbulence"), FMath::Clamp(AuroraTurbulence, 0.0f, 2.0f));
        SkyMID->SetScalarParameterValue(TEXT("AuroraDetailScale"), FMath::Clamp(AuroraDetailScale, 0.25f, 4.0f));
        SkyMID->SetScalarParameterValue(TEXT("AuroraSeed"), FMath::Clamp(AuroraSeed, 0.0f, 1000.0f));
        SkyMID->SetScalarParameterValue(TEXT("StarBrightness"), FMath::Max(0.0f, StarBrightness));
    }
}
