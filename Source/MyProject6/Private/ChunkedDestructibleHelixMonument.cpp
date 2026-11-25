#include "ChunkedDestructibleHelixMonument.h"
#include "Materials/Material.h"
#include "UObject/ConstructorHelpers.h"


namespace
{
    enum EFinish : uint8 { Aggregate, Limestone, Inset, Bronze };
    struct FLayer { FVector V[4]; };

    FVector Polar(double Angle, double Radius, double Z)
    {
        return FVector(FMath::Cos(Angle)*Radius, FMath::Sin(Angle)*Radius, Z);
    }

    // Four vertices of one radial section. The two ends can rise and taper independently.
    FLayer Section(double A, double B, double InnerA, double OuterA, double InnerB, double OuterB, double ZA, double ZB)
    {
        return {{Polar(A,InnerA,ZA), Polar(A,OuterA,ZA), Polar(B,OuterB,ZB), Polar(B,InnerB,ZB)}};
    }

    FLayer Level(double A, double B, double Inner, double Outer, double Z)
    {
        return Section(A,B,Inner,Outer,Inner,Outer,Z,Z);
    }

    void Limit(float& Value, float Min, float Max, float Fallback)
    {
        Value = FMath::Clamp(FMath::IsFinite(Value) ? Value : Fallback,Min,Max);
    }
}

struct AChunkedDestructibleHelixMonument::FBuilder : FPanelLayout
{
    const AChunkedDestructibleHelixMonument& P;
    const int32 Angular;
    const double Entrance, Start, Sweep, PlinthRadius, Neck, Core, Shoulder, CrownHeight, Fascia;

    explicit FBuilder(const AChunkedDestructibleHelixMonument& Owner)
        : FPanelLayout(Owner.MaxGeneratedCells,Owner.JointGap), P(Owner), Angular(FMath::Max(P.SegmentsPerTurn,FMath::CeilToInt(UE_TWO_PI*P.BaseRadius/P.FragmentSize))),
          Entrance(FMath::DegreesToRadians(P.EntranceAngle)), Start(Entrance+FMath::DegreesToRadians(P.StartOffset)),
          Sweep((P.bClockwise ? -1. : 1.)*UE_TWO_PI*P.Turns), PlinthRadius(P.BaseRadius*.94),
          Neck(P.BaseRadius*P.NeckRadiusRatio), Core(FMath::Min(double(P.ColumnRadius),Neck*.55)),
          Shoulder(P.Height*(1.-P.CrownHeightRatio)), CrownHeight(P.Height-Shoulder),
          Fascia(FMath::Min(double(P.BandHeight),.57*(Shoulder-P.PlinthHeight)/P.Turns))
    {}

    void Add(const FLayer& Low, const FLayer& High, EFinish Finish, bool bMullion=false)
    {
        AddPanel(Low.V,High.V,Finish,bMullion ? int32(Bronze) : INDEX_NONE);
    }

    void Ring(double A, double B, double Inner, double Outer, double Z0, double Z1, int32 Cuts, EFinish Finish)
    {
        for (int32 I=0; I<Cuts && !bOverBudget; ++I)
        {
            const double L=FMath::Lerp(A,B,double(I)/Cuts), R=FMath::Lerp(A,B,double(I+1)/Cuts);
            Add(Level(L,R,Inner,Outer,Z0),Level(L,R,Inner,Outer,Z1),Finish);
        }
    }

    void Disk(double Radius, double Z0, double Z1, EFinish Finish)
    {
        const int32 Rows=FMath::Max(1,FMath::CeilToInt(Radius/P.FragmentSize));
        const int32 CourseCount=FMath::Max(1,FMath::CeilToInt((Z1-Z0)/P.FragmentSize));
        for (int32 Row=0; Row<Rows && !bOverBudget; ++Row)
        for (int32 H=0; H<CourseCount && !bOverBudget; ++H)
        {
            // Keep angular boundaries aligned so the top is watertight, including after a neighbouring hit.
            const double Bottom=FMath::Lerp(Z0,Z1,double(H)/CourseCount), Top=FMath::Lerp(Z0,Z1,double(H+1)/CourseCount);
            Ring(Entrance,Entrance+UE_TWO_PI,Radius*Row/Rows,Radius*(Row+1)/Rows,Bottom,Top,Angular,Finish);
        }
    }

