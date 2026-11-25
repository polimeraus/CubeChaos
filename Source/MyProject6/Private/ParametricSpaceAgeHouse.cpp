#include "ParametricSpaceAgeHouse.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    enum EFinish : uint8 { Shell, Roof, Accent, Glass, Metal, Lamp, Terrazzo, Gasket, FinishCount };
    struct FQuad { FVector P[4]; };

    double Safe(double Value, double Min, double Max, double Default)
    {
        return FMath::Clamp(FMath::IsFinite(Value) ? Value : Default, Min, Max);
    }

    FQuad Offset(FQuad Quad, const FVector& Delta)
    {
        for (FVector& P : Quad.P) P += Delta;
        return Quad;
    }
}

// All dimensions and cuts are in actor space. Each solid is both a visible panel and a damage cell.
struct AParametricSpaceAgeHouse::FBuilder : FPanelLayout
{
    const AParametricSpaceAgeHouse& House;
    const FVector2D Half;
    const bool bOrbit;
    const double Deck, Height, Span;
    const int32 Sides;
    double DoorHalfAngle, DoorLine;
    FRandomStream Random;

    explicit FBuilder(const AParametricSpaceAgeHouse& Owner)
        : FPanelLayout(Owner.MaxGeneratedCells,0), House(Owner), Half(Owner.Footprint*.5),
          bOrbit(Owner.RoofForm==ESpaceAgeRoof::Orbit), Deck(Owner.DeckHeight), Height(Owner.WallHeight),
          Span(Owner.FragmentSpan), Sides(8*FMath::Clamp(FMath::CeilToInt(UE_TWO_PI*Half.GetMax()/(8.*Span)),5,18)),
          Random(Owner.FacadeSeed)
    {
        // Reserve a real, centred door bay even when an artist changes the facade density.
        double Lo=0, Hi=UE_PI*.48;
        for (int32 I=0; I<22; ++I)
        {
            const double Mid=(Lo+Hi)*.5;
            if (Outline(-UE_PI*.5+Mid,Half).X < House.EntranceWidth*.5) Lo=Mid; else Hi=Mid;
        }
        DoorHalfAngle=(Lo+Hi)*.5;
        DoorLine=-Outline(-UE_PI*.5+DoorHalfAngle,Half).Y;
    }

    FVector Outline(double A, FVector2D Radii) const
    {
        const double C=FMath::Cos(A), S=FMath::Sin(A);
        // A fourth-power footprint gives the butterfly house its long straight faces and round corners.
        const double K=bOrbit ? 1. : 1./FMath::Sqrt(FMath::Sqrt(C*C*C*C+S*S*S*S));
        return FVector(Radii.X*C*K,Radii.Y*S*K,0);
    }

    void Solid(const FQuad& Base,const FQuad& Top,EFinish Finish)
    {
        AddPanel(Base.P,Top.P,Finish);
    }

    void Prism(const FQuad& Face,const FVector& Thickness,EFinish Finish)
    {
        Solid(Face,Offset(Face,Thickness),Finish);
    }

    void Rod(FVector A,FVector B,double WidthA,double WidthB,double Depth,EFinish Finish)
    {
        const FVector Dir=(B-A).GetSafeNormal();
        if (Dir.IsZero()) return;
        FVector Side=FVector::CrossProduct(Dir,FVector::UpVector).GetSafeNormal();
        if (Side.IsZero()) Side=FVector::ForwardVector;
        const FVector T=FVector::CrossProduct(Dir,Side)*(Depth*.5);
        auto Cap=[&](FVector P,double Width) -> FQuad
        {
            const FVector S=Side*(Width*.5);
            return {{P-S-T,P+S-T,P+S+T,P-S+T}};
        };
        Solid(Cap(A,WidthA),Cap(B,WidthB),Finish);
    }

    double WingHeight(FVector P) const
    {
        const double X=P.X/(Half.X+House.EaveDepth), Y=P.Y/(Half.Y+House.EaveDepth);
        return House.RoofRise*(.075+.91*FMath::Abs(X)+.21*X+.055*Y);
    }

    double WallTop(FVector P) const
    {
        const FVector2D Size=Half+FVector2D(House.EaveDepth);
        const double R2=FMath::Square(P.X/Size.X)+FMath::Square(P.Y/Size.Y);
        return Deck+Height+(bOrbit ? House.RoofRise*(1.-R2) : WingHeight(P));
    }

    // A roof course is a strip of closed wedges. Both sides of a butterfly fold share its exact centreline.
    template <typename F>
    void Course(FVector2D Radii,double Inner,double Outer,double Thickness,EFinish Finish,F&& Elevation)
    {
        for (int32 I=0; I<Sides; ++I)
        {
            const double A=I*UE_TWO_PI/Sides, B=(I+1)*UE_TWO_PI/Sides;
            auto Sample=[&](double Angle,double R)
            {
                FVector P=Outline(Angle,Radii)*R;
                P.Z=Elevation(P,R);
                return P;
            };
            FQuad Top={{Sample(A,Inner),Sample(A,Outer),Sample(B,Outer),Sample(B,Inner)}};
            Prism(Top,FVector(0,0,-Thickness),Finish);
        }
    }

