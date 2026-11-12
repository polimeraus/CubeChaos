#include "ParametricPlatform.h"
#include "PlatformPolygonGeometry.h"
#include "Algo/Reverse.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "ConstrainedDelaunay2.h"

using namespace UE::Geometry;

namespace
{
    double BoundaryDistanceSquared(const FGeneralPolygon2d& Polygon, FVector2D Point)
    {
        int Hole, Segment; double T;
        return Polygon.DistanceSquared(Point, Hole, Segment, T);
    }
    void RemoveStraightVertices(TArray<FVector2D>& Points)
    {
        for (int32 I = Points.Num() - 1; I >= 0 && Points.Num() > 3; --I)
        {
            const FVector2D A = Points[I] - Points[(I + Points.Num() - 1) % Points.Num()];
            const FVector2D B = Points[(I + 1) % Points.Num()] - Points[I];
            if (FMath::Abs(FVector2D::CrossProduct(A, B)) <= 1.e-9 * FMath::Max(1., A.Size() * B.Size()) &&
                FVector2D::DotProduct(A, B) >= 0) Points.RemoveAt(I);
        }
    }

    TArray<FVector2D> RoundCorners(const TArray<FVector2D>& Points, double Radius, int32 Segments)
    {
        TArray<FVector2D> Result;
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            const FVector2D P = Points[I];
            const FVector2D Before = Points[(I + Points.Num() - 1) % Points.Num()] - P;
            const FVector2D After = Points[(I + 1) % Points.Num()] - P;
            const FVector2D U = Before.GetSafeNormal(), V = After.GetSafeNormal();
            const double HalfAngle = FMath::Acos(FMath::Clamp(FVector2D::DotProduct(U, V), -1., 1.)) * .5;
            const double Tangent = FMath::Tan(HalfAngle);
            if (HalfAngle < 1.e-6 || HalfAngle > UE_DOUBLE_HALF_PI - 1.e-6 || Tangent < 1.e-6)
            {
                Result.Add(P); continue;
            }
            // Adjacent corners cannot consume more than one whole edge. Local limiting also
            // keeps a narrow diagonal bridge while allowing large outer corners to stay round.
            const double Distance = FMath::Min(Radius / Tangent, .49 * FMath::Min(Before.Size(), After.Size()));
            const double R = Distance * Tangent;
            const FVector2D Center = P + (U + V).GetSafeNormal() * (R / FMath::Sin(HalfAngle));
            const FVector2D Start = P + U * Distance - Center;
            const FVector2D End = P + V * Distance - Center;
            const double Sweep = FMath::Atan2(FVector2D::CrossProduct(Start, End), FVector2D::DotProduct(Start, End));
            const double StartAngle = FMath::Atan2(Start.Y, Start.X);
            const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Abs(Sweep) / UE_DOUBLE_HALF_PI * Segments), 1, 64);
            for (int32 S = 0; S <= Steps; ++S)
            {
                const double Angle = StartAngle + Sweep * S / Steps;
                Result.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R);
            }
        }
        return Result;
    }
}

bool AParametricPlatform::UsesBlockShape() const
{
    return OuterShape >= EChunkedDecorativeFloorShape::LShape && OuterShape <= EChunkedDecorativeFloorShape::CustomBlocks;
}

bool AParametricPlatform::UsesReferenceShape() const
{
    return OuterShape >= EChunkedDecorativeFloorShape::DiagonalLink &&
        OuterShape <= EChunkedDecorativeFloorShape::SteppedBranch;
}

FGeneralPolygon2d AParametricPlatform::GetPlatformPolygon() const
{
    FGeneralPolygon2d Polygon{FPolygon2d(CachedShapeOutline)};
    for (const auto& Hole : CachedShapeHoles) Polygon.AddHole(FPolygon2d(Hole), false, false);
    return Polygon;
}

