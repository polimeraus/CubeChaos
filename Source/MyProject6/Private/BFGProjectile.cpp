#include "BFGProjectile.h"
#include "ChunkedDestructibleConeStructure.h"
#include "QuadPanelConeStructure.h"
#include "ChunkedDestructibleFloor.h"
#include "DestructibleRadialSculpture.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "ShooterNPC.h"
#include "RoundedBoxActor.h"
#include "SimpleHouseActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
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
    PlasmaLight->SetVolumetricScatteringIntensity(0.f);
    PlasmaLight->SetAffectGlobalIllumination(false);
    PlasmaLight->SetAffectTranslucentLighting(false);
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
    if (APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
    {
        View = Camera->GetCameraLocation();
        const FVector Offset = GetActorLocation()-View;
        // A missed shot lives for ten seconds, but after a fraction of that it
        // is sub-pixel or behind the viewer. Keep its swept collision running;
        // do not upload animated mesh buffers for those invisible projectiles.
        const bool bVisible = Offset.SizeSquared()<FMath::Square(10000.f)
            && (Offset.SizeSquared()<FMath::Square(250.f)
                || FVector::DotProduct(Offset.GetSafeNormal(),Camera->GetCameraRotation().Vector())>0.f);
        Plasma->SetVisibility(bVisible);
        if (!bVisible)
        {
            PlasmaLight->SetIntensity(0.f);
            return;
        }
    }
    // Collision and movement still run every frame. Small surface animation can
    // update at 30 Hz (15 Hz at distance) without quantizing the projectile path.
    VisualAccumulator += DeltaSeconds;
    const float VisualStep = FVector::DistSquared(View, GetActorLocation()) > FMath::Square(4000.f) ? 1.f/15.f : 1.f/30.f;
    if (VisualAccumulator >= VisualStep || bVisualImpact != bExploded)
    {
        VisualAccumulator = FMath::Fmod(VisualAccumulator, VisualStep);
        DrawEffect(bExploded ? ImpactAge : Age, bExploded, View);
    }
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
    float Damage = FMath::Max(0.f, DirectDamage);
    // Use the target's live health, including placed-instance overrides and
    // spawned variants. Their regular damage path still owns death presentation.
    if (const AChasingNPC* NPC = Cast<AChasingNPC>(DirectTarget))
        Damage = FMath::Max(Damage, NPC->GetCurrentHealth());
    else if (const AWhisperFollower* Follower = Cast<AWhisperFollower>(DirectTarget))
        Damage = FMath::Max(Damage, Follower->GetCurrentHealth());
    else if (const AShooterNPC* Shooter = Cast<AShooterNPC>(DirectTarget))
        Damage = FMath::Max(Damage, Shooter->CurrentHP);

    if (ADestructibleRadialSculpture* Sculpture = Cast<ADestructibleRadialSculpture>(DirectTarget))
        Sculpture->ApplyExplosionFromHit(Hit, FMath::Max(1.f, SplashRadius) * FMath::Max(1.f, SculptureRadiusMultiplier),
            Damage * FMath::Max(1.f, SculptureDamageMultiplier));
    else if (AQuadPanelConeStructure* Quad = Cast<AQuadPanelConeStructure>(DirectTarget))
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
    constexpr int32 Rings=6, Segments=12;
    FDynamicMesh3& Mesh = Plasma->GetDynamicMesh()->GetMeshRef();
    const bool bBuild = Mesh.VertexCount() != (bImpact ? 44 : (Rings+1)*(Segments+1)+24);
    if (bBuild)
    {
        Mesh.Clear();
        Mesh.EnableAttributes();
        Mesh.Attributes()->EnablePrimaryColors();
        ++VisualTopologyBuildCount;
    }
    bVisualImpact = bImpact;
    ++VisualUpdateCount;
    int32 VertexIndex = 0;
    auto Vertex = [&](FVector P, FVector N, FVector2f UV, FVector4f Color)
    {
        const int32 ID = VertexIndex++;
        if (bBuild)
        {
            Mesh.AppendVertex(FVector3d(P));
            Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(N));
            Mesh.Attributes()->PrimaryUV()->AppendElement(UV);
            Mesh.Attributes()->PrimaryColors()->AppendElement(Color);
        }
        else
        {
            Mesh.SetVertex(ID, FVector3d(P));
            Mesh.Attributes()->PrimaryNormals()->SetElement(ID, FVector3f(N));
            Mesh.Attributes()->PrimaryColors()->SetElement(ID, Color);
        }
    };
    auto Quad = [&](FVector Centre, FVector X, FVector Y, float Alpha, float Mode)
    {
        const FVector P[] = {Centre-X-Y, Centre+X-Y, Centre-X+Y, Centre+X+Y};
        const FVector2f UV[] = {{0,0},{1,0},{0,1},{1,1}};
        const int32 Base = VertexIndex;
        for (int32 I=0; I<4; ++I)
        {
            Vertex(P[I], Normal, UV[I], FVector4f(Mode,1,1,Alpha));
        }
        if (bBuild) for (FIndex3i T : {FIndex3i(Base,Base+1,Base+2),FIndex3i(Base+1,Base+3,Base+2)})
        {
            const int32 ID=Mesh.AppendTriangle(T);
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID,T);
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID,T);
            Mesh.Attributes()->PrimaryColors()->SetTriangle(ID,T);
        }
    };
    float VisualLightIntensity = 0.f;
    if (bImpact)
    {
        const float Fade = FMath::Square(FMath::Clamp(1.f-Seconds/.5f,0.f,1.f));
        const float Radius = 26.f+FMath::Sqrt(Seconds)*165.f;
        Quad(FVector::ZeroVector,Right*Radius,Up*Radius,Fade,1);
        Quad(Normal*2,Right*Radius*.5f,Up*Radius*.5f,Fade*.8f,0);
        VisualLightIntensity = ImpactLightIntensity*Fade;
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
        VisualLightIntensity = FlightLightIntensity*Pulse;
        PlasmaLight->SetAttenuationRadius(FlightLightRadius);
        // Rotating uneven volume gives the core parallax from every viewing angle.
        for(int32 R=0; R<=Rings; ++R) for(int32 S=0; S<=Segments; ++S)
        {
            const float V=float(R)/Rings,U=float(S)/Segments;
            const float Phi=V*UE_PI,Theta=U*UE_TWO_PI+Seconds*3.5f;
            const FVector N(FMath::Cos(Phi),FMath::Sin(Phi)*FMath::Cos(Theta),FMath::Sin(Phi)*FMath::Sin(Theta));
            const float Radius=11.f*Pulse*(1+.14f*FMath::Sin(Theta*5+Seconds*19)*FMath::Sin(Phi*4-Seconds*11));
            Vertex(N*Radius, N, FVector2f(U,V), FVector4f(.2f,1,1,.46f));
        }
        if (bBuild) for(int32 R=0; R<Rings; ++R) for(int32 S=0; S<Segments; ++S)
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
    // A topology/UV upload and new render proxy are needed only at spawn/impact.
    // Preserve the buffers for subsequent animated positions/normals/colors.
    if (bBuild) Plasma->NotifyMeshUpdated();
    else Plasma->FastNotifyPositionsUpdated(true, true, false);
    const float LightFade = 1.f-FMath::SmoothStep(2000.f,3500.f,float(FVector::Distance(ViewLocation,GetActorLocation())));
    // Set once: a culled light should stay at zero instead of being invalidated
    // by a full-intensity write followed by a second write back to zero.
    PlasmaLight->SetIntensity(VisualLightIntensity*LightFade);
}