    // Joined strips make the keel, pinstripes and the edge under the eaves.
    template <typename F>
    void Rim(FVector2D Radii,double BottomScale,double TopScale,double Low,double High,double Depth,EFinish Finish,F&& Elevation)
    {
        const double Inset=Depth/Radii.GetMin();
        for (int32 I=0; I<Sides; ++I)
        {
            const double A=I*UE_TWO_PI/Sides,B=(I+1)*UE_TWO_PI/Sides;
            auto Layer=[&](double R,double Z) -> FQuad
            {
                FVector OA=Outline(A,Radii)*R, OB=Outline(B,Radii)*R;
                FVector IA=Outline(A,Radii)*(R-Inset), IB=Outline(B,Radii)*(R-Inset);
                OA.Z=IA.Z=Elevation(OA)+Z; OB.Z=IB.Z=Elevation(OB)+Z;
                return {{IA,OA,OB,IB}};
            };
            Solid(Layer(BottomScale,Low),Layer(TopScale,High),Finish);
        }
    }

    // Elliptical solids for small fittings. The caller chooses the number of sectors and the two radii.
    void Drum(FVector Center,FVector2D Radius0,FVector2D Radius1,double Z0,double Z1,int32 Count,EFinish Finish)
    {
        for (int32 I=0; I<Count; ++I)
        {
            const double A=I*UE_TWO_PI/Count,B=(I+1)*UE_TWO_PI/Count;
            auto End=[&](FVector2D R,double Z) -> FQuad
            {
                const FVector O=Center+FVector(0,0,Z);
                auto Edge=[&](double T){return O+FVector(R.X*FMath::Cos(T),R.Y*FMath::Sin(T),0);};
                return {{O,Edge(A),Edge(B),O}};
            };
            Solid(End(Radius0,Z0),End(Radius1,Z1),Finish);
        }
    }

    void Foundation()
    {
        auto Flat=[](FVector){return 0.;};
        const int32 Rows=FMath::Max(2,FMath::CeilToInt(Half.GetMax()/Span));
        for (int32 I=0; I<Rows; ++I)
            Course(Half,double(I)/Rows,double(I+1)/Rows,17,Terrazzo,[&](FVector,double){return Deck;});
        const double Belly=FMath::Min(53.,Deck*.36);
        Rim(Half,.84,1.025,Deck-17-Belly,Deck-16,13,Shell,Flat);
        Rim(Half,1.025,1.025,Deck-16,Deck-7,12,Accent,Flat);
        Rim(Half,1.025,1.,Deck-7,Deck,14,Metal,Flat);

        for (int32 I=0; I<House.SupportCount; ++I)
        {
            const double A=-UE_PI*.5+(I+.5)*UE_TWO_PI/House.SupportCount;
            const FVector Heel=Outline(A,Half)*.89;
            const double Top=Deck-16, Spread=FMath::Min(.28,1.25/House.SupportCount);
            Drum(Heel,{39,34},{30,27},0,FMath::Min(13.,Deck*.16),8,Terrazzo);
            for (double Sign : {-1.,1.})
                Rod(Heel+FVector(0,0,FMath::Min(12.,Deck*.15)),
                    Outline(A+Sign*Spread,Half)*.76+FVector(0,0,Top),14,27,17,Metal);
        }
    }

    void RoofShell()
    {
        const FVector2D Size=Half+FVector2D(House.EaveDepth);
        auto Skin=[&](FVector P,double R)
        {
            const double Z=bOrbit ? House.RoofRise*(1.-R*R) : WingHeight(P);
            return Deck+Height+27.+Z;
        };
        TArray<double> Cuts={0.,.27,.52,.76,.88,.904,.965,1.};
        const int32 Steps=FMath::CeilToInt(Size.GetMax()/Span);
        for (int32 I=1; I<Steps; ++I)
        {
            const double V=double(I)/Steps;
            if (!Cuts.ContainsByPredicate([V](double S){return FMath::Abs(S-V)<.035;})) Cuts.Add(V);
        }
        Cuts.Sort();
        for (int32 I=1; I<Cuts.Num(); ++I)
        {
            const double Mid=(Cuts[I-1]+Cuts[I])*.5;
            const EFinish Color=Mid>.965 ? Shell : (Mid>=.88 && Mid<=.904 ? Accent : Roof);
            Course(Size,Cuts[I-1],Cuts[I],19,Color,Skin);
        }
        auto Edge=[&](FVector P){return Skin(P,1.);};
        Rim(Size,.997,1.,-24,-19,12,Metal,Edge);
        Rim(Size,.991,.997,-30,-24,11,Gasket,Edge);
        Rim(Size,.982,.991,-34,-30,10,Lamp,Edge);
        Rim(Size,.958,.982,-40,-34,15,Shell,Edge);
        if (House.bClerestory)
        {
            const double Top=Deck+Height+27.+(bOrbit ? House.RoofRise : WingHeight(FVector::ZeroVector));
            const double RX=Half.X*.245, RY=Half.Y*.23;
            const double Base=Top-(bOrbit ? House.RoofRise*.075 : 0.);
            Drum(FVector::ZeroVector,{RX,RY},{RX*.92,RY*.92},Base,Top+13,24,Shell);
            Drum(FVector::ZeroVector,{RX*.92,RY*.92},{RX*.86,RY*.86},Top+13,Top+46,24,Glass);
            for (int32 I=0; I<12; ++I)
            {
                const double A=I*UE_TWO_PI/12;
                FVector Outer(RX*FMath::Cos(A),RY*FMath::Sin(A),0);
                Rod(Outer*.93+FVector(0,0,Top+12),Outer*.87+FVector(0,0,Top+46),5,5,6,Metal);
            }
            Drum(FVector::ZeroVector,{RX*.97,RY*.97},{RX*.98,RY*.98},Top+46,Top+52,24,Accent);
            Drum(FVector::ZeroVector,{RX*.98,RY*.98},{RX*.46,RY*.46},Top+52,Top+70,24,Shell);
        }
    }