bool AParametricPlatform::BuildReferenceShape()
{
    // Canonical silhouettes are authored in a small logical coordinate space, then fit to
    // Shape Size. A single catalog entry covers all colors, mirrored pieces and rotations.
    const int32 Resolution = FMath::Clamp(ReferenceShape.CurveSegments, 24, 128);
    const double Opening = FMath::IsFinite(ReferenceShape.OpeningRatio)
        ? FMath::Clamp(double(ReferenceShape.OpeningRatio), .25, .7) : .5;
    const double OpeningScale = Opening * 2;
    TArray<FGeneralPolygon2d> Pieces, Cuts;
    auto Polygon = [&](TArray<FVector2D> Points, double Radius = .14)
    {
        if (Radius > 0) Points = RoundCorners(Points, Radius, FMath::Max(4, Resolution / 8));
        if (FPolygon2d(Points).IsClockwise()) Algo::Reverse(Points);
        return FGeneralPolygon2d(FPolygon2d(Points));
    };
    auto Box = [&](double X0, double Y0, double X1, double Y1, double Radius = .14)
    {
        return Polygon({{X0,Y0},{X1,Y0},{X1,Y1},{X0,Y1}}, Radius);
    };
    auto Ellipse = [&](FVector2D Center, FVector2D Radius)
    {
        TArray<FVector2D> Points;
        for (int32 I = 0; I < Resolution; ++I)
        {
            const double A = UE_DOUBLE_TWO_PI * I / Resolution;
            Points.Add(Center + FVector2D(FMath::Cos(A) * Radius.X, FMath::Sin(A) * Radius.Y));
        }
        return Polygon(Points, 0);
    };
    auto Hole = [&](double X, double Y, double R) { Cuts.Add(Ellipse({X,Y}, FVector2D(R * OpeningScale))); };
    auto HalfDisc = [&]()
    {
        TArray<FVector2D> Points;
        for (int32 I = 0; I <= Resolution / 2; ++I)
        {
            const double A = UE_DOUBLE_PI * I / (Resolution / 2);
            Points.Add(FVector2D(FMath::Cos(A), FMath::Sin(A)));
        }
        return Polygon(Points, 0);
    };
    auto ConnectedBlocks = [&](TArray<FIntPoint> Blocks)
    {
        // Round edge-connected runs as one piece so a straight bar has no seams.
        TSet<FIntPoint> Remaining;
        for (const auto P : Blocks) Remaining.Add(P);
        while (!Remaining.IsEmpty())
        {
            TArray<FIntPoint> Queue={*Remaining.CreateConstIterator()};
            Remaining.Remove(Queue[0]);
            TArray<FGeneralPolygon2d> Squares, Union;
            for (int32 I=0; I<Queue.Num(); ++I)
            {
                const FIntPoint P=Queue[I];
                Squares.Add(Box(P.X,P.Y,P.X+1,P.Y+1,0));
                for (const FIntPoint D : {FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)})
                    if (Remaining.Remove(P+D)) Queue.Add(P+D);
            }
            if (PolygonsUnion(Squares,Union,false) && Union.Num()==1)
            {
                TArray<FVector2D> Outline=Union[0].GetOuter().GetVertices();
                RemoveStraightVertices(Outline);
                Pieces.Add(Polygon(Outline,.27));
            }
        }
        for (const auto P : Blocks)
        {
            for (const auto Q : Blocks)
                if (Q.X == P.X+1 && FMath::Abs(Q.Y-P.Y) == 1 &&
                    !Blocks.Contains(FIntPoint(Q.X,P.Y)) && !Blocks.Contains(FIntPoint(P.X,Q.Y)))
                {
                    // Two tangent quarter-circle coves join the diagonal lobes. A small
                    // square bridge would leave four visible bumps along this narrow neck.
                    const double R=.5;
                    TArray<FVector2D> Neck={{0,-R},{R,-R},{R,0}};
                    const int32 Steps=FMath::Max(6,Resolution/4);
                    for (int32 I=1; I<=Steps; ++I)
                    {
                        const double A=-UE_DOUBLE_HALF_PI-UE_DOUBLE_HALF_PI*I/Steps;
                        Neck.Add(FVector2D(R+R*FMath::Cos(A),R+R*FMath::Sin(A)));
                    }
                    Neck.Add({-R,R}); Neck.Add({-R,0});
                    for (int32 I=1; I<Steps; ++I)
                    {
                        const double A=UE_DOUBLE_HALF_PI-UE_DOUBLE_HALF_PI*I/Steps;
                        Neck.Add(FVector2D(-R+R*FMath::Cos(A),-R+R*FMath::Sin(A)));
                    }
                    for (auto& V : Neck)
                    {
                        if (Q.Y>P.Y) V.Y=-V.Y;
                        V+=FVector2D(P.X+1,FMath::Max(P.Y,Q.Y));
                    }
                    Pieces.Add(Polygon(Neck,0));
                }
        }
    };
    auto InterlockBody = [&](bool Mirror)
    {
        // One side of a round U: a semicircular head, straight inner edge and
        // broad quarter-circle heel. Socket and tab halves have complementary profiles.
        TArray<FVector2D> Points;
        for (int32 I=0; I<=Resolution/4; ++I)
        {
            const double A=UE_DOUBLE_PI+UE_DOUBLE_HALF_PI*I/(Resolution/4);
            Points.Add(FVector2D(2+2*FMath::Cos(A),2+2*FMath::Sin(A)));
        }
        Points.Add({2,3});
        for (int32 I=1; I<=Resolution/2; ++I)
        {
            const double A=UE_DOUBLE_PI*I/(Resolution/2);
            Points.Add(FVector2D(1+FMath::Cos(A),3+FMath::Sin(A)));
        }
        if (Mirror) for (auto& P : Points) P.X=2-P.X;
        return Polygon(Points,.06);
    };
    switch (OuterShape)
    {
    case EChunkedDecorativeFloorShape::DiagonalLink:
        ConnectedBlocks({{0,1},{1,0}}); break;
    case EChunkedDecorativeFloorShape::OffsetLink:
        ConnectedBlocks({{0,1},{1,1},{2,0}}); break;
    case EChunkedDecorativeFloorShape::OffsetFork:
        Pieces.Add(Polygon({{0,0},{2,0},{2,.7},{3,.7},{3,1.7},{2,1.7},{2,3},{0,3},{0,2},{1.4,2},{1.4,1},{0,1}}, .27)); break;
    case EChunkedDecorativeFloorShape::OffsetArch:
        Pieces.Add(Polygon({{0,0},{1,0},{1,1},{3,1},{3,-1},{4,-1},{4,2},{3,2},{3,3},{1,3},{1,2},{0,2}}, .27)); break;
    case EChunkedDecorativeFloorShape::SteppedArch:
        ConnectedBlocks({{0,0},{1,1},{2,2},{3,1},{3,0}}); break;
    case EChunkedDecorativeFloorShape::SteppedBlock:
        Pieces.Add(Polygon({{0,0},{3,0},{3,1},{2,1},{2,2},{1,2},{1,3},{0,3}}, .30)); break;
    case EChunkedDecorativeFloorShape::PiercedSteppedBlock:
        Pieces.Add(Polygon({{0,3},{3,3},{3,0},{2,0},{2,1},{1,1},{1,2},{0,2}}, .30));
        Hole(2.12,2.12,.48); break;
    case EChunkedDecorativeFloorShape::NotchedBar:
        Pieces.Add(Box(0,0,3,1.5,.24)); Hole(1.5,0,.65); break;
    case EChunkedDecorativeFloorShape::FingerCorner:
    {
        // Large rounded shoulder above three interlocking fingers.
        TArray<FVector2D> Points = {{0,.25},{0,2.7}};
        for (int32 I=1; I<=Resolution/4; ++I)
        {
            const double A=UE_DOUBLE_PI-UE_DOUBLE_HALF_PI*I/(Resolution/4);
            Points.Add(FVector2D(1.3+1.3*FMath::Cos(A),2.7+1.3*FMath::Sin(A)));
        }
        Points.Append({{2,4},{2,1.5},{2.45,1.15},{2.45,0},{0,0}});
        Pieces.Add(Polygon(Points,.16));
        Cuts.Add(Box(.55,-.4,.88,1.1,.165)); Cuts.Add(Box(1.5,-.4,1.83,1.1,.165)); break;
    }
    case EChunkedDecorativeFloorShape::ForkNotch:
        Pieces.Add(Polygon({{0,0},{3,0},{3,1.5},{2.3,1.7},{2.3,2.7},{1.95,2.7},{1.95,1.7},
            {1.05,1.7},{1.05,2.7},{.7,2.7},{.7,1.7},{0,1.5}},.17));
        Hole(1.5,0,.65); break;
    case EChunkedDecorativeFloorShape::PiercedTrapezoid:
    case EChunkedDecorativeFloorShape::NotchedTrapezoid:
        Pieces.Add(Polygon({{0,0},{3,0},{3,3},{.7,3}},.24));
        Hole(OuterShape == EChunkedDecorativeFloorShape::PiercedTrapezoid ? 1.7 : 3,1.5,.62); break;
    case EChunkedDecorativeFloorShape::PiercedCapsule:
        Pieces.Add(Box(0,0,2,4,1)); Hole(1,1.15,.60); break;
    case EChunkedDecorativeFloorShape::RoundU:
    {
        TArray<FVector2D> Points;
        for (int32 I=0; I<=Resolution/2; ++I)
        {
            const double A=UE_DOUBLE_PI+UE_DOUBLE_PI*I/(Resolution/2);
            Points.Add(FVector2D(2+2*FMath::Cos(A),2+2*FMath::Sin(A)));
        }
        Points.Append({{4,4},{0,4}});
        Pieces.Add(Polygon(Points,.35));
        const double R=Opening*2;
        Cuts.Add(Box(2-R,2-R,2+R,5,R)); break;
    }
    case EChunkedDecorativeFloorShape::CombArch:
    {
        TArray<FVector2D> Points={{0,0},{4,0}};
        for (int32 I=0; I<=Resolution/2; ++I)
        {
            const double A=UE_DOUBLE_PI*I/(Resolution/2);
            Points.Add(FVector2D(2+2*FMath::Cos(A),.5+2.5*FMath::Sin(A)));
        }
        Pieces.Add(Polygon(Points,.22));
        for (double X : {1.,2.,3.}) Cuts.Add(Box(X-.22,-.4,X+.22,.95,.22));
        Cuts.Add(Box(1.42,1.7,2.58,4,.58)); break;
    }
    case EChunkedDecorativeFloorShape::InterlockTab:
        Pieces.Add(InterlockBody(false));
        for (double Y : {.7,1.7}) Pieces.Add(Box(1.5,Y,2.65,Y+.4,.2));
        Hole(1,3,.55); break;
    case EChunkedDecorativeFloorShape::InterlockSocket:
        Pieces.Add(InterlockBody(true));
        for (double Y : {.7,1.7}) Cuts.Add(Box(-.5,Y,.65,Y+.4,.2));
        Hole(1,3,.55); break;
    case EChunkedDecorativeFloorShape::WaistedPier:
        Pieces.Add(Polygon({{0,0},{1.8,0},{1.8,2.3},{1.05,2.8},{1.05,4.5},{0,4.5}},.28));
        Cuts.Add(Ellipse({-.2,.55},{.85,1.25})); Cuts.Add(Ellipse({-.1,4.5},{.75,1.3})); break;
    case EChunkedDecorativeFloorShape::Ring:
        Pieces.Add(Ellipse({0,0},{1,1})); Cuts.Add(Ellipse({0,0},FVector2D(Opening))); break;
    case EChunkedDecorativeFloorShape::HalfRing:
        Pieces.Add(HalfDisc()); Cuts.Add(Ellipse({0,0},FVector2D(Opening))); break;
    case EChunkedDecorativeFloorShape::HalfDisc:
        Pieces.Add(HalfDisc()); break;
    case EChunkedDecorativeFloorShape::VNotchHalfDisc:
        Pieces.Add(HalfDisc()); Cuts.Add(Polygon({{-1.1,-.2},{1.1,-.2},{0,.58}},.035)); break;
    case EChunkedDecorativeFloorShape::PiercedDiamond:
        Pieces.Add(Polygon({{0,-1},{1,0},{0,1},{-1,0}},.07)); Hole(0,0,.33); break;
    case EChunkedDecorativeFloorShape::NotchedWedge:
        Pieces.Add(Polygon({{0,0},{1.7,0},{1.7,1.3},{0,2.2}},.18));
        Cuts.Add(Box(.55,-.4,1.1,.75,.275)); break;
    case EChunkedDecorativeFloorShape::UnequalU:
        Pieces.Add(Polygon({{0,0},{4,0},{4,4},{3,4},{3,1},{1,1},{1,2},{0,2}},.30)); break;
    case EChunkedDecorativeFloorShape::SteppedBranch:
        ConnectedBlocks({{0,0},{0,1},{0,2},{0,3},{1,0},{2,1},{3,2}}); break;
    default: return false;
    }
    TArray<FGeneralPolygon2d> Combined, Result;
    if (Pieces.IsEmpty() || !PolygonsUnion(Pieces, Combined, false)) return false;
    if (Cuts.IsEmpty()) Result = Combined;
    else if (!PolygonsDifference(Combined, Cuts, Result)) return false;
    if (Result.Num() != 1) return false;

    CachedShapeOutline = Result[0].GetOuter().GetVertices();
    for (const auto& HolePolygon : Result[0].GetHoles()) CachedShapeHoles.Add(HolePolygon.GetVertices());
    FBox2D Bounds(EForceInit::ForceInit);
    auto Transform = [&](TArray<FVector2D>& Points)
    {
        for (auto& P : Points)
        {
            if (ReferenceShape.bMirror) P.X = -P.X;
            for (int32 I=0; I<FMath::Clamp(ReferenceShape.QuarterTurns,0,3); ++I) P = FVector2D(-P.Y,P.X);
            Bounds += P;
        }
    };
    Transform(CachedShapeOutline);
    for (auto& HolePoints : CachedShapeHoles) Transform(HolePoints);
    const FVector2D Size=Bounds.GetSize(), Center=Bounds.GetCenter();
    auto Fit = [&](TArray<FVector2D>& Points, bool Clockwise)
    {
        for (auto& P : Points) P = (P-Center) / Size * ShapeSize;
        if (FPolygon2d(Points).IsClockwise() != Clockwise) Algo::Reverse(Points);
    };
    Fit(CachedShapeOutline,false);
    for (auto& HolePoints : CachedShapeHoles) Fit(HolePoints,true);
    return true;
}

