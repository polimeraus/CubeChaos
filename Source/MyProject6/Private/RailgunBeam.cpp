#include "RailgunBeam.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace RailTrailMesh
{
    void Core(FDynamicMesh3& Mesh, float Length, float Radius)
    {
        constexpr int32 Sides = 16;
        Mesh.Attributes()->EnablePrimaryColors();
        auto Vertex = [&](FVector3d Position, FVector3f Normal, FVector2f CrossSection, float EndFade)
        {
            const int32 ID = Mesh.AppendVertex(Position);
            Mesh.Attributes()->PrimaryNormals()->AppendElement(Normal);
            Mesh.Attributes()->PrimaryUV()->AppendElement(CrossSection * .5f + FVector2f(.5f, .5f));
            Mesh.Attributes()->PrimaryColors()->AppendElement(FVector4f(1.f, 1.f, 1.f, EndFade));
            return ID;
        };
        auto Face = [&](FIndex3i Triangle)
        {
            const int32 ID = Mesh.AppendTriangle(Triangle);
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID, Triangle);
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, Triangle);
            Mesh.Attributes()->PrimaryColors()->SetTriangle(ID, Triangle);
        };
        const float Cap = FMath::Min(2.4f * Radius, .2f * Length);
        const float X[] = {0.f, Cap, Length - Cap, Length};
        const float Scale[] = {.16f, 1.f, 1.f, .25f};
        for (int32 Ring = 0; Ring < 4; ++Ring)
            for (int32 I = 0; I < Sides; ++I)
            {
                float S, C;
                FMath::SinCos(&S, &C, UE_TWO_PI * I / Sides);
                Vertex(FVector(X[Ring], C * Radius * Scale[Ring], S * Radius * Scale[Ring]),
                    FVector3f(0, C, S), FVector2f(C, S), (Ring == 1 || Ring == 2) ? 1.f : 0.f);
            }
        const int32 Start = Vertex(FVector::ZeroVector, FVector3f(-1, 0, 0), FVector2f::ZeroVector, 0.f);
        const int32 End = Vertex(FVector(Length, 0, 0), FVector3f(1, 0, 0), FVector2f::ZeroVector, 0.f);
        for (int32 I = 0; I < Sides; ++I)
        {
            const int32 J = (I + 1) % Sides;
            for (int32 Ring = 0; Ring < 3; ++Ring)
            {
                const int32 A = Ring * Sides + I, B = Ring * Sides + J;
                Face({A, B, A + Sides});
                Face({B, B + Sides, A + Sides});
            }
            Face({Start, J, I});
            Face({End, 3 * Sides + I, 3 * Sides + J});
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
        : FMath::Min(1.f, .62f + BeamLength * .00012f);
    SetLifeSpan(Visuals.bSpiral ? FMath::Max(Lifetime, SpiralLife) : Lifetime);
    const FVector ShotDirection = (End - Start).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
    SetActorLocationAndRotation(Start, ShotDirection.Rotation());
    Trail->SetRelativeRotation(FRotator::ZeroRotator);
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    RailTrailMesh::Core(Mesh, BeamLength, FMath::Clamp(Visuals.Width, .5f, 50.f) * .5f);
    Trail->SetMesh(MoveTemp(Mesh));
    if (EnergyMaterial)
    {
        TrailMaterial = UMaterialInstanceDynamic::Create(EnergyMaterial, this);
        TrailMaterial->SetVectorParameterValue(TEXT("Tint"), Visuals.Color);
        TrailMaterial->SetScalarParameterValue(TEXT("Brightness"), FMath::Clamp(Visuals.Intensity, 0.f, 30.f));
        TrailMaterial->SetScalarParameterValue(TEXT("Density"), FMath::Clamp(Visuals.Density, 0.f, 5.f));
        TrailMaterial->SetScalarParameterValue(TEXT("WhiteCore"), Visuals.WhiteCore);
        TrailMaterial->SetVectorParameterValue(TEXT("AxisY"), FLinearColor(GetActorRightVector()));
        TrailMaterial->SetVectorParameterValue(TEXT("AxisZ"), FLinearColor(GetActorUpVector()));
        TrailMaterial->SetScalarParameterValue(TEXT("Fade"), 1.f);
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
    UpdateParticles(ViewLocation);
}

void ARailgunBeam::UpdateParticles(FVector ViewLocation)
{
    const float Pitch = FMath::Clamp(Visuals.SpiralPitch, 50.f, 1000.f);
    constexpr int32 TubeSides = 8, MaxSteps = 1023;
    const float SampleStep = Pitch / 24.f;
    const float FilamentLength = FMath::Min(BeamLength, MaxSteps * SampleStep);
    const int32 Steps = FMath::Clamp(FMath::CeilToInt(FilamentLength / SampleStep), 2, MaxSteps);
    const float ParticleStep = Pitch / 4.5f;
    const float FirstBead = FMath::Min(Pitch * .28f, BeamLength * .5f);
    const int32 Beads = FMath::Clamp(FMath::CeilToInt((BeamLength - FirstBead) / ParticleStep), 0, 4096);
    const float HalfWidth = .5f * FMath::Clamp(Visuals.SpiralWidth, .5f, 32.f);
    int32 ExpectedVertices = (Age < .09f ? 4 : 0) + (bHasImpact && Age < .22f ? 4 : 0);
    if (Visuals.bSpiral)
        ExpectedVertices += Visuals.bSmoothSpiral ? (Steps + 1) * TubeSides : Beads * 4;
    FDynamicMesh3& Mesh = Spiral->GetDynamicMesh()->GetMeshRef();
    const bool bBuild = Mesh.VertexCount() != ExpectedVertices || !Mesh.HasAttributes()
        || bCachedSpiral != Visuals.bSpiral || bCachedSmoothSpiral != Visuals.bSmoothSpiral;
    if (bBuild)
    {
        ++ParticleTopologyBuildCount;
        Mesh.Clear();Mesh.EnableAttributes();Mesh.Attributes()->EnablePrimaryColors();
        bCachedSpiral = Visuals.bSpiral;bCachedSmoothSpiral = Visuals.bSmoothSpiral;
    }
    int32 VertexIndex = 0;
    auto Vertex = [&](FVector3d Position, FVector3f Normal, FVector2f UV, FVector4f Color)
    {
        const int32 ID = VertexIndex++;
        if (bBuild)
        {
            Mesh.AppendVertex(Position);
            Mesh.Attributes()->PrimaryNormals()->AppendElement(Normal);
            Mesh.Attributes()->PrimaryUV()->AppendElement(UV);
            Mesh.Attributes()->PrimaryColors()->AppendElement(Color);
        }
        else
        {
            Mesh.SetVertex(ID, Position);
            Mesh.Attributes()->PrimaryNormals()->SetElement(ID, Normal);
            Mesh.Attributes()->PrimaryUV()->SetElement(ID, UV);
            Mesh.Attributes()->PrimaryColors()->SetElement(ID, Color);
        }
    };
    auto Triangle = [&](FIndex3i Indices)
    {
        if (!bBuild) return;
        const int32 ID = Mesh.AppendTriangle(Indices);
        Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID, Indices);
        Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, Indices);
        Mesh.Attributes()->PrimaryColors()->SetTriangle(ID, Indices);
    };
    const FVector View = GetActorTransform().InverseTransformPosition(ViewLocation);
    auto Sprite = [&](FVector Centre, float Radius, float Alpha, float Mode = 0.f)
    {
        const FVector Normal = (View - Centre).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
        FVector Right, Up;
        Normal.FindBestAxisVectors(Right, Up);
        Right *= Radius;Up *= Radius;
        const FVector Points[] = {Centre - Right - Up, Centre + Right - Up, Centre - Right + Up, Centre + Right + Up};
        const FVector2f UVs[] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        const FVector3f FaceNormal(FVector::CrossProduct(Right, Up).GetSafeNormal());
        const int32 Base = VertexIndex;
        for (int32 I = 0; I < 4; ++I) Vertex(FVector3d(Points[I]), FaceNormal, UVs[I], FVector4f(Mode, 1, 1, Alpha));
        Triangle(FIndex3i(Base, Base + 1, Base + 2));Triangle(FIndex3i(Base + 1, Base + 3, Base + 2));
    };
    auto CentreAt = [&](float Distance)
    {
        Distance = FMath::Clamp(Distance, 0.f, BeamLength);
        const float Taper = FMath::SmoothStep(0.f, Pitch * .40f, Distance)
            * FMath::SmoothStep(0.f, Pitch * .2f, BeamLength - Distance);
        const float Radius = (FMath::Clamp(Visuals.SpiralRadius, 0.f, 50.f) + Age * 6.f) * Taper;
        float S, C;
        FMath::SinCos(&S, &C, 1.1f + Distance * UE_TWO_PI / Pitch + Age * 1.6f);
        return FVector(Distance, C * Radius, S * Radius);
    };
    if (Visuals.bSpiral && Visuals.bSmoothSpiral)
    {
        // Rotate a physical octagonal cross-section along the helix. Facing only
        // controls the optical falloff across the tube, never its silhouette.
        constexpr float Diagonal = .70710678f;
        constexpr float RingCos[] = {1, Diagonal, 0, -Diagonal, -1, -Diagonal, 0, Diagonal};
        constexpr float RingSin[] = {0, Diagonal, 1, Diagonal, 0, -Diagonal, -1, -Diagonal};
        for (int32 Index = 0; Index <= Steps; ++Index)
        {
            const float Distance = FilamentLength * Index / Steps;
            const FVector Centre = CentreAt(Distance);
            const FVector Tangent = (CentreAt(Distance + 1.f) - CentreAt(Distance - 1.f)).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
            const FVector Side = FVector::CrossProduct(Tangent, View - Centre).GetSafeNormal(UE_SMALL_NUMBER, FVector::RightVector);
            const FVector Up = FVector::CrossProduct(Tangent, FVector::RightVector).GetSafeNormal();
            const FVector Right = FVector::CrossProduct(Up, Tangent);
            const float LocalLife = FMath::Min(1.25f, .65f + Distance * .00003f);
            const float FarFade = FilamentLength < BeamLength
                ? FMath::SmoothStep(0.f, Pitch * 2.f, FilamentLength - Distance) : 1.f;
            const float Fade = FarFade * FMath::Clamp(1.f - Age / LocalLife, 0.f, 1.f);
            const int32 Base = VertexIndex;
            for (int32 I = 0; I < TubeSides; ++I)
            {
                const FVector Normal = Right * RingCos[I] + Up * RingSin[I];
                Vertex(Centre + HalfWidth * Normal, FVector3f(Normal),
                    FVector2f(0.f, .5f + .5f * FVector::DotProduct(Normal, Side)), FVector4f(1, 1, 1, Fade));
            }
            if (bBuild && Index > 0)
            {
                for (int32 I = 0; I < TubeSides; ++I)
                {
                    const int32 A = Base + I, B = Base + (I + 1) % TubeSides;
                    Triangle({A - TubeSides, B - TubeSides, A});
                    Triangle({B - TubeSides, B, A});
                }
            }
        }
    }
    else if (Visuals.bSpiral)
    {
        for (int32 I = 0; I < Beads; ++I)
        {
            const float Distance = FirstBead + I * ParticleStep;
            const float ParticleLife = FMath::Min(1.f, .62f + Distance * .00012f);
            const float Alpha = FMath::Clamp(1.f - Age / ParticleLife, 0.f, 1.f);
            Sprite(CentreAt(Distance), HalfWidth, Alpha);
        }
    }
    if (Age < .09f)
        Sprite(FVector(1, 0, 0), 4.5f, .5f * FMath::Square(1.f - Age / .09f), .5f);
    if (bHasImpact && Age < .22f)
        Sprite(ImpactLocation - FVector(.6f, 0, 0), 7.f + Age * 25.f, FMath::Square(1.f - Age / .22f), .5f);
    check(VertexIndex == ExpectedVertices);
    // Retain topology and GPU buffers while only the pose, facing and fade change.
    if (bBuild) Spiral->NotifyMeshUpdated();
    else Spiral->FastNotifyPositionsUpdated(true, true, true);
}

void ARailgunBeam::SetPreviewAge(float Seconds)
{
    Age = FMath::Max(0.f, Seconds);
    if (TrailMaterial)
    {
        TrailMaterial->SetScalarParameterValue(TEXT("Fade"), FMath::Clamp(1.f - Age / Lifetime, 0.f, 1.f));
    }
    UpdateParticles(LastViewLocation);
}

void ARailgunBeam::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
        LastViewLocation = Camera->GetCameraLocation();
    SetPreviewAge(Age + DeltaSeconds);
}