    struct FBay
    {
        FVector A,B,Normal;
        double Length;
        FBay(FVector Start,FVector End) : A(Start), B(End), Length((End-Start).Size())
        {
            Normal=FVector(B.Y-A.Y,A.X-B.X,0).GetSafeNormal();
        }
        FVector Point(double U,double Z,double Depth) const
        {
            FVector P=FMath::Lerp(A,B,U)+Normal*Depth; P.Z=Z; return P;
        }
    };

    void FacadeStrip(const FBay& Bay,double L,double R,double Z0,double Z1,double Back,double Front,EFinish Finish)
    {
        FQuad Face={{Bay.Point(L,Z0,Back),Bay.Point(R,Z0,Back),Bay.Point(R,Z1,Back),Bay.Point(L,Z1,Back)}};
        Prism(Face,Bay.Normal*(Front-Back),Finish);
    }

    // A rectangular spandrel with a genuine circular hole. Each radial slice and its bezel can break separately.
    void Porthole(const FBay& Bay,double L,double R,double Bottom,double Top)
    {
        const double U=(L+R)*.5, Z=(Bottom+Top)*.5, W=(R-L)*Bay.Length*.5, H=(Top-Bottom)*.5;
        const double Radius=FMath::Min(W,H)*.83;
        auto RingPoint=[&](double Angle,bool bSquare,double Scale,double D)
        {
            const double X=FMath::Cos(Angle),Y=FMath::Sin(Angle);
            const double Dist=bSquare ? 1./FMath::Max(FMath::Abs(X)/W,FMath::Abs(Y)/H) : Radius*Scale;
            return Bay.Point(U+X*Dist/Bay.Length,Z+Y*Dist,D);
        };
        // Include the four rectangle corners explicitly so adjacent slices cover the whole panel.
        TArray<double> Angles;
        for (int32 I=0; I<16; ++I) Angles.Add(I*UE_TWO_PI/16);
        const double Corner=FMath::Atan2(H,W);
        for (double V : {Corner,UE_PI-Corner,UE_PI+Corner,UE_TWO_PI-Corner})
            if (!Angles.ContainsByPredicate([V](double T){return FMath::Abs(T-V)<.013;})) Angles.Add(V);
        Angles.Sort();
        for (int32 I=0; I<Angles.Num(); ++I)
        {
            const double A=Angles[I],B=I+1<Angles.Num() ? Angles[I+1] : UE_TWO_PI;
            auto Slice=[&](bool bOuterSquare,double Outside,double Inside,double D,double Thickness,EFinish Finish)
            {
                FQuad Face={{RingPoint(A,bOuterSquare,Outside,D),RingPoint(B,bOuterSquare,Outside,D),
                             RingPoint(B,false,Inside,D),RingPoint(A,false,Inside,D)}};
                Prism(Face,Bay.Normal*Thickness,Finish);
            };
            Slice(true,1.,1.,-13,19,Shell);
            Slice(false,1.015,.83,6,6,Accent);
            Slice(false,.835,.77,3,7,Metal);
        }
    }

