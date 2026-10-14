#include "ParametricPickup.h"
#include "MyProject6Character.h"

#include "Components/BoxComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace
{
    float SafeValue(float Value, float Minimum, float Maximum, float Default)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Default;
    }

    // Each face owns its UV island: six upright icons without wraparound or mirrored text.
    // Normals agree at curved seams even though UVs are intentionally split there.
    struct FPickupMeshBuilder
    {
        FDynamicMesh3 Mesh;
        FPickupMeshBuilder() { Mesh.EnableAttributes(); }

        int32 Vertex(const FVector& Position, const FVector& Normal, const FVector2f& UV)
        {
            const int32 ID = Mesh.AppendVertex(FVector3d(Position));
            Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(Normal));
            Mesh.Attributes()->PrimaryUV()->AppendElement(UV);
            return ID;
        }

        void Triangle(int32 A, int32 B, int32 C)
        {
            // Unreal front faces are clockwise, opposite the cross-product order below.
            const int32 ID = Mesh.AppendTriangle(A, C, B);
            check(ID >= 0);
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID, FIndex3i(A, C, B));
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, FIndex3i(A, C, B));
        }

        void CubeFace(const FVector& Normal, const FVector& Right, const FVector& Up,
            EPickupShape Shape, float Size, float Radius, int32 Steps)
        {
            const int32 Start = Mesh.VertexCount();
            for (int32 Y = 0; Y <= Steps; ++Y)
            {
                for (int32 X = 0; X <= Steps; ++X)
                {
                    const float U = float(X) / Steps;
                    const float V = float(Y) / Steps;
                    FVector P = (Normal * .5 + Right * (U - .5) + Up * (V - .5)) * Size;
                    FVector N = Normal;
                    if (Shape == EPickupShape::Sphere)
                    {
                        N = P.GetSafeNormal();
                        P = N * Size * .5;
                    }
                    else if (Shape == EPickupShape::RoundedCube && Radius > UE_SMALL_NUMBER)
                    {
                        const float Inner = Size * .5f - Radius;
                        const FVector Closest(FMath::Clamp(P.X, -Inner, Inner), FMath::Clamp(P.Y, -Inner, Inner), FMath::Clamp(P.Z, -Inner, Inner));
                        N = (P - Closest).GetSafeNormal();
                        P = Closest + N * Radius;
                    }
                    Vertex(P + FVector(0, 0, Size * .5), N, FVector2f(1 - U, 1 - V));
                }
            }
            for (int32 Y = 0; Y < Steps; ++Y)
            {
                for (int32 X = 0; X < Steps; ++X)
                {
                    const int32 A = Start + Y * (Steps + 1) + X;
                    const int32 B = A + 1;
                    const int32 C = A + Steps + 1;
                    Triangle(A, B, C + 1);
                    Triangle(A, C + 1, C);
                }
            }
        }

        void Pyramid(float Size)
        {
            const float H = Size * .5f;
            const FVector Corners[] = { {-H, -H, 0}, {H, -H, 0}, {H, H, 0}, {-H, H, 0} };
            const FVector Apex(0, 0, Size);
            const float Slant = FMath::Sqrt(Size * Size + H * H);
            const float SideEdge = FMath::Sqrt(Size * Size + 2 * H * H);
            const float InRadius = Size * Slant / (Size + 2 * SideEdge);
            // A circle of radius .5 in UV fits inside each triangular face, without stretching.
            const float UVScale = 1 / (2 * InRadius);
            for (int32 Face = 0; Face < 4; ++Face)
            {
                const FVector A = Corners[Face];
                const FVector B = Corners[(Face + 1) % 4];
                const FVector N = FVector::CrossProduct(B - A, Apex - A).GetSafeNormal();
                const int32 First = Vertex(A, N, FVector2f(.5f + H * UVScale, 1));
                Vertex(B, N, FVector2f(.5f - H * UVScale, 1));
                Vertex(Apex, N, FVector2f(.5f, 1 - Slant * UVScale));
                Triangle(First, First + 1, First + 2);
            }
            const FVector N(0, 0, -1);
            const int32 Start = Vertex(Corners[0], N, FVector2f(0, 1));
            Vertex(Corners[3], N, FVector2f(0, 0));
            Vertex(Corners[2], N, FVector2f(1, 0));
            Vertex(Corners[1], N, FVector2f(1, 1));
            Triangle(Start, Start + 1, Start + 2);
            Triangle(Start, Start + 2, Start + 3);
        }
    };
}

