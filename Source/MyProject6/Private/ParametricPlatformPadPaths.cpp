#include "ParametricPlatform.h"
#include "Polygon2.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"

using namespace UE::Geometry;

namespace
{
    constexpr int32 MaxPads = 16, MaxSegments = 32;
    double Safe(double Value, double Min, double Max, double Default)
    {
        return FMath::Clamp(FMath::IsFinite(Value) ? Value : Default, Min, Max);
    }
    double Clearance(const FGeneralPolygon2d& Shape, const FVector2D& Point)
    {
        int Hole, Segment; double T;
        return FMath::Sqrt(Shape.DistanceSquared(Point,Hole,Segment,T)) * (Shape.Contains(Point) ? 1. : -1.);
    }
    FVector2D FitCenter(const FGeneralPolygon2d& Outline, FVector2D Anchor, FVector2D Desired, double Radius)
    {
        if (Clearance(Outline, Desired) >= Radius) return Desired;
        double Lo = 0, Hi = 1;
        for (int32 I = 0; I < 40; ++I)
        {
            const double T = (Lo + Hi) * .5;
            if (Clearance(Outline, FMath::Lerp(Anchor, Desired, T)) >= Radius) Lo = T;
            else Hi = T;
        }
        return FMath::Lerp(Anchor, Desired, Lo);
    }
    FVector2D EdgeEndpoint(const FGeneralPolygon2d& Shape, FVector2D Start, FVector2D Toward, double ClearanceRequired)
    {
        const FVector2D Direction = (Toward - Start).GetSafeNormal();
        if (Direction.IsNearlyZero() || Clearance(Shape, Start) < ClearanceRequired) return Toward;
        double FirstHit = TNumericLimits<double>::Max();
        auto CheckBoundary = [&](const TArray<FVector2D>& Outline)
        {
        for (int32 I = 0; I < Outline.Num(); ++I)
        {
            const FVector2D A = Outline[I], Edge = Outline[(I + 1) % Outline.Num()] - A;
            const double Denominator = FVector2D::CrossProduct(Direction, Edge);
            if (FMath::Abs(Denominator) < 1.e-12) continue;
            const double T = FVector2D::CrossProduct(A - Start, Edge) / Denominator;
            const double U = FVector2D::CrossProduct(A - Start, Direction) / Denominator;
            if (T >= 0 && U >= 0 && U <= 1) FirstHit = FMath::Min(FirstHit, T);
        }
        };
        CheckBoundary(Shape.GetOuter().GetVertices());
        for (const auto& Hole : Shape.GetHoles()) CheckBoundary(Hole.GetVertices());
        if (!FMath::IsFinite(FirstHit) || FirstHit == TNumericLimits<double>::Max()) return Toward;
        // Stop the round endpoint inside the actual polygon, including concave/organic outlines.
        double Lo = 0, Hi = FirstHit;
        for (int32 I = 0; I < 40; ++I)
        {
            const double T = (Lo + Hi) * .5;
            if (Clearance(Shape, Start + Direction * T) >= ClearanceRequired) Lo = T;
            else Hi = T;
        }
        return Start + Direction * Lo;
    }
}

FPlatformPadPathPattern::FPlatformPadPathPattern()
{
    auto Add = [this](const TCHAR* Id, FVector2D Position, float Radius, EPlatformPadPaint Paint, bool Show = true)
    {
        auto& Pad = Pads.AddDefaulted_GetRef();
        Pad.Id = Id; Pad.Position = Position; Pad.Radius = Radius; Pad.Paint = Paint; Pad.bShowCircle = Show;
    };
    Add(TEXT("Center"), FVector2D(0, 0), .38f, EPlatformPadPaint::Ring);
    Add(TEXT("Right"), FVector2D(-.66, -.48), .235f, EPlatformPadPaint::Ring);
    Add(TEXT("Left"), FVector2D(.65, .44), .245f, EPlatformPadPaint::Disc);
    Add(TEXT("Entrance"), FVector2D(.82, -.82), .19f, EPlatformPadPaint::Disc, false);
    Add(TEXT("Back"), FVector2D(-.82, .82), .19f, EPlatformPadPaint::Disc, false);
    for (const TCHAR* Id : {TEXT("Right"), TEXT("Left"), TEXT("Entrance"), TEXT("Back")})
    {
        auto& Path = Paths.AddDefaulted_GetRef();
        Path.FromPad = TEXT("Center"); Path.ToPad = Id;
    }
}

