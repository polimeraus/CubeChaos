#include "HousePaintSplatter.h"
#include "SimpleHouseActor.h"
#include "Algo/Reverse.h"

TArray<TArray<FVector2D>> HousePaint::MakeSplatter(float Width, float Height,
    ESimpleHouseWindowType Shape, float CornerRadius, float Spread, float Amount, int32 Seed)
{
    TArray<TArray<FVector2D>> Pieces;
    FRandomStream Random(Seed);
    const float A = FMath::Clamp(Amount, 0.f, 1.f);
    const float S = FMath::Max(Spread, 1.f);
    const FVector2D Half(Width*.5f, Height*.5f);
    const float Phase = Random.FRandRange(0.f, UE_TWO_PI);
    const float Phase2 = Random.FRandRange(0.f, UE_TWO_PI);
    const FVector2D Impact(FMath::Min(S*.08f,float(Half.X)*.15f)*Random.FRandRange(-1.f,1.f),
        FMath::Min(S*.08f,float(Half.Y)*.12f)*Random.FRandRange(-.5f,1.f));
    auto Direction = [](float Angle) { return FVector2D(FMath::Cos(Angle),FMath::Sin(Angle)); };
    auto InsideOpening = [&](FVector2D P)
    {
        if (Shape == ESimpleHouseWindowType::Circular) return P.SizeSquared() <= Half.X*Half.X;
        if (FMath::Abs(P.X)>Half.X || FMath::Abs(P.Y)>Half.Y) return false;
        if (Shape == ESimpleHouseWindowType::Arched)
        {
            const float ArchHeight = FMath::Min(float(Half.X),Height*.9f);
            const float Shoulder = Half.Y-ArchHeight;
            return P.Y<=Shoulder || FMath::Square(P.X/Half.X)+FMath::Square((P.Y-Shoulder)/ArchHeight)<=1.f;
        }
        if (Shape == ESimpleHouseWindowType::RoundedCorners)
        {
            const float R = FMath::Clamp(CornerRadius,0.f,float(FMath::Min(Half.X,Half.Y)));
            const FVector2D Q(FMath::Max(0.,FMath::Abs(P.X)-Half.X+R),FMath::Max(0.,FMath::Abs(P.Y)-Half.Y+R));
            return Q.SizeSquared()<=R*R;
        }
        return true;
    };
    auto CoreRadius = [&](float Angle)
    {
        // Start from the actual opening, then grow an uneven deposited paint mass.
        // Unlike an ellipse around the bounds this also follows wide/arched windows.
        const FVector2D D = Direction(Angle);
        float Low = 0, High = Half.Size()+S;
        for (int32 I=0; I<20; ++I)
        {
            const float Mid = (Low+High)*.5f;
            if (InsideOpening(Impact+D*Mid)) Low=Mid; else High=Mid;
        }
        const float Lobes = .12f*FMath::Sin(3*Angle+Phase)+.09f*FMath::Sin(7*Angle+Phase2)
            +.055f*FMath::Sin(13*Angle-Phase)+.035f*FMath::Sin(29*Angle+Phase2);
        return High+S*(.36f+.13f*A+Lobes*(.25f+.75f*A));
    };
    auto CorePoint = [&](float Angle,float Inset=0.f) { return Impact+Direction(Angle)*(CoreRadius(Angle)+Inset); };
    TArray<FVector2D> Patch;
    constexpr int32 CoreSegments=160;
    for (int32 I=0; I<CoreSegments; ++I)
    {
        const float Angle=UE_TWO_PI*I/CoreSegments;
        Patch.Add(CorePoint(Angle));
    }
    Pieces.Add(MoveTemp(Patch));

    // Connected, tapered fingers carry the energy of the splash. Broad roots flow
    // into thin bent tips; staggered lengths keep the silhouette from becoming a sun.
    const int32 FingerCount=FMath::RoundToInt(FMath::Lerp(9.f,30.f,A));
    for (int32 I=0; I<FingerCount; ++I)
    {
        float Angle=Phase+UE_TWO_PI*(I+Random.FRandRange(-.36f,.36f))/FingerCount;
        if (I%3!=0) Angle=Random.FRandRange(0.f,UE_TWO_PI);
        const FVector2D D=Direction(Angle), T(-D.Y,D.X);
        const FVector2D Root=CorePoint(Angle,-S*.23f);
        const float Length=S*Random.FRandRange(.55f,2.35f)*(.45f+.55f*A)*(I%5==0?1.25f:1.f);
        const float W=S*Random.FRandRange(.045f,.12f)*(I%6==0?1.4f:1.f);
        const float Bend=S*Random.FRandRange(-.24f,.24f);
        const FVector2D Shoulder=Root+D*(Length*.32f)+T*(Bend*.2f);
        const FVector2D Neck=Root+D*(Length*.75f)+T*(Bend*.65f);
        const FVector2D Tip=Root+D*Length+T*Bend;
        TArray<FVector2D> Finger,OtherEdge;
        const float Wobble=Random.FRandRange(0.f,UE_TWO_PI);
        for (int32 J=0;J<8;++J)
        {
            const float U=J/8.f;
            const FVector2D Centre=Root+D*(Length*U)+T*(Bend*U*U);
            const float Girth=W*FMath::Pow(1.f-U,1.7f)*(1.f+.12f*FMath::Sin(U*15.f+Wobble));
            Finger.Add(Centre-T*Girth);
            OtherEdge.Add(Centre+T*Girth*(.85f+.2f*U));
        }
        Finger.Add(Tip);
        for(int32 J=OtherEdge.Num()-1;J>=0;--J)Finger.Add(OtherEdge[J]);
        Pieces.Add(MoveTemp(Finger));
        if (I%4==0 && A>.35f)
        {
            const float Side=Random.FRand()>.5f?1.f:-1.f;
            const FVector2D Fork=Shoulder+D*(Length*.1f);
            const FVector2D ForkTip=Fork+D*(Length*.32f)+T*(S*.24f*Side);
            Pieces.Add({Fork-T*(W*.2f),ForkTip,Fork+T*(W*.28f)});
        }
    }

    // Small torn-off satellites follow outward flight paths, with a few elongated
    // flecks. Their size is deliberately much smaller than the deposited mass.
    const int32 DropletCount=FMath::RoundToInt(FMath::Lerp(10.f,44.f,A));
    for (int32 I=0; I<DropletCount; ++I)
    {
        const float Angle=Random.FRandRange(0,UE_TWO_PI);
        const FVector2D D=Direction(Angle),T(-D.Y,D.X);
        const FVector2D Centre=CorePoint(Angle,S*Random.FRandRange(.25f,2.4f));
        const float Radius=S*Random.FRandRange(.012f,.055f);
        const float Stretch=I%5==0?Random.FRandRange(2.2f,4.2f):Random.FRandRange(1.f,1.8f);
        TArray<FVector2D> Droplet;
        for (int32 J=0; J<10; ++J)
        {
            const float Theta=UE_TWO_PI*J/10.f;
            const float Edge=Random.FRandRange(.85f,1.12f);
            Droplet.Add(Centre+D*(FMath::Cos(Theta)*Radius*Stretch*Edge)+T*(FMath::Sin(Theta)*Radius*Edge));
        }
        Pieces.Add(MoveTemp(Droplet));
    }

    // Gravity runs start inside the wet lower/side edge, rather than floating below
    // it. Thin trails alternate with a few long heavy drips, as in thrown wall paint.
    const int32 DripCount=FMath::RoundToInt(FMath::Lerp(5.f,20.f,A));
    for (int32 I=0; I<DripCount; ++I)
    {
        const float Angle=-PI+.15f+(PI-.3f)*(I+Random.FRandRange(.1f,.9f))/DripCount;
        const FVector2D Root=CorePoint(Angle,-S*.19f);
        const float Length=S*Random.FRandRange(.35f,2.55f)*(I%6==0?1.3f:1.f)*(.5f+.5f*A);
        const float RootWidth=S*Random.FRandRange(.025f,.07f);
        const float NeckWidth=RootWidth*Random.FRandRange(.18f,.38f);
        const float Bulb=NeckWidth*Random.FRandRange(1.1f,1.8f);
        const float Lean=S*Random.FRandRange(-.035f,.035f);
        const float Wobble=Random.FRandRange(0,UE_TWO_PI);
        TArray<FVector2D> Right,Left;
        constexpr int32 Steps=9;
        for (int32 J=0; J<=Steps; ++J)
        {
            const float U=float(J)/Steps;
            const float WidthAt=NeckWidth+(RootWidth-NeckWidth)*FMath::Pow(1.f-U,5.f)
                +NeckWidth*.13f*FMath::Sin(U*18.f+Wobble);
            const float X=Root.X+Lean*U+NeckWidth*.35f*FMath::Sin(U*9.f+Wobble)*U;
            const FVector2D Centre(X,Root.Y-Length*U);
            Right.Add(Centre+FVector2D(WidthAt,0));
            Left.Add(Centre-FVector2D(WidthAt,0));
        }
        const FVector2D End=(Right.Last()+Left.Last())*.5f;
        TArray<FVector2D> Drip;
        for (const FVector2D& P:Right) Drip.Add(P);
        for (int32 J=0; J<=6; ++J)
        {
            const float Theta=-PI*J/6.f;
            Drip.Add(End+FVector2D(FMath::Cos(Theta)*Bulb,FMath::Sin(Theta)*Bulb));
        }
        for (int32 J=Left.Num()-1; J>=0; --J) Drip.Add(Left[J]);
        Pieces.Add(MoveTemp(Drip));
    }
    // Extrusion expects simple, consistently wound profiles, including mirrored
    // forks and downward runs. Wall/opening clipping is kept at the caller.
    for (TArray<FVector2D>& Polygon:Pieces)
    {
        double Area=0;
        for (int32 I=0; I<Polygon.Num(); ++I)
        {
            const FVector2D& P=Polygon[I];const FVector2D& Q=Polygon[(I+1)%Polygon.Num()];
            Area+=P.X*Q.Y-Q.X*P.Y;
        }
        if (Area<0) Algo::Reverse(Polygon);
    }
    return Pieces;
}
