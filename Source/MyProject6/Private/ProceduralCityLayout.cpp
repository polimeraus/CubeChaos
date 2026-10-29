#include "ProceduralCityLayout.h"
#include "Misc/Crc.h"

bool FProceduralCityRules::Validate(FString& Error) const
{
    auto Bad=[&](const TCHAR* S){Error=S;return false;};
    if (uint8(Layout)>uint8(ECityLayout::Organic)) return Bad(TEXT("Unknown Layout."));
    for (float V:{MinDistrictRadius,MaxDistrictRadius,DistrictGap,PositionJitter,RoundPlatformChance,BaseElevation,
        ElevationStep,RoadWidth,ExtraConnectionChance,Curvature,MaxRampSlope,StairChance,MaxStepRise,MinStepTread,
        MaxStairHeight,BuildingClearance,PlatformEdgeSetback,BuildingAccessWidth})
        if (!FMath::IsFinite(V)) return Bad(TEXT("Rules contain a non-finite number."));
    if (DistrictCount<3 || DistrictCount>25) return Bad(TEXT("DistrictCount must be 3..25."));
    if (MinDistrictRadius<1600 || MaxDistrictRadius<MinDistrictRadius || MaxDistrictRadius>12000) return Bad(TEXT("District radius range must be ordered and within 1600..12000 cm."));
    if (DistrictGap<600 || DistrictGap>30000 || PositionJitter<0 || PositionJitter>.18) return Bad(TEXT("DistrictGap must be 600..30000; PositionJitter 0..0.18."));
    for (float V:{RoundPlatformChance,ExtraConnectionChance,StairChance}) if (V<0 || V>1) return Bad(TEXT("Probabilities must be 0..1."));
    if (BaseElevation<80 || BaseElevation>10000 || ElevationStep<0 || ElevationStep>1500 || ElevationLevels<1 || ElevationLevels>5) return Bad(TEXT("Invalid elevation range."));
    if (RoadWidth<180 || RoadWidth>MinDistrictRadius*.45 || BuildingAccessWidth<120 || BuildingAccessWidth>RoadWidth) return Bad(TEXT("Road/access widths do not fit the districts."));
    if (Curvature<0 || Curvature>.2 || MaxRampSlope<1 || MaxRampSlope>20 || MaxStepRise<8 || MaxStepRise>30 || MinStepTread<25 || MinStepTread>300 || MaxStairHeight<0 || MaxStairHeight>1500) return Bad(TEXT("Invalid route slope, curvature or stair dimensions."));
    if (MinBuildingsPerDistrict<0 || MaxBuildingsPerDistrict<MinBuildingsPerDistrict || MaxBuildingsPerDistrict>6 || MaxTotalBuildings<1 || MaxTotalBuildings>64) return Bad(TEXT("Invalid building count range."));
    if (BuildingClearance<80 || PlatformEdgeSetback<80 || BuildingClearance+PlatformEdgeSetback>MinDistrictRadius*.4) return Bad(TEXT("Building clearance and setback do not fit the platforms."));
    if (MaxEstimatedBuildingCells<1000 || MaxEstimatedBuildingCells>300000 || PlacementAttempts<20 || PlacementAttempts>1000) return Bad(TEXT("Invalid generation budget."));
    Error.Reset();return true;
}

double FCityDistrictPlan::BoundaryDistance(FVector2D D) const
{
    if (bRound) return Radius;
    const FVector2D U(FMath::Abs(D.X),FMath::Abs(D.Y));
    const double H=Inradius(),C=H*.3,T=H/FMath::Max(.001,FMath::Max(U.X,U.Y));
    const FVector2D Q=U*T;
    if (FMath::Min(Q.X,Q.Y)<=H-C) return T;
    const FVector2D CornerCenter(H-C,H-C);const double Dot=FVector2D::DotProduct(CornerCenter,U);
    return Dot+FMath::Sqrt(FMath::Max(0.,Dot*Dot-CornerCenter.SizeSquared()+C*C));
}

