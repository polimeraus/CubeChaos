#include "ParametricLobbyPlatformDestructable.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "PhysicsEngine/BoxElem.h"
using namespace UE::Geometry;

namespace LobbyCells
{
    struct FVertex { FVector3d P; FVector2f UV; FVector3f N; };
    struct FClipPlane { FVector3d N; double W; };
    void Clip(TArray<FVertex>& Poly, const FClipPlane& Plane)
    {
        if (Poly.IsEmpty()) { return; }
        TArray<FVertex> Result;
        FVertex A=Poly.Last(); double DA=A.P.Dot(Plane.N)-Plane.W;
        for (const FVertex& B:Poly)
        {
            const double DB=B.P.Dot(Plane.N)-Plane.W;
            if ((DA>1.e-8)!=(DB>1.e-8))
            {
                const double T=FMath::Clamp(DA/(DA-DB),0.,1.);
                Result.Add({FMath::Lerp(A.P,B.P,T),FMath::Lerp(A.UV,B.UV,float(T)),FMath::Lerp(A.N,B.N,float(T))});
            }
            if (DB<=1.e-8) { Result.Add(B); }
            A=B; DA=DB;
        }
        Poly=MoveTemp(Result);
    }
}

void AParametricLobbyPlatformDestructable::CacheCellGeometry()
{
    using namespace LobbyCells;
    CellGeometry.Reset(); CellGeometry.SetNum(CellCount.X*CellCount.Y);
    FDynamicMesh3 Reference; BuildReferenceMesh(Reference);
    const FVector2D Pitch=FVector2D(EffectiveCellDimensions.X,EffectiveCellDimensions.Y)+CellSpacing;
    const double T=EffectiveCellDimensions.Z;
    auto Store=[&](const TArray<FVertex>& Poly,int32 Material,TArray<FPaintedTriangle>& Dest)
    {
        for (int32 I=1; I+1<Poly.Num(); ++I)
        {
            if ((Poly[I].P-Poly[0].P).Cross(Poly[I+1].P-Poly[0].P).SquaredLength()<1.e-12) { continue; }
            FPaintedTriangle& Tri=Dest.AddDefaulted_GetRef(); Tri.Material=Material;
            const int32 Indices[3]={0,I,I+1};
            for (int32 K=0; K<3; ++K) { Tri.P[K]=Poly[Indices[K]].P; Tri.UV[K]=Poly[Indices[K]].UV; Tri.N[K]=Poly[Indices[K]].N; }
        }
    };
    const auto* UVs=Reference.Attributes()->PrimaryUV();
    const auto* Normals=Reference.Attributes()->PrimaryNormals();
    // Each immutable shell triangle is clipped once into the cells it overlaps. Damage never
    // runs a mesh boolean or reclips the silhouette; it only assembles cached surviving cells.
    for (int32 Tid:Reference.TriangleIndicesItr())
    {
        const FIndex3i V=Reference.GetTriangle(Tid), U=UVs->GetTriangle(Tid), N=Normals->GetTriangle(Tid);
        TArray<FVertex> Triangle;
        FBox2D Bounds(ForceInit);
        for (int32 K=0; K<3; ++K) { const FVector3d P=Reference.GetVertex(V[K]); Triangle.Add({P,UVs->GetElement(U[K]),Normals->GetElement(N[K])}); Bounds+=FVector2D(P.X,P.Y); }
        const int32 MinX=FMath::Clamp(FMath::FloorToInt((Bounds.Min.X-GridLocalMin.X)/Pitch.X),0,CellCount.X-1);
        const int32 MinY=FMath::Clamp(FMath::FloorToInt((Bounds.Min.Y-GridLocalMin.Y)/Pitch.Y),0,CellCount.Y-1);
        const int32 MaxX=FMath::Clamp(FMath::FloorToInt((Bounds.Max.X-GridLocalMin.X)/Pitch.X),0,CellCount.X-1);
        const int32 MaxY=FMath::Clamp(FMath::FloorToInt((Bounds.Max.Y-GridLocalMin.Y)/Pitch.Y),0,CellCount.Y-1);
        for (int32 Y=MinY; Y<=MaxY; ++Y) for (int32 X=MinX; X<=MaxX; ++X)
        {
            const FVector2D Min=GridLocalMin+FVector2D(X*Pitch.X,Y*Pitch.Y), Max=Min+FVector2D(EffectiveCellDimensions.X,EffectiveCellDimensions.Y);
            TArray<FVertex> Poly=Triangle;
            Clip(Poly,{{-1,0,0},-Min.X}); Clip(Poly,{{1,0,0},Max.X});
            Clip(Poly,{{0,-1,0},-Min.Y}); Clip(Poly,{{0,1,0},Max.Y});
            Store(Poly,Reference.Attributes()->GetMaterialID()->GetValue(Tid),CellGeometry[X+Y*CellCount.X].Surface);
        }
    }
    const TArray<FVector2D> Outline=MakeOutline();
    double Inradius=1.e30;
    TArray<FClipPlane> OuterPlanes;
    for (int32 I=0; I<Outline.Num(); ++I)
    {
        const FVector2D E=(Outline[(I+1)%Outline.Num()]-Outline[I]).GetSafeNormal(), N(E.Y,-E.X);
        const double W=FVector2D::DotProduct(N,Outline[I]); Inradius=FMath::Min(Inradius,W);
        OuterPlanes.Add({FVector3d(N.X,N.Y,0),W});
    }
    const double B=FMath::Clamp(double(BevelSize),0.,FMath::Min(FMath::Min(AreaSize.X,AreaSize.Y)*.01,T*.1));
    TArray<FClipPlane> SlabPlanes=OuterPlanes;
    for (const FClipPlane& P:OuterPlanes)
    {
        if (B>0) {
            const double S=P.W/Inradius;
            SlabPlanes.Add({FVector3d(P.N.X,P.N.Y,S),P.W*(1.-B/Inradius)});
            SlabPlanes.Add({FVector3d(P.N.X,P.N.Y,-S),P.W*(1.-B/Inradius)+S*T});
        }
    }
    for (int32 Y=0; Y<CellCount.Y; ++Y) for (int32 X=0; X<CellCount.X; ++X)
    {
        FCellGeometry& Geometry=CellGeometry[X+Y*CellCount.X];
        if (Geometry.Surface.IsEmpty()) { continue; }
        const FVector2D Min=GridLocalMin+FVector2D(X*Pitch.X,Y*Pitch.Y), Max=Min+FVector2D(EffectiveCellDimensions.X,EffectiveCellDimensions.Y);
        const FVector2D Corners[4]={{Min.X,Min.Y},{Max.X,Min.Y},{Max.X,Max.Y},{Min.X,Max.Y}};
        Geometry.bBox=true;
        for (const FVector2D& C:Corners) for (const FClipPlane& P:OuterPlanes) {
            Geometry.bBox &= P.N.Dot(FVector3d(C.X,C.Y,0))<=P.W*(1.-B/Inradius)+1.e-7;
        }
        // CCW footprint edges: MinY, MaxX, MaxY, MinX. Only exposed neighbours emit caps.
        for (int32 Edge=0; Edge<4; ++Edge)
        {
            const FVector2D A=Corners[Edge], C=Corners[(Edge+1)%4], E=(C-A).GetSafeNormal();
            const FVector3d Normal(E.Y,-E.X,0);
            // A plane on the pristine outside is already present in the cached shell.
            double Low=1.e30, High=-1.e30;
            for (const FVector2D& P:Outline) { const double D=Normal.Dot(FVector3d(P.X-A.X,P.Y-A.Y,0)); Low=FMath::Min(Low,D); High=FMath::Max(High,D); }
            if (Low>=-1.e-7 || High<=1.e-7) { continue; }
            // Unreal triangle winding is clockwise when viewed from outside the cell.
            TArray<FVertex> Poly={{{A.X,A.Y,-T},{0,1},FVector3f(Normal)},{{A.X,A.Y,0},{0,0},FVector3f(Normal)},
                                 {{C.X,C.Y,0},{1,0},FVector3f(Normal)},{{C.X,C.Y,-T},{1,1},FVector3f(Normal)}};
            for (const FClipPlane& P:SlabPlanes) { Clip(Poly,P); if (Poly.IsEmpty()) { break; } }
            Store(Poly,4,Geometry.Caps[Edge]);
        }
        auto AddHull=[&](const TArray<FPaintedTriangle>& Tris) {
            for (const auto& Tri:Tris) for (const FVector3d& P:Tri.P) {
                Geometry.Hull.AddUnique(FVector(FMath::RoundToDouble(P.X*10000)/10000,FMath::RoundToDouble(P.Y*10000)/10000,FMath::RoundToDouble(P.Z*10000)/10000));
            }
        };
        AddHull(Geometry.Surface); for (const auto& Caps:Geometry.Caps) { AddHull(Caps); }
    }
}

