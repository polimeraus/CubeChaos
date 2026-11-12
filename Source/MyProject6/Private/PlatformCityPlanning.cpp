#include "ParametricPlatformCity.h"
#include "PlatformPolygonGeometry.h"
#include "Curve/PolygonOffsetUtils.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Misc/Crc.h"

using namespace UE::Geometry;

bool FPlatformCityRules::Validate(FString& Error) const
{
    auto Bad = [&](const TCHAR* Message) { Error = Message; return false; };
    for (float Value : {Width, Height, MainRoadWidth, SideRoadWidth, MinPlateSize, TargetPlateSize,
        DistrictVariation, RoadCurvature, SideRoadBend, BranchChance, LargePlateChance, Thickness, CornerRadius, RoadSampleSpacing})
        if (!FMath::IsFinite(Value)) return Bad(TEXT("All settings must be finite numbers."));
    if (Width < 1000 || Height < 1000 || Width > 100000 || Height > 100000)
        return Bad(TEXT("Width and Height must be 1000..100000 cm."));
    if (VerticalRoads < 0 || HorizontalRoads < 0 || VerticalRoads > 12 || HorizontalRoads > 12)
        return Bad(TEXT("Main road counts must be 0..12."));
    if (SideRoadWidth < 80 || MainRoadWidth < SideRoadWidth * 2 || MainRoadWidth > 5000)
        return Bad(TEXT("SideRoadWidth must be >=80; MainRoadWidth must be >=2 x SideRoadWidth and <=5000."));
    if (MinPlateSize < 200 || TargetPlateSize < MinPlateSize || TargetPlateSize > 10000)
        return Bad(TEXT("Plate sizes must satisfy 200 <= MinPlateSize <= TargetPlateSize <= 10000."));
    for (float Value : {DistrictVariation, RoadCurvature, SideRoadBend, BranchChance, LargePlateChance})
        if (Value < 0 || Value > 1) return Bad(TEXT("Variation, curvature and probability settings must be 0..1."));
    if (Thickness < 10 || Thickness > 500 || CornerRadius < 0 || CornerRadius > 5000 || CornerSegments < 2 || CornerSegments > 24)
        return Bad(TEXT("Thickness 10..500; CornerRadius 0..5000; CornerSegments 2..24."));
    if (Width - VerticalRoads * MainRoadWidth < (VerticalRoads + 1) * MinPlateSize * 1.8f ||
        Height - HorizontalRoads * MainRoadWidth < (HorizontalRoads + 1) * MinPlateSize * 1.8f)
        return Bad(TEXT("Main roads and minimum usable plate dimensions do not fit inside the city boundary."));
    if (MaxPlates < (VerticalRoads + 1) * (HorizontalRoads + 1) || MaxPlates > 256)
        return Bad(TEXT("MaxPlates must accommodate every main-road district and be <=256."));
    if (RoadSampleSpacing < 50 || RoadSampleSpacing > 300)
        return Bad(TEXT("RoadSampleSpacing must be 50..300 cm."));
    Error.Reset(); return true;
}

namespace
{
    void SortPolygons(TArray<FGeneralPolygon2d>& Polygons)
    {
        Polygons.Sort([](const FGeneralPolygon2d& A, const FGeneralPolygon2d& B)
        {
            const auto P = A.Bounds().Center(), Q = B.Bounds().Center();
            return P.Y == Q.Y ? P.X < Q.X : P.Y < Q.Y;
        });
    }

    bool HasUsableArea(const FGeneralPolygon2d& Polygon, double Diameter)
    {
        if (!Polygon.GetHoles().IsEmpty() || Polygon.SignedArea() < Diameter * Diameter || Polygon.Bounds().MinDim() < Diameter) return false;
        TArray<FGeneralPolygon2d> Inset;
        return PolygonsOffset(-Diameter * .5, TArray<FGeneralPolygon2d>{Polygon}, Inset, false, 2,
            EPolygonOffsetJoinType::Round, EPolygonOffsetEndType::Polygon, 4, 1.0) &&
            Inset.ContainsByPredicate([](const FGeneralPolygon2d& P) { return P.SignedArea() > 1; });
    }

