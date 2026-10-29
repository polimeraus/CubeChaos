#include "BombProjectile.h"
#include "ChunkedDestructibleFloor.h"
#include "DestructibleRadialSculpture.h"
#include "BFGCameraShake.h"
#include "MyProject6Character.h"
#include "NPCHitReactionComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
FVector ClosestBlastPoint(UPrimitiveComponent* Component, const FVector& Origin)
{
    FVector Closest = Origin;
    const float Distance = Component->GetClosestPointOnCollision(Origin, Closest);
    // Zero means the origin is inside the body. Some collision implementations
    // do not write OutPoint in this case.
    if (Distance == 0.f) return Origin;
    return Distance > 0.f ? Closest : Component->Bounds.GetBox().GetClosestPointTo(Origin);
}
}

ABombProjectile::ABombProjectile()
{
    PrimaryActorTick.bCanEverTick = true;
    Collision = CreateDefaultSubobject<USphereComponent>(TEXT("BombCollision"));
    SetRootComponent(Collision);
    Collision->InitSphereRadius(11.5f);
    Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Collision->SetCollisionObjectType(ECC_GameTraceChannel1);
    Collision->SetCollisionResponseToAllChannels(ECR_Block);
    Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    Collision->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
    Collision->SetGenerateOverlapEvents(false);
    Collision->SetCanEverAffectNavigation(false);
    BombMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BombMesh"));
    BombMesh->SetupAttachment(Collision);
    BombMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BombMesh->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Mesh(TEXT("/Game/Weapons/FuseBomb/Meshes/SM_FuseBomb.SM_FuseBomb"));
    BombMesh->SetStaticMesh(Mesh.Get());
    Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("BombMovement"));
    Movement->SetUpdatedComponent(Collision);
    Movement->InitialSpeed = 0.f;
    Movement->MaxSpeed = 2500.f;
    Movement->ProjectileGravityScale = 1.f;
    Movement->bShouldBounce = true;
    Movement->Bounciness = .42f;
    Movement->Friction = .42f;
    Movement->BounceVelocityStopSimulatingThreshold = 45.f;
    Movement->bForceSubStepping = true;
    Movement->MaxSimulationTimeStep = 1.f / 120.f;
    Movement->MaxSimulationIterations = 16;
    Effect = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("BombEffect"));
    Effect->SetupAttachment(Collision);
    Effect->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Effect->SetGenerateOverlapEvents(false);
    Effect->SetCastShadow(false);
    Effect->SetCanEverAffectNavigation(false);
    FuseLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FuseLight"));
    FuseLight->SetupAttachment(BombMesh);
    FuseLight->SetRelativeLocation(FVector(6, 0, 20));
    FuseLight->SetLightColor(FLinearColor(.025f, .65f, 1.f));
    FuseLight->SetIntensityUnits(ELightUnits::Lumens);
    FuseLight->SetIntensity(90.f);
    FuseLight->SetAttenuationRadius(130.f);
    FuseLight->SetCastShadows(false);
    FuseLight->SetVolumetricScatteringIntensity(0.f);
    FuseLight->SetAffectGlobalIllumination(false);
    FuseLight->SetAffectTranslucentLighting(false);
    FuseAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("FuseAudio"));
    FuseAudio->SetupAttachment(Collision);
    FuseAudio->bAutoActivate = false;
    FuseAudio->bOverrideAttenuation = true;
    FuseAudio->AttenuationOverrides.bAttenuate = true;
    FuseAudio->AttenuationOverrides.AttenuationShapeExtents = FVector(120);
    FuseAudio->AttenuationOverrides.FalloffDistance = 1500;
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> Fuse(TEXT("/Game/Weapons/FuseBomb/Audio/S_BombFuse.S_BombFuse"));
    FuseAudio->SetSound(Fuse.Get());
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> Boom(TEXT("/Game/Weapons/FuseBomb/Audio/S_BombExplosion.S_BombExplosion"));
    ExplosionSound = Boom.Get();
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> FX(TEXT("/Game/Weapons/FuseBomb/Materials/M_BombEffect.M_BombEffect"));
    EffectMaterial = FX.Get();
}

void ABombProjectile::BeginPlay()
{
    Super::BeginPlay();
    Collision->IgnoreActorWhenMoving(GetOwner(), true);
    Movement->Velocity = LaunchVelocity;
    if (HasAuthority()) GetWorldTimerManager().SetTimer(FuseTimer, this, &ABombProjectile::Explode, FMath::Max(.1f, FuseSeconds), false);
    FuseAudio->Play();
    SetLifeSpan(FMath::Max(.1f, FuseSeconds) + 1.8f);
}

