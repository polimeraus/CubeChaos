#include "ParametricPlatformCity.h"
#include "ParametricPlatform.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

AParametricPlatformCity::AParametricPlatformCity()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CityOrigin"));
    PlatformClass = AParametricPlatform::StaticClass();
    DistrictColors = {FLinearColor(.13f, .32f, .39f), FLinearColor(.26f, .36f, .25f),
        FLinearColor(.39f, .25f, .15f), FLinearColor(.26f, .22f, .39f)};
}

void AParametricPlatformCity::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
#if WITH_EDITOR
    if (bPreviewOnPlacement && GetWorld() && !GetWorld()->IsGameWorld() &&
        !IsTemplate() && !bGenerating && GetPlatforms().IsEmpty()) GenerateWithSeed(Seed);
#endif
}

#if WITH_EDITOR
void AParametricPlatformCity::PostEditChangeProperty(FPropertyChangedEvent& Event)
{
    const FProperty* Changed = Event.MemberProperty ? Event.MemberProperty : Event.Property;
    const FName Member = Changed ? Changed->GetFName() : NAME_None;
    Super::PostEditChangeProperty(Event);
    if (bAutoRebuildInEditor && !IsTemplate() && !bGenerating && GetWorld() && !GetWorld()->IsGameWorld() &&
        Event.ChangeType != EPropertyChangeType::Interactive &&
        (Member == GET_MEMBER_NAME_CHECKED(AParametricPlatformCity, Rules) ||
         Member == GET_MEMBER_NAME_CHECKED(AParametricPlatformCity, Seed) ||
         Member == GET_MEMBER_NAME_CHECKED(AParametricPlatformCity, DistrictColors) ||
         Member == GET_MEMBER_NAME_CHECKED(AParametricPlatformCity, PlatformClass))) GenerateWithSeed(Seed);
}
#endif

void AParametricPlatformCity::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority() && bGenerateOnBeginPlay)
    {
        if (bRandomizeOnBeginPlay) GenerateNewLayout();
        else GenerateWithSeed(Seed);
    }
}

void AParametricPlatformCity::DestroyPlatforms(TArray<TObjectPtr<AParametricPlatform>>& Actors)
{
    auto Old = MoveTemp(Actors); Actors.Reset();
    for (auto& Actor : Old)
        if (IsValid(Actor) && Actor->GetOwner() == this && Actor->ActorHasTag(TEXT("PlatformCity.Generated")))
        { Actor->Modify(); Actor->Destroy(); }
}

void AParametricPlatformCity::Destroyed()
{
    DestroyPlatforms(Platforms);
    Super::Destroyed();
}

void AParametricPlatformCity::ClearGenerated()
{
    if (IsTemplate() || bGenerating || !HasAuthority()) return;
    Modify(); DestroyPlatforms(Platforms); GeneratedPlan = FPlatformCityPlan();
    LastError.Reset(); GenerationReport = TEXT("Generated platforms cleared.");
}

TArray<AParametricPlatform*> AParametricPlatformCity::GetPlatforms() const
{
    TArray<AParametricPlatform*> Result;
    for (auto& Platform : Platforms)
        if (IsValid(Platform) && !Platform->IsActorBeingDestroyed() && Platform->GetOwner() == this) Result.Add(Platform);
    return Result;
}

void AParametricPlatformCity::RandomizePreview() { GenerateNewLayout(); }
void AParametricPlatformCity::RebuildPreview() { GenerateWithSeed(Seed); }
bool AParametricPlatformCity::GenerateNewLayout()
{
    int32 NewSeed = int32(FGuid::NewGuid().A);
    if (NewSeed == Seed) NewSeed ^= 0x13579BDF;
    return GenerateWithSeed(NewSeed);
}

