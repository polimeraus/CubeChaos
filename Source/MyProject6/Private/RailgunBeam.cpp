#include "RailgunBeam.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

ARailgunBeam::ARailgunBeam()
{
    PrimaryActorTick.bCanEverTick = true;
    InitialLifeSpan = .55f;
    Trail = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("EnergyTrail"));
    SetRootComponent(Trail);
    Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Trail->SetGenerateOverlapEvents(false);
    Trail->SetCanEverAffectNavigation(false);
    Trail->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Weapons/Railgun/M_RailTrail.M_RailTrail"));
    EnergyMaterial = Material.Object;
}

void ARailgunBeam::InitializeBeam(FVector Start, FVector End, float Duration)
{
    Age = 0;
    Lifetime = FMath::Clamp(Duration, .05f, 2.f);
    SetLifeSpan(Lifetime);
    ShotDirection = (End - Start).GetSafeNormal();
    if (ShotDirection.IsNearlyZero()) ShotDirection = FVector::ForwardVector;
    const float Length = FMath::Max(1.f, float(FVector::Distance(Start, End)));
    const float HalfWidth = FMath::Clamp(Visuals.Width, .5f, 50.f) * .5f;
    SetActorLocationAndRotation(Start, ShotDirection.Rotation());
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    const FVector3d Points[] = {{0, -HalfWidth, 0}, {0, HalfWidth, 0}, {Length, -HalfWidth, 0}, {Length, HalfWidth, 0}};
    const FVector2f UVs[] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    for (int32 V = 0; V < 4; ++V)
    {
        Mesh.AppendVertex(Points[V]);
        Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(0, 0, 1));
        Mesh.Attributes()->PrimaryUV()->AppendElement(UVs[V]);
    }
    for (const FIndex3i Indices : {FIndex3i(0, 2, 1), FIndex3i(1, 2, 3)})
    {
        const int32 ID = Mesh.AppendTriangle(Indices);
        Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID, Indices);
        Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, Indices);
    }
    Trail->SetMesh(MoveTemp(Mesh));
    if (EnergyMaterial)
    {
        TrailMaterial = UMaterialInstanceDynamic::Create(EnergyMaterial, this);
        TrailMaterial->SetVectorParameterValue(TEXT("Tint"), Visuals.Color);
        TrailMaterial->SetScalarParameterValue(TEXT("Brightness"), FMath::Clamp(Visuals.Intensity, 0.f, 30.f));
        TrailMaterial->SetScalarParameterValue(TEXT("Density"), FMath::Clamp(Visuals.Density, 0.f, 5.f));
        TrailMaterial->SetScalarParameterValue(TEXT("BeamLength"), Length);
        TrailMaterial->SetScalarParameterValue(TEXT("Fade"), 1.f);
        Trail->SetMaterial(0, TrailMaterial);
    }
}

void ARailgunBeam::FaceCamera(FVector ViewLocation)
{
    const FVector Side = FVector::CrossProduct(ShotDirection, ViewLocation - GetActorLocation()).GetSafeNormal();
    if (!Side.IsNearlyZero()) SetActorRotation(FRotationMatrix::MakeFromXY(ShotDirection, Side).Rotator());
}

void ARailgunBeam::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Age += DeltaSeconds;
    if (TrailMaterial)
    {
        const float Fade = FMath::Clamp(1.f - Age / Lifetime, 0.f, 1.f);
        TrailMaterial->SetScalarParameterValue(TEXT("Fade"), Fade * Fade);
        TrailMaterial->SetScalarParameterValue(TEXT("Age"), Age);
    }
    if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
        FaceCamera(Camera->GetCameraLocation());
}
