#include "ParametricPlatform.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Curve/PolygonIntersectionUtils.h"
#include "Curve/PolygonOffsetUtils.h"
#include "ConstrainedDelaunay2.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "Algo/Reverse.h"

using namespace UE::Geometry;

namespace PlatformEdges
{
    struct FVertex { FVector3d P; FVector2f UV; FVector3f N; };
    struct FClipPlane { FVector3d N; double W; };
    using FClippedPolygon = TArray<FVertex>;
    FClippedPolygon Clip(const FClippedPolygon& Input, const FClipPlane& Plane)
    {
        FClippedPolygon Result;
        if (Input.IsEmpty()) return Result;
        FVertex A = Input.Last(); double DA = A.P.Dot(Plane.N) - Plane.W;
        for (const FVertex& B : Input)
        {
            const double DB = B.P.Dot(Plane.N) - Plane.W;
            if ((DA > 0) != (DB > 0))
            {
                const double T = FMath::Clamp(DA / (DA - DB), 0., 1.);
                Result.Add({FMath::Lerp(A.P, B.P, T), FMath::Lerp(A.UV, B.UV, float(T)), FMath::Lerp(A.N, B.N, float(T))});
            }
            if (DB <= 0) Result.Add(B);
            A = B; DA = DB;
        }
        return Result;
    }
    FBox2D Bounds(const FClippedPolygon& Poly)
    {
        FBox2D Box(ForceInit);
        for (const FVertex& V : Poly) Box += FVector2D(V.P.X, V.P.Y);
        return Box;
    }
    void Subtract(const FClippedPolygon& Input, const TArray<FClipPlane>& Cutter, TArray<FClippedPolygon>& Outside)
    {
        FClippedPolygon Remaining = Input;
        for (const FClipPlane& Plane : Cutter)
        {
            if (Remaining.Num() < 3) return;
            double Min = 1.e30, Max = -1.e30;
            for (const FVertex& V : Remaining)
            {
                const double D = V.P.Dot(Plane.N) - Plane.W;
                Min = FMath::Min(Min, D); Max = FMath::Max(Max, D);
            }
            // Classify the complete polygon before exact clipping. A silhouette wall
            // can be 1e-12 outside its own prism due to dot-product rounding; clipping
            // that coplanar wall with a strict zero would otherwise discard it entirely.
            if (Max <= 1.e-7) continue;
            if (Min >= -1.e-7) { Outside.Add(MoveTemp(Remaining)); return; }
            FClippedPolygon Part = Clip(Remaining, {-Plane.N, -Plane.W});
            if (Part.Num() >= 3) Outside.Add(MoveTemp(Part));
            Remaining = Clip(Remaining, Plane);
        }
    }
    void Append(FDynamicMesh3& Mesh, const FClippedPolygon& Poly, int32 Material)
    {
        for (int32 I = 1; I + 1 < Poly.Num(); ++I)
        {
            if ((Poly[I].P - Poly[0].P).Cross(Poly[I+1].P - Poly[0].P).SquaredLength() < 1.e-12) continue;
            const int32 Indices[] = {0, I, I+1};
            FIndex3i V, U, N;
            for (int32 K = 0; K < 3; ++K)
            {
                const auto& P = Poly[Indices[K]];
                V[K] = Mesh.AppendVertex(P.P);
                U[K] = Mesh.Attributes()->PrimaryUV()->AppendElement(P.UV);
                N[K] = Mesh.Attributes()->PrimaryNormals()->AppendElement(P.N.GetSafeNormal());
            }
            const int32 ID = Mesh.AppendTriangle(V);
            Mesh.Attributes()->PrimaryUV()->SetTriangle(ID, U);
            Mesh.Attributes()->PrimaryNormals()->SetTriangle(ID, N);
            Mesh.Attributes()->GetMaterialID()->SetValue(ID, Material);
        }
    }
    double Area(const TArray<FGeneralPolygon2d>& Polygons)
    {
        double Result = 0;
        for (const auto& P : Polygons) Result += FMath::Abs(P.SignedArea());
        return Result;
    }
}

