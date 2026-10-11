#include "ParametricBallerinaRide.h"
#include "Components/ChildActorComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace BallerinaRide
{
    const FName GeneratedTag(TEXT("BallerinaGenerated"));
    using namespace UE::Geometry;

    FLinearColor Palette(const TArray<FLinearColor>& Colors, int32 Index, FLinearColor Fallback)
    { return Colors.IsEmpty() ? Fallback : Colors[Index % Colors.Num()]; }

    UMaterialInstanceDynamic* Material(UObject* Owner, UMaterialInterface* Parent, FLinearColor Tint)
    {
        auto* MID = UMaterialInstanceDynamic::Create(Parent ? Parent : UMaterial::GetDefaultMaterial(MD_Surface), Owner);
        MID->SetVectorParameterValue(TEXT("Tint"), Tint);
        return MID;
    }

    template<typename T> T* Component(AActor* Owner, USceneComponent* Parent, FName Name)
    {
        T* Result = NewObject<T>(Owner, MakeUniqueObjectName(Owner, T::StaticClass(), Name));
        Result->CreationMethod = EComponentCreationMethod::UserConstructionScript;
        Result->ComponentTags.Add(GeneratedTag);
        Result->SetMobility(EComponentMobility::Movable);
        Result->SetupAttachment(Parent);
        return Result;
    }

    void Triangle(FDynamicMesh3& Mesh, FVector A, FVector B, FVector C, FVector2f UV0, FVector2f UV1, FVector2f UV2, int32 Mat)
    {
        // UE's dynamic mesh front face is clockwise; keep UV corner order in sync.
        int32 Id = Mesh.AppendTriangle(Mesh.AppendVertex(FVector3d(A)), Mesh.AppendVertex(FVector3d(C)), Mesh.AppendVertex(FVector3d(B)));
        if (Id < 0) { return; }
        Mesh.Attributes()->GetMaterialID()->SetValue(Id, Mat);
        auto* UV = Mesh.Attributes()->PrimaryUV();
        UV->SetTriangle(Id, FIndex3i(UV->AppendElement(UV0), UV->AppendElement(UV2), UV->AppendElement(UV1)));
        FVector3f N = FVector3f(FVector::CrossProduct(B-A,C-A).GetSafeNormal());
        auto* Normals = Mesh.Attributes()->PrimaryNormals();
        Normals->SetTriangle(Id, FIndex3i(Normals->AppendElement(N), Normals->AppendElement(N), Normals->AppendElement(N)));
    }

    TSubclassOf<AActor> ValidClass(TSubclassOf<AActor> Candidate, UClass* Fallback)
    {
        // Disallow a ride inside itself, including subclasses, to prevent recursive construction.
        return Candidate && !Candidate->HasAnyClassFlags(CLASS_Abstract) && !Candidate->IsChildOf(AParametricBallerinaRide::StaticClass())
            ? Candidate : TSubclassOf<AActor>(Fallback);
    }
}

ABallerinaRideFigure::ABallerinaRideFigure()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FigureMesh"));
    SetRootComponent(Mesh); Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Asset(TEXT("/Game/Fairground/BallerinaRide/Meshes/SM_BallerinaFigure.SM_BallerinaFigure"));
    Mesh->SetStaticMesh(Asset.Get());
}

ABallerinaRideSeat::ABallerinaRideSeat()
{
    PrimaryActorTick.bCanEverTick = false;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SeatMesh"));
    SetRootComponent(Mesh); Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Asset(TEXT("/Game/Fairground/BallerinaRide/Meshes/SM_BallerinaSeat.SM_BallerinaSeat"));
    Mesh->SetStaticMesh(Asset.Get());
}

void ABallerinaRideSeat::OnConstruction(const FTransform& Transform)
{ Super::OnConstruction(Transform); SetSeatColor(PaintColor); }

void ABallerinaRideSeat::SetSeatColor(FLinearColor Color)
{
    PaintColor = Color;
    if (!Mesh->GetStaticMesh()) { return; }
    const int32 Slot = Mesh->GetMaterialIndex(TEXT("SeatPaint"));
    if (Slot != INDEX_NONE)
    {
        auto* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot));
        if (!MID) { MID = Mesh->CreateDynamicMaterialInstance(Slot); }
        if (MID) { MID->SetVectorParameterValue(TEXT("Tint"), Color); }
    }
}

