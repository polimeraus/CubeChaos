#include "ParametricPlatform.h"
#include "ParametricLobbyPlatformDestructable.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "Materials/MaterialInterface.h"
#include "Components/SceneComponent.h"
#include "UObject/UnrealType.h"

void AParametricPlatform::BeginPlay()
{
    Super::BeginPlay();
    if (PadPathSettings.bSpawnOnBeginPlay) SpawnConfiguredActors();
}

void AParametricPlatform::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearSpawnedActors();
    Super::EndPlay(Reason);
}

float AParametricPlatform::TakeDamage(float Amount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer)
{
    if (!CanDestroyCell(FIntPoint()) || !FMath::IsFinite(Amount) || Amount <= 0) return 0;
    bool Accepted;
    if (Event.IsOfType(FPointDamageEvent::ClassID))
        Accepted = DamageCellFromHit(static_cast<const FPointDamageEvent&>(Event).HitInfo, Amount);
    else if (Event.IsOfType(FRadialDamageEvent::ClassID))
    {
        const auto& Radial = static_cast<const FRadialDamageEvent&>(Event);
        Accepted = ApplyExplosionAtWorldLocation(Radial.Origin, Radial.Params.OuterRadius, Amount) > 0;
    }
    else Accepted = DamageCellAtWorldLocation(GetActorLocation(), Amount);
    if (Accepted) Super::TakeDamage(Amount, Event, EventInstigator, Causer);
    return Accepted ? Amount : 0;
}

TArray<FName> AParametricPlatform::GetPadIds() const
{
    TArray<FName> Result;
    // Keep authored order, not hash map iteration order.
    for (const auto& Pad : PadPathSettings.Pads)
        if (const auto* Resolved = ResolvedPads.Find(Pad.Id); Resolved && Resolved->Show) Result.Add(Pad.Id);
    return Result;
}

bool AParametricPlatform::GetPadWorldCenter(FName Id, FVector& Center) const
{
    Center = FVector::ZeroVector;
    const auto* Pad = ResolvedPads.Find(Id);
    if (!Pad || !Pad->Show) return false;
    Center = GetActorTransform().TransformPosition(FVector(Pad->Center.X, Pad->Center.Y, 0));
    return true;
}

bool AParametricPlatform::GetPadSpawnTransform(FName Id, FTransform& Transform) const
{
    Transform = FTransform::Identity;
    const auto* Pad = ResolvedPads.Find(Id);
    if (!Pad || !Pad->Show || !PadPathSettings.Pads.IsValidIndex(Pad->Index)) return false;
    const auto& Settings = PadPathSettings.Pads[Pad->Index];
    if (Settings.SpawnOffset.ContainsNaN() || Settings.SpawnRotation.ContainsNaN() || Settings.SpawnScale.ContainsNaN()) return false;
    Transform = FTransform(Settings.SpawnRotation, FVector(Pad->Center.X, Pad->Center.Y, 0) + Settings.SpawnOffset, Settings.SpawnScale) * GetActorTransform();
    return true;
}

bool AParametricPlatform::IsPadSurfaceIntact(FName Id) const
{
    const auto* Pad = ResolvedPads.Find(Id);
    if (!Pad || !Pad->Show || !GetPadSurfacePolygon().Contains(Pad->Center)) return false;
    // Solid platforms deliberately have no destruction grid, but their pads still
    // provide valid spawn sockets. Cellular mode additionally checks support damage.
    if (PlatformMode == EPlatformMode::Solid) return true;
    FVector Center; FIntPoint Cell; FChunkedFloorCellInfo Info;
    return GetPadWorldCenter(Id, Center) && WorldLocationToCell(Center, Cell) &&
        GetCellInfo(Cell, Info) && Info.bExists && !Info.bPendingDestruction;
}

AActor* AParametricPlatform::GetSpawnedActorAtPad(FName Id) const
{
    const auto* Actor = SpawnedPadActors.Find(Id);
    return Actor && IsValid(Actor->Get()) ? Actor->Get() : nullptr;
}