void AParametricPlatform::PrepareBevel()
{
    BevelPatches.Reset(); EffectiveBevelSize = 0;
    BevelTopPolygon = GetPlatformPolygon();
    if (!bBevelEdges || !FMath::IsFinite(BevelSize) || BevelSize <= 0 || CachedShapeOutline.Num() < 3) return;
    const FGeneralPolygon2d Original = GetPlatformPolygon();
    double B = FMath::Min3(double(BevelSize), Thickness * .1, FMath::Min(ShapeSize.X, ShapeSize.Y) * .01);
    // Corresponding mitered contours support concave outlines and holes without shrinking
    // the actor's footprint. Reduce the width before narrow bridges or small fillets fold.
    for (int32 Attempt = 0; Attempt < 18 && B > .0001; ++Attempt, B *= .5)
    {
        TArray<TArray<FVector2D>> Outer, Inner;
        ForEachShapeBoundary([&](const auto& Loop)
        {
            Outer.Add(Loop);
            FPolygon2d Offset(Loop); Offset.PolyOffset(-B);
            Inner.Add(Offset.GetVertices());
        });
        bool Valid = true;
        FGeneralPolygon2d Candidate{FPolygon2d(Inner[0])};
        for (int32 I = 1; I < Inner.Num(); ++I) Candidate.AddHole(FPolygon2d(Inner[I]), false, false);
        TArray<FBevelPatch> Patches;
        for (int32 L = 0; L < Outer.Num(); ++L)
        for (int32 I = 0; I < Outer[L].Num(); ++I)
        {
            const int32 J = (I + 1) % Outer[L].Num();
            const FVector2D A = Outer[L][I], C = Outer[L][J], AI = Inner[L][I], CI = Inner[L][J];
            Valid &= !AI.ContainsNaN() && FVector2D::DotProduct(C - A, CI - AI) > 1.e-9;
            const FVector3d Points[] = {{A.X,A.Y,-B}, {C.X,C.Y,-B}, {CI.X,CI.Y,0}, {AI.X,AI.Y,0}};
            for (int32 T = 0; T < 2; ++T)
            {
                FBevelPatch Patch;
                Patch.P[0] = Points[0]; Patch.P[1] = Points[T+1]; Patch.P[2] = Points[T+2];
                Valid &= (Patch.P[1] - Patch.P[0]).Cross(Patch.P[2] - Patch.P[0]).Z > 1.e-9;
                for (const auto& P : Patch.P) Patch.Bounds += FVector2D(P.X, P.Y);
                Patches.Add(Patch);
            }
        }
        if (!Valid) continue;
        TArray<FGeneralPolygon2d> Clean, Outside;
        if (!PolygonsUnion({Candidate}, Clean, false) || Clean.Num() != 1 || Clean[0].GetHoles().Num() != CachedShapeHoles.Num()) continue;
        if (FMath::Abs(PlatformEdges::Area(Clean) - FMath::Abs(Candidate.SignedArea())) > .01) continue;
        if (!PolygonsDifference(Clean, {Original}, Outside) || PlatformEdges::Area(Outside) > .01) continue;
        // The area check catches overlapping strips even when the inset contour remains valid.
        double BandArea = 0;
        TArray<FGeneralPolygon2d> Bands, MergedBands;
        for (const auto& Patch : Patches)
        {
            TArray<FVector2D> Points;
            for (const auto& P : Patch.P) Points.Add(FVector2D(P.X, P.Y));
            Bands.Emplace(FPolygon2d(Points)); BandArea += Bands.Last().SignedArea();
        }
        if (!PolygonsUnion(Bands, MergedBands, false) || FMath::Abs(PlatformEdges::Area(MergedBands) - BandArea) > .01) continue;
        BevelPatches = MoveTemp(Patches); BevelTopPolygon = MoveTemp(Candidate); EffectiveBevelSize = B;
        return;
    }
    ShapeWarning += TEXT(" Bevel disabled: this outline has no safe inset at the requested scale.");
}

FGeneralPolygon2d AParametricPlatform::GetPadSurfacePolygon() const
{
    const auto Shape = EffectiveBevelSize > 0 ? BevelTopPolygon : GetPlatformPolygon();
    if (BorderStyle == EPlatformBorder::Rim && BorderWidth > 0)
    {
        TArray<FGeneralPolygon2d> Inset;
        if (PolygonsOffset(-BorderWidth, {Shape}, Inset, false, 4, EPolygonOffsetJoinType::Miter) && Inset.Num() == 1)
            return Inset[0];
    }
    return Shape;
}