    void Facades()
    {
        const double Start=-UE_PI*.5+DoorHalfAngle, Arc=UE_TWO_PI-2*DoorHalfAngle;
        const int32 Count=House.FacadeBays-1;
        const int32 PortPhase=House.PortholeSpacing>0 ? Random.RandRange(0,House.PortholeSpacing-1) : 0;
        const double Sill=Deck+House.SillHeight, Head=Deck+Height-41.;
        for (int32 I=0; I<Count; ++I)
        {
            FBay Bay(Outline(Start+Arc*I/Count,Half),Outline(Start+Arc*(I+1)/Count,Half));
            const double Post=FMath::Min(.12,9./Bay.Length);
            const int32 Cuts=FMath::Max(1,FMath::CeilToInt(Bay.Length/Span));
            for (int32 J=0; J<Cuts; ++J)
            {
                const double L=double(J)/Cuts,R=double(J+1)/Cuts;
                FacadeStrip(Bay,L,R,Deck,Sill-9,-16,0,Shell);
                FacadeStrip(Bay,L,R,Sill-9,Sill,-17,6,Metal);
                const double ZA=WallTop(Bay.Point(L,0,0))+12, ZB=WallTop(Bay.Point(R,0,0))+12;
                if (bOrbit)
                {
                    FQuad Fascia={{Bay.Point(L,Head,-17),Bay.Point(R,Head,-17),Bay.Point(R,ZB,-17),Bay.Point(L,ZA,-17)}};
                    Prism(Fascia,Bay.Normal*19,Shell);
                }
                else
                {
                    // The folded roof admits a second ribbon of daylight between its rising wing tips.
                    FacadeStrip(Bay,L,R,Head,Head+12,-17,4,Shell);
                    FQuad Upper={{Bay.Point(L,Head+12,-9),Bay.Point(R,Head+12,-9),
                                  Bay.Point(R,ZB-18,-9),Bay.Point(L,ZA-18,-9)}};
                    Prism(Upper,Bay.Normal*5,Glass);
                    FQuad Edge={{Bay.Point(L,ZA-18,-17),Bay.Point(R,ZB-18,-17),Bay.Point(R,ZB,-17),Bay.Point(L,ZA,-17)}};
                    Prism(Edge,Bay.Normal*21,Shell);
                    Rod(Bay.Point(L,Head+11,-2),Bay.Point(L,ZA-16,-2),6,6,9,Metal);
                }
            }
            for (double Edge : {0.,1.-Post})
                FacadeStrip(Bay,Edge,Edge+Post,Sill,Head,-16,4,Metal);
            const bool bPort=House.PortholeSpacing>0 && (I+PortPhase)%House.PortholeSpacing==0;
            if (bPort)
            {
                Porthole(Bay,Post,1.-Post,Sill,Head);
                continue;
            }
            const double Border=FMath::Min(.09,4./Bay.Length);
            for (double Edge : {Post,1.-Post-Border})
                FacadeStrip(Bay,Edge,Edge+Border,Sill,Head,-14,0,Gasket);
            FacadeStrip(Bay,Post+Border,1.-Post-Border,Sill,Sill+5,-13,-2,Gasket);
            FacadeStrip(Bay,Post+Border,1.-Post-Border,Head-5,Head,-13,-2,Gasket);
            for (int32 J=0; J<Cuts; ++J)
            {
                const double L=FMath::Lerp(Post+Border,1.-Post-Border,double(J)/Cuts);
                const double R=FMath::Lerp(Post+Border,1.-Post-Border,double(J+1)/Cuts);
                FacadeStrip(Bay,L,R,Sill+5,Head-5,-14,-7,Glass);
            }
            if (House.SunshadeDepth>0 && (I+PortPhase)%3!=1)
            {
                for (int32 Blade=0; Blade<2; ++Blade)
                {
                    const double Z=Head-14-Blade*17;
                    FacadeStrip(Bay,.025,.975,Z,Z+4,0,House.SunshadeDepth,Blade ? Metal : Roof);
                }
            }
            if ((I+PortPhase)%4==1)
            {
                for (int32 Groove=0; Groove<3; ++Groove)
                {
                    const double Z=Deck+House.SillHeight*.36+Groove*7;
                    FacadeStrip(Bay,.13,.54-Groove*.085,Z,Z+2.8,0,2.5,Groove==0 ? Accent : Metal);
                }
            }
        }
    }

