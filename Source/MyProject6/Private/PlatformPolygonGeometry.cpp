#include "PlatformPolygonGeometry.h"
#include "Curve/PolygonOffsetUtils.h"
#include "Curve/PolygonIntersectionUtils.h"

using namespace UE::Geometry;

bool PlatformPolygonGeometry::BuildOutline(const TArray<FVector2D>& Source, float Radius, int32 Segments,
    TArray<FVector2D>& Out, FString& Error)
{
    Out.Reset(); Error.Reset();
    if (Source.Num() < 3 || Source.Num() > 4096 || !FMath::IsFinite(Radius) || Radius < 0)
    { Error = TEXT("Dynamic polygon needs 3..4096 vertices and a finite, nonnegative corner radius."); return false; }
    for (const FVector2D& P : Source)
        if (P.ContainsNaN() || P.GetAbs().GetMax() > 1000000)
        { Error = TEXT("Dynamic polygon contains an invalid vertex."); return false; }
    FPolygon2d Polygon(Source);
    Polygon.Simplify(.001, .001);
    const auto& V = Polygon.GetVertices();
    if (V.Num() < 3 || Polygon.Area() < 1)
    { Error = TEXT("Dynamic polygon must have a nonzero area."); return false; }
    for (int32 I = 0; I < V.Num(); ++I)
        for (int32 J = I + 1; J < V.Num(); ++J)
        {
            if (J == I + 1 || (I == 0 && J == V.Num() - 1)) continue;
            if (FSegment2d(V[I], V[(I + 1) % V.Num()]).Intersects(FSegment2d(V[J], V[(J + 1) % V.Num()])))
            { Error = TEXT("Dynamic polygon edges must not cross or touch themselves."); return false; }
        }
    if (Polygon.IsClockwise()) Polygon.Reverse();
    const TArray<FGeneralPolygon2d> Original = {FGeneralPolygon2d(Polygon)};
    Out = Polygon.GetVertices();
    // Opening rounds into the plate. Intersect to prevent rounding from growing into any road.
    double EffectiveRadius = FMath::Min(double(Radius), Polygon.Bounds().MinDim() * .24);
    for (int32 Attempt = 0; EffectiveRadius > .01 && Attempt < 10; ++Attempt, EffectiveRadius *= .5)
    {
        TArray<FGeneralPolygon2d> Rounded, Clipped;
        if (PolygonsOffsets(-EffectiveRadius, EffectiveRadius, Original, Rounded, false, 2,
            EPolygonOffsetJoinType::Round, EPolygonOffsetEndType::Polygon,
            FMath::Clamp(Segments, 2, 24) / UE_DOUBLE_HALF_PI, 1.0) &&
            PolygonsIntersection(Rounded, Original, Clipped) && Clipped.Num() == 1 &&
            Clipped[0].GetHoles().IsEmpty() && Clipped[0].SignedArea() > Polygon.Area() * .5)
        {
            Out = Clipped[0].GetOuter().GetVertices(); break;
        }
    }
    FPolygon2d Clean(Out); Clean.Simplify(.001, .001);
    if (Clean.IsClockwise()) Clean.Reverse();
    Out = Clean.GetVertices();
    return Out.Num() >= 3;
}

