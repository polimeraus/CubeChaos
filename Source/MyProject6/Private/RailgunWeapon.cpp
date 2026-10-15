#include "RailgunWeapon.h"
#include "RailgunBeam.h"
#include "BFGProjectile.h"
#include "MyProject6Character.h"
#include "ChunkedDestructibleConeStructure.h"
#include "QuadPanelConeStructure.h"
#include "ChunkedDestructibleFloor.h"
#include "RoundedBoxActor.h"
#include "SimpleHouseActor.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ARailgunWeapon::ARailgunWeapon()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostPhysics;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RailgunRoot"));
    Arms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RailgunArms"));
    Arms->SetupAttachment(RootComponent);
    WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RailgunMesh"));
    WeaponMesh->SetupAttachment(RootComponent);
    Muzzle = CreateDefaultSubobject<USceneComponent>(TEXT("RailgunMuzzle"));
    Muzzle->SetupAttachment(WeaponMesh);
    for (UPrimitiveComponent* Mesh : {static_cast<UPrimitiveComponent*>(Arms), static_cast<UPrimitiveComponent*>(WeaponMesh)})
    {
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->SetOnlyOwnerSee(true);
        Mesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
    }
    Arms->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Rifle(TEXT("/Game/Weapons/Rifle/Meshes/SM_Rifle.SM_Rifle"));
    WeaponMesh->SetStaticMesh(Rifle.Object);
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> RailModel(TEXT("/Game/Weapons/Railgun/Meshes/SM_QuakeRailgun.SM_QuakeRailgun"));
    RailgunModel = RailModel.Get();
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> BFGMesh(TEXT("/Game/Weapons/BFG/Meshes/SM_BFG10K.SM_BFG10K"));
    BFGModel = BFGMesh.Get();
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> BFGSound(TEXT("/Game/Weapons/BFG/Audio/S_BFGFire.S_BFGFire"));
    BFGFireSound = BFGSound.Get();
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Hold(TEXT("/Game/Weapons/Railgun/Animations/A_RailgunHold.A_RailgunHold"));
    HoldAnimation = Hold.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Sound(TEXT("/Game/Weapons/Railgun/Audio/S_RailFire.S_RailFire"));
    FireSound = Sound.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ArmSurface(TEXT("/Game/Weapons/Railgun/Materials/M_RailArms.M_RailArms"));
    ArmsMaterial = ArmSurface.Object;
}