    void Foundation()
    {
        const double Cap=FMath::Min(18.,P.PlinthHeight*.10), Z=P.PlinthHeight;
        Disk(PlinthRadius-3.,0,Z-Cap,Aggregate);
        Disk(PlinthRadius,Z-Cap,Z,Limestone);
        const double HalfDoor=FMath::Asin(FMath::Clamp(P.EntranceWidth/(2.*PlinthRadius),.12,.82));
        if (P.bEntranceStairs)
        {
            const int32 Cuts=FMath::Max(5,FMath::CeilToInt(2*HalfDoor*(PlinthRadius+P.StairRun)/P.FragmentSize));
            const double Rise=Z/P.StairCount, Run=P.StairRun/P.StairCount;
            for (int32 S=0; S<P.StairCount && !bOverBudget; ++S)
            {
                const double Inner=PlinthRadius+S*Run, Outer=Inner+Run, Top=Z-S*Rise;
                const double Nose=FMath::Min(5.,Rise*.24);
                Ring(Entrance-HalfDoor,Entrance+HalfDoor,Inner,Outer,0,Top-Nose,Cuts,Aggregate);
                Ring(Entrance-HalfDoor,Entrance+HalfDoor,Inner,Outer+FMath::Min(2.5,Run*.1),Top-Nose,Top,Cuts,Limestone);
            }
            // Sloped cheek walls flank the flight and stay outside the tread polygons.
            for (double Side:{-1.,1.})
            {
                const double Edge=Entrance+Side*HalfDoor, Far=Edge+Side*FMath::Min(.12,48./PlinthRadius);
                const int32 CutsAlong=FMath::Max(3,FMath::CeilToInt(P.StairRun/P.FragmentSize));
                for (int32 I=0; I<CutsAlong; ++I)
                {
                    const double T0=double(I)/CutsAlong,T1=double(I+1)/CutsAlong;
                    const double R0=PlinthRadius+T0*P.StairRun,R1=PlinthRadius+T1*P.StairRun;
                    const double H0=FMath::Lerp(Z+45.,14.,T0),H1=FMath::Lerp(Z+45.,14.,T1);
                    const FLayer Top={{Polar(Edge,R0,H0),Polar(Edge,R1,H1),Polar(Far,R1,H1),Polar(Far,R0,H0)}};
                    Add(Level(Edge,Far,R0,R1,0),Top,Limestone);
                }
            }
        }
        for (int32 I=0; I<P.ButtressCount; ++I)
        {
            const double A=Entrance+(I+.5)*UE_TWO_PI/P.ButtressCount;
            const double W=FMath::Min(.14,UE_PI*.36/P.ButtressCount);
            if (P.bEntranceStairs && FMath::Abs(FMath::FindDeltaAngleRadians(Entrance,A))<HalfDoor+W+.04) continue;
            Add(Level(A-W,A+W,PlinthRadius-8,PlinthRadius+P.BaseRadius*.115,0),
                Level(A-W*.72,A+W*.72,PlinthRadius-22,PlinthRadius+8,Z+Cap*.3),Limestone);
        }
    }