float ABombProjectile::GetFuseRemaining() const
{
    return bExploded ? 0.f : FMath::Max(0.f, FuseSeconds - Age);
}

void ABombProjectile::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (UGameplayStatics::IsGamePaused(this)) return;
    Age += DeltaSeconds;
    if (bExploded) ImpactAge += DeltaSeconds;
    else if (Movement->IsActive()) BombMesh->AddLocalRotation(FRotator(120.f * DeltaSeconds, 35.f * DeltaSeconds, 0));
    FVector View = GetActorLocation() + FVector(-100, 0, 40);
    if (const auto* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0)) View = Camera->GetCameraLocation();
    VisualAccumulator += DeltaSeconds;
    if (VisualAccumulator >= 1.f/30.f || bVisualImpact != bExploded)
    {
        VisualAccumulator = FMath::Fmod(VisualAccumulator, 1.f/30.f);
        DrawEffect(bExploded ? ImpactAge : Age, bExploded, View);
    }
}

void ABombProjectile::Explode()
{
    if (bExploded || !HasAuthority()) return;
    bExploded = true;
    ImpactAge = 0;
    GetWorldTimerManager().ClearTimer(FuseTimer);
    Movement->StopMovementImmediately();
    Movement->Deactivate();
    Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BombMesh->SetVisibility(false);
    FuseAudio->Stop();
    const FVector Origin = GetActorLocation();
    ApplyBlastDamage(Origin);
    DamageStructures(Origin);
    // Death callbacks must finish before launch, and newly created debris must
    // receive the same pressure as physics bodies already in the blast sphere.
    ApplyBlastImpulse(Origin);
    if (ExplosionSound) UGameplayStatics::PlaySoundAtLocation(this, ExplosionSound, Origin, 1.f);
    UGameplayStatics::PlayWorldCameraShake(this, UBFGCameraShake::StaticClass(), Origin, 0.f, BlastRadius * 3.f, 1.5f);
    SetLifeSpan(1.65f);
}

void ABombProjectile::ApplyBlastDamage(const FVector& Origin)
{
    const float Radius = FMath::Max(1.f, BlastRadius);
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BombDamage), false, this);
    GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams::AllObjects,
        FCollisionShape::MakeSphere(Radius), Query);
    TMap<AActor*, TArray<FHitResult>> Hits;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        AActor* Actor = Overlap.GetActor();
        UPrimitiveComponent* Component = Overlap.GetComponent();
        if (!IsValid(Actor) || !Component || Actor == this || !Actor->CanBeDamaged()
            || Actor->IsA<AChunkedDestructibleFloor>()) continue;
        // Contact sensors, weapons and cosmetic components must not extend a
        // character's damage radius. Their capsule is the gameplay body.
        if (auto* Character = Cast<ACharacter>(Actor)) Component = Character->GetCapsuleComponent();
        const FVector Closest = ClosestBlastPoint(Component, Origin);
        if (FVector::DistSquared(Origin, Closest) >= FMath::Square(Radius)) continue;
        Hits.FindOrAdd(Actor).Emplace(Actor, Component, Closest, (Origin-Closest).GetSafeNormal());
    }
    for (auto& Pair : Hits)
    {
        AActor* Actor = Pair.Key;
        if (!IsValid(Actor)) continue;
        if (auto* Player = Cast<AMyProject6Character>(Actor))
        {
            const float Falloff = FMath::Clamp(1.f-FVector::Distance(Origin, Pair.Value[0].ImpactPoint)/Radius, 0.f, 1.f);
            if (!Player->IsEnergyDepleted()) Player->DrainEnergy(FMath::Max(0.f, PlayerEnergyDamage)*Falloff);
        }
        else
        {
            FRadialDamageEvent Event;
            Event.DamageTypeClass = UDamageType::StaticClass();
            Event.Origin = Origin;
            Event.Params = FRadialDamageParams(FMath::Max(0.f, BlastDamage)*FMath::Max(0.f, NPCDamageMultiplier), 0.f, 0.f, Radius, 1.f);
            Event.ComponentHits = MoveTemp(Pair.Value);
            Actor->TakeDamage(Event.Params.BaseDamage, Event, GetInstigatorController(), this);
        }
    }
}

