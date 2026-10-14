#include "ParametricFantasyHouse.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "GeometryScript/MeshBasicEditFunctions.h"
#include "GeometryScript/MeshBooleanFunctions.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "GeometryScript/MeshRepairFunctions.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UDynamicMesh.h"
#include "UObject/ConstructorHelpers.h"

namespace FantasyHouse
{
using Primitives = UGeometryScriptLibrary_MeshPrimitiveFunctions;
using Edit = UGeometryScriptLibrary_MeshBasicEditFunctions;
// Slot 3 is kept unused so existing door / apex material overrides retain their IDs.
constexpr int32 WallID=0, RoofID=1, TrimID=2, DoorID=4, ApexID=5, StairID=6;

FGeometryScriptPrimitiveOptions Options(int32 Material)
{
    FGeometryScriptPrimitiveOptions O;
    O.MaterialID = Material;
    return O;
}

FVector Radial(double Degrees)
{
    const double A = FMath::DegreesToRadians(Degrees);
    return FVector(FMath::Cos(A), FMath::Sin(A), 0);
}

void Revolve(UDynamicMesh* Mesh, const TArray<FVector2D>& Profile, FVector Center, int32 Segments,
    int32 Material, double Arc=360, double Start=0)
{
    const int32 FirstTriangle = Mesh->GetMeshRef().MaxTriangleID();
    FGeometryScriptRevolveOptions O;
    O.RevolveDegrees = Arc;
    O.DegreeOffset = Start;
    Primitives::AppendRevolvePolygon(Mesh, Options(Material), FTransform(Center), Profile, O, 0, Segments);
    // UE 5.8's revolve implementation does not propagate PrimitiveOptions.MaterialID.
    // Assign only this append's triangles, preserving the other five palette slots.
    Mesh->EditMesh([&](UE::Geometry::FDynamicMesh3& M)
    {
        M.EnableAttributes();
        M.Attributes()->EnableMaterialID();
        for (int32 TID=FirstTriangle;TID<M.MaxTriangleID();++TID)
            if (M.IsTriangle(TID)) M.Attributes()->GetMaterialID()->SetValue(TID,Material);
    });
}

void Band(UDynamicMesh* Mesh, FVector Center, double R, double Z, double Width, double Height,
    int32 Segments, int32 Material=TrimID, double Arc=360, double Start=0)
{
    Revolve(Mesh, {{R-Width,Z}, {R,Z}, {R,Z+Height}, {R-Width,Z+Height}}, Center, Segments, Material, Arc, Start);
}

void Box(UDynamicMesh* Mesh, FVector Center, FVector Size, int32 Material, FQuat Rotation=FQuat::Identity)
{
    Primitives::AppendBox(Mesh, Options(Material), FTransform(Rotation,Center), Size.X,Size.Y,Size.Z,
        0,0,0,EGeometryScriptPrimitiveOriginMode::Center);
}

void Beam(UDynamicMesh* Mesh, FVector A, FVector B, double Width, int32 Material=TrimID)
{
    if (A.Equals(B,.01)) return;
    Box(Mesh,(A+B)*.5,FVector(Width,Width,(B-A).Length()),Material,
        FRotationMatrix::MakeFromZ(B-A).ToQuat());
}

TArray<FVector2D> Arch(double W, double H, EFantasyOpeningShape Shape)
{
    TArray<FVector2D> P = {{-W*.5,0},{W*.5,0}};
    if (Shape==EFantasyOpeningShape::Rectangle)
    {
        P.Append({FVector2D(W*.5,H),FVector2D(-W*.5,H)});
    }
    else
    {
        const double Rise = FMath::Min(W*(Shape==EFantasyOpeningShape::RoundArch ? .5 : .85),H*.55);
        for (int32 I=0;I<=6;++I)
        {
            const double A=PI*I/6.;
            double Z = FMath::Sin(A);
            if (Shape==EFantasyOpeningShape::PointedArch) Z = 1-FMath::Pow(FMath::Abs(FMath::Cos(A)),.75);
            P.Add({W*.5*FMath::Cos(A),H-Rise+Rise*Z});
        }
    }
    return P;
}

FTransform Facade(FVector Center, double Radius, double Z, double Angle)
{
    const FVector N = Radial(Angle), T(-N.Y,N.X,0);
    return FTransform(FRotationMatrix::MakeFromXZ(T,N).ToQuat(), Center+N*Radius+FVector(0,0,Z));
}

void Extrude(UDynamicMesh* Mesh,const TArray<FVector2D>& P,const FTransform& Transform,double Depth,int32 Material)
{
    Primitives::AppendSimpleExtrudePolygon(Mesh,Options(Material),Transform,P,Depth,0,true);
}

void DoorOpening(UDynamicMesh* Mesh, UDynamicMesh* Cutters, FVector Center, double R, double Z,
    double Angle, double W, double H, double Frame, double CutDepth, EFantasyOpeningShape Shape, bool bOpen)
{
    const TArray<FVector2D> P=Arch(W,H,Shape);
    // Frame is made from a handful of rectangular beams; no high-resolution trim meshes.
    const FTransform Surface=Facade(Center,R+3,Z,Angle);
    for (int32 I=0;I<P.Num();++I)
    {
        if (I==0) continue;
        const auto& A=P[I]; const auto& B=P[(I+1)%P.Num()];
        Beam(Mesh,Surface.TransformPosition({A.X,A.Y,0}),Surface.TransformPosition({B.X,B.Y,0}),Frame);
    }
    Extrude(Cutters,P,Facade(Center,R-CutDepth,Z,Angle),CutDepth*2,WallID);
    if (!bOpen)
    {
        Extrude(Mesh,P,Facade(Center,R-5,Z,Angle),2,DoorID);
        Beam(Mesh,Surface.TransformPosition({0,4,1}),Surface.TransformPosition({0,H-Frame,1}),3);
        const FVector Handle=Surface.TransformPosition({W*.22,H*.4,6});
        Box(Mesh,Handle,FVector(7,7,13),TrimID,Surface.GetRotation());
    }
}

double RoofRadius(EFantasyRoofShape Shape,double T,double Curve)
{
    switch (Shape)
    {
    case EFantasyRoofShape::Dome: return FMath::Pow(FMath::Max(0.,FMath::Cos(T*PI*.5)),FMath::Clamp(Curve*.6,.3,1.8));
    case EFantasyRoofShape::Onion: return FMath::Pow(1-T,.72)*(1+FMath::Sin(PI*T)*Curve*.9);
    case EFantasyRoofShape::Cone: return 1-T;
    default: return FMath::Pow(1-T,Curve+1.1);
    }
}

struct FBuilder
{
    AParametricFantasyHouse& House;
    UDynamicMesh* Mesh;
    UDynamicMesh* Shell;
    UDynamicMesh* Cutters;
    UDynamicMesh* RoofMesh;
    int32 Segments;
    double FloorHeight;
    double MainRadius;
    int32 Floors;