    // A chamfered porte-cochere and shallow nosed treads; neither jamb crosses the capsule-sized entry.
    void Entrance()
    {
        const double W=House.EntranceWidth, LandingEnd=Half.Y+House.PorchDepth;
        const double Z=Deck+1.3;
        const int32 Steps=FMath::CeilToInt(Z/17.), TreadCuts=FMath::Max(2,FMath::CeilToInt((W+66.)/Span));
        auto Slab=[&](double Back,double Front,double WB,double WF,double Top,double Thickness,EFinish Finish)
        {
            for (int32 I=0; I<TreadCuts; ++I)
            {
                const double A=2.*I/TreadCuts-1.,B=2.*(I+1)/TreadCuts-1.;
                FQuad Face={{{A*WB,-Back,Top},{B*WB,-Back,Top},{B*WF,-Front,Top},{A*WF,-Front,Top}}};
                Prism(Face,FVector(0,0,-Thickness),Finish);
            }
        };
        const double LandingHalf=W*.5+37, Rise=Z/Steps, Run=33;
        Slab(DoorLine-3,LandingEnd-16,LandingHalf,LandingHalf,Z,18,Terrazzo);
        Slab(LandingEnd-16,LandingEnd,LandingHalf,LandingHalf-12,Z,18,Terrazzo);
        for (int32 I=1; I<Steps; ++I)
        {
            const double Rear=LandingEnd+(I-1)*Run, Nose=Rear+Run, Top=Z-I*Rise;
            const double W0=LandingHalf-12+I*1.4;
            Slab(Rear-1,Nose,W0,W0,Top-4,FMath::Max(4.,Top-4.),Shell);
            Slab(Rear,Nose+2,W0+1,W0+1,Top,4,Terrazzo);
        }
        // Apron supports sit below the landing and are separate from the walking surface.
        for (double Sign : {-1.,1.})
        {
            const FVector Foot(Sign*(LandingHalf-20),-LandingEnd+21,0);
            Rod(Foot,Foot+FVector(-Sign*14,0,Z-17),18,24,19,Metal);
            if (House.bStairRails && Steps>3)
            {
                const int32 Last=Steps-1;
                auto Rail=[&](int32 Step)
                {
                    return FVector(Sign*(LandingHalf-20+Step*1.4),-LandingEnd-Step*Run+10,Z-Step*Rise+83);
                };
                for (int32 K : {0,Last/2,Last})
                    Rod(Rail(K)-FVector(0,0,82),Rail(K),5.5,5.5,5.5,Metal);
                Rod(Rail(0),Rail(Last),8,8,6,Accent);
                Rod(Rail(0)+FVector(0,43,0),Rail(0),8,8,6,Accent);
            }
        }
        const FVector A(-W*.5,-DoorLine,0),B(W*.5,-DoorLine,0);
        const FBay Portal(A,B);
        const double Clear=FMath::Min(230.,Height-18), Jamb=12./W;
        for (double U : {0.,1.-Jamb})
        {
            FacadeStrip(Portal,U,U+Jamb,Deck,Deck+Clear,-18,12,Accent);
            const double Middle=U+Jamb*.5;
            FacadeStrip(Portal,Middle-.9/W,Middle+.9/W,Deck+25,Deck+Clear-13,12,14,Lamp);
        }
        FacadeStrip(Portal,0,1,Deck+Clear,Deck+Clear+12,-18,12,Accent);
        FQuad Head={{Portal.Point(0,Deck+Clear+12,-15),Portal.Point(1,Deck+Clear+12,-15),
                     Portal.Point(1,WallTop(B)+12,-15),Portal.Point(0,WallTop(A)+12,-15)}};
        Prism(Head,Portal.Normal*18,Shell);
        if (!House.bEntranceCanopy) return;
        const double CanopyZ=Deck+Clear+9, End=LandingEnd+38, Width=LandingHalf+24;
        // The leading corners sweep out and the fascia pitches up at the tips.
        auto Wing=[&](double S,double T,double DZ)
        {
            const double WidthAt=FMath::Lerp(W*.5+19,Width,T);
            return FVector(S*WidthAt,-FMath::Lerp(DoorLine-3,End-27*FMath::Abs(S),T),
                CanopyZ+T*(11+15*FMath::Abs(S))+DZ);
        };
        for (int32 I=0; I<4; ++I)
        {
            const double L=I*.5-1.,R=L+.5;
            FQuad Top={{Wing(L,0,0),Wing(R,0,0),Wing(R,1,0),Wing(L,1,0)}};
            Prism(Top,FVector(0,0,-10),Roof);
            FQuad Lip={{Wing(L,1,-10),Wing(R,1,-10),Wing(R,1,0),Wing(L,1,0)}};
            Prism(Lip,FVector(0,-3,0),Accent);
        }
        for (double S : {-1.,1.})
        {
            const FVector Low(S*(LandingHalf-7),-LandingEnd+34,Z);
            const FVector High=Wing(S*.91,.95,-10);
            Rod(Low,High,9,12,10,Metal);
            Rod(FMath::Lerp(Low,High,.67),Wing(S,.62,-10),7,7,7,Accent);
        }
    }

    void Lounge()
    {
        const int32 Seats=FMath::Clamp(FMath::RoundToInt(Half.X/100),4,12);
        for (int32 I=0; I<Seats; ++I)
        {
            const double A=FMath::Lerp(.19*UE_PI,.81*UE_PI,(I+.022)/Seats);
            const double B=FMath::Lerp(.19*UE_PI,.81*UE_PI,(I+.978)/Seats);
            auto Cushion=[&](double Outer,double Inner,double Z0,double Z1,EFinish Finish)
            {
                auto Layer=[&](double Z)->FQuad
                {
                    const FVector Up(0,0,Deck+Z);
                    return {{Outline(A,Half)*Inner+Up,Outline(A,Half)*Outer+Up,
                             Outline(B,Half)*Outer+Up,Outline(B,Half)*Inner+Up}};
                };
                Solid(Layer(Z0),Layer(Z1),Finish);
            };
            Cushion(.86,.76,0,31,Gasket);
            Cushion(.885,.70,31,47,Accent);
            Cushion(.89,.855,47,85,Roof);
        }
        const FVector C(-Half.X*.10,Half.Y*.29,Deck);
        Drum(C,{34,27},{22,18},0,7,12,Metal);
        Drum(C,{16,13},{11,10},7,49,10,Metal);
        Drum(C,{66,44},{71,48},49,54,18,Accent);
        Drum(C,{71,48},{67,44},54,59,18,Shell);
        const double Ceiling=WallTop(C)+7.-Deck;
        const double Shade=FMath::Min(Height*.84,Ceiling-35.);
        Rod(C+FVector(0,0,Shade+15),C+FVector(0,0,Ceiling),3.5,3.5,3.5,Metal);
        Drum(C,{29,24},{54,43},Shade-7,Shade,18,Lamp);
        Drum(C,{54,43},{43,35},Shade,Shade+6,18,Accent);
        Drum(C,{43,35},{12,10},Shade+6,Shade+16,18,Shell);
    }