AActor* AParametricPlatform::SpawnActorAtPad(FName Id, TSubclassOf<AActor> ActorClass)
{
    if (!GetWorld() || !GetWorld()->IsGameWorld() || !HasAuthority() ||
        FloorPattern != EChunkedFloorPattern::CirclesAndPaths || !PadPathSettings.bEnableSpawning) return nullptr;
    const auto* Pad = ResolvedPads.Find(Id);
    if (!Pad || !Pad->Show || !PadPathSettings.Pads.IsValidIndex(Pad->Index) || !IsPadSurfaceIntact(Id)) return nullptr;
    const auto& Settings = PadPathSettings.Pads[Pad->Index];
    if (!Settings.bSpawnActor) return nullptr;
    if (AActor* Existing = GetSpawnedActorAtPad(Id)) return Existing;
    UClass* Class = ActorClass ? ActorClass.Get() : Settings.ActorClass.Get();
    if (!Class || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) ||
        Class->IsChildOf(AParametricPlatform::StaticClass()) || Class->IsChildOf(AParametricLobbyPlatformDestructable::StaticClass())) return nullptr;
    FTransform Transform;
    if (!GetPadSpawnTransform(Id, Transform)) return nullptr;
    // Reserve before FinishSpawning: a spawned actor's BeginPlay can call back into this pad.
    if (SpawnedPadActors.Contains(Id) && !SpawnedPadActors[Id]) return nullptr;
    SpawnedPadActors.Add(Id, nullptr);
    FActorSpawnParameters Params;
    Params.Owner = this; Params.SpawnCollisionHandlingOverride = PadPathSettings.SpawnCollisionHandling;
    Params.bDeferConstruction = true;
    AActor* Spawned = GetWorld()->SpawnActor<AActor>(Class, Transform, Params);
    if (!IsValid(Spawned)) { SpawnedPadActors.Remove(Id); return nullptr; }
    SpawnedPadActors[Id] = Spawned;
    Spawned->FinishSpawning(Transform);
    if (!IsValid(Spawned)) { SpawnedPadActors.Remove(Id); return nullptr; }
    if (PadPathSettings.bAttachSpawnedActors && Spawned->GetRootComponent())
    {
        // Pads can host static-mesh classes as well as characters. A static root cannot
        // attach to this movable platform; change only the newly spawned instance.
        if (GetRootComponent()->Mobility != EComponentMobility::Static &&
            Spawned->GetRootComponent()->Mobility != EComponentMobility::Movable)
            Spawned->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        Spawned->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
    }
    return Spawned;
}

TArray<AActor*> AParametricPlatform::SpawnConfiguredActors()
{
    TArray<AActor*> Result;
    for (FName Id : GetPadIds()) if (AActor* Actor = SpawnActorAtPad(Id, nullptr)) Result.Add(Actor);
    return Result;
}

bool AParametricPlatform::ClearSpawnedActorAtPad(FName Id)
{
    AActor* Actor = GetSpawnedActorAtPad(Id);
    SpawnedPadActors.Remove(Id);
    return Actor && Actor->Destroy();
}

void AParametricPlatform::ClearSpawnedActors()
{
    TArray<FName> Ids; SpawnedPadActors.GetKeys(Ids);
    for (FName Id : Ids) ClearSpawnedActorAtPad(Id);
}

void AParametricPlatform::FinalizeDestructionBatch(double CurrentTime)
{
    Super::FinalizeDestructionBatch(CurrentTime);
    for (const auto& Pair : SpawnedPadActors)
        if (IsValid(Pair.Value) && !IsPadSurfaceIntact(Pair.Key))
            Pair.Value->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}

