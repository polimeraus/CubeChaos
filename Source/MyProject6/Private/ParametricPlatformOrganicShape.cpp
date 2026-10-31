#include "ParametricPlatform.h"
#include "Algo/Reverse.h"

using namespace UE::Geometry;

namespace
{
    double SmoothUnion(double A, double B, double Radius)
    {
        if (Radius <= UE_DOUBLE_SMALL_NUMBER) return FMath::Min(A,B);
        const double H = FMath::Max(Radius-FMath::Abs(A-B),0.) / Radius;
        return FMath::Min(A,B)-H*H*Radius*.25;
    }

    double Ease(double T)
    {
        T=FMath::Clamp(T,0.,1.);
        // Zero first and second derivatives at the root, neck and end.
        return T*T*T*(T*(T*6-15)+10);
    }

    struct FOrganicSample { FVector2D Point; double Radius; };
    struct FOrganicArm
    {
        TArray<FOrganicSample> Samples;
        FVector2D End;
        double EndRadius;
    };

    // Exact distance to the union of linearly varying discs on a line segment.
    // Unlike a chain of separate circles, this has no scallops along a narrow arm.
    double TaperedSegmentDistance(FVector2D P, const FOrganicSample& A, const FOrganicSample& B)
    {
        const FVector2D D=B.Point-A.Point;
        const double Length=D.Size(), Delta=B.Radius-A.Radius;
        if (Length<1.e-8 || FMath::Abs(Delta)>=Length)
            return FMath::Min((P-A.Point).Size()-A.Radius,(P-B.Point).Size()-B.Radius);
        const FVector2D Q=P-A.Point;
        const double X=FVector2D::DotProduct(Q,D)/Length;
        const double Y=FMath::Abs(FVector2D::CrossProduct(Q,D))/Length;
        const double Slope=Delta/Length;
        const double T=FMath::Clamp((X+Slope*Y/FMath::Sqrt(1-Slope*Slope))/Length,0.,1.);
        return (P-A.Point-D*T).Size()-FMath::Lerp(A.Radius,B.Radius,T);
    }

    void SimplifyContour(TArray<FVector2D>& Points, double Tolerance)
    {
        for (int32 Pass=0; Pass<3; ++Pass)
        for (int32 I=Points.Num()-1; I>=0 && Points.Num()>12; --I)
        {
            const FVector2D A=Points[(I+Points.Num()-1)%Points.Num()];
            const FVector2D B=Points[(I+1)%Points.Num()];
            const FVector2D AB=B-A;
            const double T=FVector2D::DotProduct(Points[I]-A,AB)/FMath::Max(AB.SizeSquared(),1.e-12);
            if (T>=0 && T<=1 && (Points[I]-A-AB*T).SizeSquared()<Tolerance*Tolerance) Points.RemoveAt(I);
        }
    }
}

