#include "BFGProjectile.h"
#include "ChunkedDestructibleConeStructure.h"
#include "QuadPanelConeStructure.h"
#include "ChunkedDestructibleFloor.h"
#include "RoundedBoxActor.h"
#include "SimpleHouseActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

ABFGProjectile::ABFGProjectile()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("ProjectileRoot"));
    Plasma = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("BFGPlasma"));
    Plasma->SetupAttachment(RootComponent);
    Plasma->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Plasma->SetGenerateOverlapEvents(false);
    Plasma->SetCanEverAffectNavigation(false);
    Plasma->SetCastShadow(false);
    Plasma->bReceivesDecals = false;
    Plasma->SetTranslucentSortPriority(3);
    PlasmaLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("PlasmaLight"));
    PlasmaLight->SetupAttachment(RootComponent);
    PlasmaLight->SetLightColor(FLinearColor(.12f,1.f,.035f));
    PlasmaLight->SetIntensityUnits(ELightUnits::Lumens);
    PlasmaLight->SetIntensity(FlightLightIntensity);
    PlasmaLight->SetAttenuationRadius(FlightLightRadius);
    PlasmaLight->SetSourceRadius(14.f);
    PlasmaLight->SetCastShadows(false);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Surface(TEXT("/Game/Weapons/BFG/Materials/M_BFGPlasma.M_BFGPlasma"));
    PlasmaMaterial = Surface.Get();
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> Sound(TEXT("/Game/Weapons/BFG/Fusion/Audio/S_BFGFusionImpact.S_BFGFusionImpact"));
    ImpactSound = Sound.Get();
}

void ABFGProjectile::BeginPlay()
{
    Super::BeginPlay();
    SetLifeSpan(10.6f);
    DrawEffect(0, false, GetActorLocation() - GetActorForwardVector() * 100);
}

void ABFGProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (UGameplayStatics::IsGamePaused(this)) return;
    Age += DeltaSeconds;
    if (!bExploded && HasAuthority())
    {
        const FVector Start = GetActorLocation();
        const FVector End = Start + GetActorForwardVector() * FMath::Max(1.f, Speed) * DeltaSeconds;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BFGFlight), false, GetOwner());
        Query.AddIgnoredActor(this);
        FHitResult Hit;
        // A swept sphere prevents tunnelling even on a long frame. Pickups and
        // NPC proximity sensors ignore this same channel used by the railgun.
        if (GetWorld()->SweepSingleByChannel(Hit, Start, End, FQuat::Identity,
            ECC_GameTraceChannel1, FCollisionShape::MakeSphere(3.f), Query)) Explode(Hit);
        else
        {
            SetActorLocation(End);
            if (Age >= 10.f) Explode(FHitResult());
        }
    }
    if (bExploded) ImpactAge += DeltaSeconds;
    FVector View = GetActorLocation() - GetActorForwardVector() * 100 + FVector(0, 0, 20);
    if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0)) View = Camera->GetCameraLocation();
    DrawEffect(bExploded ? ImpactAge : Age, bExploded, View);
}