void ABombProjectile::ApplyBlastImpulse(const FVector& Origin)
{
    const float Radius = FMath::Max(1.f, ImpulseRadius);
    if (ImpulseSpeed <= 0.f && ImpulseLift <= 0.f) return;
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BombPressure), false, this);
    GetWorld()->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, FCollisionObjectQueryParams::AllObjects,
        FCollisionShape::MakeSphere(Radius), Query);
    // PhysicsOnly bodies are intentionally absent from scene queries, but still
    // need pressure. The narrow-phase distance check below bounds their effect.
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Components(*It);
        for (UPrimitiveComponent* Component : Components)
            if (!Component->IsQueryCollisionEnabled() && Component->IsAnySimulatingPhysics())
            {
                FOverlapResult& Overlap = Overlaps.AddDefaulted_GetRef();
                Overlap.Component = Component;
            }
    }
    TSet<ACharacter*> Launched;
    TSet<UPrimitiveComponent*> Pushed;
    for (const FOverlapResult& Overlap : Overlaps)
    {
        UPrimitiveComponent* Component = Overlap.GetComponent();
        AActor* Actor = Component ? Component->GetOwner() : nullptr;
        if (!IsValid(Actor) || !Component || Actor == this) continue;
        ACharacter* Character = Cast<ACharacter>(Actor);
        const bool bRagdoll = Character && Character->GetMesh() && Character->GetMesh()->IsAnySimulatingPhysics();
        const bool bLaunch = Character && !bRagdoll;
        if (bLaunch ? Launched.Contains(Character) : (Pushed.Contains(Component) || !Component->IsAnySimulatingPhysics())) continue;
        if (bLaunch) Component = Character->GetCapsuleComponent();
        const FVector Target = bLaunch ? Character->GetActorLocation() : Component->Bounds.Origin;
        const FVector Away = Target-Origin;
        const FVector Closest = Component->IsA<USkeletalMeshComponent>()
            ? Component->Bounds.GetBox().GetClosestPointTo(Origin) : ClosestBlastPoint(Component,Origin);
        const float Falloff = FMath::Clamp(1.f-FVector::Distance(Origin,Closest)/Radius,0.f,1.f);
        if (Falloff <= 0.f) continue;
        if (bLaunch)
        {
            Launched.Add(Character);
            UCharacterMovementComponent* Move = Character->GetCharacterMovement();
            if (!Move || !Move->IsActive() || Move->MovementMode == MOVE_None) continue;
            const FVector Direction = Away.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector);
            const FVector Kick = (Direction*FMath::Max(0.f,ImpulseSpeed)+FVector::UpVector*FMath::Max(0.f,ImpulseLift))*Falloff;
            // Accumulate simultaneous blasts and pre-existing jumps/death launches.
            const FVector Prior = Move->PendingLaunchVelocity.IsNearlyZero() ? Move->Velocity : Move->PendingLaunchVelocity;
            Character->LaunchCharacter(Prior+Kick,true,true);
            if (auto* Reaction = Character->FindComponentByClass<UNPCHitReactionComponent>())
                Reaction->StartBlastTumble(Kick);
        }
        else
        {
            Pushed.Add(Component);
            if (auto* Mesh = Cast<USkeletalMeshComponent>(Component))
            {
                Mesh->ForEachBodyBelow(NAME_None, true, false, [&](FBodyInstance* Body)
                {
                    if (!Body->IsInstanceSimulatingPhysics()) return;
                    FVector BodyPoint = Origin;
                    const float Distance = Body->GetDistanceToBody(Origin, BodyPoint);
                    if (Distance < 0.f) BodyPoint = Body->GetBodyBounds().GetClosestPointTo(Origin);
                    if (Distance == 0.f) BodyPoint = Origin;
                    const float BodyFalloff = FMath::Clamp(1.f-FVector::Distance(Origin,BodyPoint)/Radius, 0.f, 1.f);
                    Body->AddImpulse((Body->GetCOMPosition()-Origin).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector)
                        * FMath::Max(0.f,ImpulseSpeed)*BodyFalloff, true);
                });
            }
            else
            {
                // AddRadialImpulse measures from the centre of mass and silently
                // misses a large body whose surface alone intersects the sphere.
                Component->AddImpulse(Away.GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector)
                    * FMath::Max(0.f,ImpulseSpeed)*Falloff, NAME_None, true);
            }
            if (bRagdoll)
                Component->AddAngularImpulseInRadians(FVector::CrossProduct(FVector::UpVector,Away.GetSafeNormal())*Falloff*4.f,NAME_None,true);
        }
    }
}