void AParametricPlatform::ApplyEdgeFinish(int32 ChunkIndex, FDynamicMesh3& Mesh) const
{
    using namespace PlatformEdges;
    if (BodyMaterialIndex >= 0)
    {
        const int32 Neon = FMath::Max(FloorMaterials.Num(), 1);
        for (int32 ID : Mesh.TriangleIndicesItr())
        {
            FVector3d A,B,C; Mesh.GetTriVertices(ID,A,B,C);
            if (FMath::Min3(A.Z,B.Z,C.Z) < -.001 && Mesh.Attributes()->GetMaterialID()->GetValue(ID) != Neon)
                Mesh.Attributes()->GetMaterialID()->SetValue(ID, ClampMaterialIndex(BodyMaterialIndex));
        }
    }
    if (BevelPatches.IsEmpty()) return;
    struct FCutter { FBox2D Bounds; TArray<FClipPlane> Planes; };
    TArray<FCutter> Cutters;
    for (const auto& Patch : BevelPatches)
    {
        const FVector3d Roof = (Patch.P[1]-Patch.P[0]).Cross(Patch.P[2]-Patch.P[0]).GetSafeNormal();
        const double W = Roof.Dot(Patch.P[0]);
        for (bool Bottom : {false,true})
        {
            FCutter Cutter; Cutter.Bounds = Patch.Bounds;
            for (int32 I=0; I<3; ++I)
            {
                const FVector3d A=Patch.P[I], E=Patch.P[(I+1)%3]-A;
                const FVector3d N=FVector3d(E.Y,-E.X,0).GetSafeNormal();
                Cutter.Planes.Add({N,N.Dot(A)});
            }
            const FVector3d N(Roof.X,Roof.Y,Bottom ? -Roof.Z : Roof.Z);
            Cutter.Planes.Add({-N,-W-(Bottom ? Roof.Z*Thickness : 0)});
            Cutters.Add(MoveTemp(Cutter));
        }
    }
    FDynamicMesh3 Finished;
    Finished.EnableAttributes(); Finished.Attributes()->EnableMaterialID();
    for (int32 ID : Mesh.TriangleIndicesItr())
    {
        const FIndex3i V=Mesh.GetTriangle(ID), U=Mesh.Attributes()->PrimaryUV()->GetTriangle(ID), N=Mesh.Attributes()->PrimaryNormals()->GetTriangle(ID);
        FClippedPolygon Triangle;
        for (int32 K=0; K<3; ++K) Triangle.Add({Mesh.GetVertex(V[K]),Mesh.Attributes()->PrimaryUV()->GetElement(U[K]),Mesh.Attributes()->PrimaryNormals()->GetElement(N[K])});
        const FBox2D Box=Bounds(Triangle);
        TArray<FClippedPolygon> Pieces={Triangle};
        for (const auto& Cutter : Cutters)
        {
            if (!Box.Intersect(Cutter.Bounds)) continue;
            const FClipPlane& Roof=Cutter.Planes.Last();
            bool Above=false;
            for (const auto& P:Triangle) Above |= P.P.Dot(Roof.N)-Roof.W < -1.e-7;
            if (!Above) continue;
            TArray<FClippedPolygon> Next;
            for (const auto& Part:Pieces)
            {
                if (Bounds(Part).Intersect(Cutter.Bounds)) Subtract(Part,Cutter.Planes,Next);
                else Next.Add(Part);
            }
            Pieces=MoveTemp(Next);
            if (Pieces.IsEmpty()) break;
        }
        for (const auto& Part:Pieces) Append(Finished,Part,Mesh.Attributes()->GetMaterialID()->GetValue(ID));
    }
    const int32 Side=FMath::Clamp(ChunkSideLength,4,128);
    for (const auto& Patch:BevelPatches)
    {
        auto Emit = [&](const FBox2D* CellBox, int32 Material)
        {
            for (bool Bottom:{false,true})
            {
                FClippedPolygon Poly;
                FVector3d Normal=(Patch.P[1]-Patch.P[0]).Cross(Patch.P[2]-Patch.P[0]).GetSafeNormal();
                if (Bottom) Normal.Z=-Normal.Z;
                // Input to Append is already clockwise, like the source mesh.
                for (int32 K=0; K<3; ++K)
                {
                    FVector3d P=Patch.P[Bottom ? K : 2-K];
                    if (Bottom) P.Z=-Thickness-P.Z;
                    Poly.Add({P,FVector2f(P.X,P.Y)/FMath::Max(UVWorldUnits,1.f),FVector3f(Normal)});
                }
                if (CellBox)
                {
                    Poly=Clip(Poly,{{-1,0,0},-CellBox->Min.X}); Poly=Clip(Poly,{{1,0,0},CellBox->Max.X});
                    Poly=Clip(Poly,{{0,-1,0},-CellBox->Min.Y}); Poly=Clip(Poly,{{0,1,0},CellBox->Max.Y});
                }
                const int32 Slot=Bottom ? (BodyMaterialIndex >= 0 ? ClampMaterialIndex(BodyMaterialIndex) : Material) :
                    (BorderStyle == EPlatformBorder::Rim ? ClampMaterialIndex(BorderMaterialIndex) : Material);
                Append(Finished,Poly,Slot);
            }
        };
        if (PlatformMode==EPlatformMode::Solid) { Emit(nullptr,EvaluatePatternMaterial(FIntPoint::ZeroValue)); continue; }
        for (int32 Y=ChunkIndex/ChunkCount.X*Side; Y<FMath::Min((ChunkIndex/ChunkCount.X+1)*Side,CellCount.Y); ++Y)
        for (int32 X=ChunkIndex%ChunkCount.X*Side; X<FMath::Min((ChunkIndex%ChunkCount.X+1)*Side,CellCount.X); ++X)
        {
            const auto& Cell=Cells[ToCellIndex(FIntPoint(X,Y))];
            if (!Cell.bExists) continue;
            double X0,Y0,X1,Y1; GetCellBounds(FIntPoint(X,Y),X0,Y0,X1,Y1);
            const FBox2D Box(FVector2D(X0,Y0),FVector2D(X1,Y1));
            if (Box.Intersect(Patch.Bounds)) Emit(&Box,Cell.MaterialIndex);
        }
    }
    Mesh=MoveTemp(Finished);
}