    bool MakeRoad(TArray<FVector2D> Path, float Width, const FGeneralPolygon2d& Region, const FGeneralPolygon2d& CityBoundary,
        EPlatformCityRoadKind Kind, FPlatformCityRoad& Road, TArray<FGeneralPolygon2d>& CuttingPolygons)
    {
        FOffsetPolygon2d Offset;
        // This engine's Clipper open-path implementation halves delta internally: pass FULL width.
        Offset.Polygons.Add(MakeArrayView(Path)); Offset.Offset = Width;
        Offset.JoinType = EPolygonOffsetJoinType::Miter; Offset.MiterLimit = 2;
        Offset.EndType = EPolygonOffsetEndType::Round; Offset.MaxStepsPerRadian = 8;
        Offset.DefaultStepsPerRadianScale = .001;
        if (!Offset.ComputeResult()) return false;
        TArray<FGeneralPolygon2d> Clipped, MouthRegion, InsideCity;
        const TArray<FGeneralPolygon2d> Clip = {Region};
        // Tiny overlap at mouths prevents floating-point hairline gaps at previous road boundaries.
        if (Kind == EPlatformCityRoadKind::Side)
        {
            if (!PolygonsOffset(.001, Clip, MouthRegion, false, 2, EPolygonOffsetJoinType::Miter)) return false;
        }
        else MouthRegion = Clip;
        if (!PolygonsIntersection(Offset.Result, MouthRegion, Clipped) ||
            !PolygonsIntersection(Clipped, TArray<FGeneralPolygon2d>{CityBoundary}, InsideCity) ||
            InsideCity.Num() != 1 || !InsideCity[0].GetHoles().IsEmpty()) return false;
        TArray<TArray<FVector2D>> Lines;
        const TArray<TArrayView<FVector2d>> Views = {MakeArrayView(Path)};
        if (!PathsIntersection(Views, Clip, Lines) || Lines.Num() != 1 || Lines[0].Num() < 2) return false;
        // Subtract the UNCLIPPED cutter from its region: preclipping can leave a submicron strip
        // along the boundary and accidentally connect two districts that should be separate.
        CuttingPolygons = MoveTemp(Offset.Result); Road.Kind = Kind; Road.Width = Width;
        Road.Points = MoveTemp(Lines[0]); Road.Outline = InsideCity[0].GetOuter().GetVertices();
        return true;
    }
}