void ABFGProjectile::Explode(const FHitResult& Hit)
{
    if (bExploded || !HasAuthority()) return;
    bExploded = true;
    ImpactAge = 0;
    const FVector Direction = GetActorForwardVector();
    const FVector Origin = Hit.bBlockingHit ? Hit.ImpactPoint + Hit.ImpactNormal * 4.f : GetActorLocation();
    SetActorLocation(Origin);
    AActor* DirectTarget = Hit.GetActor();
    const float Damage = FMath::Max(0.f, DirectDamage);
    if (AQuadPanelConeStructure* Quad = Cast<AQuadPanelConeStructure>(DirectTarget))
        Quad->ApplyExplosionFromHit(Hit, SplashRadius, Damage);
    else if (AChunkedDestructibleConeStructure* Cone = Cast<AChunkedDestructibleConeStructure>(DirectTarget))
        Cone->ApplyExplosionFromHit(Hit, SplashRadius, Damage);
    else if (AChunkedDestructibleFloor* Floor = Cast<AChunkedDestructibleFloor>(DirectTarget))
        Floor->ApplyExplosionFromHit(Hit, SplashRadius, Damage);
    else if (ARoundedBoxActor* Box = Cast<ARoundedBoxActor>(DirectTarget))
        Box->CarveSphereAtWorldLocation(Hit.ImpactPoint, 65.f);
    else if (ASimpleHouseActor* House = Cast<ASimpleHouseActor>(DirectTarget); House && House->bEnableDamage)
        House->CarveSphereAtWorldLocation(Hit.ImpactPoint, 65.f);
    else if (IsValid(DirectTarget) && DirectTarget != GetOwner())
        UGameplayStatics::ApplyPointDamage(DirectTarget, Damage, Direction, Hit, GetInstigatorController(), this, nullptr);

    // Q3 excludes the direct target from splash: a single projectile cannot
    // count twice. Use the projectile channel for cover-aware radial damage.
    TArray<AActor*> Ignore = {this};
    if (GetOwner()) Ignore.Add(GetOwner());
    if (DirectTarget) Ignore.Add(DirectTarget);
    UGameplayStatics::ApplyRadialDamageWithFalloff(this, FMath::Max(0.f, SplashDamage), 0.f, Origin,
        0.f, FMath::Max(1.f, SplashRadius), 1.f, nullptr, Ignore, this, GetInstigatorController(), ECC_GameTraceChannel1);
    if (ImpactSound) UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Origin, .9f);
    SetLifeSpan(.5f);
}

void ABFGProjectile::PreviewEffect(float Seconds, bool bImpact, FVector ViewLocation)
{
    if (!HasActorBegunPlay()) DrawEffect(FMath::Max(0.f, Seconds), bImpact, ViewLocation);
}

