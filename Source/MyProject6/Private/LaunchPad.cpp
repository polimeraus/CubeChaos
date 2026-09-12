#include "LaunchPad.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ALaunchPad::ALaunchPad()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Base = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SquareBase"));
    Base->SetupAttachment(RootComponent);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
    Base->SetStaticMesh(Cube.Object);
    Base->SetCollisionProfileName(TEXT("BlockAll"));
    Symbol = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AnimatedArrows"));
    Symbol->SetupAttachment(RootComponent);
    Symbol->SetStaticMesh(Plane.Object);
    Symbol->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Symbol->SetGenerateOverlapEvents(false);
    Symbol->SetCanEverAffectNavigation(false);
    Symbol->SetCastShadow(false);
    Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("TopTrigger"));
    Trigger->SetupAttachment(RootComponent);
    Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    Trigger->SetGenerateOverlapEvents(true);
    Trigger->SetCanEverAffectNavigation(false);
    Trigger->OnComponentBeginOverlap.AddDynamic(this, &ALaunchPad::OnEnter);
    Trigger->OnComponentEndOverlap.AddDynamic(this, &ALaunchPad::OnLeave);
}

void ALaunchPad::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshPad();
}
void ALaunchPad::RefreshPad()
{
    const float Width = FMath::IsFinite(Size) ? FMath::Clamp(Size, 50.f, 1000.f) : 160.f;
    Base->SetRelativeLocation(FVector(0, 0, 4));
    Base->SetRelativeScale3D(FVector(Width / 100, Width / 100, .08f));
    Symbol->SetRelativeLocation(FVector(0, 0, 8.5f));
    Symbol->SetRelativeScale3D(FVector(Width / 100, Width / 100, 1));
    Trigger->SetRelativeLocation(FVector(0, 0, 23));
    Trigger->SetBoxExtent(FVector(Width / 2, Width / 2, 15));
    if (PadMaterial && (!MaterialInstance || MaterialInstance->Parent != PadMaterial))
        MaterialInstance = UMaterialInstanceDynamic::Create(PadMaterial, this);
    if (MaterialInstance)
    {
        Symbol->SetMaterial(0, MaterialInstance);
        MaterialInstance->SetVectorParameterValue(TEXT("Tint"), Color);
        MaterialInstance->SetScalarParameterValue(TEXT("Brightness"), bEnabled ? FMath::Clamp(Brightness, 0.f, 30.f) : .15f);
        MaterialInstance->SetScalarParameterValue(TEXT("AnimationSpeed"), bEnabled ? FMath::Clamp(AnimationSpeed, 0.f, 10.f) : 0);
    }
}
void ALaunchPad::BeginPlay()
{
    Super::BeginPlay();
    RefreshPad();
    // Also detect characters initially placed on the pad.
    if (HasAuthority()) GetWorldTimerManager().SetTimer(CheckTimer, this, &ALaunchPad::CheckCharacters, .02f, true);
}
void ALaunchPad::EndPlay(const EEndPlayReason::Type Reason)
{
    GetWorldTimerManager().ClearTimer(CheckTimer);
    Super::EndPlay(Reason);
}
void ALaunchPad::OnEnter(UPrimitiveComponent*, AActor*, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    if (!HasAuthority() || !HasActorBegunPlay()) return;
    if (!GetWorldTimerManager().IsTimerActive(CheckTimer))
        GetWorldTimerManager().SetTimer(CheckTimer, this, &ALaunchPad::CheckCharacters, .02f, true);
    CheckCharacters();
}
void ALaunchPad::OnLeave(UPrimitiveComponent*, AActor*, UPrimitiveComponent*, int32)
{
    // The next check stops the timer once the volume is empty.
}
void ALaunchPad::CheckCharacters()
{
    TArray<AActor*> Overlaps;
    Trigger->GetOverlappingActors(Overlaps, ACharacter::StaticClass());
    if (Overlaps.IsEmpty()) GetWorldTimerManager().ClearTimer(CheckTimer);
    const double Now = GetWorld()->GetTimeSeconds();
    for (auto It = LastLaunch.CreateIterator(); It; ++It)
        if (!It.Key().IsValid() || Now - It.Value() > FMath::Max(Cooldown, .1f)) It.RemoveCurrent();
    if (!bEnabled || !HasAuthority() || !FMath::IsFinite(LaunchUpSpeed) || !FMath::IsFinite(ForwardSpeed)) return;
    const float Top = Base->Bounds.GetBox().Max.Z;
    for (AActor* Actor : Overlaps)
    {
        ACharacter* Character = Cast<ACharacter>(Actor);
        if (!Character || (Character->IsPlayerControlled() ? !bAffectPlayers : !bAffectNPCs)) continue;
        auto* Movement = Character->GetCharacterMovement();
        if (!Movement || !Movement->IsActive() || Movement->MovementMode == MOVE_None) continue;
        // A capsule intersecting the side/underside must not activate the pad.
        const float Feet = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        if (Feet < Top - 5 || Feet > Top + 30 || Movement->Velocity.Z > 50 || !Movement->PendingLaunchVelocity.IsNearlyZero()) continue;
        const double* Previous = LastLaunch.Find(Character);
        if (Previous && Now - *Previous < FMath::Max(.1f, Cooldown)) continue;
        const FVector Horizontal = FRotator(0, GetActorRotation().Yaw, 0).Vector() * FMath::Clamp(ForwardSpeed, 0.f, 10000.f);
        LastLaunch.Add(Character, Now);
        Character->LaunchCharacter(Horizontal + FVector(0, 0, FMath::Clamp(LaunchUpSpeed, 0.f, 10000.f)), !bPreserveHorizontalVelocity, true);
        if (MaterialInstance) MaterialInstance->SetScalarParameterValue(TEXT("SpawnTime"), Now);
    }
}