    void PorchScreen()
    {
        const double W=House.EntranceWidth*.5, End=Half.Y+House.PorchDepth;
        const FBay Side({W+30,-DoorLine-6,0},{W+91,-End+4,0});
        const int32 Cols=FMath::Clamp(FMath::RoundToInt(Side.Length/51.),2,7);
        const double Tile=Side.Length/Cols, Rise=FMath::Min(Tile,47.);
        for (int32 Row=0; Row<House.ScreenCourses; ++Row)
        for (int32 Col=0; Col<Cols; ++Col)
        {
            const double U=(Col+.5)/Cols,Z=Deck+3+(Row+.5)*Rise;
            auto Corner=[&](int32 J,bool bHole)
            {
                const double Angle=J*UE_TWO_PI/8, X=FMath::Cos(Angle), Y=FMath::Sin(Angle);
                const double R=bHole ? .81/(FMath::Abs(X)+FMath::Abs(Y)) : 1./FMath::Max(FMath::Abs(X),FMath::Abs(Y));
                return Side.Point(U+.5*R*X/Cols,Z+.5*Rise*R*Y,-5);
            };
            for (int32 I=0; I<8; ++I)
            {
                FQuad Web={{Corner(I,false),Corner(I+1,false),Corner(I+1,true),Corner(I,true)}};
                Prism(Web,Side.Normal*12,Shell);
            }
        }
        const double Top=Deck+3+House.ScreenCourses*Rise;
        FacadeStrip(Side,0,1,Top,Top+5,-8,9,Accent);
        for (double U : {0.,1.}) Rod(Side.Point(U,0,0),Side.Point(U,Deck+5,0),13,16,13,Metal);
    }

    void Aerial()
    {
        FVector Anchor(-Half.X*(bOrbit ? .48 : .62),Half.Y*.20,0);
        const FVector2D R=Half+FVector2D(House.EaveDepth);
        const double U=Anchor.X/R.X, V=Anchor.Y/R.Y;
        Anchor.Z=Deck+Height+27.+(bOrbit ? House.RoofRise*(1.-U*U-V*V) : WingHeight(Anchor));
        const FVector Hub=Anchor+FVector(-32,0,133);
        Rod(Anchor-FVector(0,0,13),Hub,28,8,13,Metal);
        Rod(Anchor+FVector(29,0,-6),FMath::Lerp(Anchor,Hub,.65),14,6,11,Accent);
        // A Googie starburst in the front elevation, with long cardinal rays and a lit vitreous centre.
        const FVector Horizontal=FVector(.97,.24,0).GetSafeNormal();
        const FVector Forward=FVector::CrossProduct(Horizontal,FVector::UpVector);
        for (int32 I=0; I<8; ++I)
        {
            const double A=I*UE_TWO_PI/8;
            const FVector Dir=Horizontal*FMath::Cos(A)+FVector::UpVector*FMath::Sin(A);
            const FVector Cross=-Horizontal*FMath::Sin(A)+FVector::UpVector*FMath::Cos(A);
            const double Length=I%2 ? 40. : (I%4 ? 83. : 69.);
            FQuad Ray={{Hub+Dir*7,Hub+Dir*22+Cross*4.8,Hub+Dir*Length,Hub+Dir*22-Cross*4.8}};
            Prism(Ray,Forward*5,I%2 ? Metal : Accent);
        }
        for (int32 I=0; I<16; ++I)
        {
            auto Radius=[&](int32 J){return Horizontal*FMath::Cos(J*UE_TWO_PI/16)+FVector::UpVector*FMath::Sin(J*UE_TWO_PI/16);};
            const FVector DA=Radius(I), DB=Radius(I+1), Front=Hub+Forward*7;
            FQuad RimFace={{Front+DA*10,Front+DA*17,Front+DB*17,Front+DB*10}};
            Prism(RimFace,-Forward*11,Metal);
            FQuad Lens={{Front,Front+DA*10,Front+DB*10,Front}};
            Prism(Lens,Forward*3,Lamp);
        }
    }
};

AParametricSpaceAgeHouse::AParametricSpaceAgeHouse()
{
    FloorPattern=EChunkedFloorPattern::Solid;
    ChunkSideLength=48;
    MaxGeneratedCells=18000;
    DefaultCellHealth=25;
    bReactToMusic=false;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Master(
        TEXT("/Game/Structures/Destructible/SpaceAgeHomes/Materials/M_SpaceAgeFinish"));
    SurfaceMaterial=Master.Get();
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Glazing(
        TEXT("/Game/Structures/Destructible/SpaceAgeHomes/Materials/M_SpaceAgeGlazing"));
    GlazingMaterial=Glazing.Get();
}