AParametricBallerinaRide::AParametricBallerinaRide()
{
    PrimaryActorTick.bCanEverTick = true;
    RideRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RideRoot")); SetRootComponent(RideRoot);
    RideRoot->SetMobility(EComponentMobility::Movable);
    PlatformPivot = CreateDefaultSubobject<USceneComponent>(TEXT("PlatformPivot")); PlatformPivot->SetupAttachment(RideRoot);
    Skirt = CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("SegmentedSkirt")); Skirt->SetupAttachment(PlatformPivot);
    Deck = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RotatingDeck")); Deck->SetupAttachment(PlatformPivot);
    Base = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StationaryBase")); Base->SetupAttachment(RideRoot);
    Figure = CreateDefaultSubobject<UChildActorComponent>(TEXT("CentreFigure")); Figure->SetupAttachment(PlatformPivot);
    for (USceneComponent* C : {PlatformPivot.Get(), static_cast<USceneComponent*>(Skirt), static_cast<USceneComponent*>(Deck), static_cast<USceneComponent*>(Base), static_cast<USceneComponent*>(Figure)})
    { C->SetMobility(EComponentMobility::Movable); }
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Deck->SetStaticMesh(Cylinder.Object); Base->SetStaticMesh(Cylinder.Object); SphereMesh = Sphere.Object;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Surface(TEXT("/Game/Fairground/BallerinaRide/Materials/M_RideSurface.M_RideSurface"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Pattern(TEXT("/Game/Fairground/BallerinaRide/Materials/M_RideSkirt.M_RideSkirt"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Bulb(TEXT("/Game/Fairground/BallerinaRide/Materials/M_RideBulb.M_RideBulb"));
    DefaultSurfaceMaterial = Surface.Get(); DefaultPatternMaterial = Pattern.Get(); DefaultBulbMaterial = Bulb.Get();
    FigureActorClass = ABallerinaRideFigure::StaticClass(); SeatActorClass = ABallerinaRideSeat::StaticClass();
    SliceColors = {FLinearColor(1,.76f,.08f), FLinearColor(1,.9f,.47f)};
    LightColors = {FLinearColor(.035f,.6f,1),FLinearColor(1,.76f,.19f),FLinearColor(1,.055f,.25f)};
    SeatColors = {FLinearColor(.01f,.42f,.42f),FLinearColor(.55f,.018f,.22f),FLinearColor(.07f,.13f,.7f),FLinearColor(.75f,.33f,.025f)};
}

float AParametricBallerinaRide::SafeRadius() const { return FMath::Clamp(SkirtRadius,150.f,5000.f); }
float AParametricBallerinaRide::SurfaceHeight(float Radius) const
{
    const float Inner = FMath::Clamp(HubRadius,10.f,SafeRadius()-20);
    const float T = FMath::Clamp((SafeRadius()-Radius)/(SafeRadius()-Inner),0.f,1.f);
    return FMath::Clamp(SkirtHeight,10.f,5000.f)*FMath::Pow(T,FMath::Clamp(SkirtProfile,.3f,3.f));
}

FTransform AParametricBallerinaRide::EvaluatePlatformTransform(double Seconds) const
{
    if (!FMath::IsFinite(Seconds)) { Seconds=0; }
    const double Spin = FMath::DegreesToRadians(FMath::Fmod(double(InitialSpinAngle)+Seconds*SpinSpeed,360.));
    const double Wave = FMath::DegreesToRadians(FMath::Fmod(double(InitialWaveAngle)+Seconds*WaveSpeed,360.));
    const double Tilt = FMath::DegreesToRadians(FMath::Clamp(TiltAngle,0.f,35.f));
    // Rotate around a horizontal tangent. The point at azimuth Wave is the highest edge.
    // Tilt * Spin preserves independent high-edge direction; no parasitic yaw from precession.
    const FQuat TiltRotation(FVector(FMath::Sin(Wave),-FMath::Cos(Wave),0),Tilt);
    const FQuat SpinRotation(FVector::UpVector,Spin);
    const float R = SafeRadius()+FMath::Clamp(DeckOverhang,25.f,2000.f);
    const double RequiredHeight = R*FMath::Sin(Tilt)+FMath::Clamp(DeckThickness,2.f,200.f)*FMath::Cos(Tilt)+FMath::Max(GroundClearance,0.f);
    const double Height = bAutoGroundClearance ? FMath::Max(double(PivotHeight),RequiredHeight) : FMath::Max(0.f,PivotHeight);
    return FTransform(TiltRotation*SpinRotation,FVector(0,0,Height));
}

void AParametricBallerinaRide::OnConstruction(const FTransform& Transform)
{ Super::OnConstruction(Transform); RebuildRide(); }

void AParametricBallerinaRide::BeginPlay()
{ Super::BeginPlay(); MotionTime=0; bRunning=bAutoStart; RebuildRide(); }

bool AParametricBallerinaRide::ShouldTickIfViewportsOnly() const { return bPreviewMotionInEditor; }
void AParametricBallerinaRide::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if ((GetWorld() && GetWorld()->IsGameWorld() && bRunning) || (GetWorld() && !GetWorld()->IsGameWorld() && bPreviewMotionInEditor))
    { SetMotionTime(MotionTime + FMath::Max(0.f,DeltaSeconds)); }
}
void AParametricBallerinaRide::SetRideRunning(bool bInRunning) { bRunning=bInRunning; }
void AParametricBallerinaRide::ResetMotion() { SetMotionTime(0); }
void AParametricBallerinaRide::SetMotionTime(double Seconds)
{
    MotionTime=FMath::IsFinite(Seconds)?Seconds:0;
    PlatformPivot->SetRelativeTransform(EvaluatePlatformTransform(MotionTime)); UpdateLights();
}

void AParametricBallerinaRide::UpdateLights()
{
    for (int32 I=0; I<LightMIDs.Num(); ++I)
    {
        const float Phase = LightPhases.IsValidIndex(I)?LightPhases[I]:0;
        const double Pulse = bChaseLights ? .28+.72*FMath::Pow(.5+.5*FMath::Cos(MotionTime*FMath::Max(0.f,LightChaseSpeed)*2*PI-Phase),3.) : 1.;
        LightMIDs[I]->SetScalarParameterValue(TEXT("Emission"),FMath::Clamp(LightIntensity,0.f,100.f)*Pulse);
    }
}

void AParametricBallerinaRide::RebuildRide()
{
    using namespace BallerinaRide;
    // Enumerate owned components as well as current arrays: reconstruction/load must never leave orphan seats.
    TInlineComponentArray<UActorComponent*> OldComponents(this);
    for (auto* C:OldComponents) { if (C && C->ComponentHasTag(GeneratedTag)) { C->DestroyComponent(); } }
    LightMIDs.Reset(); LightPhases.Reset();
    const int32 Slices=FMath::Clamp(SliceCount,6,96), AngularSteps=FMath::Clamp(SubdivisionsPerSlice,1,16);
    const float R=SafeRadius(), Inner=FMath::Clamp(HubRadius,10.f,R-20), Overhang=FMath::Clamp(DeckOverhang,25.f,2000.f);
    const float Thickness=FMath::Clamp(DeckThickness,2.f,200.f), Height=FMath::Clamp(SkirtHeight,10.f,5000.f);
    const auto Collision=bEnableCollision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision;
    Skirt->SetTransientDeferCollisionUpdates(true);
    Skirt->EmptyOverrideMaterials();
    FDynamicMesh3 Mesh; Mesh.EnableAttributes(); Mesh.Attributes()->EnableMaterialID(); Mesh.Attributes()->SetNumUVLayers(1);
    auto* Sockets=Component<UInstancedStaticMeshComponent>(this,PlatformPivot,TEXT("BulbSockets"));
    Sockets->SetStaticMesh(SphereMesh); Sockets->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sockets->SetMaterial(0,Material(this,DefaultSurfaceMaterial,FLinearColor(.62f,.47f,.16f))); Sockets->RegisterComponent();
    auto MakeBulbs=[&](FLinearColor Color,float Phase)
    {
        auto* C=Component<UInstancedStaticMeshComponent>(this,PlatformPivot,TEXT("Bulbs"));
        C->ComponentTags.Add(TEXT("BallerinaBulb"));
        C->SetStaticMesh(SphereMesh); C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false);
        auto* MID=Material(this,BulbMaterial?BulbMaterial.Get():DefaultBulbMaterial.Get(),Color);
        C->SetMaterial(0,MID); C->RegisterComponent(); LightMIDs.Add(MID); LightPhases.Add(Phase); return C;
    };
    const float BR=FMath::Clamp(BulbRadius,1.f,12.f);
    const int32 Bulbs=FMath::Clamp(BulbsPerSeam,1,80);
    for (int32 I=0;I<Slices;++I)
    {
        const FBallerinaSliceOverride* Override=SliceOverrides.FindByPredicate([I](const auto& Entry){return Entry.SliceIndex==I;});
        UMaterialInterface* Parent=SliceMaterials.IsEmpty()?nullptr:SliceMaterials[I%SliceMaterials.Num()].Get();
        if (Override && Override->Material) { Parent=Override->Material; }
        FLinearColor Color=Palette(SliceColors,I,FLinearColor(1,.76f,.08f));
        if (Override && Override->bOverrideColor) { Color=Override->Color; }
        auto* MID=Material(this,Parent?Parent:DefaultPatternMaterial.Get(),Color);
        MID->SetScalarParameterValue(TEXT("PatternStrength"),bDecorativePattern?1.f:0.f); Skirt->SetMaterial(I,MID);
        for (int32 A=0;A<AngularSteps;++A)
        {
            const float A0=(I+float(A)/AngularSteps)*2*PI/Slices, A1=(I+float(A+1)/AngularSteps)*2*PI/Slices;
            auto Point=[&](float Radius,float Angle){return FVector(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),SurfaceHeight(Radius));};
            for (int32 J=0;J<12;++J)
            {
                const float V0=J/12.f,V1=(J+1)/12.f, R0=FMath::Lerp(Inner,R,V0),R1=FMath::Lerp(Inner,R,V1);
                const FVector P0=Point(R0,A0),P1=Point(R1,A0),P2=Point(R1,A1),P3=Point(R0,A1);
                const FVector2f T0(float(A)/AngularSteps,V0),T1(float(A)/AngularSteps,V1),T2(float(A+1)/AngularSteps,V1),T3(float(A+1)/AngularSteps,V0);
                Triangle(Mesh,P0,P1,P2,T0,T1,T2,I); Triangle(Mesh,P0,P2,P3,T0,T2,T3,I);
            }
            Triangle(Mesh,FVector(0,0,Height),Point(Inner,A0),Point(Inner,A1),FVector2f(.5,.5),FVector2f(0,0),FVector2f(1,0),I);
        }
        if (bShowSeamLights && (!Override || Override->bShowLights))
        {
            FLinearColor LC=Palette(LightColors,I,FLinearColor(1,.8f,.2f));
            if (Override && Override->bOverrideLightColor) { LC=Override->LightColor; }
            auto* C=MakeBulbs(LC,float(I)*2*PI/Slices);
            const float Angle=I*2*PI/Slices;
            for (int32 J=0;J<Bulbs;++J)
            {
                const float Radius=FMath::Lerp(Inner+BR*2,R-BR*2,(J+.5f)/Bulbs);
                const FVector P(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),SurfaceHeight(Radius)+BR*.5);
                C->AddInstance(FTransform(FQuat::Identity,P+FVector(0,0,BR*.6),FVector(BR/50)));
                Sockets->AddInstance(FTransform(FQuat::Identity,P,FVector(BR/50*1.45,BR/50*1.45,BR/50*.55)));
            }
        }
    }
    Skirt->GetDynamicMesh()->SetMesh(MoveTemp(Mesh)); Skirt->NotifyMeshUpdated();
    Skirt->SetCollisionEnabled(Collision); Skirt->SetGenerateOverlapEvents(false);
    Skirt->SetTransientDeferCollisionUpdates(false); Skirt->SetComplexAsSimpleCollisionEnabled(bEnableCollision,true);
    Deck->SetRelativeLocation(FVector(0,0,-Thickness*.5)); Deck->SetRelativeScale3D(FVector((R+Overhang)/50,(R+Overhang)/50,Thickness/100));
    Deck->SetMaterial(0,DeckMaterial?DeckMaterial.Get():Material(this,DefaultSurfaceMaterial,DeckColor)); Deck->SetCollisionEnabled(Collision);
    const float PivotZ=EvaluatePlatformTransform(MotionTime).GetLocation().Z;
    const float BaseHeight=FMath::Max(20.f,PivotZ-Thickness-20);
    Base->SetRelativeLocation(FVector(0,0,BaseHeight*.5)); Base->SetRelativeScale3D(FVector(Inner*2.2/50,Inner*2.2/50,BaseHeight/100));
    Base->SetMaterial(0,BaseMaterial?BaseMaterial.Get():Material(this,DefaultSurfaceMaterial,FLinearColor(.035f,.045f,.065f))); Base->SetCollisionEnabled(Collision);
    if (bShowRimLights)
    {
        const int32 Groups=FMath::Clamp(LightColors.Num(),1,16), Count=Slices*4;
        for (int32 G=0;G<Groups;++G)
        {
            auto* C=MakeBulbs(Palette(LightColors,G,FLinearColor(1,.8f,.2f)),G*2*PI/Groups);
            for (int32 J=G;J<Count;J+=Groups)
            {
                const float A=J*2*PI/Count;
                C->AddInstance(FTransform(FQuat::Identity,FVector((R+Overhang+BR*.3)*FMath::Cos(A),(R+Overhang+BR*.3)*FMath::Sin(A),-Thickness*.45),FVector(BR/50)));
            }
        }
    }
    FTransform FT=FigureOffset; FT.AddToTranslation(FVector(0,0,Height)); Figure->SetRelativeTransform(FT);
    Figure->SetChildActorClass(bShowFigure?ValidClass(FigureActorClass,ABallerinaRideFigure::StaticClass()):TSubclassOf<AActor>());
    if (Figure->GetChildActor()) { Figure->GetChildActor()->SetActorEnableCollision(bEnableCollision); }
    if (bShowSeats)
    {
        const int32 Count=FMath::Clamp(SeatCount,1,96);
        const float Radius=SeatRadius>0?FMath::Clamp(SeatRadius,0.f,10000.f):R+Overhang*.5f;
        const TSubclassOf<AActor> Class=ValidClass(SeatActorClass,ABallerinaRideSeat::StaticClass());
        for (int32 I=0;I<Count;++I)
        {
            const float Angle=I*360.f/Count+SeatPhaseDegrees;
            FTransform Offset=SeatOffset;
            if (bAutoFitDefaultSeats && Class->IsChildOf(ABallerinaRideSeat::StaticClass()))
            {
                FVector Scale=Offset.GetScale3D();
                const float Chord=2*FMath::Max(10.f,Radius-80)*FMath::Sin(PI/FMath::Max(3,Count));
                Scale.Y*=FMath::Clamp(Chord/165.f,.1f,1.f);
                Scale.X*=FMath::Clamp(Overhang/165.f,.1f,1.f); Offset.SetScale3D(Scale);
            }
            auto* C=Component<UChildActorComponent>(this,PlatformPivot,TEXT("RideSeat"));
            const FTransform Ring(FRotator(0,Angle,0),FVector(Radius*FMath::Cos(FMath::DegreesToRadians(Angle)),Radius*FMath::Sin(FMath::DegreesToRadians(Angle)),0));
            C->SetRelativeTransform(Offset*Ring); C->SetChildActorClass(Class); C->RegisterComponent();
            if (auto* Actor=C->GetChildActor())
            {
                Actor->SetActorEnableCollision(bEnableCollision);
                if (auto* Seat=Cast<ABallerinaRideSeat>(Actor)) { Seat->SetSeatColor(Palette(SeatColors,I,FLinearColor(.025f,.42f,.48f))); }
            }
        }
    }
    SetMotionTime(MotionTime);
}

int32 AParametricBallerinaRide::GetGeneratedSeatCount() const
{
    int32 Count=0; TInlineComponentArray<UChildActorComponent*> Components(this);
    for (auto* C:Components) { if (C->ComponentHasTag(BallerinaRide::GeneratedTag) && C->GetChildActor()) { ++Count; } } return Count;
}
int32 AParametricBallerinaRide::GetGeneratedBulbCount() const
{
    int32 Count=0; TInlineComponentArray<UInstancedStaticMeshComponent*> Components(this);
    for (auto* C:Components) { if (C->ComponentHasTag(TEXT("BallerinaBulb"))) { Count+=C->GetInstanceCount(); } } return Count;
}