void ABombProjectile::DamageStructures(const FVector& Origin)
{
    // Collision cooking and complex-only/query-disabled chunks must not decide
    // whether a destructible wall is damaged. Bounds prune distant actors; each
    // structure then tests its own cells against the world-space sphere once.
    for (TActorIterator<AChunkedDestructibleFloor> It(GetWorld()); It; ++It)
    {
        AChunkedDestructibleFloor* Structure = *It;
        if (!IsValid(Structure) || !Structure->CanBeDamaged()) continue;
        const bool bSculpture = Structure->IsA<ADestructibleRadialSculpture>();
        const float Radius = FMath::Max(1.f, BlastRadius) * (bSculpture ? FMath::Max(1.f, SculptureRadiusMultiplier) : 1.f);
        const FBox Bounds = Structure->GetComponentsBoundingBox(true);
        if (!Bounds.IsValid || Bounds.ComputeSquaredDistanceToPoint(Origin) > FMath::Square(Radius)) continue;
        Structure->DamageCellsInRadius(Origin, Radius,
            FMath::Max(0.f,BlastDamage) * (bSculpture ? FMath::Max(1.f, SculptureDamageMultiplier) : 1.f),
            EChunkedFloorBlastShape::Sphere, false);
    }
}

void ABombProjectile::PreviewEffect(float Seconds, bool bImpact, FVector ViewLocation)
{
    if (!HasActorBegunPlay())
    {
        BombMesh->SetVisibility(!bImpact);
        DrawEffect(FMath::Max(0.f, Seconds), bImpact, ViewLocation);
    }
}

