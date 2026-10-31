#include "ElectricTetherWeapon.h"
#include "NPCElectricTetherComponent.h"
#include "MyProject6Character.h"
#include "RailgunWeapon.h"
#include "Camera/CameraComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
using namespace UE::Geometry;

UElectricTetherFieldComponent::UElectricTetherFieldComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
}
void UElectricTetherFieldComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(Delta,TickType,ThisTickFunction);
    if(auto* Weapon=Cast<AElectricTetherWeapon>(GetOwner()))Weapon->DrawField(Delta);
}

AElectricTetherWeapon::AElectricTetherWeapon()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PrePhysics;
    PrimaryActorTick.bStartWithTickEnabled=false;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Field=CreateDefaultSubobject<UElectricTetherFieldComponent>(TEXT("ElectricField"));Field->SetupAttachment(RootComponent);
    Field->SetCollisionEnabled(ECollisionEnabled::NoCollision);Field->SetGenerateOverlapEvents(false);Field->SetCanEverAffectNavigation(false);Field->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> FX(TEXT("/Game/Weapons/ElectricTether/Materials/M_ArcField.M_ArcField"));Field->SetMaterial(0,FX.Get());
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Membrane(TEXT("/Game/Weapons/ElectricTether/Materials/M_ArcEnvelope.M_ArcEnvelope"));Field->SetMaterial(1,Membrane.Get());
    Hum=CreateDefaultSubobject<UAudioComponent>(TEXT("InductionHum"));Hum->SetupAttachment(RootComponent);Hum->bAutoActivate=false;
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> Loop(TEXT("/Game/Weapons/ElectricTether/Audio/S_ArcHum.S_ArcHum"));Hum->SetSound(Loop.Get());Hum->SetVolumeMultiplier(.3f);
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> Catch(TEXT("/Game/Weapons/ElectricTether/Audio/S_ArcCatch.S_ArcCatch"));CatchSound=Catch.Get();
    static ConstructorHelpers::FObjectFinderOptional<USoundBase> Release(TEXT("/Game/Weapons/ElectricTether/Audio/S_ArcRelease.S_ArcRelease"));ReleaseSound=Release.Get();
    Light=CreateDefaultSubobject<UPointLightComponent>(TEXT("ArcLight"));Light->SetupAttachment(RootComponent);Light->SetLightColor(FLinearColor(.06f,.7f,1.f));Light->SetAttenuationRadius(280);Light->SetCastShadows(false);Light->SetIntensity(0);
}
void AElectricTetherWeapon::Equip(AMyProject6Character* Character)
{ Wielder=Character;SetOwner(Character);SetInstigator(Character); }
bool AElectricTetherWeapon::HasTarget() const { return IsValid(Captive)&&Captive->IsCaptured()&&UNPCElectricTetherComponent::IsLivingNPC(Captive->GetOwner()); }
float AElectricTetherWeapon::GetSwingPower() const { return HasTarget()?FMath::Clamp(float(Captive->GetThrowVelocity().Size())/3800.f,0.f,1.f):0.f; }
void AElectricTetherWeapon::StartBeam(bool bTrackPhysicalButton)
{
    if(!HasAuthority()||!IsValid(Wielder)||Wielder->IsEnergyDepleted()||Wielder->GetActiveWeapon()!=EPlayerWeapon::ElectricTether||UGameplayStatics::IsGamePaused(this))return;
    if(bFiring)return;bTrackButton=bTrackPhysicalButton;bFiring=true;bBrokenUntilRelease=false;ReleaseFade=0;Age=0;
    SetActorTickEnabled(true);Field->SetComponentTickEnabled(true);
    Hum->Play();if(Wielder->Railgun)Wielder->Railgun->AddFireMotion(.25f);
}
void AElectricTetherWeapon::StopBeam(bool bThrow)
{
    const bool bWasTarget=HasTarget();
    if(bWasTarget)Captive->Release(bThrow);
    Captive=nullptr;ReleaseFade=bFiring?.14f:ReleaseFade;bFiring=false;bBrokenUntilRelease=false;OccludedTime=0;Hum->Stop();
    if(bWasTarget)
    {
        if(ReleaseSound)UGameplayStatics::PlaySound2D(this,ReleaseSound,.5f);
        if(Wielder&&Wielder->Railgun)Wielder->Railgun->AddFireMotion(.7f);
    }
}
void AElectricTetherWeapon::Tick(float Delta)
{
    Super::Tick(Delta);
    if(!IsValid(Wielder)||Wielder->IsEnergyDepleted()||Wielder->GetActiveWeapon()!=EPlayerWeapon::ElectricTether)
    {
        StopBeam(false);ReleaseFade=0;Field->SetVisibility(false);Light->SetIntensity(0);
        SetActorTickEnabled(false);Field->SetComponentTickEnabled(false);return;
    }
    // Key-up can be swallowed when focus is lost. Never leave an NPC permanently captured.
    if(auto* PC=Cast<APlayerController>(Wielder->GetController()); bFiring&&bTrackButton&&PC&&PC->IsLocalController()&&!PC->IsInputKeyDown(EKeys::LeftMouseButton))
        StopBeam(false);
    Age+=Delta;ReleaseFade=FMath::Max(0.f,ReleaseFade-Delta);
    if(!bFiring && ReleaseFade<=0)
    {
        Field->SetVisibility(false);Light->SetIntensity(0);
        SetActorTickEnabled(false);Field->SetComponentTickEnabled(false);return;
    }
    const FVector Eye=Wielder->GetFirstPersonCameraComponent()->GetComponentLocation();
    const FVector Aim=Wielder->GetController()?Wielder->GetControlRotation().Vector():Wielder->GetFirstPersonCameraComponent()->GetForwardVector();
    SetActorLocation(Eye);
    FCollisionQueryParams Q(SCENE_QUERY_STAT(ElectricTether),false,Wielder);Q.AddIgnoredActor(this);Q.AddIgnoredActor(Wielder->Railgun);
    if(bFiring)
    {
        if(Captive&&!HasTarget()){Captive=nullptr;bBrokenUntilRelease=true;}
        if(!HasTarget())
        {
            FHitResult Hit;BeamEnd=Eye+Aim*FMath::Clamp(CaptureRange,300.f,5000.f);
            if(GetWorld()->LineTraceSingleByChannel(Hit,Eye,BeamEnd,ECC_GameTraceChannel1,Q))
            {
                BeamEnd=Hit.ImpactPoint;
                if(!bBrokenUntilRelease&&UNPCElectricTetherComponent::IsLivingNPC(Hit.GetActor()))
                {
                    auto* C=Hit.GetActor()->FindComponentByClass<UNPCElectricTetherComponent>();
                    if(C&&C->Capture(this))
                    {
                        Captive=C;HoldDistance=FVector::Distance(Eye,Hit.GetActor()->GetActorLocation());
                        AnchorOffset=Hit.GetActor()->GetActorLocation()-(Eye+Aim*HoldDistance);OccludedTime=0;
                        if(CatchSound)UGameplayStatics::PlaySound2D(this,CatchSound,.55f);
                    }
                }
            }
        }
        if(HasTarget())
        {
            BeamEnd=Captive->GetOwner()->GetActorLocation();
            const float Radius=CastChecked<ACharacter>(Captive->GetOwner())->GetCapsuleComponent()->GetScaledCapsuleRadius();
            Q.AddIgnoredActor(Captive->GetOwner());FHitResult Obstacle;
            const bool bBlocked=GetWorld()->LineTraceSingleByChannel(Obstacle,Eye,BeamEnd,ECC_GameTraceChannel1,Q)&&Obstacle.Distance<FVector::Distance(Eye,BeamEnd)-Radius*1.5f;
            OccludedTime=bBlocked?OccludedTime+Delta:0;
            if(OccludedTime>.2f||FVector::Distance(Eye,BeamEnd)>CaptureRange*1.5f)
            {Captive->Release(false);Captive=nullptr;bBrokenUntilRelease=true;}
            else Captive->SetHoldPoint(Eye+Aim*FMath::Max(HoldDistance,240.f)+AnchorOffset);
        }
        Hum->SetPitchMultiplier(.85f+GetSwingPower()*.65f+(HasTarget()?.15f:0));
    }
}
void AElectricTetherWeapon::DrawField(float Delta)
{
    if(!IsValid(Wielder)||Wielder->IsEnergyDepleted()||Wielder->GetActiveWeapon()!=EPlayerWeapon::ElectricTether)
    {Field->SetVisibility(false);Light->SetIntensity(0);return;}
    const bool bVisible=bFiring||ReleaseFade>0;Field->SetVisibility(bVisible);Light->SetIntensity(bFiring?(HasTarget()?1700.f:650.f):0);
    if(!bVisible)return;
    SetActorLocation(Wielder->GetFirstPersonCameraComponent()->GetComponentLocation());
    if(HasTarget())BeamEnd=Captive->GetOwner()->GetActorLocation();
    const FVector Eye=GetActorLocation(),Start=Wielder->Railgun?Wielder->Railgun->GetVisualMuzzleLocation():Eye;
    const FTransform FieldToWorld=Field->GetComponentTransform();
    Light->SetWorldLocation(Start);const float Fade=bFiring?1.f:ReleaseFade/.14f;
    FDynamicMesh3 Mesh;Mesh.EnableAttributes();Mesh.Attributes()->EnablePrimaryColors();Mesh.Attributes()->EnableMaterialID();
    auto Vertex=[&](FVector P,FVector Normal,FVector2f UV,FLinearColor Color)
    {
        // Both surfaces and ribbons use the full inverse transform. PlayerStart
        // rotation must never rotate the world-space effect away from its target.
        const int ID=Mesh.AppendVertex(FieldToWorld.InverseTransformPosition(P));
        Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(FieldToWorld.InverseTransformVectorNoScale(Normal).GetSafeNormal()));
        Mesh.Attributes()->PrimaryUV()->AppendElement(UV);
        Mesh.Attributes()->PrimaryColors()->AppendElement(FVector4f(Color.R,Color.G,Color.B,Color.A));return ID;
    };
    auto Triangle=[&](int A,int B,int C,int Material)
    {
        const FIndex3i T(A,B,C);const int ID=Mesh.AppendTriangle(T);
        Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID,T);Mesh.Attributes()->PrimaryUV()->SetTriangle(ID,T);
        Mesh.Attributes()->PrimaryColors()->SetTriangle(ID,T);Mesh.Attributes()->GetMaterialID()->SetValue(ID,Material);
    };
    auto Ribbon=[&](const TArray<FVector>& Points,float Width,FLinearColor Color,bool bTaper=false)
    {
        const int Base=Mesh.VertexCount();
        for(int I=0;I<Points.Num();++I)
        {
            const float T=float(I)/(Points.Num()-1);
            const FVector Tangent=Points[FMath::Min(I+1,Points.Num()-1)]-Points[FMath::Max(I-1,0)];
            const FVector Normal=(Eye-Points[I]).GetSafeNormal();
            const float Envelope=bTaper?FMath::Pow(FMath::Max(.001f,FMath::Sin(T*UE_PI)),.55f):1.f;
            const FVector Side=FVector::CrossProduct(Tangent,Normal).GetSafeNormal(UE_SMALL_NUMBER,FVector::UpVector)*Width*Envelope;
            FLinearColor Tint=Color;Tint.A*=Fade*Envelope;
            Vertex(Points[I]-Side,Normal,FVector2f(T,0),Tint);Vertex(Points[I]+Side,Normal,FVector2f(T,1),Tint);
            if(I>0){const int A=Base+(I-1)*2;Triangle(A,A+1,A+2,0);Triangle(A+1,A+3,A+2,0);}
        }
    };
    const FVector Direction=(BeamEnd-Start).GetSafeNormal();FVector Right,Up;Direction.FindBestAxisVectors(Right,Up);
    const float Length=FVector::Distance(Start,BeamEnd),Load=GetSwingPower();
    const int Steps=FMath::Clamp(FMath::CeilToInt(Length/10),24,180);
    TArray<FVector> MainArc;
    for(int Strand=0;Strand<3;++Strand)
    {
        TArray<FVector> Points;Points.Reserve(Steps+1);const float Seed=Strand*13.71f;
        for(int I=0;I<=Steps;++I)
        {
            const float T=float(I)/Steps,Envelope=FMath::Sin(T*UE_PI);
            const float Flow=T*8-Age*3+Seed;
            const float X=FMath::PerlinNoise1D(Flow)+.32f*FMath::PerlinNoise1D(T*37+Age*8+Seed);
            const float Y=FMath::PerlinNoise1D(Flow+41.3f)+.32f*FMath::PerlinNoise1D(T*41-Age*9+Seed);
            const float Amplitude=(Strand==0?11.f:24.f)*(1+Load*.35f);
            Points.Add(FMath::Lerp(Start,BeamEnd,T)+(Right*X+Up*Y)*Envelope*Amplitude);
        }
        if(Strand==0)
        {
            MainArc=Points;Ribbon(Points,6.f,FLinearColor(.04f,.28f,.8f,.12f));
            Ribbon(Points,1.65f,FLinearColor(.3f,.75f,1.f,.85f));
        }
        else Ribbon(Points,1.1f,Strand==1?FLinearColor(.04f,.55f,1.f,.6f):FLinearColor(.34f,.14f,.8f,.38f));
    }
    // Short, branching discharges have independent lifetimes, never a static braid.
    for(int Branch=0;Branch<6;++Branch)
    {
        const float Clock=Age*(4.1f+Branch*.17f)+Branch*.73f,Life=FMath::Frac(Clock);
        if(Life>.65f)continue;
        const float Seed=FMath::FloorToFloat(Clock)*7.13f+Branch*31.7f;
        const int Index=FMath::Clamp(int((.15f+.7f*FMath::Frac(FMath::Sin(Seed)*437.3f))*Steps),1,Steps-1);
        const FVector A=MainArc[Index],Out=(Right*FMath::Sin(Seed)+Up*FMath::Cos(Seed)).GetSafeNormal();
        TArray<FVector> Points;
        for(int I=0;I<=10;++I)
        {
            const float T=I/10.f;
            Points.Add(A+Direction*(T*20)+Out*(T*(20+25*FMath::Abs(FMath::Sin(Seed))))+Up*FMath::PerlinNoise1D(Seed+T*14+Age*11)*T*12);
        }
        Ribbon(Points,.95f,FLinearColor(.12f,.62f,1.f,(1-Life/.65f)*.6f),true);
    }
    if(HasTarget())
    {
        auto* N=CastChecked<ACharacter>(Captive->GetOwner());const FVector Centre=N->GetActorLocation();
        const float H=N->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+24;
        const float R=FMath::Clamp(N->GetCapsuleComponent()->GetScaledCapsuleRadius()*1.95f,60.f,170.f);
        auto UnitPoint=[](float Theta,float Phi){return FVector(FMath::Sin(Theta)*FMath::Cos(Phi),FMath::Sin(Theta)*FMath::Sin(Phi),FMath::Cos(Theta));};
        auto Surface=[&](FVector P)
        {
            const float Ripple=1.f+.035f*FMath::PerlinNoise3D(P*3.5f+FVector(Age*.7f,-Age*.9f,Age*.4f))+.009f*FMath::Sin(Age*4+P.Z*5);
            return Centre+P*FVector(R,R,H)*Ripple;
        };
        // A filled, translucent shell carries the flowing density and fine veins.
        // Its triangles are smooth surface tessellation, never visible cage bars.
        constexpr int Rings=32,Slices=64;const int Base=Mesh.VertexCount();
        for(int Ring=0;Ring<=Rings;++Ring)
            for(int Slice=0;Slice<=Slices;++Slice)
            {
                const FVector P=UnitPoint(Ring*UE_PI/Rings,Slice*UE_TWO_PI/Slices);
                const FVector Normal=(P/FVector(R,R,H)).GetSafeNormal();
                Vertex(Surface(P),Normal,FVector2f(float(Slice)/Slices,float(Ring)/Rings),FLinearColor((P.X+1)*.5f,(P.Y+1)*.5f,(P.Z+1)*.5f,1+Captive->ImpactFlash*2));
            }
        for(int Ring=0;Ring<Rings;++Ring)
            for(int Slice=0;Slice<Slices;++Slice)
            {
                const int A=Base+Ring*(Slices+1)+Slice,B=A+Slices+1;
                if(Ring>0)Triangle(A,B,A+1,1);
                if(Ring<Rings-1)Triangle(A+1,B,B+1,1);
            }
        // Local arcs crawl across the membrane and split into smaller tributaries.
        // None closes around a latitude/longitude, avoiding a wireframe silhouette.
        for(int Arc=0;Arc<16;++Arc)
        {
            const float Clock=Age*(1.3f+Arc*.047f)+Arc*.619f,Life=FMath::Frac(Clock);
            if(Life>.72f)continue;
            const float Seed=Arc*17.31f+FMath::FloorToFloat(Clock)*11.73f;
            const float Theta=.4f+2.3f*FMath::Frac(FMath::Sin(Seed)*431.7f),Phi=Seed;
            const float Intensity=FMath::Sin(Life/.72f*UE_PI)*(.5f+Captive->ImpactFlash);
            TArray<FVector> Points;
            for(int I=0;I<=18;++I)
            {
                const float T=I/18.f;
                const float Turn=T*(.5f+.3f*FMath::Sin(Seed));
                const float Jitter=FMath::PerlinNoise1D(Seed+T*15+Age*6)*.09f;
                const FVector P=UnitPoint(Theta+Turn*FMath::Cos(Seed)+Jitter,Phi+Turn*1.4f+Jitter);
                Points.Add(Surface(P)+(Surface(P)-Centre).GetSafeNormal()*1.2f);
            }
            Ribbon(Points,1.2f,Arc%3?FLinearColor(.12f,.65f,1.f,Intensity):FLinearColor(.42f,.19f,.9f,Intensity*.7f),true);
            if(Arc%2==0)
            {
                TArray<FVector> Fork;
                for(int I=0;I<=8;++I)
                {
                    const float T=I/8.f;
                    Fork.Add(Points[9]+(Points[12]-Points[9])*T+(Points[9]-Centre).GetSafeNormal()*FMath::Sin(T*UE_PI)*8+Up*T*15);
                }
                Ribbon(Fork,.7f,FLinearColor(.22f,.6f,1.f,Intensity*.6f),true);
            }
        }
    }
    Field->SetMesh(MoveTemp(Mesh));
}
void AElectricTetherWeapon::EndPlay(const EEndPlayReason::Type Reason)
{ StopBeam(false);Super::EndPlay(Reason); }