    void Apex(FVector Tip,double Scale)
    {
        const auto& A=House.Apex;
        if (A.Shape==EFantasyApexShape::None) return;
        const double Stem=FMath::Clamp(A.StemHeight,0.f,400.f)*Scale;
        const double S=FMath::Clamp(A.Size,8.f,160.f)*Scale;
        Beam(Mesh,Tip-FVector(0,0,4),Tip+FVector(0,0,Stem),6*Scale);
        Tip.Z += Stem+S;
        if (A.Shape==EFantasyApexShape::Star)
        {
            TArray<FVector2D> Star;
            const int32 Points=FMath::Clamp(A.StarPoints,4,8);
            for (int32 I=0;I<Points*2;++I)
            {
                const double Angle=PI*.5+I*PI/Points, R=I%2 ? S*.42 : S;
                Star.Add({FMath::Cos(Angle)*R,FMath::Sin(Angle)*R});
            }
            Extrude(Mesh,Star,Facade(Tip,-4*Scale,0,A.Yaw),8*Scale,ApexID);
        }
        else if (A.Shape==EFantasyApexShape::Orb)
            Primitives::AppendSphereLatLong(Mesh,Options(ApexID),FTransform(Tip),S,8,5);
        else
            Revolve(Mesh,{{0,-S},{S*.32,0},{0,S*1.8}},Tip,6,ApexID);
    }

