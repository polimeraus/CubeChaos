#include "SleepSignalDevice.h"
#include "NPCSleepComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AudioComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ASleepSignalDevice::ASleepSignalDevice()
{
    PrimaryActorTick.bCanEverTick = true;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
    RootComponent = Collision;
    Collision->InitSphereRadius(12.f);
    Collision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    Collision->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    Collision->SetCanEverAffectNavigation(false);
    DeviceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeviceMesh"));
    DeviceMesh->SetupAttachment(RootComponent);
    DeviceMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Model(TEXT("/Game/Weapons/SleepSignal/Meshes/SM_SleepSignalDevice.SM_SleepSignalDevice"));
    DeviceMesh->SetStaticMesh(Model.Get());
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Movement"));
    Movement->SetUpdatedComponent(Collision);
    Movement->bShouldBounce = true;
    Movement->Bounciness = .08f; Movement->Friction = .9f;
    Movement->BounceVelocityStopSimulatingThreshold = 5.f;
    Movement->ProjectileGravityScale = 1.f;
    Movement->bRotationFollowsVelocity = false;
    Movement->bForceSubStepping = true;
    Movement->OnProjectileBounce.AddDynamic(this,&ASleepSignalDevice::OnBounce);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(TEXT("/Game/Weapons/SleepSignal/Materials/M_SleepSignal.M_SleepSignal"));
    SignalMaterial = Material.Get();
    for (int32 I=0;I<3;++I)
    {
        auto* Shell = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("SignalShell%d"),I));
        Shell->SetupAttachment(RootComponent); Shell->SetStaticMesh(Sphere.Object);
        Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision); Shell->SetCastShadow(false);
        Shell->SetCanEverAffectNavigation(false); Shell->SetVisibility(false);
        Shells.Add(Shell);
    }
    Light = CreateDefaultSubobject<UPointLightComponent>(TEXT("SignalLight"));
    Light->SetupAttachment(RootComponent); Light->SetRelativeLocation(FVector(0,0,20));
    Light->SetIntensity(0); Light->SetAttenuationRadius(200); Light->SetCastShadows(false);
    Audio = CreateDefaultSubobject<UAudioComponent>(TEXT("SignalHum"));
    Audio->SetupAttachment(RootComponent); Audio->bAutoActivate = false;
    Audio->bOverrideAttenuation = true;
    Audio->AttenuationOverrides.bAttenuate = true;
    Audio->AttenuationOverrides.FalloffDistance = 1400.f;
    Audio->AttenuationOverrides.AttenuationShapeExtents = FVector(150.f);
    Audio->SetVolumeMultiplier(.3f);
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> Hum(TEXT("/Game/Weapons/SleepSignal/Audio/S_SleepSignalHum.S_SleepSignalHum"));
    Audio->SetSound(Hum.Get());
}

void ASleepSignalDevice::BeginPlay()
{
    Super::BeginPlay();
    SignalSphereDiameter = FMath::IsFinite(SignalSphereDiameter) ? FMath::Clamp(SignalSphereDiameter,100.f,10000.f) : 1000.f;
    SignalDuration = FMath::IsFinite(SignalDuration) ? FMath::Clamp(SignalDuration,.2f,300.f) : 12.f;
    PulseInterval = FMath::Clamp(PulseInterval,.3f,5.f);
    WakeDelay = FMath::Clamp(WakeDelay,.2f,30.f);
    Movement->Velocity = LaunchVelocity;
    if (GetOwner()) Collision->IgnoreActorWhenMoving(GetOwner(),true);
    Collision->IgnoreActorWhenMoving(this,true);
}

