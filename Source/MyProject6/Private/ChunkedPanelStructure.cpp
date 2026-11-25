#include "ChunkedPanelStructure.h"
#include "DestructibleFaceUV.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"

using UE::Geometry::FDynamicMesh3;

namespace
{
    constexpr int32 Faces[6][4] = {{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
}

void AChunkedPanelStructure::FPanelLayout::AddPanel(const FVector (&Lower)[4], const FVector (&Upper)[4], uint8 Slot, int32 Trim)
{
    if (bOverBudget) return;
    if (Result.Num()>=Limit) { bOverBudget=true; return; }
    FPanel& Panel=Result.Emplace_GetRef();
    Panel.Bounds.Init();
    Panel.Center=FVector::ZeroVector;
    Panel.Slot=Slot;
    Panel.EdgeTrimSlot=Trim;
    for (int32 I=0; I<8; ++I)
    {
        Panel.V[I]=I<4 ? Lower[I] : Upper[I-4];
        Panel.Center+=Panel.V[I];
        Panel.Bounds+=Panel.V[I];
    }
    Panel.Center/=8.;
    const double Contraction=FMath::Clamp(1.-JointGap/FMath::Max(1.,Panel.Bounds.GetSize().GetMax()),.9,1.);
    Panel.Bounds.Init();
    for (FVector& V : Panel.V)
    {
        V=Panel.Center+(V-Panel.Center)*Contraction;
        Panel.Bounds+=V;
    }
    // The longitudinal faces of a rising, tapering ribbon need not be coplanar.
    // Use supporting planes of the same eight points passed to Chaos for point damage.
    for (int32 I=0; I<6; ++I)
    for (int32 J=I+1; J<7; ++J)
    for (int32 K=J+1; K<8; ++K)
    {
        FVector N=FVector::CrossProduct(Panel.V[J]-Panel.V[I],Panel.V[K]-Panel.V[I]).GetSafeNormal();
        if (N.IsZero()) continue;
        if ((Panel.Center-Panel.V[I]).Dot(N)>0.) N=-N;
        const FPlane Plane(Panel.V[I],N);
        bool bSupports=true;
        for (const FVector& V:Panel.V) if (Plane.PlaneDot(V)>.002) {bSupports=false;break;}
        if (bSupports && !Panel.Planes.ContainsByPredicate([&](const FPlane& Existing){return Existing.Equals(Plane,.0001);}))
            Panel.Planes.Add(Plane);
    }
    if (Panel.Planes.Num()<4) { Result.Pop(EAllowShrinking::No); return; }
    Bounds+=Panel.Bounds;
}


void AChunkedPanelStructure::InstallLayout(FPanelLayout&& Layout, float FragmentSize)
{
    Panels=MoveTemp(Layout.Result);
    PanelsPerChunk=FMath::Clamp(ChunkSideLength,4,128);
    CellCount=FIntPoint(Panels.Num(),1);
    ChunkCount=FIntPoint(FMath::DivideAndRoundUp(Panels.Num(),PanelsPerChunk),1);
    ChunkBounds.Init(FBox(ForceInit),ChunkCount.X);
    for (int32 I=0; I<Panels.Num(); ++I) ChunkBounds[I/PanelsPerChunk]+=Panels[I].Bounds;
    EffectiveGridSize=FVector2D(Layout.Bounds.GetSize());
    AreaSize=EffectiveGridSize;
    GridLocalMin=FVector2D(Layout.Bounds.Min);
    EffectiveCellDimensions=FVector(FragmentSize);
    CellDimensions=EffectiveCellDimensions;
    CellSpacing=FVector2D(Layout.JointGap);
}

#if WITH_EDITOR
bool AChunkedPanelStructure::CanEditChange(const FProperty* Property) const
{
    if (Property)
    {
        const FName N=Property->GetFName();
        if (N==TEXT("AreaSize") || N==TEXT("CellDimensions") || N==TEXT("CellSpacing") ||
            N==TEXT("FloorPattern") || N==TEXT("FloorMaterials") || N==TEXT("PatternCellScale") || N==TEXT("PatternRandomSeed")) return false;
    }
    return Super::CanEditChange(Property);
}
#endif

bool AChunkedPanelStructure::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Panels.IsValidIndex(Cell.X); }

FVector AChunkedPanelStructure::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Panels[Cell.X].Center:FVector::ZeroVector; }