bool PlatformCity::MakePlan(const FPlatformCityRules& R, int32 Seed, FPlatformCityPlan& Out, FString& Error)
{
    if (!R.Validate(Error)) return false;
    FRandomStream Random(Seed); FPlatformCityPlan Plan;
    const FVector2D Half(R.Width * .5, R.Height * .5);
    const FGeneralPolygon2d Boundary(FPolygon2d(TArray<FVector2D>{-Half, {Half.X, -Half.Y}, Half, {-Half.X, Half.Y}}));
    TArray<FGeneralPolygon2d> MainCorridors;
    auto MainAxis = [&](double Span, double Length, int32 Count, bool Vertical)
    {
        const double Minimum = R.MinPlateSize * 1.8;
        const double Extra = Span - Count * R.MainRoadWidth - (Count + 1) * Minimum;
        TArray<double> Weights, Gaps; double Sum = 0, MinimumGap = Span;
        for (int32 I = 0; I <= Count; ++I)
        { const double W = Random.FRandRange(1 - R.DistrictVariation * .85f, 1 + R.DistrictVariation * .85f); Weights.Add(W); Sum += W; }
        for (double W : Weights) { const double Gap = Minimum + Extra * W / Sum; Gaps.Add(Gap); MinimumGap = FMath::Min(MinimumGap, Gap); }
        double Cursor = -Span * .5;
        for (int32 I = 0; I < Count; ++I)
        {
            Cursor += Gaps[I]; const double Base = Cursor + R.MainRoadWidth * .5;
            const double Phase = Random.FRandRange(-UE_PI, UE_PI), Phase2 = Random.FRandRange(-UE_PI, UE_PI);
            const double Amplitude = MinimumGap * .25 * R.RoadCurvature * Random.FRandRange(.65f, 1.f);
            const double Extension = R.MainRoadWidth * 2;
            const int32 Samples = FMath::CeilToInt((Length + Extension * 2) / R.RoadSampleSpacing);
            TArray<FVector2D> Path;
            for (int32 S = 0; S <= Samples; ++S)
            {
                const double Along = -Length * .5 - Extension + (Length + Extension * 2) * S / Samples;
                const double T = Along / Length + .5;
                const double Across = Base + Amplitude * (.7 * FMath::Sin(T * UE_DOUBLE_TWO_PI + Phase) + .3 * FMath::Sin(T * UE_DOUBLE_PI + Phase2));
                Path.Add(Vertical ? FVector2D(Across, Along) : FVector2D(Along, Across));
            }
            FPlatformCityRoad Road; TArray<FGeneralPolygon2d> Cutters;
            if (!MakeRoad(Path, R.MainRoadWidth, Boundary, Boundary, Vertical ? EPlatformCityRoadKind::VerticalMain : EPlatformCityRoadKind::HorizontalMain, Road, Cutters)) return false;
            MainCorridors.Append(Cutters); Plan.Roads.Add(MoveTemp(Road));
            Cursor += R.MainRoadWidth;
        }
        return true;
    };
    if (!MainAxis(R.Width, R.Height, R.VerticalRoads, true) || !MainAxis(R.Height, R.Width, R.HorizontalRoads, false))
    { Error = TEXT("Could not construct the main road corridors."); return false; }
    TArray<FGeneralPolygon2d> Districts;
    if (MainCorridors.IsEmpty()) Districts.Add(Boundary);
    else
    {
        TArray<FGeneralPolygon2d> Network;
        if (!PolygonsUnion(MainCorridors, Network, false) || !PolygonsDifference(TArray<FGeneralPolygon2d>{Boundary}, Network, Districts))
        { Error = TEXT("Main road subtraction failed."); return false; }
    }
    if (Districts.Num() != (R.VerticalRoads + 1) * (R.HorizontalRoads + 1))
    {
        Error = FString::Printf(TEXT("Main road topology: got %d districts, expected %d. Areas:"), Districts.Num(), (R.VerticalRoads+1)*(R.HorizontalRoads+1));
        for (const auto& D : Districts) Error += FString::Printf(TEXT(" %.6f"), D.SignedArea());
        return false;
    }
    SortPolygons(Districts);
    struct FRegion { FGeneralPolygon2d Polygon; int32 District, Depth; };
    TArray<FRegion> Pending;
    for (int32 I = 0; I < Districts.Num(); ++I)
    {
        if (!HasUsableArea(Districts[I], R.MinPlateSize))
        { Error = TEXT("A main-road district is too narrow. Reduce curvature/width or increase the city dimensions."); return false; }
        Pending.Add({MoveTemp(Districts[I]), I, 0});
    }
    for (int32 Index = 0; Index < Pending.Num(); ++Index)
    {
        const FRegion Region = Pending[Index]; const auto Bounds = Region.Polygon.Bounds();
        const FVector2D Size = (Bounds.Max - Bounds.Min);
        const double TargetArea = R.TargetPlateSize * R.TargetPlateSize * Random.FRandRange(.85f, 1.35f);
        const bool Budget = Plan.Plots.Num() + Pending.Num() - Index < R.MaxPlates;
        const bool KeepLarge = Random.FRand() < R.LargePlateChance * (Region.Depth == 0 ? .5f : 1.f);
        bool Split = false;
        if (Budget && Region.Depth < 7 && !KeepLarge && Region.Polygon.SignedArea() > TargetArea * 1.5 &&
            (Region.Depth == 0 || Random.FRand() < R.BranchChance))
        {
            for (int32 Attempt = 0; Attempt < 8 && !Split; ++Attempt)
            {
                const bool CutX = (Size.X * Random.FRandRange(.75f, 1.25f) > Size.Y) != (Attempt % 3 == 2);
                const double AcrossSpan = CutX ? Size.X : Size.Y, AlongSpan = CutX ? Size.Y : Size.X;
                const double Base = (CutX ? Bounds.Min.X : Bounds.Min.Y) + AcrossSpan * Random.FRandRange(.33f, .67f);
                const double Start = (CutX ? Bounds.Min.Y : Bounds.Min.X) - R.SideRoadWidth * 2;
                const double Length = AlongSpan + R.SideRoadWidth * 4;
                const double Bend = AcrossSpan * .15 * R.SideRoadBend * Random.FRandRange(-1.f, 1.f);
                const double Tilt = AcrossSpan * .22 * R.SideRoadBend * Random.FRandRange(-1.f, 1.f);
                const int32 Samples = FMath::Max(4, FMath::CeilToInt(Length / R.RoadSampleSpacing));
                TArray<FVector2D> Path;
                for (int32 S = 0; S <= Samples; ++S)
                {
                    const double T = double(S) / Samples;
                    const double Along = Start + Length * T;
                    const double Across = Base + Tilt * (T - .5) + Bend * FMath::Sin(T * UE_DOUBLE_TWO_PI);
                    Path.Add(CutX ? FVector2D(Across, Along) : FVector2D(Along, Across));
                }
                FPlatformCityRoad Road; TArray<FGeneralPolygon2d> Cutters;
                if (!MakeRoad(Path, R.SideRoadWidth, Region.Polygon, Boundary, EPlatformCityRoadKind::Side, Road, Cutters)) continue;
                TArray<FGeneralPolygon2d> Pieces;
                if (!PolygonsDifference(TArray<FGeneralPolygon2d>{Region.Polygon}, Cutters, Pieces) || Pieces.Num() != 2) continue;
                if (!HasUsableArea(Pieces[0], R.MinPlateSize) || !HasUsableArea(Pieces[1], R.MinPlateSize)) continue;
                // Boolean clipping near a nearly tangent boundary can produce inconsistent cuts.
                // Accept only a complete land/road partition; otherwise try another road route.
                TArray<FGeneralPolygon2d> Recorded;
                if (!PolygonsIntersection(TArray<FGeneralPolygon2d>{Region.Polygon},
                    TArray<FGeneralPolygon2d>{FGeneralPolygon2d(FPolygon2d(Road.Outline))}, Recorded)) continue;
                double RoadArea = 0; for (const auto& P : Recorded) RoadArea += P.SignedArea();
                const double PartitionError = Pieces[0].SignedArea() + Pieces[1].SignedArea() + RoadArea - Region.Polygon.SignedArea();
                if (FMath::Abs(PartitionError) > .1) continue;
                SortPolygons(Pieces);
                for (auto& Piece : Pieces) Pending.Add({MoveTemp(Piece), Region.District, Region.Depth + 1});
                Plan.Roads.Add(MoveTemp(Road)); Split = true;
            }
        }
        if (Split) continue;
        FPlatformCityPlot& Plot = Plan.Plots.AddDefaulted_GetRef();
        Plot.Center = Bounds.Center(); Plot.Size = Size; Plot.District = Region.District;
        for (const FVector2D& V : Region.Polygon.GetOuter().GetVertices()) Plot.SourceOutline.Add(V - Plot.Center);
        if (!PlatformPolygonGeometry::BuildOutline(Plot.SourceOutline, R.CornerRadius, R.CornerSegments, Plot.Outline, Error)) return false;
    }
    FString Signature = FString::Printf(TEXT("%d|%.3f|%.3f|%.3f|"), Seed, R.Width, R.Height, R.Thickness);
    for (const auto& Plot : Plan.Plots)
    {
        Signature += FString::Printf(TEXT("%.4f,%.4f:"), Plot.Center.X, Plot.Center.Y);
        for (const FVector2D& P : Plot.Outline) Signature += FString::Printf(TEXT("%.4f,%.4f;"), P.X, P.Y);
    }
    Plan.Signature = FString::Printf(TEXT("PLATFORM-CITY-2-%08X"), FCrc::StrCrc32(*Signature));
    Out = MoveTemp(Plan); Error.Reset(); return true;
}