void AParametricPlatform::UpdatePadPathMaterial()
{
    PadPathWarning.Reset();
    ResolvedPads.Reset();
    if (FloorPattern != EChunkedFloorPattern::CirclesAndPaths)
    {
        PadPathMaterialInstance = nullptr;
        return;
    }
    auto Warn = [this](const FString& Message)
    {
        if (!PadPathWarning.IsEmpty()) PadPathWarning += TEXT("\n");
        PadPathWarning += Message;
    };
    if (CachedShapeOutline.Num() < 3)
    {
        PadPathMaterialInstance = nullptr;
        Warn(TEXT("The platform needs a valid shape before painting a layout."));
        return;
    }
    if (!PadPathSettings.SurfaceMaterial)
    {
        PadPathMaterialInstance = nullptr;
        Warn(TEXT("Assign the Circles & Paths Surface Material to display this pattern."));
    }
    else if (!PadPathMaterialInstance || PadPathMaterialInstance->Parent != PadPathSettings.SurfaceMaterial)
    {
        PadPathMaterialInstance = UMaterialInstanceDynamic::Create(PadPathSettings.SurfaceMaterial, this);
        PadPathMaterialInstance->SetFlags(RF_Transient);
    }
    auto* M = PadPathMaterialInstance.Get();
    if (M)
    {
        M->SetVectorParameterValue(TEXT("Color"), PadPathSettings.BackgroundColor);
        M->SetVectorParameterValue(TEXT("YellowColor"), PadPathSettings.PatternColor);
        M->SetScalarParameterValue(TEXT("Emission"), Safe(PadPathSettings.BackgroundEmission, 0, 20, .18));
        M->SetScalarParameterValue(TEXT("YellowEmission"), Safe(PadPathSettings.PatternEmission, 0, 20, .55));
        M->SetScalarParameterValue(TEXT("Roughness"), Safe(PadPathSettings.Roughness, .02, 1, .19));
        M->SetScalarParameterValue(TEXT("Metallic"), Safe(PadPathSettings.Metallic, 0, 1, .32));
        M->SetScalarParameterValue(TEXT("UVWorldUnits"), FMath::Max(UVWorldUnits, 1.f));
        M->SetScalarParameterValue(TEXT("ShowLayout"), PadPathSettings.bShowLayout ? 1 : 0);
    }
    const FVector2D Half(ShapeSize.X * .5, (OuterShape == EChunkedDecorativeFloorShape::Circle ||
        OuterShape == EChunkedDecorativeFloorShape::Square ? ShapeSize.X : ShapeSize.Y) * .5);
    const double MinimumHalf = FMath::Min(Half.X, Half.Y);
    auto Position = [Half](FVector2D P) { return FVector2D(Safe(P.X, -1, 1, 0) * Half.X, Safe(P.Y, -1, 1, 0) * Half.Y); };
    TMap<FName, int32> Counts;
    for (const auto& Pad : PadPathSettings.Pads) ++Counts.FindOrAdd(Pad.Id);
    if (PadPathSettings.Pads.Num() > MaxPads) Warn(TEXT("Only the first 16 pads are supported."));
    const FGeneralPolygon2d ShapePolygon = GetPadSurfacePolygon();
    const double AnchorClearance = FMath::Max(0., Clearance(ShapePolygon, CachedShapeFanCenter) - .01);
    int32 PaintedCount = 0;
    for (int32 I = 0; I < FMath::Min(PadPathSettings.Pads.Num(), MaxPads); ++I)
    {
        const auto& Pad = PadPathSettings.Pads[I];
        if (!Pad.bEnabled) continue;
        if (Pad.Id.IsNone() || Counts[Pad.Id] != 1)
        {
            Warn(FString::Printf(TEXT("Pad %d has an empty or duplicate ID; skipped."), I));
            continue;
        }
        FVector2D Center = Position(Pad.Position);
        double Radius = Safe(Pad.Radius, .001, .95, .22) * MinimumHalf;
        if (Pad.bShowCircle && PadPathSettings.bFitPadsInsidePlatform)
        {
            Radius = FMath::Min(Radius, AnchorClearance);
            Center = FitCenter(ShapePolygon, CachedShapeFanCenter, Center, Radius);
        }
        else if (Pad.bShowCircle && Clearance(ShapePolygon, Center) < Radius)
            Warn(FString::Printf(TEXT("Pad %s extends beyond the platform and will be clipped."), *Pad.Id.ToString()));
        ResolvedPads.Add(Pad.Id, {Center, Radius, Pad.bShowCircle, I});
        if (!Pad.bShowCircle) continue;
        const double Inner = Pad.Paint == EPlatformPadPaint::Ring ? Radius * (1 - Safe(Pad.RingWidth, .01, 1, .14)) : 0;
        if (M) M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Pad%d"), PaintedCount)), FLinearColor(Center.X, Center.Y, Radius, Inner));
        ++PaintedCount;
    }
    // Gameplay sockets resolve from the same fitted circles even without a surface material.
    if (!M) return;
    M->SetScalarParameterValue(TEXT("PadCount"), PaintedCount);
    for (int32 I = PaintedCount; I < MaxPads; ++I)
        M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Pad%d"), I)), FLinearColor::Transparent);
    int32 SegmentCount = 0;
    for (const auto& Path : PadPathSettings.Paths)
    {
        if (!Path.bEnabled) continue;
        const FResolvedPlatformPad* From = ResolvedPads.Find(Path.FromPad), *To = ResolvedPads.Find(Path.ToPad);
        if (!From || !To || From == To)
        {
            Warn(FString::Printf(TEXT("Path %s -> %s has missing, disabled or identical endpoints; skipped."), *Path.FromPad.ToString(), *Path.ToPad.ToString()));
            continue;
        }
        const double HalfWidth = Safe(Path.Width, .001, .5, .055) * MinimumHalf;
        TArray<FVector2D> Points = {From->Center};
        // More than 32 waypoints cannot contribute to a 32-segment material.
        for (int32 I = 0; I < FMath::Min(Path.Waypoints.Num(), MaxSegments); ++I) Points.Add(Position(Path.Waypoints[I]));
        Points.Add(To->Center);
        if (Path.bExtendToEdge && !To->Show)
            Points.Last() = EdgeEndpoint(ShapePolygon, Points[Points.Num() - 2], Points.Last(), HalfWidth + Safe(Path.EdgeInset, 0, 1.e6, 65));
        if (SegmentCount + Points.Num() - 1 > MaxSegments || Path.Waypoints.Num() > MaxSegments)
            Warn(TEXT("Only the first 32 path segments are supported."));
        for (int32 I = 1; I < Points.Num() && SegmentCount < MaxSegments; ++I)
        {
            if (Points[I].Equals(Points[I - 1], .0001)) continue;
            M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Segment%d"), SegmentCount)),
                FLinearColor(Points[I - 1].X, Points[I - 1].Y, Points[I].X, Points[I].Y));
            M->SetScalarParameterValue(FName(*FString::Printf(TEXT("PathWidth%d"), SegmentCount++)), HalfWidth);
        }
    }
    M->SetScalarParameterValue(TEXT("SegmentCount"), SegmentCount);
    for (int32 I = SegmentCount; I < MaxSegments; ++I)
    {
        M->SetVectorParameterValue(FName(*FString::Printf(TEXT("Segment%d"), I)), FLinearColor::Transparent);
        M->SetScalarParameterValue(FName(*FString::Printf(TEXT("PathWidth%d"), I)), 0);
    }
}