void AParametricPlatform::BevelSimpleCollision(FKAggregateGeom& Collision) const
{
    if (BevelPatches.IsEmpty()) return;
    FKAggregateGeom Result;
    auto Process = [&](const TArray<FVector2D>& Footprint, const FKBoxElem* Box, const FKConvexElem* Convex)
    {
        const FPolygon2d Polygon(Footprint);
        FBox2D Bounds(ForceInit); for (const auto& P:Footprint) Bounds+=P;
        TArray<const FBevelPatch*> Nearby;
        TArray<FGeneralPolygon2d> Cuts;
        for (const auto& Patch:BevelPatches) if (Bounds.Intersect(Patch.Bounds))
        {
            Nearby.Add(&Patch);
            Cuts.Emplace(FPolygon2d(TArray<FVector2D>{{Patch.P[0].X,Patch.P[0].Y},{Patch.P[1].X,Patch.P[1].Y},{Patch.P[2].X,Patch.P[2].Y}}));
        }
        if (Nearby.IsEmpty())
        {
            if (Box) Result.BoxElems.Add(*Box); else Result.ConvexElems.Add(*Convex);
            return;
        }
        auto Add = [&](const TArray<FVector2D>& Points, const FBevelPatch* Patch)
        {
            if (Points.Num()<3 || FPolygon2d(Points).Area()<1.e-7) return;
            auto& Hull=Result.ConvexElems.AddDefaulted_GetRef();
            FVector3d N(0,0,1); double W=0;
            if (Patch) { N=(Patch->P[1]-Patch->P[0]).Cross(Patch->P[2]-Patch->P[0]).GetSafeNormal(); W=N.Dot(Patch->P[0]); }
            for (const auto& P:Points)
            {
                const double Z=(W-N.X*P.X-N.Y*P.Y)/N.Z;
                Hull.VertexData.Add(FVector(P.X,P.Y,Z)); Hull.VertexData.Add(FVector(P.X,P.Y,-Thickness-Z));
            }
            Hull.UpdateElemBox();
        };
        auto AddPolygons = [&](const TArray<FGeneralPolygon2d>& Polygons, const FBevelPatch* Patch)
        {
            for (const auto& P:Polygons)
            {
                if (P.GetHoles().IsEmpty() && P.GetOuter().IsConvex()) { Add(P.GetOuter().GetVertices(),Patch); continue; }
                FConstrainedDelaunay2d T; T.bOutputCCW=true; T.Add(P);
                if (!T.Triangulate()) continue;
                for (const auto& Tri:T.Triangles) Add({T.Vertices[Tri.A],T.Vertices[Tri.B],T.Vertices[Tri.C]},Patch);
            }
        };
        TArray<FGeneralPolygon2d> Interior;
        PolygonsDifference({FGeneralPolygon2d(Polygon)},Cuts,Interior);
        AddPolygons(Interior,nullptr);
        for (int32 I=0; I<Nearby.Num(); ++I)
        {
            TArray<FGeneralPolygon2d> Intersection;
            PolygonsIntersection({FGeneralPolygon2d(Polygon)},{Cuts[I]},Intersection);
            AddPolygons(Intersection,Nearby[I]);
        }
    };
    for (const auto& Box:Collision.BoxElems)
    {
        const FVector2D Min(Box.Center.X-Box.X*.5,Box.Center.Y-Box.Y*.5), Max(Box.Center.X+Box.X*.5,Box.Center.Y+Box.Y*.5);
        Process({Min,{Max.X,Min.Y},Max,{Min.X,Max.Y}},&Box,nullptr);
    }
    for (const auto& Convex:Collision.ConvexElems)
    {
        TArray<FVector2D> Points;
        for (const auto& P:Convex.VertexData) Points.AddUnique(FVector2D(P.X,P.Y));
        Process(Points,nullptr,&Convex);
    }
    Collision=MoveTemp(Result);
}
