#include "RailgunBeam.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace RailTrailMesh
{
    void Quad(FDynamicMesh3& Mesh, FVector Centre, FVector Right, FVector Up, float Alpha = 1.f, float Mode = 0.f)
    {
        const FVector Points[] = {Centre - Right - Up, Centre + Right - Up, Centre - Right + Up, Centre + Right + Up};
        const FVector2f UVs[] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        const FVector3f Normal(FVector::CrossProduct(Right, Up).GetSafeNormal());
        const int32 Base = Mesh.VertexCount();
        for (int32 V = 0; V < 4; ++V)
        {
            Mesh.AppendVertex(FVector3d(Points[V]));
            Mesh.Attributes()->PrimaryNormals()->AppendElement(Normal);
            Mesh.Attributes()->PrimaryUV()->AppendElement(UVs[V]);
            if (Mesh.Attributes()->HasPrimaryColors())
                Mesh.Attributes()->PrimaryColors()->AppendElement(FVector4f(Mode, 1, 1, Alpha));
        }
        for (FIndex3i Triangle : {FIndex3i(Base, Base + 1, Base + 2), FIndex3i(Base + 1, Base + 3, Base + 2)})
        {
            const int32 ID = Mesh.AppendTriangle(Triangle);
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID, Triangle);
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, Triangle);
            if (Mesh.Attributes()->HasPrimaryColors()) Mesh.Attributes()->PrimaryColors()->SetTriangle(ID, Triangle);
        }
    }
}

ARailgunBeam::ARailgunBeam()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("BeamRoot"));
    Trail = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("EnergyTrail"));
    Spiral = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("SpiralParticles"));
    for (UDynamicMeshComponent* Mesh : {Trail.Get(), Spiral.Get()})
    {
        Mesh->SetupAttachment(RootComponent);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->bReceivesDecals = false;
        Mesh->SetTranslucentSortPriority(2);
    }
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Weapons/Railgun/Materials/M_RailTrail.M_RailTrail"));
    EnergyMaterial = Material.Object;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Particles(TEXT("/Game/Weapons/Railgun/Materials/M_RailSpiral.M_RailSpiral"));
    ParticleMaterial = Particles.Get();
}

void ARailgunBeam::InitializeBeam(FVector Start, FVector End, float Duration)
{
    Age = 0;
    bHasImpact = false;
    Lifetime = FMath::Clamp(Duration, .05f, 2.f);
    BeamLength = FMath::Max(float(FVector::Distance(Start, End)), 1.f);
    const float SpiralLife = Visuals.bSmoothSpiral ? FMath::Min(1.25f, .65f + BeamLength * .00003f)
        : FMath::Min(5.f, .6f + BeamLength * .0002f);
    SetLifeSpan(Visuals.bSpiral ? FMath::Max(Lifetime, SpiralLife) : Lifetime);
    ShotDirection = (End - Start).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    SetActorLocationAndRotation(Start, ShotDirection.Rotation());
    Trail->SetRelativeRotation(FRotator::ZeroRotator);
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    RailTrailMesh::Quad(Mesh, FVector(BeamLength * .5f, 0, 0), FVector(BeamLength * .5f, 0, 0),
        FVector(0, FMath::Clamp(Visuals.Width, .5f, 50.f) * .5f, 0));
    Trail->SetMesh(MoveTemp(Mesh));
    if (EnergyMaterial)
    {
        TrailMaterial = UMaterialInstanceDynamic::Create(EnergyMaterial, this);
        TrailMaterial->SetVectorParameterValue(TEXT("Tint"), Visuals.Color);
        TrailMaterial->SetScalarParameterValue(TEXT("Brightness"), FMath::Clamp(Visuals.Intensity, 0.f, 30.f));
        TrailMaterial->SetScalarParameterValue(TEXT("Density"), FMath::Clamp(Visuals.Density, 0.f, 5.f));
        TrailMaterial->SetScalarParameterValue(TEXT("BeamLength"), BeamLength);
        TrailMaterial->SetScalarParameterValue(TEXT("Fade"), 1.f);
        TrailMaterial->SetScalarParameterValue(TEXT("Age"), 0.f);
        Trail->SetMaterial(0, TrailMaterial);
    }
    if (ParticleMaterial)
    {
        SpiralMaterial = UMaterialInstanceDynamic::Create(ParticleMaterial, this);
        SpiralMaterial->SetVectorParameterValue(TEXT("Tint"), Visuals.SpiralColor);
        SpiralMaterial->SetScalarParameterValue(TEXT("Brightness"), FMath::Clamp(Visuals.Intensity, 0.f, 30.f));
        SpiralMaterial->SetScalarParameterValue(TEXT("Density"), FMath::Clamp(Visuals.Density, 0.f, 5.f));
        Spiral->SetMaterial(0, SpiralMaterial);
    }
    FaceCamera(Start - ShotDirection * 100.f + FVector(0, 0, 20));
}

void ARailgunBeam::SetImpact(FVector Location)
{
    bHasImpact = true;
    ImpactLocation = GetActorTransform().InverseTransformPosition(Location);
    UpdateParticles(LastViewLocation);
}

void ARailgunBeam::FaceCamera(FVector ViewLocation)
{
    LastViewLocation = ViewLocation;
    FVector Side = FVector::CrossProduct(ShotDirection, ViewLocation - GetActorLocation()).GetSafeNormal();
    if (Side.IsNearlyZero()) Side = GetActorRightVector();
    // Rotate only the flat core; the spiral's phase stays fixed in world space.
    Trail->SetWorldRotation(FRotationMatrix::MakeFromXY(ShotDirection, Side).Rotator());
    UpdateParticles(ViewLocation);
}

