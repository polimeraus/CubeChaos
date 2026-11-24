#include "PickupRetroMesh.h"
#include "ParametricPickup.h"
#include "Algo/Reverse.h"
#include "CompGeom/PolygonTriangulation.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

using namespace UE::Geometry;

namespace
{
    using FOutline = TArray<FVector2d>;
    // UV1.x encodes the housing finish. Slot zero retains the actor's procedural icon material.
    enum class EFinish : uint8 { Enamel, Metal, Inlay, Socket, Lamp };
    struct FSection { double Depth, Width, Height; };

    struct FFace
    {
        FVector Centre, Normal, Right, Up;

        FFace(const FVector& P, const FVector& Direction) : Centre(P), Normal(Direction.GetSafeNormal())
        {
            Right = FMath::Abs(Normal.Z) > .99 ? FVector::ForwardVector
                : FVector::CrossProduct(Normal, FVector::UpVector).GetSafeNormal();
            Up = FVector::CrossProduct(Right, Normal).GetSafeNormal();
        }
        FVector At(double X, double Y, double D) const { return Centre + X*Right + Y*Up + D*Normal; }
        FFace Offset(double X, double Y, double D=0.) const
        {
            FFace Copy = *this;
            Copy.Centre = At(X,Y,D);
            return Copy;
        }
    };

    FOutline Circle(int32 Count, int32 Lobes=0, double Scallop=0.)
    {
        FOutline Points;
        Points.Reserve(Count);
        for (int32 I=0; I<Count; ++I)
        {
            const double A = UE_TWO_PI * I / Count;
            const double Radius = 1. - Scallop*.5*(1.-FMath::Cos((A-UE_HALF_PI)*Lobes));
            Points.Emplace(Radius*FMath::Cos(A),Radius*FMath::Sin(A));
        }
        return Points;
    }

    FOutline RoundedPolygon(FOutline Corners, double Fillet, int32 Steps)
    {
        const int32 Count = Corners.Num();
        double Area = 0.;
        for (int32 I=0; I<Count; ++I)
        {
            const auto& P=Corners[I]; const auto& Q=Corners[(I+1)%Count];
            Area += P.X*Q.Y-P.Y*Q.X;
        }
        if (Area<0.) Algo::Reverse(Corners);
        FOutline Points;
        Points.Reserve(Count*(Steps+1));
        for (int32 I=0; I<Count; ++I)
        {
            const auto& P=Corners[I];
            const auto A=FMath::Lerp(P,Corners[(I+Count-1)%Count],Fillet);
            const auto B=FMath::Lerp(P,Corners[(I+1)%Count],Fillet);
            for (int32 J=0; J<=Steps; ++J)
            {
                const double T = double(J)/Steps;
                Points.Add((1.-T)*(1.-T)*A + (2.*T*(1.-T))*P + T*T*B);
            }
        }
        return Points;
    }

    class FHousingBuilder
    {
        FDynamicMesh3& Mesh;
        const double Size, Depth, Bezel;
        const int32 Detail, Lights, Petals;
        const FOutline Round, SmallRound;

        int32 Vertex(const FVector& P, const FVector& N, const FVector2f& UV, EFinish Finish)
        {
            const int32 I=Mesh.AppendVertex(Size*FVector(P.X*Depth,P.Y,P.Z+.5));
            Mesh.Attributes()->PrimaryNormals()->AppendElement(FVector3f(FVector(N.X/Depth,N.Y,N.Z).GetSafeNormal()));
            Mesh.Attributes()->PrimaryUV()->AppendElement(UV);
            Mesh.Attributes()->GetUVLayer(1)->AppendElement(FVector2f(uint8(Finish),0));
            return I;
        }