    void Roof(FVector Center,double R,double Z,double H,EFantasyRoofShape Shape,double Scale)
    {
        const auto& Settings=House.Roof;
        const int32 Steps=FMath::Clamp(Settings.ProfileSteps,4,16);
        const double Curve=FMath::Clamp(Settings.Curvature,.3f,3.f);
        FVector Lean(Settings.TipOffset.X,Settings.TipOffset.Y,0);
        Lean=Lean.GetClampedToMaxSize(H*.6)*Scale;
        TArray<FVector2D> Profile={{0,0},{R,0}};
        for (int32 I=1;I<Steps;++I)
        {
            const double T=double(I)/Steps;
            Profile.Add({R*RoofRadius(Shape,T,Curve),H*T});
        }
        Profile.Add({0,H});
        RoofMesh->Reset();
        Revolve(RoofMesh,Profile,FVector::ZeroVector,Segments,RoofID);
        RoofMesh->EditMesh([&](UE::Geometry::FDynamicMesh3& M)
        {
            for (int32 VID:M.VertexIndicesItr())
            {
                FVector P=M.GetVertex(VID);
                const double T=FMath::Clamp(P.Z/H,0.,1.);
                P+=Lean*T*T;
                M.SetVertex(VID,P);
            }
        });
        Edit::AppendMesh(Mesh,RoofMesh,FTransform(Center+FVector(0,0,Z)));
        Band(Mesh,Center,R+4,Z-10,15,12,Segments);
        if (Settings.bRibs)
        {
            const int32 Ribs=FMath::Clamp(Settings.RibCount,3,12);
            for (int32 J=0;J<Ribs;++J)
            {
                // Follow a roof meridian at a polygon vertex so the ribs remain on the faceted surface.
                const double Angle=FMath::RoundToInt(double(J)*Segments/Ribs)*360./Segments;
                FVector Last=Center+Radial(Angle)*(R+2)+FVector(0,0,Z);
                for (int32 I=1;I<=Steps;++I)
                {
                    const double T=double(I)/Steps;
                    const FVector P=Center+Radial(Angle)*(R*RoofRadius(Shape,T,Curve)+2*(1-T))+
                        FVector(0,0,Z+H*T)+Lean*T*T;
                    Beam(Mesh,Last,P,FMath::Max(2.,4*Scale)); Last=P;
                }
            }
        }
        Apex(Center+FVector(0,0,Z+H)+Lean,Scale);
    }