AParametricPickup::AParametricPickup()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    bReplicates = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    MotionPivot = CreateDefaultSubobject<USceneComponent>(TEXT("MotionPivot"));
    MotionPivot->SetupAttachment(RootComponent);
    Body = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("PickupBody"));
    Body->SetupAttachment(MotionPivot);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetGenerateOverlapEvents(false);
    Body->SetCanEverAffectNavigation(false);
    CollectionTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("CollectionTrigger"));
    CollectionTrigger->SetupAttachment(MotionPivot);
    CollectionTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    CollectionTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    CollectionTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    CollectionTrigger->SetGenerateOverlapEvents(true);
    CollectionTrigger->SetCanEverAffectNavigation(false);
    CollectionTrigger->OnComponentBeginOverlap.AddDynamic(this, &AParametricPickup::OnOverlap);
    // Optional while the setup script is creating the material for the first time.
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> DefaultMaterial(TEXT("/Game/Gameplay/Pickups/M_PickupSurface.M_PickupSurface"));
    SurfaceMaterial = DefaultMaterial.Get();
    static ConstructorHelpers::FObjectFinder<USoundBase> DefaultSound(
        TEXT("/Game/Musics/SoundFx/Free_Sounds_Pack/wav/Special_Collectible_9-1.Special_Collectible_9-1"));
    CollectionSound = DefaultSound.Object;
}

void AParametricPickup::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshPickup();
}

void AParametricPickup::RefreshPickup()
{
    const float Width = SafeValue(Size, 1, 10000, 60);
    const float Radius = SafeValue(CornerRadius, 0, Width * .499f, Width * .15f);
    const int32 Steps = Shape == EPickupShape::Cube ? 1 : FMath::Clamp(SurfaceSubdivisions, 4, 32);
    FPickupMeshBuilder Builder;
    if (Shape == EPickupShape::Pyramid)
    {
        Builder.Pyramid(Width);
    }
    else
    {
        Builder.CubeFace(FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Shape, Width, Radius, Steps);
        Builder.CubeFace(FVector(-1, 0, 0), FVector(0, -1, 0), FVector(0, 0, 1), Shape, Width, Radius, Steps);
        Builder.CubeFace(FVector(0, 1, 0), FVector(-1, 0, 0), FVector(0, 0, 1), Shape, Width, Radius, Steps);
        Builder.CubeFace(FVector(0, -1, 0), FVector(1, 0, 0), FVector(0, 0, 1), Shape, Width, Radius, Steps);
        Builder.CubeFace(FVector(0, 0, 1), FVector(1, 0, 0), FVector(0, 1, 0), Shape, Width, Radius, Steps);
        Builder.CubeFace(FVector(0, 0, -1), FVector(1, 0, 0), FVector(0, -1, 0), Shape, Width, Radius, Steps);
    }
    Body->SetMesh(MoveTemp(Builder.Mesh));
    // Also migrate component attachments on existing Blueprint instances.
    Body->AttachToComponent(MotionPivot, FAttachmentTransformRules::KeepRelativeTransform);
    CollectionTrigger->AttachToComponent(MotionPivot, FAttachmentTransformRules::KeepRelativeTransform);
    Body->SetRelativeLocation(FVector(0, 0, -Width * .5));
    CollectionTrigger->SetRelativeLocation(FVector::ZeroVector);
    CollectionTrigger->SetBoxExtent(FVector(Width * .5 + SafeValue(CollectionPadding, 0, 500, 12)));
    RefreshMotion();
    RefreshMaterial();
    OnRep_Collected();
}