void AParametricSpaceAgeHouse::RebuildHouse()
{
    if (!IsTemplate() && GetWorld()) { SetLifeSpan(0); GenerateFloor(false); }
}

void AParametricSpaceAgeHouse::BeginPlay()
{
    Super::BeginPlay();
    OnCellDestroyed.AddUniqueDynamic(this,&AParametricSpaceAgeHouse::ReleasePanel);
}

void AParametricSpaceAgeHouse::ReleasePanel(FIntPoint Cell,FVector WorldLocation,bool bRadialDestruction)
{
    if (LoosePanelLimit<=0 || !Panels.IsValidIndex(Cell.X) || GetNetMode()==NM_DedicatedServer) return;
    LoosePanels.RemoveAll([](const TWeakObjectPtr<AActor>& Actor){return !Actor.IsValid();});
    if (LoosePanels.Num()>=FMath::Clamp(LoosePanelLimit,0,32)) return;
    const FPanel& Source=Panels[Cell.X];
    FRandomStream Random(uint32(FacadeSeed)^GetTypeHash(Cell.X));
    const double Scale=Random.FRandRange(.54f,.76f);
    FPanel Fragment;
    Fragment.Center=FVector::ZeroVector;
    Fragment.Slot=0;
    for (int32 I=0; I<8; ++I) Fragment.V[I]=(Source.V[I]-Source.Center)*Scale;
    UE::Geometry::FDynamicMesh3 Mesh;
    Mesh.EnableVertexNormals(FVector3f::UnitZ()); Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1); Mesh.Attributes()->EnableMaterialID();
    DrawPanel(Mesh,Fragment,0);

    FActorSpawnParameters Params;
    Params.Owner=this;
    Params.ObjectFlags|=RF_Transient;
    Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Piece=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),WorldLocation,GetActorRotation(),Params);
    if (!Piece) return;
    auto* Body=NewObject<UDynamicMeshComponent>(Piece,NAME_None,RF_Transient);
    Piece->SetRootComponent(Body);
    Body->SetMobility(EComponentMobility::Movable);
    Body->SetCanEverAffectNavigation(false);
    Body->SetGenerateOverlapEvents(false);
    Body->SetMesh(MoveTemp(Mesh));
    const int32 Slot=Cells[Cell.X].MaterialIndex;
    if (FloorMaterials.IsValidIndex(Slot)) Body->SetMaterial(0,FloorMaterials[Slot]);
    Body->SetComplexAsSimpleCollisionEnabled(false,false);
    Body->SetCollisionObjectType(ECC_PhysicsBody);
    Body->SetCollisionResponseToAllChannels(ECR_Ignore);
    Body->SetCollisionResponseToChannel(ECC_WorldStatic,ECR_Block);
    Body->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
    Body->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    FKAggregateGeom Shapes;
    FKConvexElem& Hull=Shapes.ConvexElems.Emplace_GetRef();
    for (const FVector& V:Fragment.V) Hull.VertexData.AddUnique(V);
    Hull.UpdateElemBox();
    Body->SetSimpleCollisionShapes(Shapes,true);
    Body->SetWorldTransform(FTransform(GetActorQuat(),WorldLocation,GetActorScale3D()));
    Body->RegisterComponent();
    Body->SetSimulatePhysics(true);
    const FVector Out=GetActorTransform().TransformVectorNoScale(Source.Center.GetSafeNormal2D());
    Body->SetPhysicsLinearVelocity(Out*(bRadialDestruction ? 190. : 80.)+FVector::UpVector*Random.FRandRange(85,155)
        +GetDebrisBlastImpulse(WorldLocation));
    Body->SetPhysicsAngularVelocityInDegrees(Random.VRand()*Random.FRandRange(100,215));
    Piece->SetLifeSpan(LoosePanelLifetime);
    LoosePanels.Add(Piece);
}

void AParametricSpaceAgeHouse::ClearAdditionalRuntimeEffects()
{
    Super::ClearAdditionalRuntimeEffects();
    if (GetWorld()) GetWorldTimerManager().ClearTimer(DemolitionTimer);
    for (const TWeakObjectPtr<AActor>& Piece:LoosePanels)
        if (Piece.IsValid()) Piece->Destroy();
    LoosePanels.Reset();
}

bool AParametricSpaceAgeHouse::HasIntactPanels() const
{
    return Cells.ContainsByPredicate([](const auto& Cell){return Cell.bExists;});
}

void AParametricSpaceAgeHouse::FinalizeDestructionBatch(double CurrentTime)
{
    Super::FinalizeDestructionBatch(CurrentTime);
    if (bRemoveWhenEmpty && HasActorBegunPlay() && !HasIntactPanels())
    {
        const float Delay=FMath::Max(FlashDuration,LoosePanels.IsEmpty() ? 0.f : LoosePanelLifetime)+.1f;
        GetWorldTimerManager().SetTimer(DemolitionTimer,this,&AParametricSpaceAgeHouse::RemoveEmptyHouse,Delay,false);
    }
}

