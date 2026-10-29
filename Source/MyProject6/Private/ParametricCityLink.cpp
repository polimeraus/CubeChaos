#include "ParametricCityLink.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;
namespace CityLinkMesh
{
struct FBuilder
{
    FDynamicMesh3 Mesh;FKAggregateGeom Collision;
    FBuilder(){Mesh.EnableAttributes();Mesh.Attributes()->EnableMaterialID();Mesh.Attributes()->SetNumUVLayers(1);}
    void Triangle(FVector A,FVector B,FVector C,int32 Material)
    {
        const FVector3f N=FVector3f(FVector::CrossProduct(B-A,C-A).GetSafeNormal());
        if (N.IsNearlyZero()) return;
        int32 V[3]={Mesh.AppendVertex(A),Mesh.AppendVertex(C),Mesh.AppendVertex(B)};
        const int32 T=Mesh.AppendTriangle(V[0],V[1],V[2]);
        const FIndex3i Normals(Mesh.Attributes()->PrimaryNormals()->AppendElement(N),Mesh.Attributes()->PrimaryNormals()->AppendElement(N),Mesh.Attributes()->PrimaryNormals()->AppendElement(N));
        Mesh.Attributes()->PrimaryNormals()->SetTriangle(T,Normals);
        const FIndex3i UV(Mesh.Attributes()->PrimaryUV()->AppendElement(FVector2f(A.X,A.Y)/200),Mesh.Attributes()->PrimaryUV()->AppendElement(FVector2f(C.X,C.Y)/200),Mesh.Attributes()->PrimaryUV()->AppendElement(FVector2f(B.X,B.Y)/200));
        Mesh.Attributes()->PrimaryUV()->SetTriangle(T,UV);Mesh.Attributes()->GetMaterialID()->SetValue(T,Material);
    }
    void Quad(FVector A,FVector B,FVector C,FVector D,int32 M){Triangle(A,B,C,M);Triangle(A,C,D,M);}
    void Hull(const FVector (&V)[8])
    {
        FVector Center=FVector::ZeroVector;for (auto P:V) Center+=P;Center/=8;
        auto& C=Collision.ConvexElems.Emplace_GetRef();for (auto P:V) C.VertexData.AddUnique(P-Center);
        C.SetTransform(FTransform(Center));C.UpdateElemBox();
    }
    void Prism(FVector A,FVector B,FVector SA,FVector SB,double Half,double Height,int32 M,bool Top=true)
    {
        const FVector V[8]={A-SA*Half-FVector(0,0,Height),B-SB*Half-FVector(0,0,Height),B+SB*Half-FVector(0,0,Height),A+SA*Half-FVector(0,0,Height),
            A-SA*Half,B-SB*Half,B+SB*Half,A+SA*Half};
        if (Top) Quad(V[4],V[5],V[6],V[7],M);
        Quad(V[3],V[2],V[1],V[0],M);Quad(V[0],V[1],V[5],V[4],M);Quad(V[2],V[3],V[7],V[6],M);
        Quad(V[1],V[2],V[6],V[5],M);Quad(V[3],V[0],V[4],V[7],M);Hull(V);
    }
};
}

AParametricCityLink::AParametricCityLink(const FObjectInitializer& I):Super(I)
{
    PrimaryActorTick.bCanEverTick=false;bEnableComputeMeshPool=false;
    Points={{0,0,0},{1800,0,0}};
    GetDynamicMeshComponent()->bUseAsyncCooking=false;GetDynamicMeshComponent()->SetCanEverAffectNavigation(true);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> M(TEXT("/Game/GameplayActors/LobbyPlatform/Materials/M_LobbyTrim"));
    if (M.Succeeded()) SurfaceMaterial=M.Object;
}
void AParametricCityLink::OnConstruction(const FTransform& T){Super::OnConstruction(T);RebuildLink();}
void AParametricCityLink::PostRegisterAllComponents(){Super::PostRegisterAllComponents();if (!IsTemplate()) RebuildLink();}