void ARailgunBeam::UpdateParticles(FVector ViewLocation)
{
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnablePrimaryColors();
    const FVector View = GetActorTransform().InverseTransformPosition(ViewLocation);
    auto Sprite = [&](FVector Centre, float Radius, float Alpha, float Mode = 0.f)
    {
        const FVector Normal = (View - Centre).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        FVector Right, Up;
        Normal.FindBestAxisVectors(Right, Up);
        RailTrailMesh::Quad(Mesh, Centre, Right * Radius, Up * Radius, Alpha, Mode);
    };
    if (Visuals.bSpiral && Visuals.bSmoothSpiral)
    {
        // A smooth world-space helix. Only the thin ribbon's facing changes with the
        // viewer; its centreline and phase never follow the camera or the weapon.
        const float Pitch = FMath::Clamp(Visuals.SpiralPitch, 50.f, 1000.f);
        const int32 Steps = FMath::CeilToInt(FMath::Clamp(BeamLength / FMath::Min(8.f, Pitch / 48.f), 2.f, 4096.f));
        auto CentreAt = [&](float Distance)
        {
            Distance = FMath::Clamp(Distance, 0.f, BeamLength);
            const float Taper = FMath::SmoothStep(0.f, 40.f, Distance) * FMath::SmoothStep(0.f, 30.f, BeamLength - Distance);
            const float Radius = (FMath::Clamp(Visuals.SpiralRadius, 0.f, 50.f) + Age * 9.f) * Taper;
            const float Angle = PI + Distance * UE_TWO_PI / Pitch;
            return FVector(Distance, FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius);
        };
        for (int32 Index = 0; Index <= Steps; ++Index)
        {
            const float Distance = BeamLength * Index / Steps;
            const FVector Centre = CentreAt(Distance);
            const FVector Tangent = (CentreAt(Distance + 1.f) - CentreAt(Distance - 1.f)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
            const FVector Side = FVector::CrossProduct(Tangent, View - Centre).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
            const FVector3f Normal(FVector::CrossProduct(Tangent, Side).GetSafeNormal());
            const float LocalLife = FMath::Min(1.25f, .65f + Distance * .00003f);
            const float Fade = FMath::Pow(FMath::Clamp(1.f - Age / LocalLife, 0.f, 1.f), 1.3f);
            const float HalfWidth = FMath::Clamp(Visuals.SpiralWidth, .5f, 10.f) * .5f * (1.f + Age * .4f);
            for (int32 Edge = 0; Edge < 2; ++Edge)
            {
                Mesh.AppendVertex(FVector3d(Centre + Side * HalfWidth * (Edge ? 1.f : -1.f)));
                Mesh.Attributes()->PrimaryNormals()->AppendElement(Normal);
                Mesh.Attributes()->PrimaryUV()->AppendElement(FVector2f(Distance / 70.f, float(Edge)));
                Mesh.Attributes()->PrimaryColors()->AppendElement(FVector4f(1, 1, 1, Fade));
            }
            if (Index > 0)
            {
                const int32 V = Index * 2;
                for (FIndex3i Triangle : {FIndex3i(V - 2, V, V - 1), FIndex3i(V - 1, V, V + 1)})
                {
                    const int32 ID = Mesh.AppendTriangle(Triangle);
                    Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID, Triangle);
                    Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, Triangle);
                    Mesh.Attributes()->PrimaryColors()->SetTriangle(ID, Triangle);
                }
            }
        }
    }
    else if (Visuals.bSpiral)
    {
        // Q3 proportions: 4u radius, 180u pitch, one 1.1u sprite per 10u, 6u/s drift.
        // All particles exist on the same frame as the hitscan.
        const float Step = FMath::Max(25.f, BeamLength / 4096.f);
        for (float Distance = FMath::Min(50.f, BeamLength * .5f); Distance < BeamLength; Distance += Step)
        {
            const float ParticleLife = FMath::Min(5.f, .6f + Distance * .0002f);
            const float Alpha = FMath::Clamp(1.f - Age / ParticleLife, 0.f, 1.f);
            if (Alpha <= 0) continue;
            const float Angle = PI + Distance * UE_TWO_PI / FMath::Clamp(Visuals.SpiralPitch, 50.f, 1000.f);
            const float Radius = FMath::Clamp(Visuals.SpiralRadius, 0.f, 50.f) + Age * 15.f;
            Sprite(FVector(Distance, FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius), 2.75f, Alpha);
        }
    }
    if (Age < .09f)
        Sprite(FVector(1, 0, 0), 4.5f, .5f * FMath::Square(1.f - Age / .09f), .5f);
    if (bHasImpact && Age < .22f)
        Sprite(ImpactLocation - FVector(.6f, 0, 0), 7.f + Age * 25.f, FMath::Square(1.f - Age / .22f), .5f);
    Spiral->SetMesh(MoveTemp(Mesh));
}

void ARailgunBeam::SetPreviewAge(float Seconds)
{
    Age = FMath::Max(0.f, Seconds);
    if (TrailMaterial)
    {
        TrailMaterial->SetScalarParameterValue(TEXT("Fade"), FMath::Clamp(1.f - Age / Lifetime, 0.f, 1.f));
        TrailMaterial->SetScalarParameterValue(TEXT("Age"), Age);
    }
    UpdateParticles(LastViewLocation);
}

void ARailgunBeam::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
        LastViewLocation = Camera->GetCameraLocation();
    SetPreviewAge(Age + DeltaSeconds);
    FVector Side = FVector::CrossProduct(ShotDirection, LastViewLocation - GetActorLocation()).GetSafeNormal();
    if (!Side.IsNearlyZero()) Trail->SetWorldRotation(FRotationMatrix::MakeFromXY(ShotDirection, Side).Rotator());
}