    double RadiusAt(double T) const {return Neck+(P.BaseRadius-Neck)*FMath::Pow(1.-T,P.TaperExponent);}
    double WidthAt(double T) const {return FMath::Min(P.WalkwayWidth*(1.-.22*T),RadiusAt(T)-Core*.71);}
    double HeightAt(double T) const
    {
        // Integrate a positive periodic grade. Normalisation also lands partial turns exactly at the lantern.
        const double Phase=Start-Entrance;
        auto Rise=[&](double U){return U+P.RiseVariation*(FMath::Sin(Phase+Sweep*U)-FMath::Sin(Phase))/Sweep;};
        return FMath::Lerp(double(P.PlinthHeight),Shoulder,Rise(T)/Rise(1.));
    }
    double BandAt(double T) const {return FMath::Lerp(Fascia,Fascia*P.UpperBandScale,T);}

    void Ribbon()
    {
        const int32 Count=FMath::CeilToInt(Angular*P.Turns);
        const double MinBand=BandAt(1.);
        const double Available=FMath::Max(0.,.885-.32-1.2/MinBand);
        const double Stripe=FMath::Min(double(P.InsetBandFraction),Available);
        const bool bInset=Stripe*MinBand>=.8;
        const double StripeLow=.885-(bInset ? Stripe : 0.);
        // Sub-centimetre reveals at extreme scales collapse into the adjoining stone.
        const double RequestedJoint=FMath::Min(P.FasciaJointWidth/Fascia,(StripeLow-.32)*.45);
        const double Joint=RequestedJoint*MinBand>=.8 ? RequestedJoint : 0.;
        for (int32 I=0; I<Count && !bOverBudget; ++I)
        {
            const double U=double(I)/Count,V=double(I+1)/Count;
            const double A=Start+Sweep*U,B=Start+Sweep*V;
            auto Layer=[&](double H,double Setback,bool bWholeDeck)
            {
                auto Radii=[&](double T)
                {
                    const double R=RadiusAt(T),W=WidthAt(T);
                    const double Wall=FMath::Min(P.ParapetThickness*(1.-.25*T),W*.30);
                    return FVector2D(R-(bWholeDeck?W:Wall),R-Setback*(1.-.30*T));
                };
                const FVector2D L=Radii(U),R=Radii(V);
                return Section(A,B,L.X,L.Y,R.X,R.Y,HeightAt(U)+(H-.32)*BandAt(U),HeightAt(V)+(H-.32)*BandAt(V));
            };
            Add(Layer(0,8,true),Layer(.32,0,true),Aggregate);
            if (Joint>0.) Add(Layer(.32,3.8,false),Layer(.32+Joint,3.8,false),Bronze);
            Add(Layer(.32+Joint,0,false),Layer(StripeLow,1.6,false),Aggregate);
            if (bInset)
                Add(Layer(StripeLow,4.2,false),Layer(.885,4.2,false),Inset,I%2==0);
            Add(Layer(.885,1.5,false),Layer(.96,-1.3,false),Limestone);
            Add(Layer(.96,-1.3,false),Layer(1.,1.7,false),Aggregate);
        }
        Disk(Neck*.90,Shoulder-BandAt(1)*.32,Shoulder,Limestone);
    }