void ABombProjectile::DrawEffect(float Seconds, bool bImpact, FVector ViewLocation)
{
    if (!DynamicMaterial && EffectMaterial)
    {
        DynamicMaterial = UMaterialInstanceDynamic::Create(EffectMaterial, this);
        Effect->SetMaterial(0, DynamicMaterial);
    }
    if (DynamicMaterial) DynamicMaterial->SetScalarParameterValue(TEXT("Age"), Seconds);
    bVisualImpact = bImpact;
    const FVector Normal = GetActorTransform().InverseTransformVectorNoScale(
        (ViewLocation-GetActorLocation()).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector));
    const FVector WorldUp = GetActorTransform().InverseTransformVectorNoScale(FVector::UpVector);
    FVector Right = FVector::CrossProduct(Normal,WorldUp).GetSafeNormal();
    if (Right.IsNearlyZero()) Right = FVector::RightVector;
    const FVector Up = FVector::CrossProduct(Right,Normal).GetSafeNormal();
    constexpr int32 RingSegments=48, Rays=8;
    FDynamicMesh3& Mesh = Effect->GetDynamicMesh()->GetMeshRef();
    const bool bBuild = Mesh.VertexCount() != (bImpact ? (RingSegments+1+Rays)*4 : 28);
    if (bBuild)
    {
        Mesh.Clear(); Mesh.EnableAttributes(); Mesh.Attributes()->EnablePrimaryColors();
    }
    int32 VertexIndex=0;
    auto QuadPoints = [&](const FVector* Points,FVector4f Data)
    {
        const FVector2f UV[]={{0,1},{1,1},{0,0},{1,0}};
        const int32 Base=VertexIndex;
        for (int32 I=0;I<4;++I)
        {
            const int32 ID=VertexIndex++;
            if (bBuild)
            {
                Mesh.AppendVertex(FVector3d(Points[I]));
                Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(Normal));
                Mesh.Attributes()->PrimaryUV()->AppendElement(UV[I]);
                Mesh.Attributes()->PrimaryColors()->AppendElement(Data);
            }
            else
            {
                Mesh.SetVertex(ID,FVector3d(Points[I]));
                Mesh.Attributes()->PrimaryNormals()->SetElement(ID,FVector3f(Normal));
                Mesh.Attributes()->PrimaryColors()->SetElement(ID,Data);
            }
        }
        if (bBuild) for (FIndex3i T:{FIndex3i(Base,Base+1,Base+2),FIndex3i(Base+1,Base+3,Base+2)})
        {
            const int32 ID=Mesh.AppendTriangle(T);
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID,T);
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID,T);
            Mesh.Attributes()->PrimaryColors()->SetTriangle(ID,T);
        }
    };
    auto Quad = [&](FVector Centre,FVector X,FVector Y,float Mode,float Palette,float Alpha)
    {
        const FVector P[]={Centre-X-Y,Centre+X-Y,Centre-X+Y,Centre+X+Y};
        QuadPoints(P,FVector4f(Mode,Palette,1,Alpha));
    };
    if (bImpact)
    {
        const float T=FMath::Max(0.f,Seconds);
        // One fast, compact neon flash: no patterned cloud or confetti layers.
        const float CoreFade=FMath::Exp(-T*8.f);
        const float CoreSize=35.f+135.f*(1.f-FMath::Exp(-T*12.f));
        Quad(FVector::ZeroVector,Right*CoreSize,Up*CoreSize,0,0,CoreFade);
        // A broad ground-hugging pressure front accompanies the plasma body.
        for (int32 Ring=0;Ring<1;++Ring)
        {
            const float RingAge=FMath::Max(0.f,T-Ring*.055f);
            const float Fade=FMath::Clamp(RingAge*22.f,0.f,1.f)*FMath::Square(FMath::Clamp(1.f-RingAge/1.45f,0.f,1.f));
            const float Radius=18.f+570.f*(1.f-FMath::Exp(-RingAge*3.3f));
            const float Width=12.f+14.f*Fade;
            const FVector Axis = Ring==0 ? WorldUp : FVector(Ring==1?.72f:-.32f,Ring==1?.32f:.85f,.62f).GetSafeNormal();
            FVector X,Y; Axis.FindBestAxisVectors(X,Y);
            for (int32 I=0;I<RingSegments;++I)
            {
                const float A=I*UE_TWO_PI/RingSegments, B=(I+1)*UE_TWO_PI/RingSegments;
                const FVector D0=X*FMath::Cos(A)+Y*FMath::Sin(A),D1=X*FMath::Cos(B)+Y*FMath::Sin(B);
                const FVector P[]={D0*(Radius-Width),D1*(Radius-Width),D0*(Radius+Width),D1*(Radius+Width)};
                QuadPoints(P,FVector4f(.33f,Ring==1?1.f:0.f,1,Fade*(I%8==0?.35f:1.f)));
            }
        }
        FRandomStream Random(43817);
        for (int32 I=0;I<Rays;++I)
        {
            const FVector Direction=Random.VRand();
            const float Speed=Random.FRandRange(220.f,540.f);
            const FVector Centre=Direction*(35.f+Speed*T);
            const FVector Tangent=FVector::CrossProduct(Normal,Direction).GetSafeNormal(UE_SMALL_NUMBER,Right);
            const float Fade=FMath::Square(FMath::Clamp(1.f-T/.45f,0.f,1.f));
            Quad(Centre,Direction*(8.f+40.f*T),Tangent*(1.f+1.5f*Fade),.33f,I%4==0?1.f:0.f,Fade);
        }
        FuseLight->SetWorldLocation(GetActorLocation());
        FuseLight->SetLightColor(FLinearColor(.025f,.65f,1.f));
        FuseLight->SetIntensity(32000.f*FMath::Exp(-T/.045f)+6500.f*FMath::Exp(-T/.20f));
        FuseLight->SetAttenuationRadius(850.f);
        FuseLight->SetSourceRadius(35.f);
    }
    else
    {
        const FVector Tip=GetActorTransform().InverseTransformPosition(BombMesh->GetComponentTransform().TransformPosition(FVector(6,0,20)));
        Quad(Tip,Right*3,Up*3,1,0,.8f);
        for (int32 I=0;I<6;++I)
        {
            const float Phase=FMath::Frac(Seconds*4+I/6.f);
            const FVector Drift(FMath::Sin(I*7.f)*Phase*9,FMath::Cos(I*5.f)*Phase*9,Phase*10);
            Quad(Tip+Drift,Right*.8f,Up*.8f,1,0,1.f-Phase);
        }
        FuseLight->SetIntensity(70.f+40.f*FMath::Abs(FMath::Sin(Seconds*36.f)));
        FuseAudio->SetPitchMultiplier(1.f+.4f*FMath::Clamp(Seconds/FMath::Max(.1f,FuseSeconds),0.f,1.f));
    }
    // Additive layers need no sorting; topology is allocated once per phase.
    if (bBuild) Effect->NotifyMeshUpdated();
    else Effect->FastNotifyPositionsUpdated(true,true,false);
}