void ARailgunWeapon::Equip(AMyProject6Character* Character)
{
    if (!Character) return;
    Wielder = Character;
    SetOwner(Character);
    SetInstigator(Character);
    AttachToComponent(Character->GetFirstPersonCameraComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
    USkeletalMeshComponent* Original = Character->GetFirstPersonMesh();
    Arms->SetSkeletalMeshAsset(Original->GetSkeletalMeshAsset());
    for (int32 Slot = 0; Slot < Original->GetNumMaterials(); ++Slot)
        Arms->SetMaterial(Slot, ArmsMaterial ? ArmsMaterial.Get() : Original->GetMaterial(Slot));
    Arms->OverrideAnimationData(HoldAnimation, true, true, 0.f, 1.f);
    Arms->TickAnimation(0.f, false);
    Arms->RefreshBoneTransforms();
    // Place the authored two-handed pose by its grip, so both hands stay in the
    // first-person frame, and align the rifle's +Y barrel with camera forward.
    const FTransform GripPose = Arms->GetSocketTransform(TEXT("HandGrip_R"), RTS_Component);
    const FQuat BaseRotation = FRotator(0, -90, 0).Quaternion();
    const FVector BarrelDirection = BaseRotation.RotateVector(GripPose.GetRotation().RotateVector(FVector::YAxisVector));
    const FQuat PoseRotation = FQuat::FindBetweenNormals(BarrelDirection, FVector::ForwardVector) * BaseRotation;
    Arms->SetRelativeRotation(PoseRotation);
    Arms->SetRelativeLocation(FVector(45, 11, -16) - PoseRotation.RotateVector(GripPose.GetLocation()));
    Original->SetVisibility(false);
    Original->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    // Template rifle and mannequin share the authored HandGrip_R socket.
    WeaponMesh->AttachToComponent(Arms, FAttachmentTransformRules::SnapToTargetNotIncludingScale, TEXT("HandGrip_R"));
    Muzzle->SetRelativeLocation(FVector(0, 52, 10));
    RestLocation = GetRootComponent()->GetRelativeLocation();
    ViewMotion = FRailgunViewMotion();
    PreviousVelocity = Character->GetVelocity();
    PreviousAim = Character->GetBaseAimRotation();
    AddTickPrerequisiteComponent(Character->GetCharacterMovement());
}

void ARailgunWeapon::ActivateRailgun()
{
    WeaponMesh->EmptyOverrideMaterials();
    ChargeMaterial = nullptr;
    if (RailgunModel) WeaponMesh->SetStaticMesh(RailgunModel);
    Muzzle->SetRelativeLocation(FVector(0, 68, 10));
    // The rail's broad receiver needs more clearance below the crosshair than the rifle.
    // Move both hands and weapon together, keeping the authored grip intact.
    RestLocation = FVector(10, 3, -9);
    GetRootComponent()->SetRelativeLocation(RestLocation);
    if (!ChargeMaterial)
    {
        const int32 Slot = WeaponMesh->GetMaterialIndex(TEXT("RailEnergy"));
        if (Slot != INDEX_NONE) ChargeMaterial = WeaponMesh->CreateDynamicMaterialInstance(Slot);
    }
}

void ARailgunWeapon::ActivateBFG()
{
    WeaponMesh->EmptyOverrideMaterials();
    ChargeMaterial = nullptr;
    if (BFGModel) WeaponMesh->SetStaticMesh(BFGModel);
    Muzzle->SetRelativeLocation(FVector(0, 64, 12));
    RestLocation = FVector(26, 6, -15);
    GetRootComponent()->SetRelativeLocation(RestLocation);
    const int32 Slot = WeaponMesh->GetMaterialIndex(TEXT("BFGEnergy"));
    if (Slot != INDEX_NONE) ChargeMaterial = WeaponMesh->CreateDynamicMaterialInstance(Slot);
}

bool ARailgunWeapon::FireBFG()
{
    UWorld* World = GetWorld();
    if (!World || !Wielder || !Wielder->bBFGEquipped || !HasAuthority() || Wielder->IsEnergyDepleted()
        || UGameplayStatics::IsGamePaused(this) || World->GetTimeSeconds() + .0001 < NextBFGFireTime) return false;
    UCameraComponent* Camera = Wielder->GetFirstPersonCameraComponent();
    const FVector Eye = Camera->GetComponentLocation();
    FVector Start = GetVisualMuzzleLocation();
    FVector End = Eye + Camera->GetForwardVector() * 50000.f;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BFGAim), false, Wielder);
    Query.AddIgnoredActor(this);
    FHitResult AimHit, CoverHit;
    if (World->LineTraceSingleByChannel(AimHit, Eye, End, ECC_GameTraceChannel1, Query)) End = AimHit.ImpactPoint;
    const bool bMuzzleBlocked = World->SweepSingleByChannel(CoverHit, Eye, Start, FQuat::Identity,
        ECC_GameTraceChannel1, FCollisionShape::MakeSphere(3.f), Query);
    if (bMuzzleBlocked) Start = Eye;
    const FVector Direction = (End - Start).GetSafeNormal(UE_SMALL_NUMBER, Camera->GetForwardVector());
    const FTransform Transform(Direction.Rotation(), Start);
    auto* Projectile = World->SpawnActorDeferred<ABFGProjectile>(ABFGProjectile::StaticClass(), Transform,
        Wielder, Wielder, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Projectile) return false;
    Projectile->DirectDamage = FMath::Max(0.f, BFGDamage);
    Projectile->SplashDamage = FMath::Max(0.f, BFGSplashDamage);
    Projectile->SplashRadius = FMath::Max(1.f, BFGSplashRadius);
    Projectile->FinishSpawning(Transform);
    // A protruding first-person barrel must not spawn a projectile beyond cover.
    if (bMuzzleBlocked) Projectile->Explode(CoverHit);
    NextBFGFireTime = World->GetTimeSeconds() + FMath::Max(.05f, BFGFireInterval);
    ++BFGShotsFired;
    if (BFGFireSound) UGameplayStatics::PlaySound2D(this, BFGFireSound, .65f);
    if (ChargeMaterial) ChargeMaterial->SetScalarParameterValue(TEXT("Emission"), .25f);
    AddFireMotion(.65f);
    return true;
}