    void Tower(FVector Center,double R,int32 Storeys,double FH,double TopScale,bool Main,EFantasyRoofShape Shape,double RoofH)
    {
        const double H=Storeys*FH, RT=R*TopScale;
        const double Wall=FMath::Min(double(FMath::Clamp(House.Body.WallThickness,8.f,60.f)),R*.25);
        const double Scale=Main?1:R/MainRadius;
        Shell->Reset(); Cutters->Reset();
        Revolve(Shell,{{R-Wall,24},{R,24},{RT,H},{RT-Wall,H}},Center,Segments,WallID);
        // Foundation is also the ground floor, where the interior staircase starts.
        Revolve(Mesh,{{0,0},{R+12,0},{R+12,26},{0,26}},Center,Segments,WallID);
        Band(Mesh,Center,R+18,14,24,16,Segments);
        for (int32 F=1;F<=Storeys;++F)
        {
            if (House.Body.bFloorBands)
                Band(Mesh,Center,FMath::Lerp(R,RT,double(F)/Storeys)+8,F*FH-14,16,15,Segments);
        }

        const auto& W=House.Windows;
        if (W.bEnabled && W.PerFloor>0)
        {
            const int32 Count=Main?FMath::Clamp(W.PerFloor,0,16):FMath::Clamp(W.PerFloor/2,3,8);
            const double Width=FMath::Min(double(FMath::Clamp(W.Width,20.f,300.f))*FMath::Sqrt(Scale),2*PI*FMath::Min(R,RT)/Count*.55);
            const double Sill=FMath::Clamp(double(W.SillHeight)*(Main?1:.7),15.,FH*.45);
            const double Height=FMath::Clamp(double(W.Height)*(Main?1:.8),30.,FH-Sill-24);
            for (int32 F=0;F<Storeys;++F)
            {
                for (int32 J=0;J<Count;++J)
                {
                    const double Angle=W.AngleOffset+J*360./Count;
                    const double Z=F*FH+Sill;
                    const double SurfaceR=FMath::Lerp(R,RT,FMath::Clamp((Z+Height*.5-24)/(H-24),0.,1.));
                    if (Main && F==0 && House.Door.bEnabled)
                    {
                        const double Clearance=FMath::RadiansToDegrees((Width+FMath::Clamp(House.Door.Width,45.f,250.f))*.6/R);
                        if (FMath::Abs(FMath::FindDeltaAngleDegrees(Angle,double(House.Door.Angle)))<Clearance) continue;
                    }
                    // Avoid windows buried inside the main tower or neighbouring turrets.
                    if (!Main && FVector::Dist2D(Center+Radial(Angle)*SurfaceR,FVector::ZeroVector)<MainRadius*1.10) continue;
                    // Windows are always actual holes: no glazing, frame, mullions, or sill mesh.
                    const double Depth=Wall+FMath::Abs(RT-R)+30;
                    Extrude(Cutters,Arch(Width,Height,W.Shape),Facade(Center,SurfaceR-Depth,Z,Angle),Depth*2,WallID);
                }
            }
        }
        if (Main && House.Door.bEnabled)
        {
            const auto& D=House.Door;
            const double Width=FMath::Min(double(FMath::Clamp(D.Width,45.f,250.f)),R*.85);
            const double Height=FMath::Clamp(double(D.Height),80.,FH-30);
            const double SurfaceR=FMath::Lerp(R,RT,(26+Height*.5)/(H));
            DoorOpening(Mesh,Cutters,Center,SurfaceR,26,D.Angle,Width,Height,11,
                Wall+FMath::Abs(RT-R)+35,D.Shape,D.bOpen);
            const int32 Steps=FMath::Clamp(D.Steps,0,8);
            const FVector N=Radial(D.Angle);
            for (int32 I=0;I<Steps;++I)
            {
                const double HeightStep=26.*(Steps-I)/Steps;
                Box(Mesh,Center+N*(R+15+I*26)+FVector(0,0,HeightStep*.5),
                    FVector(Width+30,30,HeightStep),WallID,FRotator(0,D.Angle+90,0).Quaternion());
            }
        }
        if (Cutters->GetTriangleCount()>0)
            UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(Shell,FTransform::Identity,Cutters,
                FTransform::Identity,EGeometryScriptBooleanOperation::Subtract,FGeometryScriptMeshBooleanOptions());
        Edit::AppendMesh(Mesh,Shell,FTransform::Identity);
        Roof(Center,RT+FMath::Clamp(House.Roof.Overhang,0.f,250.f)*Scale,H,RoofH,Shape,Scale);
        if (House.bFloatingBase)
        {
            const double D=FMath::Clamp(House.FloatingBaseDepth,50.f,1000.f)*Scale;
            Revolve(Mesh,{{0,-D},{R*.22,-D*.8},{R*.62,-D*.48},{R*.9,-D*.2},{R,0},{0,0}},Center,Segments,WallID);
            Band(Mesh,Center,R*.66,-D*.48,12*Scale,12*Scale,Segments);
            Revolve(Mesh,{{0,-D-40*Scale},{15*Scale,-D-10*Scale},{0,-D+10*Scale}},Center,8,TrimID);
        }
    }

    void Staircase()
    {
        const auto& S=House.Stairs;
        House.StairStepCount=0;
        House.GeneratedStairHeight=0;
        if (!S.bEnabled) return;

        constexpr double GroundZ=26;
        const double Ceiling=Floors*FloorHeight;
        const double Height=FMath::Clamp(S.bAutoHeight ?
            (Floors>1 ? (Floors-1)*FloorHeight : Ceiling-GroundZ-40) : double(S.Height),
            40.,Ceiling-GroundZ-40);
        const double NarrowRadius=MainRadius*FMath::Min(1.,double(FMath::Clamp(House.Body.TopRadiusScale,.65f,1.4f)));
        const double Wall=FMath::Min(double(FMath::Clamp(House.Body.WallThickness,8.f,60.f)),MainRadius*.25);
        const double OuterLimit=(NarrowRadius-Wall)*FMath::Cos(PI/Segments)-12;
        if (OuterLimit<=10) return;
        const double MinimumWidth=FMath::Min(30.,OuterLimit-5);
        const double Inner=FMath::Clamp(double(S.InnerRadius),5.,OuterLimit-MinimumWidth);
        const double Outer=Inner+FMath::Clamp(double(S.Width),MinimumWidth,OuterLimit-Inner);
        const double MaxRise=FMath::Clamp(S.MaxStepHeight,8.f,30.f);
        const int32 Count=FMath::CeilToInt(Height/MaxRise);
        const double Rise=Height/Count;
        const double AngleStep=360./FMath::Clamp(S.StepsPerTurn,12,48);
        const double Direction=S.bClockwise ? -1 : 1;
        House.StairStepCount=Count;
        House.GeneratedStairHeight=Height;

        for (int32 I=0;I<Count;++I)
        {
            // Each four-corner tread is one closed low-poly prism. Shared angular
            // boundaries and exact rises make a continuous climbable stair.
            double A=S.StartAngle+Direction*AngleStep*I;
            double B=A+Direction*AngleStep;
            if (A>B) Swap(A,B);
            const FVector RA=Radial(A), RB=Radial(B);
            const TArray<FVector2D> Tread={{Inner*RA.X,Inner*RA.Y},{Outer*RA.X,Outer*RA.Y},
                {Outer*RB.X,Outer*RB.Y},{Inner*RB.X,Inner*RB.Y}};
            const double Bottom=GroundZ+I*Rise;
            Extrude(Mesh,Tread,FTransform(FVector(0,0,Bottom)),Rise,StairID);
        }
    }