    void Frame()
    {
        // Freestanding piers from the entrance terrace up to the underside of the first turn.
        // Their heads follow the local ramp grade so a reversed or steeper spiral stays seated.
        for (int32 I=0; I<P.LowerPierCount && !bOverBudget; ++I)
        {
            const double T=(I+.52)/(P.LowerPierCount*P.Turns);
            const double Radius=RadiusAt(T)-WidthAt(T)*.56;
            const double Width=FMath::Min3(double(P.LowerPierWidth),WidthAt(T)*.78,UE_TWO_PI*Radius/P.LowerPierCount*.48);
            const double A=Start+Sweep*T, Z=P.PlinthHeight;
            const double Ceiling=HeightAt(T)-.32*BandAt(T);
            if (Ceiling<Z+28.) continue;
            auto Pier=[&](double H,double Flare)
            {
                const double HalfAngle=Width*(FMath::Lerp(.56,.45,H)+Flare)/Radius;
                const double Offset=Width*.12*(1.-H),Depth=Width*(FMath::Lerp(.53,.42,H)+Flare);
                const double L=A-HalfAngle, R=A+HalfAngle;
                auto Under=[&](double Phi)
                {
                    const double U=FMath::Clamp((Phi-Start)/Sweep,0.,1.);
                    return FMath::Lerp(Z,HeightAt(U)-.32*BandAt(U)+1.,H);
                };
                return Section(L,R,Radius+Offset-Depth,Radius+Offset+Depth,Radius+Offset-Depth,Radius+Offset+Depth,Under(L),Under(R));
            };
            const double NeckLine=1.-FMath::Min(.18,16./(Ceiling-Z));
            const int32 Courses=FMath::Max(1,FMath::CeilToInt((Ceiling-Z)*NeckLine/P.FragmentSize));
            for (int32 J=0; J<Courses; ++J)
                Add(Pier(NeckLine*J/Courses,0.),Pier(NeckLine*(J+1)/Courses,0.),Limestone);
            Add(Pier(NeckLine,.02),Pier(1.,.12),Aggregate);
        }
        if (!P.bCentralColumn) return;
        const int32 Sides=2*P.ColumnFlutes;
        const int32 Levels=FMath::Max(1,FMath::CeilToInt((Shoulder-P.PlinthHeight)/P.FragmentSize));
        for (int32 H=0; H<Levels && !bOverBudget; ++H)
        for (int32 I=0; I<Sides; ++I)
        {
            const double A=Entrance+UE_TWO_PI*I/Sides,B=Entrance+UE_TWO_PI*(I+1)/Sides;
            auto Flute=[&](double T)
            {
                const double R=Core*(1.-.16*T);
                return Section(A,B,0,R*(I%2?.86:1.),0,R*(I%2?1.:.86),HeightAt(T),HeightAt(T));
            };
            Add(Flute(double(H)/Levels),Flute(double(H+1)/Levels),Bronze);
        }
        const int32 Count=FMath::FloorToInt(P.BracesPerTurn*P.Turns);
        for (int32 I=0; I<Count && !bOverBudget; ++I)
        {
            const double T=(I+.68)/(P.BracesPerTurn*P.Turns);
            const double A=Start+Sweep*T;
            const FVector D=Polar(A,1,0),Side(-D.Y,D.X,0);
            const double Rim=RadiusAt(T)-WidthAt(T)*.43,Root=Core*(1.-.16*T)*.85;
            const double Top=HeightAt(T)-.30*BandAt(T);
            const double Drop=FMath::Min((Rim-Root)*.30,(Shoulder-P.PlinthHeight)/P.Turns*.49);
            const int32 Bays=FMath::Max(1,FMath::CeilToInt((Rim-Root)/P.FragmentSize));
            auto Rail=[&](double T0,double T1,bool bTop)
            {
                auto Point=[&](double F,double S)
                {
                    const double Z=bTop?Top:FMath::Max(double(P.PlinthHeight),Top-FMath::Lerp(Drop,12.,F));
                    return D*FMath::Lerp(Root,Rim,F)+Side*(S*FMath::Lerp(Core*.30,17.,F))+FVector(0,0,Z);
                };
                return FLayer{{Point(T0,-1),Point(T1,-1),Point(T1,1),Point(T0,1)}};
            };
            if (Top>P.PlinthHeight+5.)
                for (int32 J=0; J<Bays; ++J) Add(Rail(double(J)/Bays,double(J+1)/Bays,false),Rail(double(J)/Bays,double(J+1)/Bays,true),Aggregate);
        }
    }

    double CrownRadius(double T) const {return Neck*FMath::Pow(1.-T,P.CrownTaper);}
    FLayer Shell(double L,double R,double T) const
    {
        const double Radius=CrownRadius(T);
        const double Inner=FMath::Max(0.,Radius-FMath::Min(42.,Neck*.17));
        return Level(L,R,Inner,Radius,Shoulder+CrownHeight*T);
    }