FTransform AChunkedPanelStructure::GetCellFlashTransform(FIntPoint Cell) const
{
    if (!ShouldCellExistInGeneratedShape(Cell)) return FTransform::Identity;
    const FPanel& S=Panels[Cell.X];
    return FTransform(FQuat::Identity,S.Center,S.Bounds.GetSize()*(FMath::Max(.01f,FlashScale)/100.f));
}

int32 AChunkedPanelStructure::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Panels[Cell.X].Slot:0; }

void AChunkedPanelStructure::DrawPanel(FDynamicMesh3& Mesh,const FPanel& Panel,int32 Material) const
{
    auto Quad=[&](const FVector& A,const FVector& B,const FVector& C,const FVector& D,int32 Slot)
    {
        const FVector N=(B-A).Cross(C-A).GetSafeNormal(), M=(C-A).Cross(D-A).GetSafeNormal();
        const FDestructibleFaceUVs UV=ProjectDestructibleFaceUVs(A,B,C,D,N+M,UVWorldUnits);
        auto Triangle=[&](const FVector& P,const FVector& Q,const FVector& R,const FVector& Normal,
                          const FVector2f& U,const FVector2f& V,const FVector2f& W)
        {
            if (Normal.IsZero()) return;
            if (Normal.Dot((P+Q+R)/3.-Panel.Center)>=0.)
                AppendMeshTriangle(Mesh,P,Q,R,FVector3f(Normal),Slot,U,V,W);
            else
                AppendMeshTriangle(Mesh,P,R,Q,FVector3f(-Normal),Slot,U,W,V);
        };
        Triangle(A,B,C,N,UV.A,UV.B,UV.C);
        Triangle(A,C,D,M,UV.A,UV.C,UV.D);
    };
    for (int32 I=0; I<6; ++I)
    {
        const int32* F=Faces[I];
        const FVector& A=Panel.V[F[0]], &B=Panel.V[F[1]], &C=Panel.V[F[2]], &D=Panel.V[F[3]];
        if (Panel.EdgeTrimSlot != INDEX_NONE && (I==3 || I==5) && Material==Panel.Slot)
        {
            // Inlaid metal breaks up the smoked panels without independent, wafer-thin collision.
            const double Alpha=FMath::Clamp(2.8/FMath::Max(1.,FVector::Distance(A,B)),.012,.12);
            const FVector L=FMath::Lerp(A,B,Alpha),U=FMath::Lerp(D,C,Alpha);
            Quad(A,L,U,D,Panel.EdgeTrimSlot); Quad(L,B,C,U,Material);
        }
        else Quad(A,B,C,D,Material);
    }
}

void AChunkedPanelStructure::BuildChunkMesh(int32 Index,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());
    Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);
    Mesh.Attributes()->EnableMaterialID();
    for (int32 I=Index*PanelsPerChunk; I<FMath::Min((Index+1)*PanelsPerChunk,Panels.Num()); ++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) DrawPanel(Mesh,Panels[I],Cells[I].MaterialIndex);
}

void AChunkedPanelStructure::BuildChunkSimpleCollision(int32 Index,FKAggregateGeom& Collision) const
{
    Collision.EmptyElements();
    if (!bGenerateCollision) return;
    for (int32 I=Index*PanelsPerChunk; I<FMath::Min((Index+1)*PanelsPerChunk,Panels.Num()); ++I)
    {
        if (!Cells.IsValidIndex(I) || !Cells[I].bExists) continue;
        const FPanel& S=Panels[I];
        FKConvexElem& Hull=Collision.ConvexElems.Emplace_GetRef();
        for (const FVector& V:S.V) Hull.VertexData.AddUnique(V-S.Center);
        Hull.SetTransform(FTransform(S.Center));
        Hull.UpdateElemBox();
    }
}