        void Triangle(int32 A, int32 B, int32 C, int32 Slot)
        {
            const FIndex3i Tri(A,B,C);
            const int32 ID=Mesh.AppendTriangle(Tri);
            check(ID>=0);
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID,Tri);
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID,Tri);
            Mesh.Attributes()->GetUVLayer(1)->SetTriangle(ID,Tri);
            Mesh.Attributes()->GetMaterialID()->SetValue(ID,Slot);
        }

        void Cap(const FFace& F, const FOutline& Loop, const FSection& S, double Sign, EFinish Finish, int32 Slot=1)
        {
            FVector2d Mid=FVector2d::ZeroVector;
            for (const FVector2d& P:Loop) Mid+=P;
            Mid/=Loop.Num();
            const auto UV=[](const FVector2d& P){return FVector2f(.5+.5*P.X,.5-.5*P.Y);};
            const FVector N=F.Normal*Sign;
            const int32 First=Mesh.VertexCount();
            for (const FVector2d& P:Loop)
                Vertex(F.At(P.X*S.Width,P.Y*S.Height,S.Depth),N,UV(P),Finish);
            // The centre sees every edge of a round lens or rosette. Concave feather tips can hide an edge.
            bool bFan=true;
            for (int32 I=0; I<Loop.Num(); ++I)
                bFan &= FVector2d::CrossProduct(Loop[I]-Mid,Loop[(I+1)%Loop.Num()]-Mid)>1.e-10;
            auto FaceTriangle=[&](int32 A,int32 B,int32 C)
            {
                if (Sign>0.) Triangle(A,B,C,Slot);
                else Triangle(A,C,B,Slot);
            };
            if (bFan)
            {
                const int32 Centre=Vertex(F.At(Mid.X*S.Width,Mid.Y*S.Height,S.Depth),N,UV(Mid),Finish);
                for (int32 I=0; I<Loop.Num(); ++I)
                    FaceTriangle(Centre,First+I,First+(I+1)%Loop.Num());
            }
            else
            {
                TArray<FIndex3i> Triangles;
                PolygonTriangulation::TriangulateSimplePolygon(Loop,Triangles,false);
                for (const FIndex3i& T:Triangles) FaceTriangle(First+T.A,First+T.B,First+T.C);
            }
        }

        void Loft(const FFace& F, const FOutline& Loop, const TArray<FSection>& Sections, EFinish Finish, bool IconFront=false)
        {
            const int32 Count=Loop.Num();
            const auto Point=[&](int32 Ring,int32 I)
            {
                const FSection& S=Sections[Ring];
                return F.At(Loop[I].X*S.Width,Loop[I].Y*S.Height,S.Depth);
            };
            const int32 First=Mesh.VertexCount();
            for (int32 Ring=0; Ring<Sections.Num(); ++Ring)
            {
                for (int32 I=0; I<=Count; ++I)
                {
                    const int32 K=I%Count;
                    const FVector Along=Point(FMath::Min(Ring+1,Sections.Num()-1),K)-Point(FMath::Max(0,Ring-1),K);
                    const FVector Around=Point(Ring,(K+1)%Count)-Point(Ring,(K+Count-1)%Count);
                    // These vectors use housing-width units, so a tiny lamp bevel needs a smaller squared tolerance.
                    Vertex(Point(Ring,K),FVector::CrossProduct(Along,Around).GetSafeNormal(1.e-20),
                           FVector2f(float(I)/Count,float(Ring)/(Sections.Num()-1)),Finish);
                }
            }
            for (int32 Ring=0; Ring+1<Sections.Num(); ++Ring)
                for (int32 I=0; I<Count; ++I)
                {
                    const int32 A=First+Ring*(Count+1)+I, B=A+Count+1;
                    Triangle(A,B+1,B,1); Triangle(A,A+1,B+1,1);
                }
            Cap(F,Loop,Sections[0],-1.,Finish);
            Cap(F,Loop,Sections.Last(),1.,Finish,IconFront ? 0 : 1);
        }

        void Badge(const FFace& F,double W,double H)
        {
            const double Lip=1.-.135*Bezel;
            Loft(F,Round,{{-.018,.95*W,.95*H},{-.004,W,H},{.009,W,H},{.021,.96*W,.96*H}},EFinish::Metal);
            Loft(F,Round,{{.019,.955*W,.955*H},{.026,.94*W,.94*H},{.029,(Lip+.024)*W,(Lip+.024)*H}},EFinish::Inlay);
            Loft(F,Round,{{.028,(Lip+.019)*W,(Lip+.019)*H},{.035,Lip*W,Lip*H},{.038,(Lip-.018)*W,(Lip-.018)*H}},EFinish::Socket,true);
        }

        void Bulb(const FFace& F,double R)
        {
            Loft(F,SmallRound,{{-.003,R*1.18,R*1.18},{.0035,R*1.18,R*1.18},{.006,R,R}},EFinish::Metal);
            Loft(F,SmallRound,{{.005,R*.93,R*.93},{.010,R*.84,R*.84},{.015,R*.42,R*.42}},EFinish::Lamp);
        }

        void ArcLights(const FFace& F,double W,double H,double A,double B,double Radius)
        {
            for (int32 I=0; I<Lights; ++I)
            {
                const double T=Lights==1 ? .5 : double(I)/(Lights-1);
                const double Angle=FMath::DegreesToRadians(FMath::Lerp(A,B,T));
                Bulb(F.Offset(W*FMath::Cos(Angle),H*FMath::Sin(Angle)),Radius);
            }
        }

        void Token()
        {
            const FFace F(FVector::ZeroVector,FVector::ForwardVector);
            // Three swept feathers on each side give the token a silhouette at an oblique angle.
            for (double Sign:{-1.,1.})
                for (int32 I=0; I<3; ++I)
                {
                    const double Y=.105-I*.081;
                    const double Tip=.50-I*.034;
                    const FOutline Wing=RoundedPolygon({{Sign*.19,Y-.058},{Sign*.32,Y-.065},{Sign*(Tip-.025),Y+.014},
                        {Sign*Tip,Y+.107},{Sign*(Tip-.097),Y+.063},{Sign*.22,Y+.035}},.19,3);
                    Loft(F,Wing,{{-.055,.98,.98},{-.043,1.,1.},{.043,1.,1.},{.055,.98,.98}},EFinish::Metal);
                    for (double Face:{-1.,1.})
                    {
                        const FOutline Stripe=RoundedPolygon({{Sign*.28,Y-.027},{Sign*(Tip-.041),Y+.024},
                            {Sign*(Tip-.025),Y+.065},{Sign*.30,Y+.009}},.18,2);
                        const double D=Face*.056;
                        Loft(F,Stripe,{{D-.002,1.,1.},{D+.002,1.,1.}},I==1 ? EFinish::Enamel : EFinish::Inlay);
                    }
                }
            const FOutline Edge=Circle(Detail*4,12,.037);
            Loft(F,Edge,{{-.118,.29,.405},{-.09,.315,.435},{.09,.315,.435},{.118,.29,.405}},EFinish::Enamel);
            Loft(F,Edge,{{-.018,.317,.437},{.018,.317,.437}},EFinish::Metal);
            for (double S:{-1.,1.})
            {
                const FFace Side(FVector(S*.116,0,0),FVector(S,0,0));
                Badge(Side,.275,.305);
                ArcLights(Side.Offset(0,0,.002),.244,.357,46,134,.0125);
                Loft(Side.Offset(0,-.362,.003),SmallRound,{{0,.050,.014},{.007,.041,.010}},EFinish::Inlay);
            }
        }

        void Jukebox()
        {
            FOutline Arch={{-.84,-1.},{.84,-1.},{1.,-.82},{1.,.17}};
            for (int32 I=1; I<=Detail*2; ++I)
            {
                const double A=UE_PI*I/(Detail*2);
                Arch.Emplace(FMath::Cos(A),.17+.83*FMath::Sin(A));
            }
            Arch.Emplace(-1.,-.82);
            const FFace F(FVector(0,0,.025),FVector::ForwardVector);
            Loft(F,Arch,{{-.219,.345,.425},{-.186,.382,.462},{-.15,.394,.472},{.15,.394,.472},
                         {.186,.382,.462},{.219,.345,.425}},EFinish::Enamel);
            const FOutline Plinth=RoundedPolygon({{-.89,-1.},{.89,-1.},{1.,-.40},{.94,.76},{.81,1.},{-.81,1.},{-.94,.76},{-1.,-.40}},.18,3);
            Loft(F.Offset(0,-.464),Plinth,{{-.213,.34,.052},{-.18,.375,.058},{.18,.375,.058},{.213,.34,.052}},EFinish::Metal);
            for (double S:{-1.,1.})
            {
                const FFace Side(FVector(S*.217,0,.025),FVector(S,0,0));
                Loft(Side,Arch,{{-.013,.351,.432},{.006,.349,.430},{.013,.337,.414}},EFinish::Metal);
                Loft(Side,Arch,{{.012,.33,.407},{.017,.324,.400}},EFinish::Inlay);
                Loft(Side,Arch,{{.016,.306,.380},{.023,.302,.375}},EFinish::Enamel);
                Badge(Side.Offset(0,.047,.023),.248,.246);
                ArcLights(Side.Offset(0,.046,.026),.263,.295,28,152,.012);
                for (int32 I=0; I<3; ++I)
                {
                    const double W=.208-I*.029;
                    const FFace Slat=Side.Offset(0,-.232-I*.043,.024);
                    Loft(Slat,Round,{{0.,W,.0125},{.007,W*.98,.012},{.011,W*.93,.007}},I==1 ? EFinish::Metal : EFinish::Inlay);
                }
                // The jukebox spins in play. Its narrow sides carry a smaller emblem and two speaker slots.
                const FFace Panel(FVector(0,S*.391,-.012),FVector(0,S,0));
                Badge(Panel,.126,.143);
                for (int32 I=0; I<2; ++I)
                {
                    const double W=.103-I*.018;
                    Loft(Panel.Offset(0,-.196-I*.041),Round,{{0.,W,.013},{.008,W*.96,.010}},
                         I==0 ? EFinish::Inlay : EFinish::Metal);
                }
            }
        }

        void Saucer()
        {
            const FFace Z(FVector::ZeroVector,FVector::UpVector);
            Loft(Z,Round,{{-.36,.095,.095},{-.331,.143,.143},{-.25,.166,.166},{-.17,.26,.26},{-.095,.419,.419},
                          {-.043,.485,.485},{.002,.491,.491},{.073,.415,.415},{.112,.302,.302}},EFinish::Enamel);
            Loft(Z,Round,{{-.046,.485,.485},{-.031,.50,.50},{-.008,.50,.50},{.003,.49,.49}},EFinish::Metal);
            Loft(Z,Round,{{.005,.487,.487},{.021,.471,.471}},EFinish::Inlay);
            Loft(Z,Round,{{-.385,.072,.072},{-.366,.114,.114},{-.344,.128,.128}},EFinish::Lamp);
            Loft(Z,Round,{{.086,.317,.317},{.108,.327,.327},{.13,.310,.310}},EFinish::Metal);
            Loft(Z,Round,{{.12,.306,.306},{.176,.302,.302},{.249,.26,.26},{.313,.20,.20},
                          {.359,.129,.129},{.373,.066,.066}},EFinish::Enamel);
            Loft(Z,Round,{{.365,.079,.079},{.389,.074,.074},{.405,.045,.045}},EFinish::Inlay);
            for (int32 I=0; I<4; ++I)
            {
                const double A=I*UE_HALF_PI;
                const FVector Radial(FMath::Cos(A),FMath::Sin(A),0);
                const FFace Window(Radial*.276+FVector(0,0,.216),Radial+FVector(0,0,.36));
                Badge(Window,.159,.143);
            }
            for (int32 I=0; I<Lights; ++I)
            {
                const double A=UE_TWO_PI*I/FMath::Max(1,Lights)+UE_PI*.125;
                const FVector R(FMath::Cos(A),FMath::Sin(A),0);
                Bulb(FFace(R*.405+FVector(0,0,.080),R*.42+FVector::UpVector),.017);
            }
        }

        void Daisy()
        {
            const FFace F(FVector::ZeroVector,FVector::ForwardVector);
            const FOutline Bloom=Circle(Petals*FMath::Max(6,Detail),Petals,.255);
            Loft(F,Bloom,{{-.122,.426,.426},{-.089,.480,.480},{-.046,.492,.492},
                          {.046,.492,.492},{.089,.480,.480},{.122,.426,.426}},EFinish::Metal);
            for (double S:{-1.,1.})
            {
                const FFace Face(FVector(S*.095,0,0),FVector(S,0,0));
                Loft(Face,Bloom,{{-.006,.475,.475},{.017,.461,.461},{.036,.43,.43}},EFinish::Inlay);
                Loft(Face,Bloom,{{.034,.424,.424},{.058,.405,.405},{.070,.373,.373}},EFinish::Enamel);
                Badge(Face.Offset(0,0,.071),.234,.234);
                for (int32 I=0; I<Petals; ++I)
                {
                    const double A=UE_HALF_PI+UE_TWO_PI*I/Petals;
                    const FFace Seed=Face.Offset(.316*FMath::Cos(A),.316*FMath::Sin(A),.069);
                    Loft(Seed,SmallRound,{{0.,.020,.020},{.009,.016,.016}},EFinish::Inlay);
                }
                for (int32 I=0; I<Lights; ++I)
                {
                    const double A=UE_TWO_PI*I/FMath::Max(1,Lights)+UE_HALF_PI;
                    Bulb(Face.Offset(.26*FMath::Cos(A),.26*FMath::Sin(A),.071),.0105);
                }
            }
        }

    public:
        FHousingBuilder(FDynamicMesh3& Target,const AParametricPickup& P,double InSize,double InDepth)
            : Mesh(Target),Size(InSize),Depth(InDepth),
              Bezel(FMath::IsFinite(P.BezelWidth) ? FMath::Clamp(P.BezelWidth,.5f,1.6f) : 1.f),
              Detail(FMath::Clamp(P.SurfaceSubdivisions,4,32)),Lights(FMath::Clamp(P.BulbCount,0,16)),
              Petals(FMath::Clamp(P.PetalCount,5,12)),Round(Circle(Detail*2)),SmallRound(Circle(FMath::Clamp(Detail,8,16)))
        {
            Mesh.EnableAttributes();
            Mesh.Attributes()->SetNumUVLayers(2);
            Mesh.Attributes()->EnableMaterialID();
        }
        void Make(EPickupShape Shape)
        {
            switch (Shape)
            {
            case EPickupShape::WingedToken: Token(); break;
            case EPickupShape::Jukebox: Jukebox(); break;
            case EPickupShape::FlyingSaucer: Saucer(); break;
            case EPickupShape::DaisyCharm: Daisy(); break;
            default: checkNoEntry();
            }
        }
    };
}

bool PickupRetro::IsHousing(EPickupShape Shape)
{
    return Shape>=EPickupShape::WingedToken && Shape<=EPickupShape::DaisyCharm;
}

void PickupRetro::Build(FDynamicMesh3& Mesh,const AParametricPickup& Pickup,double Size,double Depth)
{
    FHousingBuilder(Mesh,Pickup,Size,Depth).Make(Pickup.Shape);
}