bool AParametricPickup::ShouldTickIfViewportsOnly() const
{
    return bPreviewMotionInEditor && Motion != EPickupMotion::Stationary && !bCollected;
}

bool AParametricPickup::CanAnimate() const
{
    return !bCollected && Motion != EPickupMotion::Stationary && GetWorld()
        && (GetWorld()->IsGameWorld() || bPreviewMotionInEditor);
}

void AParametricPickup::RefreshMotion()
{
    MotionElapsed = 0;
    SetActorTickEnabled(CanAnimate());
    ApplyMotion(0);
}

void AParametricPickup::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!CanAnimate())
    {
        SetActorTickEnabled(false);
        if (!bCollected) ApplyMotion(0);
        return;
    }
    if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0) MotionElapsed += DeltaSeconds;
    ApplyMotion(MotionElapsed);
}

void AParametricPickup::ApplyMotion(double ElapsedSeconds)
{
    const double HalfSize = SafeValue(Size, 1, 10000, 60) * .5;
    FVector Position(0, 0, HalfSize);
    FQuat Rotation = FQuat::Identity;
    if (CanAnimate())
    {
        Position.Z += SafeValue(HoverHeight, 0, 10000, 0);
        const double Phase = SafeValue(MotionPhase, 0, 1, 0);
        if (Motion == EPickupMotion::Bob || Motion == EPickupMotion::SpinAndBob)
        {
            const double Cycle = FMath::Fmod(ElapsedSeconds / SafeValue(BobPeriod, .05f, 3600, 2) + Phase, 1.);
            Position.Z += .5 * SafeValue(BobHeight, 0, 10000, 30) * (1 - FMath::Cos(2 * UE_PI * Cycle));
        }
        if (Motion == EPickupMotion::Orbit || Motion == EPickupMotion::OrbitAndSpin)
        {
            const double Cycle = FMath::Fmod(ElapsedSeconds / SafeValue(OrbitPeriod, .05f, 3600, 4) + Phase, 1.);
            const double Radius = SafeValue(OrbitRadius, 0, 10000, 40);
            Position.X += Radius * FMath::Cos(2 * UE_PI * Cycle);
            Position.Y += Radius * FMath::Sin(2 * UE_PI * Cycle);
        }
        if (Motion == EPickupMotion::Spin || Motion == EPickupMotion::SpinAndBob || Motion == EPickupMotion::OrbitAndSpin)
        {
            const FVector Axis = SpinAxis == EPickupSpinAxis::X ? FVector::ForwardVector
                : SpinAxis == EPickupSpinAxis::Y ? FVector::RightVector : FVector::UpVector;
            const double Degrees = FMath::Fmod(ElapsedSeconds * SafeValue(SpinSpeed, -3600, 3600, 90), 360.);
            Rotation = FQuat(Axis, FMath::DegreesToRadians(Degrees));
        }
    }
    // Absolute relative transforms avoid drift and leave the placed actor anchor unchanged.
    MotionPivot->SetRelativeLocationAndRotation(Position, Rotation);
}