    void Balcony()
    {
        const auto& B=House.Balcony;
        if (!B.bEnabled) return;
        const int32 Floor=FMath::Clamp(B.Floor,0,Floors-1);
        const double Z=Floor*FloorHeight+26;
        const double Inner=FMath::Lerp(MainRadius,MainRadius*FMath::Clamp(House.Body.TopRadiusScale,.65f,1.4f),double(Floor)/Floors)-4;
        const double Outer=Inner+FMath::Clamp(B.Depth,30.f,250.f);
        const double Arc=FMath::Clamp(B.ArcDegrees,30.f,360.f), Start=B.CenterAngle-Arc*.5;
        const int32 Steps=FMath::Max(3,FMath::CeilToInt(Segments*Arc/360));
        Revolve(Mesh,{{Inner,Z-22},{Outer-15,Z-22},{Outer,Z-8},{Outer,Z},{Inner,Z}},FVector::ZeroVector,Steps,WallID,Arc,Start);
        Band(Mesh,FVector::ZeroVector,Outer+2,Z-8,12,10,Steps,TrimID,Arc,Start);
        const double RailH=FMath::Clamp(B.RailHeight,35.f,150.f);
        Band(Mesh,FVector::ZeroVector,Outer,Z+RailH,9,9,Steps,TrimID,Arc,Start);
        Band(Mesh,FVector::ZeroVector,Outer,Z+RailH*.35,6,5,Steps,TrimID,Arc,Start);
        const int32 Posts=FMath::Clamp(B.Posts,3,40);
        for (int32 I=0;I<=Posts;++I)
        {
            if (Arc==360 && I==Posts) continue;
            const double A=Start+Arc*I/Posts;
            // Polygonal rail radius at this angle, matching the revolved rail facets.
            const double Local=FMath::Fmod(Arc*I/Posts,Arc/Steps)-Arc/Steps*.5;
            const double R=(Outer-4)*FMath::Cos(FMath::DegreesToRadians(Arc/Steps*.5))/FMath::Cos(FMath::DegreesToRadians(Local));
            const FVector P=Radial(A)*R+FVector(0,0,Z);
            Beam(Mesh,P,P+FVector(0,0,RailH),7);
        }
        if (Arc<359.9)
        {
            for (double A:{Start,Start+Arc})
            {
                const FVector P=Radial(A)*Inner+FVector(0,0,Z+RailH+4);
                Beam(Mesh,P,Radial(A)*(Outer-4)+FVector(0,0,Z+RailH+4),8);
            }
        }
        for (int32 I=0;I<3;++I)
        {
            const double A=Start+Arc*(I+1)/4;
            Beam(Mesh,Radial(A)*Inner+FVector(0,0,Z-75),Radial(A)*(Outer-15)+FVector(0,0,Z-20),12);
        }
    }
};
}