void AParametricSpaceAgeHouse::RemoveEmptyHouse()
{
    if (bRemoveWhenEmpty && !HasIntactPanels()) Destroy();
}

void AParametricSpaceAgeHouse::UpdateFinishes()
{
    UMaterialInterface* Enamel=SurfaceMaterial ? SurfaceMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
    const FLinearColor Colors[]={Palette.Shell,Palette.Roof,Palette.Accent,Palette.Glass,Palette.Metal,Palette.Lamp,
        Palette.Terrazzo,FLinearColor(.023f,.034f,.037f)};
    const float Roughness[]={.34f,.29f,.30f,.19f,.27f,.28f,.70f,.54f};
    const float Metallic[]={.14f,.23f,.22f,.34f,.72f,.17f,.04f,.18f};
    Finishes.SetNum(FinishCount);
    FloorMaterials.SetNum(FinishCount);
    for (int32 I=0; I<FinishCount; ++I)
    {
        UMaterialInterface* Parent=I==Glass && GlazingMaterial ? GlazingMaterial.Get() : Enamel;
        if (!Finishes[I] || Finishes[I]->Parent!=Parent)
        {
            Finishes[I]=UMaterialInstanceDynamic::Create(Parent,this);
            Finishes[I]->SetFlags(RF_Transient);
        }
        auto* M=Finishes[I].Get();
        M->SetVectorParameterValue(TEXT("Tint"),Colors[I]);
        M->SetScalarParameterValue(TEXT("Roughness"),Roughness[I]);
        M->SetScalarParameterValue(TEXT("Metallic"),Metallic[I]);
        M->SetScalarParameterValue(TEXT("Finish"),I==Glass ? 1.f : (I==Terrazzo ? 2.f : 0.f));
        M->SetScalarParameterValue(TEXT("Glow"),I==Lamp ? LampGlow : (I==Glass ? .35f : .105f));
        if (I==Glass) M->SetScalarParameterValue(TEXT("Opacity"),GlassOpacity);
        FloorMaterials[I]=M;
    }
}

bool AParametricSpaceAgeHouse::CalculateLayout()
{
    Footprint.X=Safe(Footprint.X,860,2400,1360);
    Footprint.Y=Safe(Footprint.Y,660,1800,920);
    DeckHeight=Safe(DeckHeight,34,280,132); WallHeight=Safe(WallHeight,240,520,285);
    RoofRise=Safe(RoofRise,55,370,154); EaveDepth=Safe(EaveDepth,35,300,118);
    SillHeight=Safe(SillHeight,40,FMath::Min(145.,WallHeight*.46),78);
    SunshadeDepth=Safe(SunshadeDepth,0,65,23); PorchDepth=Safe(PorchDepth,75,260,136);
    EntranceWidth=Safe(EntranceWidth,120,310,174); FragmentSpan=Safe(FragmentSpan,55,210,115);
    LampGlow=Safe(LampGlow,0,5,1.35); FacadeBays=FMath::Clamp(FacadeBays,8,24);
    GlassOpacity=Safe(GlassOpacity,.15,.9,.49);
    LoosePanelLimit=FMath::Clamp(LoosePanelLimit,0,32); LoosePanelLifetime=Safe(LoosePanelLifetime,.2,4.,1.4);
    PortholeSpacing=FMath::Clamp(PortholeSpacing,0,9); SupportCount=FMath::Clamp(SupportCount,4,12);
    ScreenCourses=FMath::Clamp(ScreenCourses,0,5);

    FBuilder Build(*this);
    Build.Foundation(); Build.RoofShell(); Build.Facades(); Build.Entrance();
    if (ScreenCourses>0) Build.PorchScreen();
    if (bInteriorLounge) Build.Lounge();
    if (bStarAerial) Build.Aerial();
    if (Build.bOverBudget || Build.Result.IsEmpty())
    {
        GenerationWarning=TEXT("Fragment budget exceeded. Increase Fragment Span or Max Generated Cells.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);
        return false;
    }
    GenerationWarning.Reset();
    GeneratedFragments=Build.Result.Num();
    InstallLayout(MoveTemp(Build),FragmentSpan);
    UpdateFinishes();
    return true;
}

float AParametricSpaceAgeHouse::TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer)
{
    if (!CanBeDamaged() || !FMath::IsFinite(Amount) || Amount<=0) return 0;
    if (Event.IsOfType(FPointDamageEvent::ClassID))
    {
        if (!DamageCellFromHit(static_cast<const FPointDamageEvent&>(Event).HitInfo,Amount)) return 0;
    }
    else if (Event.IsOfType(FRadialDamageEvent::ClassID))
    {
        const auto& Blast=static_cast<const FRadialDamageEvent&>(Event);
        if (!DamageCellsInRadius(Blast.Origin,Blast.Params.OuterRadius,Amount,EChunkedFloorBlastShape::Sphere,
                Blast.Params.DamageFalloff>0.f)) return 0;
    }
    return Super::TakeDamage(Amount,Event,InstigatorController,Causer);
}