bool ARailgunWeapon::Fire()
{
    UWorld* World = GetWorld();
    if (!World || !Wielder || !Wielder->bRailgunEquipped || !HasAuthority() || Wielder->IsEnergyDepleted()
        || UGameplayStatics::IsGamePaused(this) || World->GetTimeSeconds() < NextFireTime) return false;
    UCameraComponent* Camera = Wielder->GetFirstPersonCameraComponent();
    const FVector Start = Camera->GetComponentLocation();
    const FVector Direction = Camera->GetForwardVector();
    FVector End = Start + Direction * FMath::Clamp(Range, 100.f, 100000.f);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RailgunShot), false, Wielder);
    Query.AddIgnoredActor(this);
    // Use the project's projectile channel: NPC capsules and destructible instance meshes block it,
    // while pickups, contact sensors and health widgets do not intercept the shot.
    // Find the crosshair endpoint without letting a pawn stop the rail. Keep damage
    // separate until the muzzle's cover check has accepted the shot.
    FCollisionQueryParams AimQuery = Query;
    FHitResult Hit;
    for (int32 Index = 0; Index < 64; ++Index)
    {
        if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_GameTraceChannel1, AimQuery)) break;
        if (APawn* Pawn = Cast<APawn>(Hit.GetActor())) AimQuery.AddIgnoredActor(Pawn);
        else { End = Hit.ImpactPoint; break; }
    }
    FVector BeamStart = GetVisualMuzzleLocation();
    // Check camera-to-muzzle too, so a barrel protruding into cover cannot shoot through it.
    FHitResult CoverHit;
    bool bHit = false;
    TArray<FHitResult> ShotHits;
    if (World->LineTraceSingleByChannel(CoverHit, Start, BeamStart, ECC_GameTraceChannel1, Query))
    {
        Hit = CoverHit;
        End = Hit.ImpactPoint;
        BeamStart = Start;
        bHit = true;
        ShotHits.Add(Hit);
    }
    else
    {
        // One hit per pawn; penetrate actors but stop at the first solid surface.
        const FVector TraceEnd = End + (End - BeamStart).GetSafeNormal() * .1f;
        for (int32 Index = 0; Index < 64; ++Index)
        {
            if (!World->LineTraceSingleByChannel(Hit, BeamStart, TraceEnd, ECC_GameTraceChannel1, Query)) break;
            ShotHits.Add(Hit);
            if (APawn* Pawn = Cast<APawn>(Hit.GetActor())) Query.AddIgnoredActor(Pawn);
            else { End = Hit.ImpactPoint; bHit = true; break; }
        }
    }
    NextFireTime = World->GetTimeSeconds() + FMath::Max(.1f, FireInterval);
    if (ChargeMaterial) ChargeMaterial->SetScalarParameterValue(TEXT("Emission"), .15f);
    ++ShotsFired;
    LastShotEnd = End;
    for (const FHitResult& ShotHit : ShotHits) ApplyHit(ShotHit, Direction);
    FActorSpawnParameters Spawn;
    Spawn.Owner = Wielder;
    Spawn.Instigator = Wielder;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (ARailgunBeam* Beam = World->SpawnActor<ARailgunBeam>(ARailgunBeam::StaticClass(), BeamStart, FRotator::ZeroRotator, Spawn))
    {
        Beam->Visuals = Visuals;
        Beam->InitializeBeam(BeamStart, End, TrailDuration);
        if (bHit) Beam->SetImpact(End);
        Beam->FaceCamera(Start);
    }
    if (FireSound) UGameplayStatics::PlaySound2D(this, FireSound, .7f);
    AddFireMotion();
    return true;
}

FVector ARailgunWeapon::GetVisualMuzzleLocation() const
{
    if (!Wielder) return Muzzle->GetComponentLocation();
    FMinimalViewInfo View;
    Wielder->GetFirstPersonCameraComponent()->GetCameraView(0.f, View);
    return UGameplayStatics::TransformWorldToFirstPerson(View, Muzzle->GetComponentLocation(), false);
}

