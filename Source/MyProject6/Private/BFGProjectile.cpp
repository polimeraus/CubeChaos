#include "BFGProjectile.h"
#include "ChunkedDestructibleConeStructure.h"
#include "QuadPanelConeStructure.h"
#include "ChunkedDestructibleFloor.h"
#include "RoundedBoxActor.h"
#include "SimpleHouseActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DynamicMeshComponent.h"
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
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Surface(TEXT("/Game/Weapons/Railgun/M_BFGPlasma.M_BFGPlasma"));
    PlasmaMaterial = Surface.Get();
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> Sound(TEXT("/Game/Weapons/Railgun/S_BFGImpact.S_BFGImpact"));
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
    if (ImpactSound) UGameplayStatics::PlaySoundAtLocation(this, ImpactSound, Origin, .65f);
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
        const float Radius = 25.f+Seconds*340.f;
        Quad(FVector::ZeroVector,Right*Radius,Up*Radius,Fade*.7f,0);
        Quad(FVector::ZeroVector,Right*Radius,Up*Radius,Fade*.3f,1);
        for (int32 I=0; I<12; ++I)
        {
            const float Angle = I*UE_TWO_PI/12.f+.16f*FMath::Sin(I*4.3f);
            const FVector Radial = Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle);
            const FVector Tangent = -Right*FMath::Sin(Angle)+Up*FMath::Cos(Angle);
            const FVector Centre = Radial*Radius*(.6f+.5f*FMath::Abs(FMath::Sin(I*7.3f)));
            Quad(Centre,Radial*(5.f+Seconds*45.f),Tangent*2.f,Fade,0);
        }
    }
    else
    {
        const float Pulse = 1.f+.07f*FMath::Sin(Seconds*65.f);
        Quad(FVector::ZeroVector,Right*16.f*Pulse,Up*16.f*Pulse,1,0);
        // Short luminous wake follows the projectile in world space.
        const FVector Side = FVector::CrossProduct(FVector::ForwardVector,View).GetSafeNormal(UE_SMALL_NUMBER,Right);
        Quad(FVector(-35,0,0),FVector(32,0,0),Side*7.f,.65f,.5f);
        for (int32 I=1; I<=3; ++I)
            Quad(FVector(-I*15.f,0,0),Right*(8.f-I),Up*(8.f-I),.3f/I,0);
    }
    Plasma->SetMesh(MoveTemp(Mesh));
}