void AParametricPlatform::PrepareParametricShape()
{
    PreparedParametricOutline.Reset(); PreparedParametricHoles.Reset(); PreparedParametricWarning.Reset();
    const auto& Settings=ParametricShapeSettings;
    auto Warn=[&](const TCHAR* Message)
    {
        if (!PreparedParametricWarning.IsEmpty()) PreparedParametricWarning+=TEXT("\n");
        PreparedParametricWarning+=Message;
    };
    bool Sanitized=false;
    auto Safe=[&](double Value,double Min,double Max,double Default)
    {
        if (!FMath::IsFinite(Value)) { Sanitized=true; return Default; }
        Sanitized |= Value<Min || Value>Max;
        return FMath::Clamp(Value,Min,Max);
    };
    const double CenterRadius=Safe(Settings.CenterRadius,50,20000,800);
    const double Scale=Safe(Settings.UniformScale,.05,20,1);
    const double Blend=Safe(Settings.JunctionBlend,0,2000,220);
    const int32 Count=FMath::Clamp(Settings.ArmCount,0,16);
    const int32 Samples=FMath::Clamp(Settings.CurveSamples,16,128);
    const double Rotation=FMath::DegreesToRadians(Safe(Settings.RotationDegrees,-360000,360000,-90));
    const double Spread=FMath::DegreesToRadians(Safe(Settings.SpreadDegrees,0,360,360));
    Sanitized |= Count!=Settings.ArmCount || Samples!=Settings.CurveSamples;
    TMap<int32,const FPlatformParametricArmOverride*> Overrides;
    for (int32 I=0; I<FMath::Min(Settings.ArmOverrides.Num(),64); ++I)
    {
        const auto& Override=Settings.ArmOverrides[I];
        if (Override.ArmIndex<0 || Override.ArmIndex>=Count) continue;
        if (Overrides.Contains(Override.ArmIndex)) { Warn(TEXT("Duplicate arm index: the first override is used.")); continue; }
        Overrides.Add(Override.ArmIndex,&Override);
    }
    if (Settings.ArmOverrides.Num()>64) Warn(TEXT("Only the first 64 arm override entries are read."));

    TArray<FOrganicArm> Arms;
    FBox2D Bounds(FVector2D(-CenterRadius),FVector2D(CenterRadius));
    double MinimumFeature=CenterRadius*2;
    for (int32 I=0; I<Count; ++I)
    {
        const auto* const* Found=Overrides.Find(I);
        const auto* Override=Found ? *Found : nullptr;
        if (Override && !Override->bEnabled) continue;
        const bool Custom=Override && Override->bOverrideShape;
        const auto& Arm=Custom ? Override->Shape : Settings.DefaultArm;
        const double Offset=Count<=1 ? 0 : (Spread>=UE_DOUBLE_TWO_PI-.0001
            ? UE_DOUBLE_TWO_PI*I/Count : Spread*(double(I)/(Count-1)-.5));
        const double Angle=Rotation+Offset+(Override ? FMath::DegreesToRadians(Safe(Override->AngleOffset,-360000,360000,0)) : 0);
        const FVector2D D(FMath::Cos(Angle),FMath::Sin(Angle)), N(-D.Y,D.X);
        const double Length=Safe(Arm.Length,50,50000,1700);
        const double EndRadius=Safe(Arm.EndRadius,25,10000,650);
        const double RootRadius=Safe(Arm.RootWidth,25,10000,1000)*.5;
        const double NeckRadius=Safe(Arm.NeckWidth,25,10000,380)*.5;
        const double EndWidth=Safe(Arm.EndWidth,25,10000,1000);
        const double EndRadiusOnPath=FMath::Min(EndWidth*.5,EndRadius);
        const double Neck=Safe(Arm.NeckPosition,.15,.85,.48);
        double Bend=Safe(Arm.Bend,-1,1,.22), S=Safe(Arm.SCurve,-1,1,0);
        if (!Custom && Settings.bMirrorBends && Count>1)
        {
            const double Side=FMath::Abs(FMath::Sin(Offset))<1.e-6 ? 0 : -FMath::Sign(FMath::Sin(Offset));
            Bend*=Side; S*=Side;
        }
        const double EndOffset=Safe(Arm.EndOffset,-30000,30000,0);
        const FVector2D P0=D*(CenterRadius*.35);
        const FVector2D P3=D*(CenterRadius+Length)+N*EndOffset;
        const double Span=CenterRadius*.65+Length;
        const FVector2D P1=P0+D*(Span/3)+N*(Span*(Bend+S)+EndOffset/3);
        const FVector2D P2=P0+D*(Span*2/3)+N*(Span*(Bend-S)+EndOffset*2/3);
        auto& Resolved=Arms.Emplace_GetRef();
        Resolved.End=P3; Resolved.EndRadius=EndRadius;
        Bounds+=P3-FVector2D(EndRadius); Bounds+=P3+FVector2D(EndRadius);
        MinimumFeature=FMath::Min(MinimumFeature,FMath::Min3(RootRadius,NeckRadius,EndRadiusOnPath)*2);
        for (int32 J=0; J<=Samples; ++J)
        {
            const double T=double(J)/Samples, U=1-T;
            const FVector2D P=P0*(U*U*U)+P1*(3*U*U*T)+P2*(3*U*T*T)+P3*(T*T*T);
            const double Radius=T<Neck ? FMath::Lerp(RootRadius,NeckRadius,Ease(T/Neck))
                : FMath::Lerp(NeckRadius,EndRadiusOnPath,Ease((T-Neck)/(1-Neck)));
            Resolved.Samples.Add({P,Radius});
            Bounds+=P-FVector2D(Radius); Bounds+=P+FVector2D(Radius);
        }
    }
    if (Sanitized) Warn(TEXT("Non-finite or out-of-range shape settings were limited to safe values."));
    // Smooth unions expand by at most Blend/4 per overlap. Margin also provides an
    // outside border for marching squares; it never becomes part of the authored shape.
    const double Margin=Blend*(Arms.Num()+1)*.25+MinimumFeature*.5;
    Bounds.Min-=FVector2D(Margin); Bounds.Max+=FVector2D(Margin);
    const double Longest=FMath::Max(Bounds.GetSize().X,Bounds.GetSize().Y);
    const int32 Requested=FMath::Clamp(Settings.OutlineResolution,96,512);
    const int32 Resolution=FMath::Clamp(FMath::Max(Requested,FMath::CeilToInt(Longest/(MinimumFeature/5))),96,512);
    const double Step=Longest/Resolution;
    if (MinimumFeature<Step*3) Warn(TEXT("Some arms are thinner than the outline resolution. Increase their widths or shorten the layout."));
    const int32 NX=FMath::Max(2,FMath::CeilToInt(Bounds.GetSize().X/Step));
    const int32 NY=FMath::Max(2,FMath::CeilToInt(Bounds.GetSize().Y/Step));
    const int32 Stride=NX+1, Total=Stride*(NY+1);
    auto Position=[&](int32 X,int32 Y) { return Bounds.Min+FVector2D(X*Step,Y*Step); };
    TArray<double> Field;
    Field.SetNumUninitialized(Total);
    for (int32 Y=0; Y<=NY; ++Y) for (int32 X=0; X<=NX; ++X)
        Field[Y*Stride+X]=Position(X,Y).Size()-CenterRadius;
    TArray<double> ArmField;
    ArmField.SetNumUninitialized(Total);
    for (const auto& Arm : Arms)
    {
        for (double& V : ArmField) V=Longest*10;
        for (int32 I=1; I<Arm.Samples.Num(); ++I)
        {
            const auto& A=Arm.Samples[I-1]; const auto& B=Arm.Samples[I];
            const double R=FMath::Max(A.Radius,B.Radius)+Blend+Step;
            const FVector2D Lo(FMath::Min(A.Point.X,B.Point.X)-R,FMath::Min(A.Point.Y,B.Point.Y)-R);
            const FVector2D Hi(FMath::Max(A.Point.X,B.Point.X)+R,FMath::Max(A.Point.Y,B.Point.Y)+R);
            const int32 X0=FMath::Clamp(FMath::FloorToInt((Lo.X-Bounds.Min.X)/Step),0,NX);
            const int32 X1=FMath::Clamp(FMath::CeilToInt((Hi.X-Bounds.Min.X)/Step),0,NX);
            const int32 Y0=FMath::Clamp(FMath::FloorToInt((Lo.Y-Bounds.Min.Y)/Step),0,NY);
            const int32 Y1=FMath::Clamp(FMath::CeilToInt((Hi.Y-Bounds.Min.Y)/Step),0,NY);
            for (int32 Y=Y0; Y<=Y1; ++Y) for (int32 X=X0; X<=X1; ++X)
            {
                double& V=ArmField[Y*Stride+X];
                V=FMath::Min(V,TaperedSegmentDistance(Position(X,Y),A,B));
            }
        }
        for (int32 Y=0; Y<=NY; ++Y) for (int32 X=0; X<=NX; ++X)
        {
            const int32 ID=Y*Stride+X;
            const double V=SmoothUnion(ArmField[ID],(Position(X,Y)-Arm.End).Size()-Arm.EndRadius,Blend);
            Field[ID]=SmoothUnion(Field[ID],V,Blend);
        }
    }
    // Keep an exact zero away from lattice vertices so every contour vertex has two edges.
    for (double& V : Field) if (FMath::Abs(V)<1.e-8) V=1.e-8;
    TArray<FVector2D> Vertices;
    TArray<FIntPoint> Neighbors;
    TMap<int32,int32> EdgeVertices;
    bool Invalid=false;
    for (int32 Y=0; Y<NY; ++Y) for (int32 X=0; X<NX; ++X)
    {
        const FIntPoint C[4]={{X,Y},{X+1,Y},{X+1,Y+1},{X,Y+1}};
        const double F[4]={Field[Y*Stride+X],Field[Y*Stride+X+1],Field[(Y+1)*Stride+X+1],Field[(Y+1)*Stride+X]};
        const int32 Mask=(F[0]<0?1:0)|(F[1]<0?2:0)|(F[2]<0?4:0)|(F[3]<0?8:0);
        if (Mask==0 || Mask==15) continue;
        const int32 EdgeIDs[4]={Y*NX+X,NX*(NY+1)+Y*Stride+X+1,(Y+1)*NX+X,NX*(NY+1)+Y*Stride+X};
        auto Vertex=[&](int32 Edge)
        {
            if (const int32* Existing=EdgeVertices.Find(EdgeIDs[Edge])) return *Existing;
            const int32 Next=(Edge+1)%4;
            const double T=FMath::Clamp(F[Edge]/(F[Edge]-F[Next]),1.e-7,1.-1.e-7);
            const int32 Index=Vertices.Add(FMath::Lerp(Position(C[Edge].X,C[Edge].Y),Position(C[Next].X,C[Next].Y),T));
            Neighbors.Add(FIntPoint(-1,-1)); EdgeVertices.Add(EdgeIDs[Edge],Index);
            return Index;
        };
        auto Connect=[&](int32 E0,int32 E1)
        {
            const int32 A=Vertex(E0), B=Vertex(E1);
            auto Add=[&](int32 From,int32 To)
            {
                auto& Pair=Neighbors[From];
                if (Pair.X<0) Pair.X=To; else if (Pair.Y<0) Pair.Y=To; else Invalid=true;
            };
            Add(A,B); Add(B,A);
        };
        switch (Mask)
        {
        case 1: case 14: Connect(3,0); break;
        case 2: case 13: Connect(0,1); break;
        case 3: case 12: Connect(3,1); break;
        case 4: case 11: Connect(1,2); break;
        case 6: case 9: Connect(0,2); break;
        case 7: case 8: Connect(3,2); break;
        case 5: case 10:
        {
            // Asymptotic decider resolves saddle cells consistently across adjacent arms.
            const bool Pair01=F[0]*F[2]-F[1]*F[3]>0;
            if (Pair01) { Connect(0,1); Connect(2,3); }
            else { Connect(3,0); Connect(1,2); }
            break;
        }
        }
    }
    TBitArray<> Visited(false,Vertices.Num());
    TArray<TArray<FVector2D>> Loops;
    for (int32 Start=0; Start<Vertices.Num() && !Invalid; ++Start)
    {
        if (Visited[Start]) continue;
        TArray<FVector2D> Loop;
        int32 Previous=-1, Current=Start;
        do
        {
            if (!Vertices.IsValidIndex(Current) || Visited[Current] || Neighbors[Current].Y<0) { Invalid=true; break; }
            Visited[Current]=true; Loop.Add(Vertices[Current]);
            const FIntPoint Pair=Neighbors[Current];
            const int32 Next=Pair.X!=Previous ? Pair.X : Pair.Y;
            Previous=Current; Current=Next;
        } while (Current!=Start);
        if (Loop.Num()>=3)
        {
            SimplifyContour(Loop,Step*.035);
            Loops.Add(MoveTemp(Loop));
        }
    }
    if (Invalid || Loops.IsEmpty()) { Warn(TEXT("Could not form a closed outline. Increase outline resolution or the narrowest arm width.")); return; }
    int32 Largest=0;
    for (int32 I=1; I<Loops.Num(); ++I)
        if (FPolygon2d(Loops[I]).Area()>FPolygon2d(Loops[Largest]).Area()) Largest=I;
    const FPolygon2d Outer(Loops[Largest]);
    for (int32 I=0; I<Loops.Num(); ++I)
        if (I!=Largest && !Outer.Contains(Loops[I][0]))
        { Warn(TEXT("An arm is too thin to remain connected at this resolution. Increase its neck width.")); return; }
    PreparedParametricOutline=MoveTemp(Loops[Largest]);
    for (int32 I=0; I<Loops.Num(); ++I) if (I!=Largest) PreparedParametricHoles.Add(MoveTemp(Loops[I]));
    FVector2D Half=FVector2D::ZeroVector;
    auto Finish=[&](TArray<FVector2D>& Loop,bool Clockwise)
    {
        for (auto& V : Loop)
        {
            V*=Scale;
            Half.X=FMath::Max(Half.X,FMath::Abs(V.X)); Half.Y=FMath::Max(Half.Y,FMath::Abs(V.Y));
        }
        if (FPolygon2d(Loop).IsClockwise()!=Clockwise) Algo::Reverse(Loop);
    };
    Finish(PreparedParametricOutline,false);
    for (auto& Hole : PreparedParametricHoles) Finish(Hole,true);
    ShapeSize=(Half+FVector2D(.01))*2;
}