bool AParametricPlatformCity::GenerateWithSeed(int32 InSeed)
{
    if (IsTemplate() || !GetWorld() || bGenerating || !HasAuthority()) return false;
    const FVector Scale = GetActorScale3D(); const FRotator Rotation = GetActorRotation();
    if (GetActorTransform().ContainsNaN() || !Scale.Equals(FVector::OneVector, .001) ||
        !FMath::IsNearlyZero(Rotation.Pitch, .01) || !FMath::IsNearlyZero(Rotation.Roll, .01))
    { LastError = TEXT("Use scale 1,1,1 and yaw rotation only; edit Width/Height under City | Layout."); return false; }
    if (!PlatformClass || PlatformClass->HasAnyClassFlags(CLASS_Abstract))
    { LastError = TEXT("PlatformClass must be a concrete ParametricPlatform class."); return false; }
    FPlatformCityPlan Plan;
    if (!PlatformCity::MakePlan(Rules, InSeed, Plan, LastError)) return false;
    TGuardValue<bool> Generating(bGenerating, true);
    TArray<TObjectPtr<AParametricPlatform>> Staged;
    auto Fail = [&](const FString& Reason) { LastError = Reason; DestroyPlatforms(Staged); return false; };
    for (int32 I = 0; I < Plan.Plots.Num(); ++I)
    {
        const auto& Plot = Plan.Plots[I];
        const FTransform World = FTransform(FVector(Plot.Center, 0)) * GetActorTransform();
        FActorSpawnParameters Params;
        Params.Owner = this; Params.OverrideLevel = GetLevel(); Params.bDeferConstruction = true;
        Params.bAllowDuringConstructionScript = true; Params.ObjectFlags |= RF_Transactional;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Platform = GetWorld()->SpawnActor<AParametricPlatform>(PlatformClass, World, Params);
        if (!Platform) return Fail(TEXT("Could not spawn a platform; previous layout retained."));
        Staged.Add(Platform); Platform->Tags.Add(TEXT("PlatformCity.Generated"));
        Platform->PlatformMode = EPlatformMode::Solid;
        Platform->OuterShape = EChunkedDecorativeFloorShape::DynamicPolygon;
        Platform->PolygonOutline = Plot.SourceOutline; Platform->ShapeSize = Plot.Size;
        Platform->Thickness = Rules.Thickness; Platform->TileSize = FVector2D(100, 100);
        Platform->bRoundCorners = true; Platform->CornerRadius = Rules.CornerRadius;
        Platform->CornerSegments = Rules.CornerSegments;
        Platform->bBevelEdges = false;
        Platform->FloorPattern = EChunkedFloorPattern::Solid;
        Platform->BorderStyle = EPlatformBorder::InsetBand;
        Platform->BoundaryInset = 14; Platform->BorderWidth = 9;
        Platform->PrimaryMaterialIndex = 0; Platform->SecondaryMaterialIndex = 1;
        Platform->BodyMaterialIndex = 2; Platform->bTintPalette = true;
        const FLinearColor Color = DistrictColors.IsEmpty() ? FLinearColor(.15f, .3f, .4f) : DistrictColors[Plot.District % DistrictColors.Num()];
        Platform->PaletteTints = {Color, Color * 1.5f + FLinearColor(.08f, .08f, .08f), Color * .35f};
        Platform->bReactToMusic = false; Platform->bGenerateCollision = true;
        Platform->FinishSpawning(World);
        Platform->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        Platform->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
        TArray<FVector2D> Outline; Platform->GetPlatformOutline(Outline);
        if (Platform->GetPlatformMeshCount() < 1 || Outline.Num() < 3 || !Platform->ShapeWarning.IsEmpty())
            return Fail(TEXT("Platform geometry failed: ") + Platform->ShapeWarning);
        for (const auto& Point : Outline)
            if (FMath::Abs(Point.X) > Plot.Size.X * .5 + .1 || FMath::Abs(Point.Y) > Plot.Size.Y * .5 + .1)
                return Fail(TEXT("A platform shape exceeded its reserved plot."));
#if WITH_EDITOR
        if (!GetWorld()->IsGameWorld())
        {
            Platform->SetActorLabel(FString::Printf(TEXT("CityPlate_%02d_%03d"), Plot.District + 1, I + 1));
            Platform->SetFolderPath(FName(*FString::Printf(TEXT("PlatformCities/%s"), *GetActorLabel())));
        }
#endif
    }
    Modify(); DestroyPlatforms(Platforms); Platforms = MoveTemp(Staged);
    AppliedRules = Rules; GeneratedPlan = MoveTemp(Plan); Seed = InSeed; ++GenerationSerial;
    GenerationReport = FString::Printf(TEXT("%d dynamic polygon platforms | %.0f x %.0f cm | %d vertical + %d horizontal main roads, %d side roads | seed %d"),
        Platforms.Num(), Rules.Width, Rules.Height, Rules.VerticalRoads, Rules.HorizontalRoads,
        GeneratedPlan.Roads.Num() - Rules.VerticalRoads - Rules.HorizontalRoads, Seed);
    LastError.Reset(); return true;
}