void AParametricPickup::RefreshMaterial()
{
    if (!SurfaceMaterial)
    {
        MaterialInstance = nullptr;
        Body->SetMaterial(0, UMaterial::GetDefaultMaterial(MD_Surface));
        return;
    }
    if (!MaterialInstance || MaterialInstance->Parent != SurfaceMaterial)
        MaterialInstance = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
    Body->SetMaterial(0, MaterialInstance);
    // Preserve the material parameter name used by existing material instances.
    MaterialInstance->SetScalarParameterValue(TEXT("Motif"), static_cast<uint8>(Icon));
    MaterialInstance->SetVectorParameterValue(TEXT("IconColor"), IconColor);
    MaterialInstance->SetVectorParameterValue(TEXT("BodyColor"), BodyColor);
    MaterialInstance->SetScalarParameterValue(TEXT("IconScale"), SafeValue(IconScale, .1f, 1, .72f));
    MaterialInstance->SetScalarParameterValue(TEXT("IconRotation"), FMath::IsFinite(IconRotation) ? FMath::Fmod(IconRotation, 360.f) : 0);
    MaterialInstance->SetScalarParameterValue(TEXT("Roughness"), SafeValue(Roughness, 0, 1, .32f));
    MaterialInstance->SetScalarParameterValue(TEXT("Metallic"), SafeValue(Metallic, 0, 1, .3f));
    MaterialInstance->SetScalarParameterValue(TEXT("PulseEnabled"), bPulse ? 1 : 0);
    MaterialInstance->SetScalarParameterValue(TEXT("PulsePeriod"), SafeValue(PulsePeriod, .05f, 3600, 2));
    const float Maximum = SafeValue(MaximumGlow, 0, 100, 6);
    MaterialInstance->SetScalarParameterValue(TEXT("MinimumGlow"), SafeValue(MinimumGlow, 0, Maximum, 0));
    MaterialInstance->SetScalarParameterValue(TEXT("MaximumGlow"), Maximum);
    MaterialInstance->SetScalarParameterValue(TEXT("PulsePhase"), SafeValue(PulsePhase, 0, 1, 0));
}

void AParametricPickup::BeginPlay()
{
    Super::BeginPlay();
    RefreshPickup();
    if (HasAuthority() && bAutoCollect)
        GetWorldTimerManager().SetTimerForNextTick(this, &AParametricPickup::CheckInitialOverlaps);
}

void AParametricPickup::CheckInitialOverlaps()
{
    if (!bAutoCollect || bCollected) return;
    TArray<AActor*> Overlaps;
    CollectionTrigger->GetOverlappingActors(Overlaps, APawn::StaticClass());
    for (AActor* Other : Overlaps)
        if (TryCollect(Cast<APawn>(Other))) break;
}

void AParametricPickup::OnOverlap(UPrimitiveComponent*, AActor* Other, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
    if (bAutoCollect && HasActorBegunPlay()) TryCollect(Cast<APawn>(Other));
}

bool AParametricPickup::TryCollect(APawn* Collector)
{
    if (!HasAuthority() || !HasActorBegunPlay() || bCollected || !IsValid(Collector)
        || (bPlayerOnly && !Collector->IsPlayerControlled()) || !CollectionTrigger->IsOverlappingActor(Collector)) return false;
    bCollected = true; // Set before callbacks to prevent reentrant/double rewards.
    OnRep_Collected();
    ForceNetUpdate();
    // Native collection path covers every placed instance and future spawn, regardless of motif.
    if (AMyProject6Character* Player = Cast<AMyProject6Character>(Collector)) Player->EquipRailgun();
    if (CollectionSound && SoundVolume > 0)
        MulticastCollectionSound(CollectionSound, MotionPivot->GetComponentLocation(),
            SafeValue(SoundVolume, 0, 4, .65f), SafeValue(SoundPitch, .25f, 4, 1), SoundAttenuation);
    OnCollected.Broadcast(this, Collector);
    if (IsValid(this)) ReceiveCollected(Collector);
    // Keep a short replication window before removing the hidden actor.
    if (IsValid(this)) SetLifeSpan(0.2f);
    return true;
}

void AParametricPickup::OnRep_Collected()
{
    SetActorTickEnabled(CanAnimate());
    Body->SetVisibility(!bCollected, true);
    CollectionTrigger->SetCollisionEnabled(bCollected ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
}

void AParametricPickup::MulticastCollectionSound_Implementation(USoundBase* Sound, FVector Location,
    float Volume, float Pitch, USoundAttenuation* Attenuation)
{
    if (GetNetMode() == NM_DedicatedServer || !Sound || Volume <= 0) return;
    // The audio belongs to the world, not the pickup; actor destruction cannot cut it off.
    UGameplayStatics::SpawnSoundAtLocation(GetWorld(), Sound, Location, FRotator::ZeroRotator,
        Volume, Pitch, 0, Attenuation, nullptr, true);
}

void AParametricPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AParametricPickup, bCollected);
}