    // For each course, join the stone between two consecutive openings. No triangles or hulls span an aperture.
    void PerforatedCourse(int32 Bays,int32 Slices,double T0,double T1,double HalfGap0,double HalfGap1)
    {
        const double Bay=UE_TWO_PI/Bays;
        for (int32 I=0; I<Bays && !bOverBudget; ++I)
        for (int32 J=0; J<Slices; ++J)
        {
            const double A=Entrance+I*Bay,B=A+Bay;
            auto Boundary=[&](double Gap,double Alpha){return FMath::Lerp(A+Gap*Bay,B-Gap*Bay,Alpha);};
            const double U=double(J)/Slices,V=double(J+1)/Slices;
            Add(Shell(Boundary(HalfGap0,U),Boundary(HalfGap0,V),T0),Shell(Boundary(HalfGap1,U),Boundary(HalfGap1,V),T1),Aggregate);
        }
    }

    void Crown()
    {
        if (!P.bCrown) return;
        const int32 ArcSlices=FMath::Max(4,FMath::DivideAndRoundUp(P.SegmentsPerTurn/2,P.LanternArches));
        PerforatedCourse(P.LanternArches,ArcSlices,0,.025,0,0);
        // The spring line rises into a true semicircular arch above the open lantern.
        PerforatedCourse(P.LanternArches,ArcSlices,.025,.105,.38,.38);
        for (int32 I=0; I<7; ++I)
        {
            const double A=double(I)*UE_HALF_PI/7,B=double(I+1)*UE_HALF_PI/7;
            PerforatedCourse(P.LanternArches,ArcSlices,.105+.155*FMath::Sin(A),.105+.155*FMath::Sin(B),.38*FMath::Cos(A),.38*FMath::Cos(B));
        }
        const int32 Bays=FMath::Max(1,P.LancetCount);
        const int32 Slices=FMath::Max(3,FMath::DivideAndRoundUp(P.SegmentsPerTurn/2,Bays));
        PerforatedCourse(Bays,Slices,.26,.29,0,0);
        auto Lancet=[&](double T)
        {
            return P.LancetCount ? .29*FMath::Pow(FMath::Max(0.,FMath::Sin(UE_PI*T)),.66)*(1.-.68*T) : 0.;
        };
        for (int32 I=0; I<8; ++I)
        {
            const double A=double(I)/8,B=double(I+1)/8;
            PerforatedCourse(Bays,Slices,.29+.29*A,.29+.29*B,Lancet(A),Lancet(B));
        }
        const double TipStart=1.-FMath::Pow(.043,1./P.CrownTaper);
        const int32 Rows=FMath::Max(3,FMath::CeilToInt((TipStart-.58)*CrownHeight/P.FragmentSize));
        for (int32 I=0; I<Rows; ++I)
            PerforatedCourse(Bays,Slices,FMath::Lerp(.58,TipStart,double(I)/Rows),FMath::Lerp(.58,TipStart,double(I+1)/Rows),0,0);
        FLayer Foot,Tip;
        for (int32 I=0; I<4; ++I)
        {
            Foot.V[I]=Polar(Entrance+UE_HALF_PI*I,CrownRadius(TipStart),Shoulder+TipStart*CrownHeight);
            Tip.V[I]=FVector(0,0,P.Height);
        }
        Add(Foot,Tip,Bronze);
    }
};