void ARailgunWeapon::ApplyHit(const FHitResult& Hit, const FVector& Direction)
{
    AActor* Target = Hit.GetActor();
    if (AQuadPanelConeStructure* QuadCone = Cast<AQuadPanelConeStructure>(Target))
        QuadCone->DestroyConeCellFromHit(Hit);
    else if (AChunkedDestructibleConeStructure* Cone = Cast<AChunkedDestructibleConeStructure>(Target))
        Cone->DestroyConeCellFromHit(Hit);
    else if (AChunkedDestructibleFloor* Floor = Cast<AChunkedDestructibleFloor>(Target))
        Floor->DamageCellFromHit(Hit, FMath::Max(0.f, Damage));
    else if (ARoundedBoxActor* Box = Cast<ARoundedBoxActor>(Target))
        Box->CarveSphereAtWorldLocation(Hit.ImpactPoint, 24.f);
    else if (ASimpleHouseActor* House = Cast<ASimpleHouseActor>(Target); House && House->bEnableDamage)
        House->CarveSphereAtWorldLocation(Hit.ImpactPoint, 24.f);
    else if (Target)
        UGameplayStatics::ApplyPointDamage(Target, FMath::Max(0.f, Damage), Direction, Hit, Wielder->GetController(), this, nullptr);
    if (UPrimitiveComponent* Body = Hit.GetComponent(); Body && Body->IsSimulatingPhysics(Hit.BoneName))
        Body->AddImpulseAtLocation(Direction * 25000.f, Hit.ImpactPoint, Hit.BoneName);
}

void ARailgunWeapon::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!Wielder || DeltaSeconds <= 0.f || Wielder->IsEnergyDepleted()
        || UGameplayStatics::IsGamePaused(this)) return;
    if (ChargeMaterial)
    {
        const double NextShot = Wielder->bBFGEquipped ? NextBFGFireTime : NextFireTime;
        const float Interval = Wielder->bBFGEquipped ? BFGFireInterval : FireInterval;
        const float Charge = 1.f - FMath::Clamp(float(NextShot - GetWorld()->GetTimeSeconds()) / FMath::Max(.05f, Interval), 0.f, 1.f);
        ChargeMaterial->SetScalarParameterValue(TEXT("Emission"), .15f + 2.25f * Charge);
    }
    const FVector Velocity = Wielder->GetVelocity();
    const FRotator Aim = Wielder->GetBaseAimRotation();
    const FRotator AimDelta = (Aim - PreviousAim).GetNormalized();
    const FQuat Heading = FRotator(0, Aim.Yaw, 0).Quaternion();
    const FVector LocalVelocity = Heading.UnrotateVector(Velocity);
    const FVector LocalAcceleration = Heading.UnrotateVector((Velocity - PreviousVelocity) / DeltaSeconds);
    // Ignore discontinuous camera cuts/teleports instead of giving them a large sway impulse.
    const bool bCameraCut = FMath::Abs(AimDelta.Yaw) > 45.f || FMath::Abs(AimDelta.Pitch) > 45.f || DeltaSeconds > .1f;
    const FVector2D LookRate = bCameraCut ? FVector2D::ZeroVector : FVector2D(AimDelta.Yaw, AimDelta.Pitch) / DeltaSeconds;
    ViewMotion.Advance(DeltaSeconds, LocalVelocity, LocalAcceleration, LookRate, Wielder->GetCharacterMovement()->IsFalling());
    PreviousVelocity = Velocity;
    PreviousAim = Aim;
    const float Amount = FMath::Clamp(MotionAmount, 0.f, 1.5f);
    const FQuat Rotation = (ViewMotion.Rotation * Amount).Quaternion();
    // Rotate around the grip, keeping both hands and the rifle together through every layer.
    const FVector GripPivot(45, 11, -16);
    SetActorRelativeLocation(RestLocation + ViewMotion.Location * Amount + GripPivot - Rotation.RotateVector(GripPivot));
    SetActorRelativeRotation(Rotation);
}

void ARailgunWeapon::AddFireMotion(float Strength)
{
    ViewMotion.AddShot(Strength);
}

void ARailgunWeapon::AddLandingMotion(float DownwardSpeed)
{
    ViewMotion.Land(DownwardSpeed);
}