bool AParametricPlatform::ImportLobbySettings(const AParametricLobbyPlatformDestructable* Lobby)
{
    // Share damage, collision and animation settings, without copying runtime materials or actor state.
    for (TFieldIterator<FProperty> It(AChunkedDestructibleFloor::StaticClass()); It; ++It)
    {
        FProperty* P = *It;
        if (P->GetOwnerClass() != AChunkedDestructibleFloor::StaticClass() || !P->HasAnyPropertyFlags(CPF_Edit) ||
            P->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient) || P->GetFName() == TEXT("FloorMaterials")) continue;
        P->CopyCompleteValue(P->ContainerPtrToValuePtr<void>(this), P->ContainerPtrToValuePtr<void>(Lobby));
    }
    switch (Lobby->Shape)
    {
    case ELobbyPlatformShape::Ellipse: OuterShape = EChunkedDecorativeFloorShape::Ellipse; break;
    case ELobbyPlatformShape::Triangle: OuterShape = EChunkedDecorativeFloorShape::RegularPolygon; PolygonSides = 3; break;
    case ELobbyPlatformShape::Hexagon: OuterShape = EChunkedDecorativeFloorShape::RegularPolygon; PolygonSides = 6; break;
    case ELobbyPlatformShape::RegularPolygon: OuterShape = EChunkedDecorativeFloorShape::RegularPolygon; PolygonSides = Lobby->PolygonSides; break;
    default: OuterShape = EChunkedDecorativeFloorShape::Grid; break;
    }
    ShapeSize = FVector2D(Lobby->Width, Lobby->Depth); Thickness = Lobby->Thickness; TileSize = Lobby->CellSize;
    PlatformMode = EPlatformMode::Cellular; SetCanBeDamaged(Lobby->bDestructible && Lobby->CanBeDamaged());
    FillRule = EChunkedFloorFillRule::StretchCells; bGenerateCollision = Lobby->bEnableCollision;
    bGenerateBottomFaces = true;
    bRoundCorners = Lobby->Shape != ELobbyPlatformShape::Rectangle && Lobby->CornerRadius > 0;
    CornerRadius = Lobby->CornerRadius; CornerSegments = Lobby->CornerSegments; CircleEdgeSegments = FMath::Clamp(Lobby->CornerSegments,4,32)*8;
    bBevelEdges = Lobby->BevelSize > 0; BevelSize = Lobby->BevelSize;
    BorderStyle = Lobby->RimWidth > 0 ? EPlatformBorder::Rim : EPlatformBorder::None; BorderWidth = Lobby->RimWidth;
    BoundaryInset = 0; BorderMaterialIndex = 1; BodyMaterialIndex = 2; PrimaryMaterialIndex = 0; SecondaryMaterialIndex = 2;
    // The existing palette supplies rim/body colors; the existing analytic pattern supplies the top.
    UMaterialInterface* Trim = Lobby->TrimMaterial;
    if (!Trim) Trim = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/GameplayActors/Ground/Materials/M_ParametricGround.M_ParametricGround"));
    FloorMaterials = {Trim,Trim,Trim}; bTintPalette = true;
    PaletteTints = {Lobby->SurfaceColor,Lobby->RimColor,Lobby->BodyColor};
    FloorPattern = EChunkedFloorPattern::CirclesAndPaths;
    auto& Settings = PadPathSettings;
    Settings.BackgroundColor = Lobby->SurfaceColor; Settings.PatternColor = Lobby->YellowColor;
    Settings.Roughness = Lobby->SurfaceRoughness; Settings.Metallic = Lobby->SurfaceMetallic;
    Settings.BackgroundEmission = Lobby->SurfaceEmission; Settings.PatternEmission = Lobby->YellowEmission;
    Settings.bFitPadsInsidePlatform = Lobby->bFitPadsInsidePlatform; Settings.bShowLayout = Lobby->bShowYellowLayout;
    Settings.bEnableSpawning = true; Settings.bSpawnOnBeginPlay = Lobby->bSpawnOnBeginPlay;
    Settings.bAttachSpawnedActors = Lobby->bAttachSpawnedActors; Settings.SpawnCollisionHandling = Lobby->SpawnCollisionHandling;
    Settings.Pads.Reset(); Settings.Paths.Reset();
    for (const auto& From : Lobby->Pads)
    {
        auto& To = Settings.Pads.AddDefaulted_GetRef();
        To.Id=From.Id; To.bEnabled=From.bEnabled; To.bShowCircle=From.bShowCircle;
        To.Position=From.Position; To.Radius=From.Radius; To.RingWidth=From.RingWidth;
        To.Paint=From.Paint==ELobbyPadPaint::Ring ? EPlatformPadPaint::Ring : EPlatformPadPaint::Disc;
        To.bSpawnActor=From.ActorClass!=nullptr; To.ActorClass=From.ActorClass;
        To.SpawnOffset=From.SpawnOffset; To.SpawnRotation=From.SpawnRotation; To.SpawnScale=From.SpawnScale;
    }
    for (const auto& From : Lobby->Paths)
    {
        auto& To = Settings.Paths.AddDefaulted_GetRef();
        To.FromPad=From.FromPad; To.ToPad=From.ToPad; To.bEnabled=From.bEnabled;
        To.Waypoints=From.Waypoints; To.Width=From.Width; To.EdgeInset=From.EdgeInset;
    }
    SideNeon.Mode = EFloorSideNeonMode::Stripe; SideNeon.Scope = EFloorSideNeonScope::AllExposedSides;
    SideNeon.Color = Lobby->CyanColor; SideNeon.Intensity = Lobby->NeonIntensity; SideNeon.BodyColor = Lobby->BodyColor;
    SideNeon.StripePosition = 1-Lobby->NeonLayerPosition; SideNeon.StripeWidth = Thickness*Lobby->NeonLayerThickness;
    GenerateFloor(false);
    return true;
}