bool AParametricPlatform::UsesRoundedCorners() const
{
    return bRoundCorners && FMath::IsFinite(CornerRadius) && CornerRadius > UE_SMALL_NUMBER &&
        OuterShape != EChunkedDecorativeFloorShape::Circle && OuterShape != EChunkedDecorativeFloorShape::Ellipse && OuterShape != EChunkedDecorativeFloorShape::RoundedBlob;
}

void AParametricPlatform::RebuildShapeOutline()
{
    ShapeWarning.Reset();
    CachedShapeHoles.Reset();
    if (OuterShape == EChunkedDecorativeFloorShape::DynamicPolygon)
    {
        CachedShapeTriangles.Reset();
        if (!PlatformPolygonGeometry::BuildOutline(PolygonOutline, bRoundCorners ? CornerRadius : 0,
            CornerSegments, CachedShapeOutline, ShapeWarning)) return;
    }
    else if (OuterShape == EChunkedDecorativeFloorShape::ParametricShape)
    {
        CachedShapeOutline = PreparedParametricOutline;
        CachedShapeHoles = PreparedParametricHoles;
        CachedShapeTriangles.Reset();
        ShapeWarning = PreparedParametricWarning;
        if (CachedShapeOutline.Num() < 3) return;
    }
    else if (UsesReferenceShape())
    {
        CachedShapeOutline.Reset(); CachedShapeTriangles.Reset();
        if (!BuildReferenceShape()) { ShapeWarning = TEXT("The reference shape could not be built."); return; }
    }
    else if (OuterShape == EChunkedDecorativeFloorShape::Ellipse || OuterShape == EChunkedDecorativeFloorShape::RegularPolygon)
    {
        CachedShapeOutline.Reset();
        const int32 Count = OuterShape == EChunkedDecorativeFloorShape::Ellipse ? FMath::Clamp(CircleEdgeSegments, 16, 512) : FMath::Clamp(PolygonSides, 3, 32);
        for (int32 I = 0; I < Count; ++I)
        {
            const double A = UE_DOUBLE_HALF_PI + UE_DOUBLE_TWO_PI * I / Count;
            CachedShapeOutline.Add(FVector2D(FMath::Cos(A) * ShapeSize.X * .5, FMath::Sin(A) * ShapeSize.Y * .5));
        }
    }
    else if (!UsesBlockShape())
    {
        Super::RebuildShapeOutline();
        if (!UsesRoundedCorners()) return; // Preserve exact legacy geometry when rounding is off.
    }
    else
    {
        CachedShapeOutline.Reset(); CachedShapeTriangles.Reset();
        CachedShapeFanCenter = FVector2D::ZeroVector;
        TSet<FIntPoint> Blocks;
        if (OuterShape == EChunkedDecorativeFloorShape::CustomBlocks)
        {
            if (BlockShape.Blocks.Num() > 256) ShapeWarning = TEXT("Only the first 256 custom blocks are used.");
            for (int32 I = 0; I < FMath::Min(BlockShape.Blocks.Num(), 256); ++I)
            {
                const FIntPoint P = BlockShape.Blocks[I];
                Blocks.Add(FIntPoint(FMath::Clamp(P.X, -64, 64), FMath::Clamp(P.Y, -64, 64)));
            }
        }
        else
        {
            const int32 N = FMath::Clamp(BlockShape.Columns, OuterShape == EChunkedDecorativeFloorShape::LShape ? 2 : 3, 32);
            const int32 M = FMath::Clamp(BlockShape.Rows, OuterShape == EChunkedDecorativeFloorShape::Cross ? 3 : 2, 32);
            const int32 Arm = FMath::Clamp(BlockShape.ArmThickness, 1, FMath::Max(1, (FMath::Min(N, M) - 1) / 2));
            const double Position = FMath::IsFinite(BlockShape.BranchPosition) ? FMath::Clamp(double(BlockShape.BranchPosition), 0., 1.) : .5;
            const int32 Stem = FMath::RoundToInt((N - Arm) * Position);
            const int32 Bar = (M - Arm) / 2;
            for (int32 Y = 0; Y < M; ++Y) for (int32 X = 0; X < N; ++X)
            {
                bool Filled = false;
                switch (OuterShape)
                {
                case EChunkedDecorativeFloorShape::LShape: Filled = X < Arm || Y < Arm; break;
                case EChunkedDecorativeFloorShape::TShape: Filled = Y >= M - Arm || (X >= Stem && X < Stem + Arm); break;
                case EChunkedDecorativeFloorShape::Zigzag:
                    Filled = (Y < Arm && X < N - Arm) || (Y >= M - Arm && X >= Arm) || (X >= Arm && X < N - Arm); break;
                case EChunkedDecorativeFloorShape::UShape: Filled = X < Arm || X >= N - Arm || Y < Arm; break;
                case EChunkedDecorativeFloorShape::Cross: Filled = (X >= Stem && X < Stem + Arm) || (Y >= Bar && Y < Bar + Arm); break;
                default: break;
                }
                if (Filled) Blocks.Add(FIntPoint(X, Y));
            }
        }
        if (Blocks.IsEmpty()) { ShapeWarning = TEXT("Add at least one custom block."); return; }
        TSet<FIntPoint> Rotated;
        FIntPoint Min(100000, 100000), Max(-100000, -100000);
        for (FIntPoint P : Blocks)
        {
            if (BlockShape.bMirror) P.X = -P.X;
            for (int32 I = 0; I < FMath::Clamp(BlockShape.QuarterTurns, 0, 3); ++I) P = FIntPoint(-P.Y, P.X);
            Rotated.Add(P);
            Min.X = FMath::Min(Min.X, P.X); Min.Y = FMath::Min(Min.Y, P.Y);
            Max.X = FMath::Max(Max.X, P.X); Max.Y = FMath::Max(Max.Y, P.Y);
        }
        const FVector2D Pitch(ShapeSize.X / (Max.X - Min.X + 1), ShapeSize.Y / (Max.Y - Min.Y + 1));
        auto Point = [&](double X, double Y) { return FVector2D((X - Min.X) * Pitch.X, (Y - Min.Y) * Pitch.Y) - ShapeSize * .5; };
        TArray<FGeneralPolygon2d> Squares;
        auto AddRectangle = [&](FVector2D A, FVector2D B)
        {
            Squares.Emplace(FPolygon2d(TArray<FVector2D>{A, FVector2D(B.X, A.Y), B, FVector2D(A.X, B.Y)}));
        };
        const double Bridge = FMath::IsFinite(BlockShape.DiagonalBridgeWidth)
            ? FMath::Clamp(double(BlockShape.DiagonalBridgeWidth), 0., .95 * FMath::Min(Pitch.X, Pitch.Y)) : 0;
        for (const FIntPoint P : Rotated)
        {
            AddRectangle(Point(P.X, P.Y), Point(P.X + 1, P.Y + 1));
            if (OuterShape != EChunkedDecorativeFloorShape::CustomBlocks || Bridge <= UE_SMALL_NUMBER) continue;
            for (int32 Dy : {-1, 1})
            {
                if (Rotated.Contains(P + FIntPoint(1, Dy)) && !Rotated.Contains(P + FIntPoint(1, 0)) && !Rotated.Contains(P + FIntPoint(0, Dy)))
                {
                    const FVector2D Corner = Point(P.X + 1, P.Y + (Dy > 0 ? 1 : 0));
                    AddRectangle(Corner - FVector2D(Bridge * .5), Corner + FVector2D(Bridge * .5));
                }
            }
        }
        TArray<FGeneralPolygon2d> Combined;
        if (!PolygonsUnion(Squares, Combined, false) || Combined.Num() != 1 || !Combined[0].GetHoles().IsEmpty())
        {
            ShapeWarning = TEXT("Use one connected block layout without enclosed holes. For corner-only contacts, increase Diagonal Bridge Width.");
            return;
        }
        CachedShapeOutline = Combined[0].GetOuter().GetVertices();
        // Point contacts may appear as a self-touching loop rather than two union components.
        TSet<FVector2D> Unique;
        for (const FVector2D P : CachedShapeOutline)
        {
            if (Unique.Contains(P))
            {
                CachedShapeOutline.Reset(); ShapeWarning = TEXT("Corner-only block contacts need a positive Diagonal Bridge Width."); return;
            }
            Unique.Add(P);
        }
    }

    RemoveStraightVertices(CachedShapeOutline);
    if (FPolygon2d(CachedShapeOutline).IsClockwise()) Algo::Reverse(CachedShapeOutline);
    if (UsesRoundedCorners() && !UsesReferenceShape() && OuterShape != EChunkedDecorativeFloorShape::ParametricShape && OuterShape != EChunkedDecorativeFloorShape::DynamicPolygon)
    {
        const TArray<FVector2D> Rounded = RoundCorners(CachedShapeOutline, CornerRadius, FMath::Clamp(CornerSegments, 1, 32));
        const TArray<FGeneralPolygon2d> Input = {FGeneralPolygon2d(FPolygon2d(Rounded))};
        TArray<FGeneralPolygon2d> Clean;
        if (PolygonsUnion(Input, Clean, false) && Clean.Num() == 1 && Clean[0].GetHoles().IsEmpty())
            CachedShapeOutline = Clean[0].GetOuter().GetVertices();
        else ShapeWarning = TEXT("Corner radius could not form a valid outline; using sharp corners.");
    }
    if (FPolygon2d(CachedShapeOutline).IsClockwise()) Algo::Reverse(CachedShapeOutline);
    // A fan from the center would incorrectly fill the open notch of a U/C/S or branched piece.
    FConstrainedDelaunay2d Triangulation;
    Triangulation.bOutputCCW = true;
    Triangulation.Add(GetPlatformPolygon());
    CachedShapeTriangles.Reset();
    if (!Triangulation.Triangulate())
    {
        CachedShapeOutline.Reset(); ShapeWarning = TEXT("This outline could not be triangulated. Reduce the corner radius or adjust the blocks."); return;
    }
    const FGeneralPolygon2d Boundary = GetPlatformPolygon();
    double BestClearance = Boundary.Contains(FVector2D::ZeroVector) ? BoundaryDistanceSquared(Boundary,FVector2D::ZeroVector) : -1;
    CachedShapeFanCenter = FVector2D::ZeroVector;
    for (const FIndex3i T : Triangulation.Triangles)
    {
        auto& Triangle = CachedShapeTriangles.Emplace_GetRef();
        Triangle.A = Triangulation.Vertices[T.A]; Triangle.B = Triangulation.Vertices[T.B]; Triangle.C = Triangulation.Vertices[T.C];
        Triangle.Bounds = FBox2D(EForceInit::ForceInit);
        Triangle.Bounds += Triangle.A; Triangle.Bounds += Triangle.B; Triangle.Bounds += Triangle.C;
        const double A = (Triangle.B - Triangle.C).Size(), B = (Triangle.C - Triangle.A).Size(), C = (Triangle.A - Triangle.B).Size();
        const FVector2D Interior = (Triangle.A * A + Triangle.B * B + Triangle.C * C) / FMath::Max(A + B + C, 1.e-9);
        const double Distance = BoundaryDistanceSquared(Boundary,Interior);
        if (Distance > BestClearance && Boundary.Contains(Interior)) { BestClearance = Distance; CachedShapeFanCenter = Interior; }
    }
}

bool AParametricPlatform::IsPointInsideOuterShape(const FVector2D& Point, float Inset) const
{
    if (!UsesBlockShape() && !UsesRoundedCorners() && !UsesReferenceShape() && OuterShape != EChunkedDecorativeFloorShape::ParametricShape && OuterShape != EChunkedDecorativeFloorShape::Ellipse && OuterShape != EChunkedDecorativeFloorShape::RegularPolygon && OuterShape != EChunkedDecorativeFloorShape::DynamicPolygon)
        return Super::IsPointInsideOuterShape(Point, Inset);
    if (CachedShapeOutline.Num() < 3 || !IsPointInsideGeneratedShape(Point)) return false;
    return Inset <= 0 || BoundaryDistanceSquared(GetPlatformPolygon(),Point) >= FMath::Square(double(Inset));
}