AParametricFantasyHouse::AParametricFantasyHouse()
{
    PrimaryActorTick.bCanEverTick=false;
    HouseMesh=CreateDefaultSubobject<UDynamicMeshComponent>(TEXT("FantasyHouseMesh"));
    SetRootComponent(HouseMesh);
    HouseMesh->SetMobility(EComponentMobility::Movable);
    HouseMesh->SetGenerateOverlapEvents(false);
    HouseMesh->SetCollisionObjectType(ECC_WorldStatic);
    HouseMesh->SetCollisionResponseToAllChannels(ECR_Block);
    HouseMesh->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Mat(TEXT("/Game/Structures/FantasyHouse/M_FantasyHouse"));
    BaseMaterial=Mat.Get();
}

void AParametricFantasyHouse::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildHouse();
}

void AParametricFantasyHouse::BeginPlay()
{
    Super::BeginPlay();
    // Loaded levels retain the serialized mesh. Runtime spawns use OnConstruction.
    if (HouseMesh->GetDynamicMesh()->GetTriangleCount()==0) RebuildHouse();
    else UpdateMaterials();
}

void AParametricFantasyHouse::UpdateMaterials()
{
    const TArray<FLinearColor> Colors={Palette.Walls,Palette.Roof,Palette.Trim,FLinearColor::Black,Palette.Door,Palette.Apex,Palette.Stairs};
    UMaterialInterface* Base=BaseMaterial?BaseMaterial.Get():UMaterial::GetDefaultMaterial(MD_Surface);
    for (int32 I=0;I<7;++I)
    {
        if (MaterialOverrides.IsValidIndex(I) && MaterialOverrides[I])
        {
            HouseMesh->SetMaterial(I,MaterialOverrides[I]); continue;
        }
        UMaterialInstanceDynamic* MID=Cast<UMaterialInstanceDynamic>(HouseMesh->GetMaterial(I));
        if (!MID || MID->Parent!=Base) MID=UMaterialInstanceDynamic::Create(Base,this);
        MID->SetVectorParameterValue(TEXT("Tint"),Colors[I]);
        MID->SetScalarParameterValue(TEXT("Glow"),I==5?FMath::Clamp(Palette.ApexGlow,0.f,20.f):0.f);
        MID->SetScalarParameterValue(TEXT("Metallic"),I==2?.55f:.05f);
        HouseMesh->SetMaterial(I,MID);
    }
}

void AParametricFantasyHouse::RebuildHouse()
{
    if (bRebuilding || !HouseMesh || HasAnyFlags(RF_ClassDefaultObject)) return;
    TGuardValue<bool> Guard(bRebuilding,true);
    const double Start=FPlatformTime::Seconds();
    // Construct off-component, then publish once. No intermediate render/collision updates.
    UDynamicMesh* Mesh=NewObject<UDynamicMesh>(this);
    UDynamicMesh* Shell=NewObject<UDynamicMesh>(this);
    UDynamicMesh* Cutters=NewObject<UDynamicMesh>(this);
    UDynamicMesh* RoofScratch=NewObject<UDynamicMesh>(this);
    FantasyHouse::FBuilder Builder{*this,Mesh,Shell,Cutters,RoofScratch,FMath::Clamp(RadialSegments,8,32),
        FMath::Clamp(Body.FloorHeight,180.f,600.f),FMath::Clamp(Body.Radius,120.f,2000.f),FMath::Clamp(Body.Floors,1,6)};
    Builder.Tower(FVector::ZeroVector,Builder.MainRadius,Builder.Floors,Builder.FloorHeight,
        FMath::Clamp(Body.TopRadiusScale,.65f,1.4f),true,Roof.Shape,FMath::Clamp(Roof.Height,50.f,2000.f));
    Builder.Balcony();
    Builder.Staircase();
    FRandomStream Random(Seed);
    const int32 Count=FMath::Clamp(Turrets.Count,0,5);
    const double RadiusScale=FMath::Clamp(Turrets.RadiusScale,.2f,.65f);
    for (int32 I=0;I<Count;++I)
    {
        const double Angle=Turrets.StartAngle+(Count>1?FMath::Clamp(Turrets.SpreadAngle,0.f,360.f)*I/(Count==1?1:(Turrets.SpreadAngle>=359.9f?Count:Count-1)):0);
        const double V=FMath::Clamp(Turrets.HeightVariation,0.f,.5f);
        const double H=Builder.Floors*Builder.FloorHeight*FMath::Clamp(Turrets.HeightScale,.5f,1.8f)*Random.FRandRange(1-V,1+V);
        const int32 N=FMath::Clamp(FMath::RoundToInt(H/Builder.FloorHeight),1,6);
        FVector Center=FantasyHouse::Radial(Angle)*(Builder.MainRadius*FMath::Clamp(Turrets.DistanceScale,.65f,1.2f));
        Center.Z=Builder.FloorHeight*.25;
        Builder.Tower(Center,Builder.MainRadius*RadiusScale,N,H/N,1.06,false,Turrets.RoofShape,
            FMath::Clamp(Roof.Height,50.f,2000.f)*RadiusScale*1.35);
    }
    // RevolvePolygon leaves duplicate axis vertices at roof tips / closed foundations.
    // Collapse those degeneracies before normal generation and collision cooking.
    FGeometryScriptDegenerateTriangleOptions Repair;
    Repair.MinTriangleArea=1.e-7;
    Repair.MinEdgeLength=1.e-5;
    UGeometryScriptLibrary_MeshRepairFunctions::RepairMeshDegenerateGeometry(Mesh,Repair);
    FGeometryScriptWeldEdgesOptions Weld;
    Weld.Tolerance=1.e-4f;
    Weld.bOnlyUniquePairs=false;
    UGeometryScriptLibrary_MeshRepairFunctions::WeldMeshEdges(Mesh,Weld);
    if (bFlatShading) UGeometryScriptLibrary_MeshNormalsFunctions::SetPerFaceNormals(Mesh);
    else UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Mesh,FGeometryScriptCalculateNormalsOptions());
    TriangleCount=Mesh->GetTriangleCount();
    HouseMesh->SetDynamicMesh(Mesh);
    UpdateMaterials();
    HouseMesh->SetCollisionEnabled(bEnableCollision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    HouseMesh->SetComplexAsSimpleCollisionEnabled(bEnableCollision,true);
    LastBuildMilliseconds=(FPlatformTime::Seconds()-Start)*1000.;
}