bool AChunkedPanelStructure::FPanel::Contains(const FVector& Point,double Tolerance) const
{
    if (!Bounds.ExpandBy(Tolerance).IsInsideOrOn(Point)) return false;
    for (const FPlane& P:Planes) if (P.PlaneDot(Point)>Tolerance) return false;
    return true;
}

bool AChunkedPanelStructure::FindPanel(const FVector& Local,FIntPoint& Cell,double Tolerance) const
{
    Cell=FIntPoint::NoneValue;
    double Nearest=TNumericLimits<double>::Max();
    for (int32 Chunk=0; Chunk<ChunkBounds.Num(); ++Chunk)
    {
        if (!ChunkBounds[Chunk].ExpandBy(Tolerance).IsInsideOrOn(Local)) continue;
        for (int32 I=Chunk*PanelsPerChunk; I<FMath::Min((Chunk+1)*PanelsPerChunk,Panels.Num()); ++I)
        {
            if (!Cells.IsValidIndex(I) || !Cells[I].bExists || !Panels[I].Contains(Local,Tolerance)) continue;
            const double Distance=FVector::DistSquared(Local,Panels[I].Center);
            if (Distance<Nearest) { Nearest=Distance; Cell=FIntPoint(I,0); }
        }
    }
    return Cell.X!=INDEX_NONE;
}

bool AChunkedPanelStructure::WorldLocationToCell(FVector Point,FIntPoint& Cell) const
{ return FindPanel(GetActorTransform().InverseTransformPosition(Point),Cell,.02); }

bool AChunkedPanelStructure::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    if (Hit.GetActor()!=this) return false;
    const FVector Point=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPanel(GetActorTransform().InverseTransformPosition(Point),Cell,.7);
}

int32 AChunkedPanelStructure::SelectCellsInRadius(FVector Point,float Radius,EChunkedFloorBlastShape Shape,TFunctionRef<void(int32,float)> Visit)
{
    if (!FMath::IsFinite(Radius) || Radius<=0) return 0;
    const FVector Local=GetActorTransform().InverseTransformPosition(Point),Scale=GetActorScale3D().GetAbs();
    const FVector Extent=FVector(Radius)/Scale.ComponentMax(FVector(.00001));
    const FBox Query(Local-Extent,Local+Extent);
    int32 Count=0;
    for (int32 Chunk=0; Chunk<ChunkBounds.Num(); ++Chunk)
    {
        if (!Query.Intersect(ChunkBounds[Chunk])) continue;
        for (int32 I=Chunk*PanelsPerChunk; I<FMath::Min((Chunk+1)*PanelsPerChunk,Panels.Num()); ++I)
        {
            const FPanel& S=Panels[I];
            if (!Cells.IsValidIndex(I) || !Cells[I].bExists || !Query.Intersect(S.Bounds)) continue;
            const FVector D=((S.Center-Local)*Scale).GetAbs();
            double Distance=D.Length();
            if (Shape==EChunkedFloorBlastShape::Square) Distance=D.GetMax();
            else if (Shape==EChunkedFloorBlastShape::Diamond) Distance=D.X+D.Y+D.Z;
            else if (Shape==EChunkedFloorBlastShape::Sphere)
            {
                if (S.Contains(Local,.001)) Distance=0.;
                else
                {
                    double Nearest=TNumericLimits<double>::Max();
                    for (const auto& F:Faces)
                    {
                        for (int32 T=1; T<3; ++T)
                        {
                            const FVector A=(S.V[F[0]]-Local)*Scale,B=(S.V[F[T]]-Local)*Scale,C=(S.V[F[T+1]]-Local)*Scale;
                            if (FVector::CrossProduct(B-A,C-A).IsNearlyZero()) continue;
                            Nearest=FMath::Min(Nearest,FMath::ClosestPointOnTriangleToPoint(FVector::ZeroVector,A,B,C).SizeSquared());
                        }
                    }
                    Distance=FMath::Sqrt(Nearest);
                }
            }
            if (Distance<=Radius) { Visit(I,float(Distance)); ++Count; }
        }
    }
    return Count;
}

void AChunkedPanelStructure::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/PanelsPerChunk);
}
