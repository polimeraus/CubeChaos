#include "RailgunWeapon.h"
#include "RailgunBeam.h"
#include "MyProject6Character.h"
#include "ChunkedDestructibleConeStructure.h"
#include "ChunkedDestructibleFloor.h"
#include "RoundedBoxActor.h"
#include "SimpleHouseActor.h"
#include "Animation/AnimSequence.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

ARailgunWeapon::ARailgunWeapon()
{
    PrimaryActorTick.bCanEverTick = true;
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
    static ConstructorHelpers::FObjectFinder<UAnimSequence> Hold(TEXT("/Game/Weapons/Railgun/A_RailgunHold.A_RailgunHold"));
    HoldAnimation = Hold.Object;
    static ConstructorHelpers::FObjectFinder<USoundBase> Sound(TEXT("/Game/Weapons/Railgun/S_RailFire.S_RailFire"));
    FireSound = Sound.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> ArmSurface(TEXT("/Game/Weapons/Railgun/M_RailArms.M_RailArms"));
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
}

bool ARailgunWeapon::Fire()
{
    UWorld* World = GetWorld();
    if (!World || !Wielder || !HasAuthority() || Wielder->IsEnergyDepleted()
        || UGameplayStatics::IsGamePaused(this) || World->GetTimeSeconds() < NextFireTime) return false;
    UCameraComponent* Camera = Wielder->GetFirstPersonCameraComponent();
    const FVector Start = Camera->GetComponentLocation();
    const FVector Direction = Camera->GetForwardVector();
    FVector End = Start + Direction * FMath::Clamp(Range, 100.f, 100000.f);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(RailgunShot), false, Wielder);
    Query.AddIgnoredActor(this);
    // Use the project's projectile channel: NPC capsules and destructible instance meshes block it,
    // while pickups, contact sensors and health widgets do not intercept the shot.
    FHitResult Hit;
    bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_GameTraceChannel1, Query);
    if (bHit) End = Hit.ImpactPoint;
    FVector BeamStart = GetVisualMuzzleLocation();
    // Check camera-to-muzzle too, so a barrel protruding into cover cannot shoot through it.
    FHitResult CoverHit;
    if (World->LineTraceSingleByChannel(CoverHit, Start, BeamStart, ECC_GameTraceChannel1, Query))
    {
        Hit = CoverHit;
        End = Hit.ImpactPoint;
        BeamStart = Start;
        bHit = true;
    }
    else if (World->LineTraceSingleByChannel(CoverHit, BeamStart, End, ECC_GameTraceChannel1, Query))
    {
        Hit = CoverHit;
        End = Hit.ImpactPoint;
        bHit = true;
    }
    NextFireTime = World->GetTimeSeconds() + FMath::Max(.1f, FireInterval);
    ++ShotsFired;
    LastShotEnd = End;
    if (bHit) ApplyHit(Hit, Direction);
    FActorSpawnParameters Spawn;
    Spawn.Owner = Wielder;
    Spawn.Instigator = Wielder;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (ARailgunBeam* Beam = World->SpawnActor<ARailgunBeam>(ARailgunBeam::StaticClass(), BeamStart, FRotator::ZeroRotator, Spawn))
    {
        Beam->Visuals = Visuals;
        Beam->InitializeBeam(BeamStart, End, TrailDuration);
        Beam->FaceCamera(Start);
    }
    if (FireSound) UGameplayStatics::PlaySound2D(this, FireSound, .7f);
    Recoil = 1.f;
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
    if (AChunkedDestructibleConeStructure* Cone = Cast<AChunkedDestructibleConeStructure>(Target))
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
    Recoil = FMath::FInterpTo(Recoil, 0.f, DeltaSeconds, 13.f);
    SetActorRelativeLocation(RestLocation + FVector(-4.f * Recoil, 0, 1.2f * Recoil));
    SetActorRelativeRotation(FRotator(2.f * Recoil, 0, 0));
}