AChunkedDestructibleHelixMonument::AChunkedDestructibleHelixMonument()
{
    FloorPattern=EChunkedFloorPattern::Solid;
    ChunkSideLength=48;
    MaxGeneratedCells=18000;
    DefaultCellHealth=25;
    bReactToMusic=false;
    bGenerateBottomFaces=true;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stone(TEXT("/Game/Structures/Destructible/HelixMonument/Materials/MI_HelixAggregate"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Light(TEXT("/Game/Structures/Destructible/HelixMonument/Materials/MI_HelixLimestone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Glass(TEXT("/Game/Structures/Destructible/HelixMonument/Materials/MI_HelixSmokedInset"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Metal(TEXT("/Game/Structures/Destructible/HelixMonument/Materials/MI_HelixBronze"));
    AggregateMaterial=Stone.Get(); LimestoneMaterial=Light.Get(); InsetMaterial=Glass.Get(); BronzeMaterial=Metal.Get();
}

void AChunkedDestructibleHelixMonument::RebuildMonument()
{
    if (!IsTemplate() && GetWorld()) { SetLifeSpan(0); GenerateFloor(false); }
}

bool AChunkedDestructibleHelixMonument::CalculateLayout()
{
    Limit(Height,900,7500,3050);
    Limit(BaseRadius,400,3000,1030);
    Limit(NeckRadiusRatio,.1f,.48f,.22f);
    Limit(TaperExponent,.65f,1.8f,.96f);
    Limit(Turns,1,7,4.15f);
    Limit(WalkwayWidth,80,650,225);
    Limit(RiseVariation,0,.75f,.48f);
    Limit(BandHeight,40,420,244);
    Limit(UpperBandScale,.4f,1,.83f);
    Limit(ParapetThickness,10,80,34);
    Limit(InsetBandFraction,0,.54f,.31f);
    Limit(FasciaJointWidth,0,8,3.8f);
    Limit(CrownHeightRatio,.15f,.45f,.345f);
    Limit(CrownTaper,.85f,2,1.42f);
    Limit(PlinthHeight,50,FMath::Min(480.f,Height*.27f),228);
    Limit(EntranceWidth,180,2000,880);
    Limit(StairRun,90,950,350);
    Limit(ColumnRadius,35,270,101);
    Limit(LowerPierWidth,45,280,116);
    Limit(FragmentSize,65,380,156);
    Limit(JointGap,0,3,0);
    EntranceAngle=FMath::IsFinite(EntranceAngle)?FMath::UnwindDegrees(FMath::Fmod(EntranceAngle,360.f)):-90;
    StartOffset=FMath::IsFinite(StartOffset)?FMath::UnwindDegrees(FMath::Fmod(StartOffset,360.f)):40;
    StairCount=FMath::Clamp(StairCount,3,28);
    ColumnFlutes=FMath::Clamp(ColumnFlutes,6,32);
    ButtressCount=FMath::Clamp(ButtressCount,0,24);
    BracesPerTurn=FMath::Clamp(BracesPerTurn,0,8);
    LowerPierCount=FMath::Clamp(LowerPierCount,0,16);
    LanternArches=FMath::Clamp(LanternArches,2,8);
    LancetCount=FMath::Clamp(LancetCount,0,16);
    SegmentsPerTurn=FMath::Clamp(SegmentsPerTurn,24,108);

    FBuilder Build(*this);
    Build.Foundation();
    Build.Ribbon();
    Build.Frame();
    Build.Crown();
    if (Build.bOverBudget || Build.Result.IsEmpty())
    {
        GenerationWarning=TEXT("Fragment budget exceeded. Increase Fragment Size or Max Generated Cells, or reduce the angular resolution.");
        UE_LOG(LogTemp, Warning, TEXT("%s: %s"),*GetName(),*GenerationWarning);
        return false;
    }
    GenerationWarning.Reset();
    GeneratedFragments=Build.Result.Num();
    GeneratedHeight=Build.Bounds.Max.Z;
    InstallLayout(MoveTemp(Build),FragmentSize);
    FloorMaterials={AggregateMaterial,LimestoneMaterial,InsetMaterial,BronzeMaterial};
    for (auto& M:FloorMaterials) if (!M) M=UMaterial::GetDefaultMaterial(MD_Surface);
    return true;
}
