#include "ProceduralCityGenerator.h"
#include "ParametricCityLink.h"
#include "ParametricLobbyPlatform.h"
#include "ParametricGround.h"
#include "ChunkedDestructibleRoundTemple.h"
#include "ChunkedDestructibleHouse.h"
#include "ChunkedDestructiblePolygonTower.h"
#include "ChunkedDestructibleSpireTemple.h"
#include "ParametricFantasyHouse.h"
#include "Components/SceneComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

namespace CityCatalog
{
FBox Bounds(AActor* Actor)
{
    FBox Box(ForceInit);TArray<UMeshComponent*> Components;Actor->GetComponents<UMeshComponent>(Components,true);
    for (auto* C:Components) if (C->IsRegistered())
        Box+=C->CalcBounds(C->GetComponentTransform().GetRelativeTransform(Actor->GetActorTransform())).GetBox();
    return Box;
}
double Radius(const FBox& Box,FVector Offset)
{
    const double X=FMath::Max(FMath::Abs(Box.Min.X+Offset.X),FMath::Abs(Box.Max.X+Offset.X));
    const double Y=FMath::Max(FMath::Abs(Box.Min.Y+Offset.Y),FMath::Abs(Box.Max.Y+Offset.Y));
    return FMath::Sqrt(X*X+Y*Y);
}
}