void AParametricFantasyHouse::NewVariation()
{
    Modify();
    Seed=Seed==MAX_int32?0:Seed+1;
    RebuildHouse();
}

void AParametricFantasyHouse::ApplyPreset()
{
    Modify();
    Body=FFantasyBodySettings(); Roof=FFantasyRoofSettings(); Apex=FFantasyApexSettings();
    Windows=FFantasyWindowSettings(); Door=FFantasyDoorSettings(); Balcony=FFantasyBalconySettings();
    Turrets=FFantasyTurretSettings(); Palette=FFantasyPalette();
    Stairs=FFantasyStairSettings();
    bFloatingBase=false; FloatingBaseDepth=230; RadialSegments=16; bFlatShading=true; Seed=42;
    if (Preset==EFantasyHousePreset::MoonObservatory)
    {
        Body.Floors=2; Body.Radius=340; Roof.Shape=EFantasyRoofShape::Dome; Roof.Height=280;
        Roof.Curvature=1.2f; Roof.TipOffset=FVector2D::ZeroVector; Apex.Shape=EFantasyApexShape::Orb;
        Turrets.Count=1; Turrets.RoofShape=EFantasyRoofShape::Dome; Turrets.StartAngle=35;
        Windows.Shape=EFantasyOpeningShape::RoundArch; Balcony.ArcDegrees=230;
        Palette.Walls=FLinearColor(.48f,.57f,.58f); Palette.Roof=FLinearColor(.035f,.22f,.26f);
        Palette.Apex=FLinearColor(.18f,.75f,1.f);
    }
    else if (Preset==EFantasyHousePreset::FloatingPalace)
    {
        bFloatingBase=true; Body.Floors=4; Roof.Shape=EFantasyRoofShape::Onion; Roof.Height=520;
        Roof.Curvature=1.35f; Roof.TipOffset=FVector2D::ZeroVector;
        Turrets.Count=3; Turrets.StartAngle=0; Turrets.SpreadAngle=210; Turrets.HeightVariation=.35f;
        Turrets.RoofShape=EFantasyRoofShape::SweptSpire; Turrets.HeightScale=1.05f;
        Balcony.Floor=2; Balcony.ArcDegrees=220; Door.Steps=0;
        Palette.Walls=FLinearColor(.34f,.22f,.50f); Palette.Roof=FLinearColor(.16f,.055f,.24f);
    }
    RebuildHouse();
}