void AParametricPlatform::ApplyPadPathSurface(FDynamicMesh3& Mesh) const
{
    if (FloorPattern != EChunkedFloorPattern::CirclesAndPaths || !PadPathMaterialInstance || !Mesh.HasAttributes()) return;
    auto* MaterialIDs = Mesh.Attributes()->GetMaterialID();
    auto* UVs = Mesh.Attributes()->PrimaryUV();
    for (int32 ID : Mesh.TriangleIndicesItr())
    {
        const FIndex3i V = Mesh.GetTriangle(ID);
        const FVector3d A = Mesh.GetVertex(V.A), B = Mesh.GetVertex(V.B), C = Mesh.GetVertex(V.C);
        if (!FMath::IsNearlyZero(A.Z) || !FMath::IsNearlyZero(B.Z) || !FMath::IsNearlyZero(C.Z) ||
            MaterialIDs->GetValue(ID) != ClampMaterialIndex(PrimaryMaterialIndex)) continue;
        MaterialIDs->SetValue(ID, PadPathMaterialIndex());
        const FIndex3i UV = UVs->GetTriangle(ID);
        for (int32 I = 0; I < 3; ++I)
        {
            const FVector3d P = Mesh.GetVertex(V[I]);
            UVs->SetElement(UV[I], FVector2f(P.X, P.Y) / FMath::Max(UVWorldUnits, 1.f));
        }
    }
}