AProceduralCityGenerator::AProceduralCityGenerator()
{
    PrimaryActorTick.bCanEverTick=false;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("CityOrigin"));
    PlatformClass=AParametricLobbyPlatform::StaticClass();LinkClass=AParametricCityLink::StaticClass();
    DistrictColors={FLinearColor(.02f,.65f,1),FLinearColor(.65f,.035f,.9f),FLinearColor(1,.22f,.035f),FLinearColor(.08f,.75f,.45f)};
    auto Add=[&](const TCHAR* Id,UClass* C,float Radius,float Min,float Max,int32 Cells)
    {
        auto& E=BuildingCatalog.AddDefaulted_GetRef();E.Id=Id;E.ActorClass=C;E.FootprintRadius=Radius;E.MinScale=Min;E.MaxScale=Max;E.EstimatedCells=Cells;
    };
    Add(TEXT("RoundTemple"),AChunkedDestructibleRoundTemple::StaticClass(),1900,.45f,.65f,5510);
    Add(TEXT("House"),AChunkedDestructibleHouse::StaticClass(),1100,.65f,.85f,2500);
    Add(TEXT("PolygonTower"),AChunkedDestructiblePolygonTower::StaticClass(),2800,.3f,.45f,10000);
    Add(TEXT("FantasyHouse"),AParametricFantasyHouse::StaticClass(),1000,.7f,1,1000);
    Add(TEXT("SpireTemple"),AChunkedDestructibleSpireTemple::StaticClass(),2700,.3f,.4f,12000);
}
void AProceduralCityGenerator::OnConstruction(const FTransform& T)
{
    Super::OnConstruction(T);
#if WITH_EDITOR
    if (bAutoRebuildInEditor && GetWorld() && !GetWorld()->IsGameWorld() && !IsTemplate() && !bGenerating) RebuildSameSeed();
#endif
}
void AProceduralCityGenerator::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority() && bGenerateOnBeginPlay)
    {
        if (bNewSeedOnBeginPlay) GenerateNewSeed();else RebuildSameSeed();
        if (bMovePlayersToEntryOnRegenerate)
            GetWorldTimerManager().SetTimerForNextTick(this,&AProceduralCityGenerator::MovePlayersToEntry);
    }
}
void AProceduralCityGenerator::Destroyed()
{
    DestroyOwned(GeneratedActors);Super::Destroyed();
}
void AProceduralCityGenerator::DestroyOwned(TArray<TObjectPtr<AActor>>& Actors)
{
    TArray<TObjectPtr<AActor>> Old=MoveTemp(Actors);Actors.Reset();
    for (auto& A:Old) if (IsValid(A) && A->GetOwner()==this && A->ActorHasTag(TEXT("ProceduralCity.Generated"))) A->Destroy();
}
void AProceduralCityGenerator::ClearGenerated()
{
    if (IsTemplate() || bGenerating || !HasAuthority()) return;
    Modify();DestroyOwned(GeneratedActors);GeneratedPlan=FProceduralCityPlan();GeneratedActorCount=0;GenerationReport=TEXT("Generated city cleared.");LastError.Reset();
}
TArray<AActor*> AProceduralCityGenerator::GetGeneratedActors() const
{
    TArray<AActor*> Result;for (auto A:GeneratedActors) if (IsValid(A) && A->GetOwner()==this) Result.Add(A);return Result;
}
bool AProceduralCityGenerator::CheckTransform()
{
    const FVector S=GetActorScale3D();const FRotator R=GetActorRotation();
    if (S.ContainsNaN() || !FMath::IsNearlyEqual(S.X,S.Y,.001) || !FMath::IsNearlyEqual(S.X,S.Z,.001) || S.X<=0 ||
        !FMath::IsNearlyZero(R.Pitch,.01) || !FMath::IsNearlyZero(R.Roll,.01))
    {LastError=TEXT("Use positive uniform scale and yaw rotation only; slope/clearance rules assume an upright city.");return false;}
    return true;
}
bool AProceduralCityGenerator::CheckCatalogClasses()
{
    for (const auto& C:BuildingCatalog)
        if (C.ActorClass && (C.ActorClass->IsChildOf(StaticClass()) || C.ActorClass->HasAnyClassFlags(CLASS_Abstract)))
        {LastError=TEXT("Catalog entries must be concrete building classes, never city generators: ")+C.Id.ToString();return false;}
    return true;
}
bool AProceduralCityGenerator::ValidateCurrentSettings()
{
    FProceduralCityPlan Candidate;
    if (!CheckTransform() || !CheckCatalogClasses() || !ProceduralCity::MakePlan(Rules,Seed,BuildingCatalog,ExclusionZones,Candidate,LastError)) return false;
    GenerationReport=FString::Printf(TEXT("Settings valid: %d districts, %d connections, %d buildings. %s\n%s"),Candidate.Districts.Num(),Candidate.Connections.Num(),Candidate.Buildings.Num(),*Candidate.Signature,*Candidate.Notes);
    return true;
}
bool AProceduralCityGenerator::RebuildSameSeed(){return GenerateWithSeed(Seed);}
void AProceduralCityGenerator::BuildPreview(){RebuildSameSeed();}
void AProceduralCityGenerator::RandomizePreview(){GenerateNewSeed();}
void AProceduralCityGenerator::CheckPreview(){ValidateCurrentSettings();}
void AProceduralCityGenerator::ImportRules(){LoadRulesFromFile();}
void AProceduralCityGenerator::MeasureCatalog(){CalibrateCatalogFootprints();}
bool AProceduralCityGenerator::GenerateNewSeed()
{
    int32 NewSeed=int32(FGuid::NewGuid().A);if (NewSeed==Seed) NewSeed^=0x13579BDF;
    return GenerateWithSeed(NewSeed);
}
void AProceduralCityGenerator::ApplyLayoutPreset(ECityLayout Layout)
{
    Rules=FProceduralCityRules();Rules.Layout=Layout;
    if (Layout==ECityLayout::Grid){Rules.DistrictCount=9;Rules.RoundPlatformChance=.2f;Rules.Curvature=0;Rules.PositionJitter=.04f;}
    if (Layout==ECityLayout::Organic){Rules.DistrictCount=7;Rules.RoundPlatformChance=1;Rules.Curvature=.18f;Rules.ExtraConnectionChance=.65f;}
}
FTransform AProceduralCityGenerator::GetEntryTransform() const
{
    return FTransform(FRotator(0,GetActorRotation().Yaw+90,0),GetActorTransform().TransformPosition(GeneratedPlan.EntryLocation));
}
void AProceduralCityGenerator::MovePlayersToEntry()
{
    if (!bMovePlayersToEntryOnRegenerate || GeneratedPlan.Districts.IsEmpty() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
    const FTransform Entry=GetEntryTransform();int32 Index=0;
    for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if (auto* PC=It->Get()) if (APawn* Pawn=PC->GetPawn())
        {Pawn->SetActorLocationAndRotation(Entry.GetLocation()+FVector(100*Index++,0,0),Entry.Rotator(),false,nullptr,ETeleportType::TeleportPhysics);PC->SetControlRotation(Entry.Rotator());}
}

bool AProceduralCityGenerator::GenerateWithSeed(int32 InSeed)
{
    if (IsTemplate() || !GetWorld() || bGenerating || !HasAuthority()) return false;
    if (!CheckTransform() || !CheckCatalogClasses()) return false;
    if (!PlatformClass || !LinkClass){LastError=TEXT("PlatformClass and LinkClass are required.");return false;}
    FProceduralCityPlan Plan;
    if (!ProceduralCity::MakePlan(Rules,InSeed,BuildingCatalog,ExclusionZones,Plan,LastError)) return false;
    bGenerating=true;ON_SCOPE_EXIT{bGenerating=false;};
    TArray<TObjectPtr<AActor>> Staged;
    auto Spawn=[&](UClass* Class,const FTransform& Local)->AActor*
    {
        FActorSpawnParameters Params;Params.Owner=this;Params.OverrideLevel=GetLevel();Params.bDeferConstruction=true;Params.bAllowDuringConstructionScript=true;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const FTransform World=Local*GetActorTransform();
        AActor* A=GetWorld()->SpawnActor<AActor>(Class,World,Params);
        if (A){A->Tags.Add(TEXT("ProceduralCity.Generated"));Staged.Add(A);}return A;
    };
    auto Finish=[&](AActor* A,const FTransform& Local,const FString& Label)
    {
        const FTransform World=Local*GetActorTransform();A->FinishSpawning(World);
        if (A->GetRootComponent()) A->GetRootComponent()->SetMobility(EComponentMobility::Movable);
        A->AttachToActor(this,FAttachmentTransformRules::KeepWorldTransform);
#if WITH_EDITOR
        if (!GetWorld()->IsGameWorld()){A->SetActorLabel(Label);A->SetFolderPath(FName(*FString::Printf(TEXT("GeneratedCities/%s"),*GetActorLabel())));}
#endif
    };
    auto Fail=[&](const FString& S){LastError=S;DestroyOwned(Staged);return false;};
    auto Color=[&](int32 I){return DistrictColors.IsEmpty()?FLinearColor(0,.6f,1):DistrictColors[I%DistrictColors.Num()];};
    if (bCreateFoundation)
    {
        const FTransform T(FVector(0,0,-5));auto* Ground=Cast<AParametricGround>(Spawn(AParametricGround::StaticClass(),T));
        if (!Ground) return Fail(TEXT("Foundation spawn failed."));
        Ground->Shape=Rules.Layout==ECityLayout::Grid?EParametricGroundShape::Square:EParametricGroundShape::Circle;
        Ground->Size=Plan.FoundationRadius*2;Ground->Thickness=100;Ground->GroundColor=FoundationColor;Ground->BoundaryColor=Color(0);
        Ground->BoundaryInset=180;Ground->BoundaryWidth=25;Finish(Ground,T,TEXT("City_Foundation"));
    }
    for (int32 D=0;D<Plan.Districts.Num();++D)
    {
        const auto& District=Plan.Districts[D];const FTransform T(District.Center);
        auto* Platform=Cast<AParametricLobbyPlatform>(Spawn(PlatformClass,T));if (!Platform) return Fail(TEXT("Platform spawn failed."));
        Platform->Shape=District.bRound?ELobbyPlatformShape::Ellipse:ELobbyPlatformShape::RoundedRectangle;
        Platform->Width=Platform->Depth=District.bRound?2*District.Radius:District.Radius*UE_SQRT_2;
        Platform->CornerRadius=District.Inradius()*.3;Platform->Thickness=110;Platform->RimWidth=20;Platform->BevelSize=6;
        Platform->bDestructible=false;Platform->bSpawnOnBeginPlay=false;Platform->bFitPadsInsidePlatform=false;
        Platform->SurfaceColor=Color(D)*.13f+FLinearColor(.018f,.018f,.028f);Platform->BodyColor=FoundationColor;
        Platform->RimColor=Color(D)*.28f;Platform->CyanColor=Color(D);Platform->YellowColor=Color(D)*.55f+FLinearColor(.18f,.15f,.12f);
        Platform->SurfaceRoughness=.55f;Platform->SurfaceEmission=.05f;Platform->YellowEmission=.25f;Platform->NeonIntensity=GuideEmission;
        Platform->Pads.Reset();Platform->Paths.Reset();
        const double Half=Platform->Width*.5;
        FLobbyPlatformPad Plaza;Plaza.Id=TEXT("Plaza");Plaza.Paint=ELobbyPadPaint::Ring;Plaza.Radius=D==0?.67f:.18f;Plaza.RingWidth=.04f;Platform->Pads.Add(Plaza);
        for (int32 I=0;I<District.Ports.Num();++I)
        {
            FLobbyPlatformPad Port;Port.Id=FName(*FString::Printf(TEXT("Port%d"),I));Port.bShowCircle=false;
            Port.Position=District.Ports[I]*(District.BoundaryDistance(District.Ports[I])-100)/Half;Platform->Pads.Add(Port);
        }
        for (int32 I=0;I<Plan.Buildings.Num();++I) if (Plan.Buildings[I].District==D)
        {
            const auto& B=Plan.Buildings[I];FLobbyPlatformPad Pad;Pad.Id=FName(*FString::Printf(TEXT("Plot%d"),I));
            Pad.Position=FVector2D(B.Center-District.Center)/Half;Pad.Radius=(B.Radius+65)/Half;Pad.Paint=ELobbyPadPaint::Ring;Pad.RingWidth=.025f;
            Platform->Pads.Add(Pad);
            if (D!=0){FLobbyPlatformPath Path;Path.FromPad=TEXT("Plaza");Path.ToPad=Pad.Id;Path.Width=Rules.BuildingAccessWidth/Platform->Width;Platform->Paths.Add(Path);}
        }
        Finish(Platform,T,FString::Printf(TEXT("City_District_%02d"),D));
        if (!Platform->LayoutWarning.IsEmpty()) return Fail(TEXT("Platform paint layout: ")+Platform->LayoutWarning);
    }
    auto MakeLink=[&](const TArray<FVector>& Points,ECityLinkKind Kind,bool Rails,int32 Palette,const FString& Label)->bool
    {
        const FTransform T=FTransform::Identity;auto* Link=Cast<AParametricCityLink>(Spawn(LinkClass,T));if (!Link) return false;
        Link->Points=Points;Link->Kind=Kind;Link->Width=Rules.RoadWidth;Link->Thickness=30;
        Link->MaxStepRise=Rules.MaxStepRise;Link->MinStepTread=Rules.MinStepTread;Link->bGuardRails=Rails;Link->GuideColor=Color(Palette);
        Finish(Link,T,Label);if (!Link->GenerationWarning.IsEmpty()){LastError=Link->GenerationWarning;return false;}return true;
    };
    for (int32 I=0;I<Plan.Connections.Num();++I)
    {
        const auto& C=Plan.Connections[I];
        if (!MakeLink(C.Points,C.Kind,Rules.bGuardRails,C.From,FString::Printf(TEXT("City_Link_%02d_%02d"),C.From,C.To))) return Fail(TEXT("Connection mesh failed: ")+LastError);
    }
    for (int32 D=0;D<Plan.Districts.Num();++D)
    {
        const auto& District=Plan.Districts[D];
        for (int32 I=0;I<District.Ports.Num();++I)
        {
            const auto& Dir=District.Ports[I];const FVector Start=District.Center+FVector(Dir*(D==0?District.Inradius()*.67:0),1);
            const FVector End=District.Center+FVector(Dir*(District.BoundaryDistance(Dir)-80),1);
            if (!MakeLink({Start,End},ECityLinkKind::Road,false,D,FString::Printf(TEXT("City_Street_%02d_%02d"),D,I))) return Fail(TEXT("Internal road failed."));
        }
    }
    TArray<FVector> Ring;
    for (int32 I=0;I<=64;++I){const double A=I*UE_DOUBLE_TWO_PI/64;Ring.Add(Plan.Districts[0].Center+FVector(FMath::Cos(A),FMath::Sin(A),0)*Plan.Districts[0].Inradius()*.67+FVector(0,0,1));}
    if (!MakeLink(Ring,ECityLinkKind::Road,false,0,TEXT("City_HubRing"))) return Fail(TEXT("Hub ring failed."));
    for (int32 I=0;I<Plan.Buildings.Num();++I)
    {
        const auto& B=Plan.Buildings[I];const auto& C=BuildingCatalog[B.CatalogIndex];
        const FQuat Rotation(FRotator(0,B.Yaw,0));
        const FTransform T(Rotation,B.Center+Rotation.RotateVector(C.PivotOffset*B.Scale),FVector(B.Scale));
        auto* A=Spawn(C.ActorClass,T);if (!A) return Fail(TEXT("Building spawn failed for ")+C.Id.ToString());
        Finish(A,T,FString::Printf(TEXT("City_D%02d_%s_%02d"),B.District,*C.Id.ToString(),I));
        // Validate the real geometry after construction, not just the catalog promise.
        const FBox Bounds=CityCatalog::Bounds(A);
        if (!Bounds.IsValid) return Fail(TEXT("Catalog actor has no mesh bounds: ")+C.Id.ToString());
        const double Actual=CityCatalog::Radius(Bounds,C.PivotOffset)*B.Scale;
        if (Actual>B.Radius+2 || FMath::Abs((Bounds.Min.Z+C.PivotOffset.Z)*B.Scale)>3)
            return Fail(FString::Printf(TEXT("Catalog footprint/pivot mismatch for %s (actual radius %.0f, reserved %.0f). Use Calibrate Catalog Footprints."),*C.Id.ToString(),Actual,B.Radius));
    }
    const FTransform Entry(FRotator(0,90,0),Plan.EntryLocation);
    auto* Start=Spawn(APlayerStart::StaticClass(),Entry);if (!Start) return Fail(TEXT("PlayerStart spawn failed."));
    Finish(Start,Entry,TEXT("City_PlayerStart"));
    Modify();DestroyOwned(GeneratedActors);GeneratedActors=MoveTemp(Staged);GeneratedPlan=MoveTemp(Plan);Seed=InSeed;
    GeneratedActorCount=GeneratedActors.Num();++GenerationSerial;LastError.Reset();
    GenerationReport=FString::Printf(TEXT("%s | Seed %d | %d districts | %d links | %d buildings | %d estimated cells | %d actors\n%s"),
        *GeneratedPlan.Signature,Seed,GeneratedPlan.Districts.Num(),GeneratedPlan.Connections.Num(),GeneratedPlan.Buildings.Num(),GeneratedPlan.EstimatedCells,GeneratedActorCount,*GeneratedPlan.Notes);
    MovePlayersToEntry();return true;
}

bool AProceduralCityGenerator::LoadRulesFromFile()
{
    FString Path=RulesFile;if (FPaths::IsRelative(Path)) Path=FPaths::Combine(FPaths::ProjectDir(),Path);
    if (!FPaths::FileExists(Path)){LastError=TEXT("Cannot read rules file: ")+Path;return false;}
    FConfigFile Config;Config.Read(Path);
    if (!Config.FindSection(TEXT("ProceduralCity"))){LastError=TEXT("Rules file requires a [ProceduralCity] section.");return false;}
    const TCHAR* Section=TEXT("ProceduralCity");FProceduralCityRules New=Rules;
    int32 NewSeed=Seed;Config.GetInt(Section,TEXT("Seed"),NewSeed);
    FString Mode;if (Config.GetString(Section,TEXT("Layout"),Mode))
    {
        if (Mode==TEXT("Radial")) New.Layout=ECityLayout::Radial;else if (Mode==TEXT("Grid")) New.Layout=ECityLayout::Grid;
        else if (Mode==TEXT("Organic")) New.Layout=ECityLayout::Organic;else {LastError=TEXT("Layout must be Radial, Grid or Organic.");return false;}
    }
#define CITY_INT(Name) Config.GetInt(Section,TEXT(#Name),New.Name)
#define CITY_FLOAT(Name) Config.GetFloat(Section,TEXT(#Name),New.Name)
#define CITY_BOOL(Name) Config.GetBool(Section,TEXT(#Name),New.Name)
    CITY_INT(DistrictCount);CITY_INT(ElevationLevels);CITY_INT(MinBuildingsPerDistrict);CITY_INT(MaxBuildingsPerDistrict);
    CITY_INT(MaxTotalBuildings);CITY_INT(MaxEstimatedBuildingCells);CITY_INT(PlacementAttempts);
    CITY_FLOAT(MinDistrictRadius);CITY_FLOAT(MaxDistrictRadius);CITY_FLOAT(DistrictGap);CITY_FLOAT(PositionJitter);CITY_FLOAT(RoundPlatformChance);
    CITY_FLOAT(BaseElevation);CITY_FLOAT(ElevationStep);CITY_FLOAT(RoadWidth);CITY_FLOAT(ExtraConnectionChance);CITY_FLOAT(Curvature);
    CITY_FLOAT(MaxRampSlope);CITY_FLOAT(StairChance);CITY_FLOAT(MaxStepRise);CITY_FLOAT(MinStepTread);CITY_FLOAT(MaxStairHeight);
    CITY_FLOAT(BuildingClearance);CITY_FLOAT(PlatformEdgeSetback);CITY_FLOAT(BuildingAccessWidth);CITY_BOOL(bGuardRails);CITY_BOOL(bHubLandmark);
#undef CITY_INT
#undef CITY_FLOAT
#undef CITY_BOOL
    if (!New.Validate(LastError)) return false;
    Modify();Rules=New;Seed=NewSeed;GenerationReport=TEXT("Rules imported. Rebuild Same Seed / Generate New Seed to apply.");LastError.Reset();return true;
}

bool AProceduralCityGenerator::CalibrateCatalogFootprints()
{
    if (IsTemplate() || !GetWorld() || GetWorld()->IsGameWorld() || bGenerating)
    {LastError=TEXT("Calibrate in an editor world before runtime.");return false;}
    TArray<FCityBuildingCatalogEntry> Updated=BuildingCatalog;
    for (auto& C:Updated)
    {
        if (!C.ActorClass) continue;
        if (C.ActorClass->IsChildOf(StaticClass())) {LastError=TEXT("A city generator cannot be a building catalog entry.");return false;}
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=GetWorld()->SpawnActor<AActor>(C.ActorClass,FVector(0,0,-1000000),FRotator::ZeroRotator,Params);
        if (!A){LastError=TEXT("Calibration spawn failed: ")+C.Id.ToString();return false;}
        const FBox Box=CityCatalog::Bounds(A);
        if (Box.IsValid)
        {
            C.PivotOffset={-Box.GetCenter().X,-Box.GetCenter().Y,-Box.Min.Z};
            C.FootprintRadius=CityCatalog::Radius(Box,C.PivotOffset)+10;
            if (auto* Cells=Cast<AChunkedDestructibleFloor>(A)){const FIntPoint Count=Cells->GetCellCount();C.EstimatedCells=FMath::Max(1,Count.X*Count.Y);}
        }
        A->Destroy();
        if (!Box.IsValid){LastError=TEXT("No generated mesh bounds to calibrate: ")+C.Id.ToString();return false;}
    }
    Modify();BuildingCatalog=MoveTemp(Updated);LastError.Reset();GenerationReport=TEXT("Catalog footprints, base pivots and cell estimates calibrated. Source classes unchanged.");return true;
}