void ASleepSignalDevice::OnBounce(const FHitResult& Hit, const FVector&)
{
    if (bLanded || Hit.ImpactNormal.Z < .65f) return;
    bLanded = bEmitting = true;
    Movement->StopSimulating(Hit);
    // Keep the unit planted on the actual hit surface; no sliding or long grenade bounce.
    SetActorRotation(FRotationMatrix::MakeFromZ(Hit.ImpactNormal).Rotator());
    if (Hit.GetComponent() && Hit.GetComponent()->Mobility == EComponentMobility::Movable)
        AttachToComponent(Hit.GetComponent(), FAttachmentTransformRules::KeepWorldTransform);
    Audio->FadeIn(.25f,.3f);
    EmitSignal();
}

float ASleepSignalDevice::GetSignalRemaining() const
{
    return bEmitting ? FMath::Max(0.f,SignalDuration-EmissionAge) : 0.f;
}

void ASleepSignalDevice::EmitSignal()
{
    if (!HasAuthority()) return;
    const float RadiusSquared = FMath::Square(SignalSphereDiameter*.5f);
    for (TActorIterator<ACharacter> It(GetWorld());It;++It)
    {
        if (FVector::DistSquared(It->GetActorLocation(),GetActorLocation()) > RadiusSquared) continue;
        if (auto* Sleep = It->FindComponentByClass<UNPCSleepComponent>())
            Sleep->ReceiveSignal(this,FMath::Max(.3f,WakeDelay));
    }
}

void ASleepSignalDevice::Tick(float Delta)
{
    Super::Tick(Delta);
    if (!bLanded)
    {
        FlightAge += Delta;
        DeviceMesh->SetRelativeRotation(FRotator(FMath::Sin(FlightAge*7.f)*16.f,FlightAge*25.f,0));
        if (FlightAge > 8.f) Destroy();
        return;
    }
    DeviceMesh->SetRelativeRotation(FMath::RInterpTo(DeviceMesh->GetRelativeRotation(),FRotator::ZeroRotator,Delta,10.f));
    EmissionAge += Delta;
    if (bEmitting && EmissionAge >= SignalDuration) { bEmitting = false; Audio->FadeOut(.4f,0.f); }
    if (bEmitting)
    {
        ScanAccumulator += Delta;
        if (ScanAccumulator >= .1f) { ScanAccumulator = 0.f; EmitSignal(); }
    }
    const float Fade = FMath::Clamp((SignalDuration+.5f-EmissionAge)/.5f,0.f,1.f);
    UpdateVisual(EmissionAge,Fade);
    // Remain alive until all final leases expire, allowing a graceful wake delay.
    if (EmissionAge > SignalDuration+FMath::Max(.5f,WakeDelay)+.2f) Destroy();
}

void ASleepSignalDevice::UpdateVisual(float Seconds, float Fade)
{
    if (ShellMaterials.IsEmpty() && SignalMaterial)
        for (const auto& Shell : Shells) ShellMaterials.Add(Shell->CreateDynamicMaterialInstance(0,SignalMaterial));
    for (int32 I=0;I<Shells.Num();++I)
    {
        const float Phase = FMath::Frac(Seconds/FMath::Max(.3f,PulseInterval) + I/3.f);
        const float Radius = FMath::Lerp(16.f,SignalSphereDiameter*.5f,Phase);
        Shells[I]->SetRelativeScale3D(FVector(Radius/50.f));
        Shells[I]->SetVisibility(Fade > .001f && SignalMaterial != nullptr);
        if (ShellMaterials.IsValidIndex(I) && ShellMaterials[I])
        {
            ShellMaterials[I]->SetScalarParameterValue(TEXT("PulseAlpha"),FMath::Sin(Phase*UE_PI)*Fade*.34f);
            ShellMaterials[I]->SetScalarParameterValue(TEXT("Clock"),Seconds);
            ShellMaterials[I]->SetVectorParameterValue(TEXT("Tint"),SignalColor);
        }
    }
    Light->SetLightColor(SignalColor);
    Light->SetIntensity((70.f+40.f*FMath::Sin(Seconds*UE_TWO_PI/FMath::Max(.3f,PulseInterval)))*Fade);
}

void ASleepSignalDevice::PreviewSignal(float Seconds) { UpdateVisual(Seconds); }