namespace
{
    bool CircleFits(const TArray<FVector2D>& Outline, FVector2D Point, double Radius)
    {
        bool Inside = false;
        for (int32 I = 0, J = Outline.Num() - 1; I < Outline.Num(); J = I++)
        {
            const FVector2D A = Outline[I], B = Outline[J], Edge = B - A;
            if ((A.Y > Point.Y) != (B.Y > Point.Y) &&
                Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X) Inside = !Inside;
            const double T = FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / FMath::Max(Edge.SizeSquared(), .000001), 0., 1.);
            if ((Point - A - Edge * T).SizeSquared() < Radius * Radius) return false;
        }
        return Inside;
    }
}

bool AParametricPlatformCity::FindSpawnTransform(float FootprintRadius, float EdgeMargin, int32 SamplingSeed,
    FTransform& SurfaceTransform, AParametricPlatform*& Platform) const
{
    Platform = nullptr; SurfaceTransform = FTransform::Identity;
    if (!FMath::IsFinite(FootprintRadius) || !FMath::IsFinite(EdgeMargin) || FootprintRadius <= 0 || EdgeMargin < 0) return false;
    const auto Available = GetPlatforms(); if (Available.IsEmpty()) return false;
    FRandomStream Random(SamplingSeed);
    const int32 Start = Random.RandRange(0, Available.Num() - 1);
    for (int32 I = 0; I < Available.Num(); ++I)
    {
        auto* Candidate = Available[(Start + I) % Available.Num()];
        // Generated shapes have no holes. Reject edited/damaged plates rather than sampling their voids.
        if (Candidate->PlatformMode != EPlatformMode::Solid || Candidate->GetPlatformHoleCount() != 0) continue;
        const FVector Scale = Candidate->GetActorScale3D();
        if (Scale.GetMin() <= 0 || !FMath::IsNearlyEqual(Scale.X, Scale.Y, .001)) continue;
        const double Radius = (FootprintRadius + EdgeMargin) / Scale.X;
        TArray<FVector2D> Outline; Candidate->GetPlatformOutline(Outline);
        const FVector2D Size = Candidate->GetPlatformSize();
        if (Size.X < Radius * 2 || Size.Y < Radius * 2) continue;
        for (int32 Attempt = 0; Attempt < 80; ++Attempt)
        {
            const FVector2D Point(Random.FRandRange(-Size.X * .5 + Radius, Size.X * .5 - Radius),
                Random.FRandRange(-Size.Y * .5 + Radius, Size.Y * .5 - Radius));
            if (!CircleFits(Outline, Point, Radius)) continue;
            SurfaceTransform = FTransform(Candidate->GetActorQuat(), Candidate->GetActorTransform().TransformPosition(FVector(Point, 0)));
            Platform = Candidate; return true;
        }
    }
    return false;
}