bool AParametricLobbyPlatformDestructable::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{
    const int32 I=Cell.X+Cell.Y*CellCount.X;
    return CellGeometry.IsValidIndex(I) && CellGeometry[I].Hull.Num()>=4;
}

void AParametricLobbyPlatformDestructable::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableAttributes(); Mesh.Attributes()->EnableMaterialID();
    auto* Normals=Mesh.Attributes()->PrimaryNormals(); auto* UVs=Mesh.Attributes()->PrimaryUV();
    auto Append=[&](const TArray<FPaintedTriangle>& Triangles) {
        for (const auto& Tri:Triangles) {
            FIndex3i V,U,N;
            for (int32 K=0;K<3;++K) { V[K]=Mesh.AppendVertex(Tri.P[K]); U[K]=UVs->AppendElement(Tri.UV[K]); N[K]=Normals->AppendElement(Tri.N[K]); }
            const int32 Id=Mesh.AppendTriangle(V); Normals->SetTriangle(Id,N); UVs->SetTriangle(Id,U); Mesh.Attributes()->GetMaterialID()->SetValue(Id,Tri.Material);
        }
    };
    const int32 Side=FMath::Clamp(ChunkSideLength,4,128), StartX=ChunkIndex%ChunkCount.X*Side, StartY=ChunkIndex/ChunkCount.X*Side;
    const FIntPoint Neighbours[4]={{0,-1},{1,0},{0,1},{-1,0}};
    for (int32 Y=StartY;Y<FMath::Min(StartY+Side,CellCount.Y);++Y) for (int32 X=StartX;X<FMath::Min(StartX+Side,CellCount.X);++X)
    {
        const FIntPoint Cell(X,Y); const int32 I=ToCellIndex(Cell); if (!Cells[I].bExists) { continue; }
        const FCellGeometry& Geometry=CellGeometry[I]; Append(Geometry.Surface);
        for (int32 E=0;E<4;++E) {
            const FIntPoint N=Cell+Neighbours[E];
            const bool bGap=(E%2==0?CellSpacing.Y:CellSpacing.X)>1.e-7;
            if (bGap || !IsValidCell(N) || !Cells[ToCellIndex(N)].bExists) { Append(Geometry.Caps[E]); }
        }
    }
}

void AParametricLobbyPlatformDestructable::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
{
    const int32 Side=FMath::Clamp(ChunkSideLength,4,128), StartX=ChunkIndex%ChunkCount.X*Side, StartY=ChunkIndex/ChunkCount.X*Side;
    for (int32 Y=StartY;Y<FMath::Min(StartY+Side,CellCount.Y);++Y) for (int32 X=StartX;X<FMath::Min(StartX+Side,CellCount.X);++X)
    {
        const FIntPoint Cell(X,Y); const int32 I=ToCellIndex(Cell); if (!Cells[I].bExists) { continue; }
        const FCellGeometry& Geometry=CellGeometry[I];
        if (Geometry.bBox) {
            FKBoxElem& Box=Collision.BoxElems.AddDefaulted_GetRef(); Box.Center=GetCellLocalCenter(Cell);
            Box.X=EffectiveCellDimensions.X; Box.Y=EffectiveCellDimensions.Y; Box.Z=EffectiveCellDimensions.Z;
        } else {
            FKConvexElem& Convex=Collision.ConvexElems.AddDefaulted_GetRef(); Convex.VertexData=Geometry.Hull; Convex.UpdateElemBox();
        }
    }
}