void AParametricCityLink::RebuildLink()
{
    if (IsTemplate() || bBuildingMesh) return;
    bBuildingMesh=true;ON_SCOPE_EXIT{bBuildingMesh=false;};
    GenerationWarning.Reset();
    auto Bad=[&](const TCHAR* S){GenerationWarning=S;};
    if (Points.Num()<2 || Points.Num()>256) {Bad(TEXT("Use 2..256 centreline points. Previous mesh retained."));return;}
    for (auto P:Points) if (P.ContainsNaN()){Bad(TEXT("Non-finite route point."));return;}
    for (float V:{Width,Thickness,MaxStepRise,MinStepTread,RailHeight,GuideWidth,DashLength,DashGap}) if (!FMath::IsFinite(V)) {Bad(TEXT("Non-finite dimensions."));return;}
    if (Width<120 || Width>5000 || Thickness<5 || Thickness>1000 || MaxStepRise<8 || MaxStepRise>30 || MinStepTread<25 ||
        RailHeight<60 || RailHeight>300 || GuideWidth<2 || GuideWidth>Width*.1 || DashLength<20 || DashGap<10)
    {Bad(TEXT("Invalid route, paint or stair dimensions."));return;}
    TArray<double> Distances;Distances.Add(0);
    for (int32 I=1;I<Points.Num();++I)
    {const double L=FVector2D(Points[I]-Points[I-1]).Length();if (L<.1){Bad(TEXT("Consecutive points require horizontal separation."));return;}Distances.Add(Distances.Last()+L);}
    const double Total=Distances.Last(),Rise=Points.Last().Z-Points[0].Z;
    if (Total>300000){Bad(TEXT("Route exceeds the 3 km mesh budget."));return;}
    const bool Stairs=Kind==ECityLinkKind::Stairs && FMath::Abs(Rise)>.01;
    const int32 StepCount=Stairs?FMath::Max(1,FMath::CeilToInt(FMath::Abs(Rise)/MaxStepRise)):0;
    if (StepCount>512 || (StepCount && Total/StepCount<MinStepTread)){Bad(TEXT("Stairs cannot satisfy minimum tread / maximum rise. Extend the path."));return;}
    auto Sample=[&](double D)
    {
        const double At=FMath::Clamp(D,0.,Total);int32 I=1;while (I<Distances.Num()-1 && Distances[I]<At) ++I;
        return FMath::Lerp(Points[I-1],Points[I],(At-Distances[I-1])/(Distances[I]-Distances[I-1]));
    };
    auto Side=[&](double D){const FVector Dir=Sample(FMath::Min(Total,D+2))-Sample(FMath::Max(0.,D-2));return FVector(-Dir.Y,Dir.X,0).GetSafeNormal();};
    TArray<double> Cuts=Distances;
    // Split at material boundaries, so dash/edge marks are real coplanar material regions.
    for (double D=0;D<Total;D+=DashLength+DashGap) {Cuts.Add(D);Cuts.Add(FMath::Min(Total,D+DashLength));}
    for (int32 S=1;S<StepCount;++S) Cuts.Add(Total*S/StepCount);
    Cuts.Sort();for (int32 I=Cuts.Num()-1;I>0;--I) if (Cuts[I]-Cuts[I-1]<.05) Cuts.RemoveAt(I);
    if (Cuts.Num()>6000){Bad(TEXT("Route subdivision budget exceeded."));return;}
    CityLinkMesh::FBuilder B;
    const double H=Width*.5,Inset=12,G=GuideWidth;
    const double Bands[]={-H,-H+Inset,-H+Inset+G,-G,G,H-Inset-G,H-Inset,H};
    for (int32 I=1;I<Cuts.Num();++I)
    {
        FVector A=Sample(Cuts[I-1]),Z=Sample(Cuts[I]);const FVector SA=Side(Cuts[I-1]),SZ=Side(Cuts[I]);
        const double Mid=(Cuts[I-1]+Cuts[I])*.5;
        if (Stairs)
        {const int32 S=FMath::Min(StepCount-1,FMath::FloorToInt(Mid/Total*StepCount));A.Z=Z.Z=Points[0].Z+Rise*(S+(Rise>0?1:0))/StepCount;}
        B.Prism(A,Z,SA,SZ,H,Thickness,0,false);
        const bool Dash=FMath::Fmod(Mid,DashLength+DashGap)<DashLength;
        for (int32 S=0;S<7;++S)
        {
            const int32 Material=(S==1 || S==5)?1:(S==3 && Dash?2:0);
            B.Quad(A+SA*Bands[S],Z+SZ*Bands[S],Z+SZ*Bands[S+1],A+SA*Bands[S+1],Material);
        }
        if (bGuardRails)
            for (double Sign:{-1.,1.}) for (double Height:{double(RailHeight),RailHeight*.5})
                B.Prism(A+SA*(H-7)*Sign+FVector(0,0,Height),Z+SZ*(H-7)*Sign+FVector(0,0,Height),SA,SZ,6,8,3);
    }
    if (bGuardRails) for (double D=0;D<=Total;D+=220)
    {
        FVector P=Sample(D);const FVector S=Side(D),T(S.Y,-S.X,0);
        if (Stairs){const int32 Step=FMath::Min(StepCount-1,FMath::FloorToInt(D/Total*StepCount));P.Z=Points[0].Z+Rise*(Step+(Rise>0?1:0))/StepCount;}
        for (double Sign:{-1.,1.})
        {const FVector C=P+S*(H-7)*Sign+FVector(0,0,RailHeight);B.Prism(C-T*6,C+T*6,S,S,6,RailHeight,3);}
    }
    auto* Component=GetDynamicMeshComponent();const bool WasRegistered=Component->IsRegistered();
    if (WasRegistered) Component->UnregisterComponent();
    Component->SetTransientDeferCollisionUpdates(true);
    auto* Replacement=NewObject<UDynamicMesh>(Component);Replacement->SetMesh(MoveTemp(B.Mesh));Component->SetDynamicMesh(Replacement);
    Component->SetComplexAsSimpleCollisionEnabled(false,false);
    Component->CollisionType=CTF_UseSimpleAsComplex;
    Component->SetSimpleCollisionShapes(B.Collision,false);
    Component->SetCollisionProfileName(TEXT("BlockAll"));Component->SetCollisionEnabled(bEnableCollision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    Component->SetCollisionObjectType(ECC_WorldStatic);Component->SetCollisionResponseToAllChannels(ECR_Block);
    UMaterialInterface* Parent=SurfaceMaterial?SurfaceMaterial.Get():UMaterial::GetDefaultMaterial(MD_Surface);
    Materials.Reset();const FLinearColor Colors[]={RoadColor,GuideColor,DashColor,GuideColor*.35f};
    for (int32 I=0;I<4;++I)
    {
        auto* M=UMaterialInstanceDynamic::Create(Parent,this);M->SetFlags(RF_Transient);M->SetVectorParameterValue(TEXT("Color"),Colors[I]);
        M->SetScalarParameterValue(TEXT("Roughness"),.55f);M->SetScalarParameterValue(TEXT("Metallic"),.15f);
        M->SetScalarParameterValue(TEXT("Emission"),I==1?.8f:.08f);M->SetScalarParameterValue(TEXT("InteriorEnabled"),0);
        Materials.Add(M);Component->SetMaterial(I,M);
    }
    Component->SetTransientDeferCollisionUpdates(false);Component->UpdateCollision(false);
    if (WasRegistered) Component->RegisterComponent();
    Component->NotifyMeshUpdated();
    GeneratedLength=Total;GeneratedSteps=StepCount;
}