void ABFGProjectile::DrawEffect(float Seconds, bool bImpact, FVector ViewLocation)
{
    if (!Material && PlasmaMaterial)
    {
        Material = UMaterialInstanceDynamic::Create(PlasmaMaterial, this);
        Plasma->SetMaterial(0, Material);
    }
    if (Material) Material->SetScalarParameterValue(TEXT("Age"), Seconds);
    const FVector View = GetActorTransform().InverseTransformPosition(ViewLocation);
    const FVector Normal = View.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
    FVector Right, Up;
    Normal.FindBestAxisVectors(Right, Up);
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnablePrimaryColors();
    auto Quad = [&](FVector Centre, FVector X, FVector Y, float Alpha, float Mode)
    {
        const FVector P[] = {Centre-X-Y, Centre+X-Y, Centre-X+Y, Centre+X+Y};
        const FVector2f UV[] = {{0,0},{1,0},{0,1},{1,1}};
        const int32 Base = Mesh.VertexCount();
        for (int32 I=0; I<4; ++I)
        {
            Mesh.AppendVertex(FVector3d(P[I]));
            Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(Normal));
            Mesh.Attributes()->PrimaryUV()->AppendElement(UV[I]);
            Mesh.Attributes()->PrimaryColors()->AppendElement(FVector4f(Mode,1,1,Alpha));
        }
        for (FIndex3i T : {FIndex3i(Base,Base+1,Base+2),FIndex3i(Base+1,Base+3,Base+2)})
        {
            const int32 ID=Mesh.AppendTriangle(T);
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID,T);
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID,T);
            Mesh.Attributes()->PrimaryColors()->SetTriangle(ID,T);
        }
    };
    if (bImpact)
    {
        const float Fade = FMath::Square(FMath::Clamp(1.f-Seconds/.5f,0.f,1.f));
        const float Radius = 26.f+FMath::Sqrt(Seconds)*165.f;
        Quad(FVector::ZeroVector,Right*Radius,Up*Radius,Fade,1);
        Quad(Normal*2,Right*Radius*.5f,Up*Radius*.5f,Fade*.8f,0);
        PlasmaLight->SetIntensity(ImpactLightIntensity*Fade);
        PlasmaLight->SetAttenuationRadius(FlightLightRadius*1.65f);
        for (int32 I=0; I<9; ++I)
        {
            const float Angle = I*2.39996f+.36f*FMath::Sin(I*4.3f);
            const FVector Radial = Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle);
            const FVector Tangent = -Right*FMath::Sin(Angle)+Up*FMath::Cos(Angle);
            const FVector Centre = Radial*Radius*(.6f+.5f*FMath::Abs(FMath::Sin(I*7.3f)));
            Quad(Centre,Radial*(8.f+Seconds*38.f),Tangent*(3.f+Seconds*5.f),Fade*.7f,0);
        }
    }
    else
    {
        const float Pulse = 1.f+.055f*FMath::Sin(Seconds*39.f)+.035f*FMath::Sin(Seconds*71.f);
        PlasmaLight->SetIntensity(FlightLightIntensity*Pulse);
        PlasmaLight->SetAttenuationRadius(FlightLightRadius);
        // Rotating uneven volume gives the core parallax from every viewing angle.
        const int32 Rings=10, Segments=20;
        for(int32 R=0; R<=Rings; ++R) for(int32 S=0; S<=Segments; ++S)
        {
            const float V=float(R)/Rings,U=float(S)/Segments;
            const float Phi=V*UE_PI,Theta=U*UE_TWO_PI+Seconds*3.5f;
            const FVector N(FMath::Cos(Phi),FMath::Sin(Phi)*FMath::Cos(Theta),FMath::Sin(Phi)*FMath::Sin(Theta));
            const float Radius=11.f*Pulse*(1+.14f*FMath::Sin(Theta*5+Seconds*19)*FMath::Sin(Phi*4-Seconds*11));
            Mesh.AppendVertex(FVector3d(N*Radius));
            Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(N));
            Mesh.Attributes()->PrimaryUV()->AppendElement(FVector2f(U,V));
            Mesh.Attributes()->PrimaryColors()->AppendElement(FVector4f(.2f,1,1,.46f));
        }
        for(int32 R=0; R<Rings; ++R) for(int32 S=0; S<Segments; ++S)
        {
            const int32 A=R*(Segments+1)+S,B=A+Segments+1;
            for(FIndex3i T : {FIndex3i(A,B,A+1),FIndex3i(A+1,B,B+1)})
            {
                const int32 ID=Mesh.AppendTriangle(T);
                Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID,T);
                Mesh.Attributes()->PrimaryUV()->SetTriangle(ID,T);
                Mesh.Attributes()->PrimaryColors()->SetTriangle(ID,T);
            }
        }
        Quad(FVector::ZeroVector,Right*29.f*Pulse,Up*29.f*Pulse,.85f,0);
        // Short irregular wake, with moving wisps rather than evenly spaced beads.
        const FVector Side = FVector::CrossProduct(FVector::ForwardVector,View).GetSafeNormal(UE_SMALL_NUMBER,Right);
        Quad(FVector(-31,0,0),FVector(39,0,0),Side*13.f,.7f,.5f);
        for (int32 I=0; I<4; ++I)
        {
            const float Phase=FMath::Frac(Seconds*9.f+I*.25f);
            const FVector Centre(-8.f-Phase*64.f,FMath::Sin(I*7.f+Seconds*12.f)*Phase*8.f,FMath::Cos(I*9.f+Seconds*15.f)*Phase*6.f);
            const float Size=12.f*(1.f-Phase)+3.f;
            Quad(Centre,Right*Size,Up*Size,.35f*(1-Phase),0);
        }
    }
    Plasma->SetMesh(MoveTemp(Mesh));
}