namespace ProceduralCity
{
double PointSegmentDistance(FVector2D P,FVector2D A,FVector2D B)
{
    const FVector2D AB=B-A;
    const double T=FMath::Clamp(FVector2D::DotProduct(P-A,AB)/FMath::Max(AB.SizeSquared(),.0001),0.,1.);
    return (P-(A+AB*T)).Length();
}
static double Cross(FVector2D A,FVector2D B) {return A.X*B.Y-A.Y*B.X;}
static bool Crosses(FVector2D A,FVector2D B,FVector2D C,FVector2D D)
{
    const double Den=Cross(B-A,D-C);
    if (FMath::Abs(Den)<.001) return false;
    const double T=Cross(C-A,D-C)/Den,U=Cross(C-A,B-A)/Den;
    return T>0.001 && T<.999 && U>.001 && U<.999;
}
static double PathLength(const TArray<FVector>& P)
{
    double L=0;for (int32 I=1;I<P.Num();++I) L+=FVector2D(P[I]-P[I-1]).Length();return L;
}
static bool RouteClear(const FCityConnectionPlan& C,const FProceduralCityPlan& P,
    const FProceduralCityRules& R,const TArray<FCityExclusionZone>& Exclusions)
{
    for (int32 S=1;S<C.Points.Num();++S)
    {
        const FVector2D A(C.Points[S-1]),B(C.Points[S]);
        for (int32 D=0;D<P.Districts.Num();++D)
            if (D!=C.From && D!=C.To && PointSegmentDistance(FVector2D(P.Districts[D].Center),A,B)<P.Districts[D].Radius+R.RoadWidth*.5+80) return false;
        for (const auto& E:Exclusions) if (PointSegmentDistance(E.Center,A,B)<E.Radius+R.RoadWidth*.5+80) return false;
        for (const auto& Other:P.Connections)
        {
            // Shared ports join on the district's open circulation surface.
            if (Other.From==C.From || Other.To==C.From || Other.From==C.To || Other.To==C.To) continue;
            for (int32 T=1;T<Other.Points.Num();++T)
                if (Crosses(A,B,FVector2D(Other.Points[T-1]),FVector2D(Other.Points[T]))) return false;
        }
    }
    return true;
}

bool MakePlan(const FProceduralCityRules& R,int32 Seed,const TArray<FCityBuildingCatalogEntry>& Catalog,
    const TArray<FCityExclusionZone>& Exclusions,FProceduralCityPlan& Out,FString& Error)
{
    if (!R.Validate(Error)) return false;
    TSet<FName> Ids;
    for (const auto& C:Catalog)
    {
        if (C.Id.IsNone() || Ids.Contains(C.Id) || !FMath::IsFinite(C.Weight) || C.Weight<0 ||
            !FMath::IsFinite(C.FootprintRadius) || C.FootprintRadius<20 || C.PivotOffset.ContainsNaN() ||
            !FMath::IsFinite(C.MinScale) || !FMath::IsFinite(C.MaxScale) || C.MinScale<.1 || C.MaxScale<C.MinScale || C.MaxScale>3 ||
            !FMath::IsFinite(C.FrontYaw) || C.EstimatedCells<1)
        {Error=TEXT("Catalog IDs must be unique; footprint, scale, pivot, weight and cell estimates must be valid.");return false;}
        Ids.Add(C.Id);
    }
    for (const auto& E:Exclusions) if (E.Center.ContainsNaN() || !FMath::IsFinite(E.Radius) || E.Radius<=0)
    {Error=TEXT("Invalid exclusion zone.");return false;}
    FProceduralCityPlan P;P.Seed=Seed;FRandomStream RNG(Seed);
    const double Spacing=(2*R.MaxDistrictRadius+R.DistrictGap)/(1-2*R.PositionJitter);
    TArray<FIntPoint> Grid;
    for (int32 Y=-5;Y<=5;++Y) for (int32 X=-5;X<=5;++X) Grid.Add({X,Y});
    Grid.Sort([](const FIntPoint& A,const FIntPoint& B){const int32 DA=A.X*A.X+A.Y*A.Y,DB=B.X*B.X+B.Y*B.Y;
        return DA==DB ? (A.Y==B.Y?A.X<B.X:A.Y<B.Y) : DA<DB;});
    const double Phase=RNG.FRandRange(-UE_PI,UE_PI);
    for (int32 I=0;I<R.DistrictCount;++I)
    {
        FCityDistrictPlan& D=P.Districts.AddDefaulted_GetRef();
        D.Radius=I==0?R.MaxDistrictRadius:RNG.FRandRange(R.MinDistrictRadius,R.MaxDistrictRadius);
        D.bRound=I==0 || RNG.FRand()<R.RoundPlatformChance;D.PaletteIndex=I%4;
        D.Center.Z=R.BaseElevation+(I==0?0:RNG.RandRange(0,R.ElevationLevels-1))*R.ElevationStep;
        if (!I) continue;
        FVector2D XY;
        if (R.Layout==ECityLayout::Radial)
        {
            const double Step=UE_DOUBLE_TWO_PI/(R.DistrictCount-1);
            const double Radius=FMath::Max(Spacing,Spacing/(2*FMath::Sin(Step*.5)))*(1+2*R.PositionJitter);
            const double A=Phase+(I-1)*Step+RNG.FRandRange(-R.PositionJitter,R.PositionJitter)*Step*.35;
            XY=FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius;
        }
        else if (R.Layout==ECityLayout::Grid) XY=FVector2D(Grid[I].X,Grid[I].Y)*Spacing;
        else XY={I*Spacing,FMath::Sin(I*1.4+Phase)*Spacing*.65};
        if (R.Layout!=ECityLayout::Radial) XY+=FVector2D(RNG.FRandRange(-1.f,1.f),RNG.FRandRange(-1.f,1.f))*Spacing*R.PositionJitter;
        D.Center.X=XY.X;D.Center.Y=XY.Y;
    }
    for (const auto& D:P.Districts) for (const auto& E:Exclusions)
        if ((FVector2D(D.Center)-E.Center).Length()<D.Radius+E.Radius+80)
        {Error=TEXT("A district intersects an exclusion zone. Change the seed, layout or exclusion position. Previous city retained.");return false;}

    struct FEdge {int32 A,B;double Distance,Bend,Choice;};TArray<FEdge> Edges;
    for (int32 A=0;A<P.Districts.Num();++A) for (int32 B=A+1;B<P.Districts.Num();++B)
        Edges.Add({A,B,FVector2D(P.Districts[B].Center-P.Districts[A].Center).Length(),RNG.FRandRange(-1.f,1.f),RNG.FRand()});
    Edges.Sort([](const FEdge& A,const FEdge& B){return A.Distance==B.Distance?(A.A==B.A?A.B<B.B:A.A<B.A):A.Distance<B.Distance;});
    TArray<int32> Groups;for (int32 I=0;I<R.DistrictCount;++I) Groups.Add(I);
    auto Connect=[&](const FEdge& E)->bool
    {
        auto& A=P.Districts[E.A];auto& B=P.Districts[E.B];
        if (A.Ports.Num()>=6 || B.Ports.Num()>=6) return false;
        const FVector2D Direction=FVector2D(B.Center-A.Center).GetSafeNormal(),Side(-Direction.Y,Direction.X);
        const FVector Start=A.Center+FVector(Direction*(A.BoundaryDistance(Direction)-90),0);
        const FVector End=B.Center-FVector(Direction*(B.BoundaryDistance(-Direction)-90),0);
        FCityConnectionPlan C;C.From=E.A;C.To=E.B;
        for (double BendFactor:{1.,.4,0.})
        {
            C.Points.Reset();const FVector Mid=(Start+End)*.5+FVector(Side*(End-Start).Size2D()*E.Bend*R.Curvature*BendFactor,0);
            constexpr int32 Segments=16;
            for (int32 S=0;S<=Segments;++S)
            {const double T=double(S)/Segments;C.Points.Add((1-T)*(1-T)*Start+2*(1-T)*T*Mid+T*T*End);}
            C.Length=PathLength(C.Points);
            // Constant height change per travelled metre keeps curved ramps within the slope limit.
            double Along=0;
            for (int32 S=1;S<C.Points.Num();++S)
            {Along+=FVector2D(C.Points[S]-C.Points[S-1]).Length();C.Points[S].Z=FMath::Lerp(Start.Z,End.Z,Along/C.Length);}
            const double Rise=FMath::Abs(End.Z-Start.Z),Slope=FMath::RadiansToDegrees(FMath::Atan2(Rise,double(C.Length)));
            if (Slope>R.MaxRampSlope || !RouteClear(C,P,R,Exclusions)) continue;
            const int32 Steps=FMath::Max(1,FMath::CeilToInt(Rise/R.MaxStepRise));
            C.Kind=Rise<.1?ECityLinkKind::Bridge:ECityLinkKind::Ramp;
            if (Rise>.1 && Rise<=R.MaxStairHeight && C.Length/Steps>=R.MinStepTread && E.Choice<R.StairChance) C.Kind=ECityLinkKind::Stairs;
            P.Connections.Add(C);A.Ports.Add(Direction);B.Ports.Add(-Direction);return true;
        }
        return false;
    };
    // Minimum spanning backbone first; optional loops may never compromise connectivity.
    for (const FEdge& E:Edges) if (Groups[E.A]!=Groups[E.B] && Connect(E))
    {const int32 Old=Groups[E.B],New=Groups[E.A];for (int32& G:Groups) if (G==Old) G=New;}
    for (int32 G:Groups) if (G!=Groups[0]) {Error=TEXT("No connected walkable route network fits these rules. Reduce elevation or enlarge district gaps.");return false;}
    for (const FEdge& E:Edges)
    {
        if (E.Choice>R.ExtraConnectionChance || E.Distance>Spacing*2.1) continue;
        bool Exists=false;for (const auto& C:P.Connections) Exists|=C.From==E.A && C.To==E.B;
        if (!Exists) Connect(E);
    }

    TArray<int32> Uses;Uses.Init(0,Catalog.Num());
    for (int32 D=0;D<P.Districts.Num() && P.Buildings.Num()<R.MaxTotalBuildings;++D)
    {
        const auto& District=P.Districts[D];const bool Landmark=D==0 && R.bHubLandmark;
        const int32 Desired=Landmark?1:(D==0?0:RNG.RandRange(R.MinBuildingsPerDistrict,R.MaxBuildingsPerDistrict));
        int32 Made=0;
        for (int32 Attempt=0;Attempt<R.PlacementAttempts && Made<Desired && P.Buildings.Num()<R.MaxTotalBuildings;++Attempt)
        {
            double Total=0;for (int32 I=0;I<Catalog.Num();++I)
                if (Catalog[I].ActorClass && (!Landmark || Catalog[I].bAllowAsLandmark) && P.EstimatedCells+Catalog[I].EstimatedCells<=R.MaxEstimatedBuildingCells)
                    Total+=Catalog[I].Weight/(1+Uses[I]*.35);
            if (Total<=0) break;
            double Pick=RNG.FRand()*Total;int32 Selected=INDEX_NONE;
            for (int32 I=0;I<Catalog.Num();++I)
                if (Catalog[I].ActorClass && (!Landmark || Catalog[I].bAllowAsLandmark) && P.EstimatedCells+Catalog[I].EstimatedCells<=R.MaxEstimatedBuildingCells)
                {Pick-=Catalog[I].Weight/(1+Uses[I]*.35);if (Pick<=0){Selected=I;break;}}
            if (Selected==INDEX_NONE) continue;
            const auto& C=Catalog[Selected];const double Scale=RNG.FRandRange(C.MinScale,C.MaxScale),Radius=C.FootprintRadius*Scale;
            if (Landmark && Radius+R.BuildingClearance>District.Inradius()*.67-R.RoadWidth*.5) continue;
            const double Limit=District.Inradius()-R.PlatformEdgeSetback-Radius;
            if (Limit<100) continue;
            const double Angle=RNG.FRand()*UE_DOUBLE_TWO_PI,Distance=Landmark?0:FMath::Sqrt(RNG.FRand())*Limit;
            const FVector2D Offset(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance),Center=FVector2D(District.Center)+Offset;
            bool Clear=Landmark || Distance>Radius+R.RoadWidth+R.BuildingClearance;
            if (!Landmark) for (const auto& Port:District.Ports)
                if (PointSegmentDistance(Offset,FVector2D::ZeroVector,Port*District.BoundaryDistance(Port))<Radius+R.RoadWidth*.5+R.BuildingClearance) Clear=false;
            for (const auto& B:P.Buildings) if (B.District==D)
            {
                if ((Center-FVector2D(B.Center)).Length()<Radius+B.Radius+R.BuildingClearance) Clear=false;
                // Keep each building's approach corridor open, including previously placed approaches.
                if (PointSegmentDistance(FVector2D(B.Center),FVector2D(District.Center),Center)<B.Radius+R.BuildingAccessWidth*.5+R.BuildingClearance) Clear=false;
                if (PointSegmentDistance(Center,FVector2D(District.Center),FVector2D(B.Center))<Radius+R.BuildingAccessWidth*.5+R.BuildingClearance) Clear=false;
            }
            if (!Clear) continue;
            FCityBuildingPlan& B=P.Buildings.AddDefaulted_GetRef();B.District=D;B.CatalogIndex=Selected;B.Center=FVector(Center,District.Center.Z);
            B.Radius=Radius;B.Scale=Scale;B.Yaw=(Landmark?-90:FMath::RadiansToDegrees(FMath::Atan2(-Offset.Y,-Offset.X)))-C.FrontYaw;
            P.EstimatedCells+=C.EstimatedCells;++Uses[Selected];++Made;
        }
        if (Made<Desired) P.Notes+=FString::Printf(TEXT("District %d: fitted %d/%d requested buildings; circulation/setbacks/budget take priority.\n"),D,Made,Desired);
    }
    P.EntryLocation=P.Districts[0].Center+FVector(0,-P.Districts[0].Inradius()*.67,110);
    for (const auto& D:P.Districts) P.FoundationRadius=FMath::Max(P.FoundationRadius,float(D.Center.Size2D()+D.Radius+1000));
    FString Digest=FString::Printf(TEXT("V1:%d;"),Seed);
    for (const auto& D:P.Districts) Digest+=FString::Printf(TEXT("D:%.3f,%.3f,%.3f,%.3f,%d;"),D.Center.X,D.Center.Y,D.Center.Z,D.Radius,D.bRound);
    for (const auto& C:P.Connections)
    {Digest+=FString::Printf(TEXT("C:%d,%d,%d;"),C.From,C.To,int32(C.Kind));for (const auto& V:C.Points) Digest+=FString::Printf(TEXT("%.2f,%.2f,%.2f;"),V.X,V.Y,V.Z);}
    for (const auto& B:P.Buildings) Digest+=FString::Printf(TEXT("B:%s,%s,%d,%.3f,%.3f,%.3f,%.3f;"),*Catalog[B.CatalogIndex].Id.ToString(),*Catalog[B.CatalogIndex].ActorClass->GetPathName(),B.District,B.Center.X,B.Center.Y,B.Scale,B.Yaw);
    P.Signature=FString::Printf(TEXT("CITY-V1-%08X"),FCrc::StrCrc32(*Digest));
    if (!ValidatePlan(R,P,Error)) return false;
    Out=MoveTemp(P);return true;
}

bool ValidatePlan(const FProceduralCityRules& R,const FProceduralCityPlan& P,FString& Error)
{
    auto Bad=[&](const TCHAR* S){Error=S;return false;};
    if (!R.Validate(Error)) return false;
    if (P.Districts.Num()!=R.DistrictCount) return Bad(TEXT("District count mismatch."));
    for (int32 I=0;I<P.Districts.Num();++I)
    {
        const auto& A=P.Districts[I];
        if (A.Center.ContainsNaN() || A.Radius<=0) return Bad(TEXT("Invalid district geometry."));
        for (int32 J=I+1;J<P.Districts.Num();++J)
            if (FVector2D(A.Center-P.Districts[J].Center).Length()<A.Radius+P.Districts[J].Radius+R.DistrictGap-1) return Bad(TEXT("District clearance violated."));
    }
    TSet<int32> Reached;Reached.Add(0);
    for (int32 Pass=0;Pass<R.DistrictCount;++Pass) for (const auto& C:P.Connections)
    {
        if (!P.Districts.IsValidIndex(C.From) || !P.Districts.IsValidIndex(C.To) || C.From==C.To || C.Points.Num()<2) return Bad(TEXT("Invalid route endpoints."));
        if (Reached.Contains(C.From)) Reached.Add(C.To);if (Reached.Contains(C.To)) Reached.Add(C.From);
        const double L=PathLength(C.Points),Rise=FMath::Abs(C.Points.Last().Z-C.Points[0].Z);
        if (L<1 || FMath::RadiansToDegrees(FMath::Atan2(Rise,L))>R.MaxRampSlope+.01) return Bad(TEXT("Route slope exceeds the rule."));
        if (C.Kind==ECityLinkKind::Stairs && L/FMath::Max(1,FMath::CeilToInt(Rise/R.MaxStepRise))<R.MinStepTread-.01) return Bad(TEXT("Stair tread is too short."));
        for (int32 S=1;S<C.Points.Num();++S) for (int32 D=0;D<P.Districts.Num();++D)
            if (D!=C.From && D!=C.To && PointSegmentDistance(FVector2D(P.Districts[D].Center),FVector2D(C.Points[S-1]),FVector2D(C.Points[S]))<P.Districts[D].Radius+R.RoadWidth*.5+79) return Bad(TEXT("Route intersects an unrelated district."));
    }
    if (Reached.Num()!=P.Districts.Num()) return Bad(TEXT("Disconnected district graph."));
    if (P.Buildings.Num()>R.MaxTotalBuildings || P.EstimatedCells>R.MaxEstimatedBuildingCells) return Bad(TEXT("Building budget exceeded."));
    for (int32 I=0;I<P.Buildings.Num();++I)
    {
        const auto& B=P.Buildings[I];if (!P.Districts.IsValidIndex(B.District) || B.Center.ContainsNaN() || B.Radius<=0) return Bad(TEXT("Invalid plot."));
        const auto& D=P.Districts[B.District];const FVector2D Offset(B.Center-D.Center);
        if (Offset.Length()+B.Radius+R.PlatformEdgeSetback>D.Inradius()+.1) return Bad(TEXT("Building exceeds its platform setback."));
        if (B.District!=0) for (const auto& Port:D.Ports)
            if (PointSegmentDistance(Offset,FVector2D::ZeroVector,Port*D.BoundaryDistance(Port))<B.Radius+R.RoadWidth*.5+R.BuildingClearance-.1) return Bad(TEXT("Building blocks a circulation corridor."));
        for (int32 J=I+1;J<P.Buildings.Num();++J)
            if (FVector2D(B.Center-P.Buildings[J].Center).Length()<B.Radius+P.Buildings[J].Radius+R.BuildingClearance-.1) return Bad(TEXT("Building footprints overlap."));
    }
    Error.Reset();return true;
}
}
