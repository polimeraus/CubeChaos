#include "ChunkedDestructibleTorus.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;
namespace
{
    // Counter-clockwise corners along outward normals. Shared mesh helpers reverse UE winding.
    constexpr int32 TorusFaces[6][4]={{4,5,6,7},{3,2,1,0},{0,1,5,4},{2,3,7,6},{1,2,6,5},{3,0,4,7}};
}

AChunkedDestructibleTorus::AChunkedDestructibleTorus()
{
    FloorPattern=EChunkedFloorPattern::Solid;ChunkSideLength=96;MaxGeneratedCells=50000;
    bGenerateBottomFaces=true;bReactToMusic=false;DefaultCellHealth=25;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Panel(TEXT("/Game/Structures/Destructible/Torus/Materials/MI_TorusSilver"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Edge(TEXT("/Game/Structures/Destructible/Torus/Materials/MI_TorusEdge"));
    PanelMaterial=Panel.Get();EdgeMaterial=Edge.Get();
}
void AChunkedDestructibleTorus::RebuildTorus() { GenerateFloor(false); }

#if WITH_EDITOR
bool AChunkedDestructibleTorus::CanEditChange(const FProperty* P) const
{
    if (P) for (const TCHAR* Derived:{TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
        TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
        TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
        if (P->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(P);
}
#endif

bool AChunkedDestructibleTorus::FPiece::Contains(FVector P,double Tolerance) const
{
    if (!Bounds.ExpandBy(Tolerance).IsInsideOrOn(P)) return false;
    for (const auto& F:TorusFaces)
    {
        const FVector N=FVector::CrossProduct(V[F[1]]-V[F[0]],V[F[3]]-V[F[0]]).GetSafeNormal();
        if (FVector::DotProduct(P-V[F[0]],N)>Tolerance) return false;
    }
    return true;
}

void AChunkedDestructibleTorus::AddPiece(const FVector (&V)[8],bool bShelf)
{
    if (bBudgetExceeded) return;
    if (Pieces.Num()>=FMath::Clamp(MaxGeneratedCells,1,100000)) { bBudgetExceeded=true;return; }
    FPiece P;P.bShelf=bShelf;P.Bounds=FBox(ForceInit);
    FVector Center=FVector::ZeroVector;
    for (const auto& X:V) {Center+=X;P.Bounds+=X;}Center/=8;
    const double Size=P.Bounds.GetSize().GetMin();
    if (Size<.001) return;
    const double Shrink=1-FMath::Min(double(PieceGap)/Size,.2);
    P.Bounds=FBox(ForceInit);
    for (int32 I=0;I<8;++I) {P.V[I]=Center+(V[I]-Center)*Shrink;P.Bounds+=P.V[I];}
    Pieces.Add(P);
}

void AChunkedDestructibleTorus::BuildMeridians(double Start,double Span,const TArray<FVector2D>& Shelves)
{
    const bool Rounded=bRoundedEnds && OpeningAngle>0;
    const double Cap=Rounded?FMath::Asin(double(TubeRadius)/RingRadius):0;
    const double Extent=Span+2*Cap,Half=MeridianThickness*.5;
    for (int32 M=0;M<MeridianCount && !bBudgetExceeded;++M)
    {
        const double Along=-Cap+Extent*(M+.5)/MeridianCount;
        const double Outside=FMath::Max3(0.,-Along,Along-Span);
        const double Offset=Outside>0?RingRadius*FMath::Sin(Outside)+Half:Half;
        const double Radius=FMath::Sqrt(FMath::Max(0.,FMath::Square(double(TubeRadius))-Offset*Offset));
        if (Radius<1) continue;
        const double Center=RingRadius*FMath::Cos(Outside);
        const double Angle=Start+Along;
        const FVector N(FMath::Cos(Angle),FMath::Sin(Angle),0),T(-N.Y,N.X,0);
        // Trim only 0.01 cm from the poles to avoid zero-volume collision slivers.
        const double Polar=FMath::Acos(1-.01/Radius);
        TArray<double> Heights;
        for (int32 H=0;H<=CurveSegments;++H)
            Heights.Add(TubeRadius-Radius*FMath::Cos(FMath::Lerp(Polar,UE_PI-Polar,double(H)/CurveSegments)));
        const double Bottom=Heights[0],Top=Heights.Last();
        for (const auto& Shelf:Shelves)
        {
            if (Shelf.X>Bottom && Shelf.X<Top) Heights.Add(Shelf.X);
            if (Shelf.Y>Bottom && Shelf.Y<Top) Heights.Add(Shelf.Y);
        }
        Heights.Sort();
        for (int32 H=0;H<Heights.Num()-1 && !bBudgetExceeded;++H)
        {
            const double Z0=Heights[H],Z1=Heights[H+1],Mid=(Z0+Z1)*.5;
            if (Z1-Z0<.001) continue;
            bool InShelf=false;for (const auto& Shelf:Shelves) InShelf|=Mid>Shelf.X && Mid<Shelf.Y;
            if (InShelf) continue;
            const double W0=FMath::Sqrt(FMath::Max(0.,Radius*Radius-FMath::Square(Z0-TubeRadius)));
            const double W1=FMath::Sqrt(FMath::Max(0.,Radius*Radius-FMath::Square(Z1-TubeRadius)));
            const int32 Columns=FMath::Max(1,FMath::CeilToInt(2*FMath::Max(W0,W1)/PieceSize));
            for (int32 C=0;C<Columns && !bBudgetExceeded;++C)
            {
                FVector V[8];
                for (int32 Layer=0;Layer<2;++Layer)
                {
                    const double Width=Layer?W1:W0,Z=Layer?Z1:Z0;
                    const double Lo=Center+Width*(-1+2.*C/Columns),Hi=Center+Width*(-1+2.*(C+1)/Columns);
                    auto Point=[&](double R,double Side){return N*R+T*Side+FVector(0,0,Z);};
                    V[Layer*4]=Point(Lo,-Half);V[Layer*4+1]=Point(Hi,-Half);
                    V[Layer*4+2]=Point(Hi,Half);V[Layer*4+3]=Point(Lo,Half);
                }
                AddPiece(V,false);
            }
        }
    }
}

void AChunkedDestructibleTorus::BuildParallel(double Start,double Span,double Z0,double Z1)
{
    const double ZDistance=FMath::Max(FMath::Abs(Z0-TubeRadius),FMath::Abs(Z1-TubeRadius));
    const double Width=FMath::Sqrt(FMath::Max(0.,FMath::Square(double(TubeRadius))-ZDistance*ZDistance));
    if (Width<1) return;
    const bool Rounded=bRoundedEnds && OpeningAngle>0;
    const double Cap=Rounded?FMath::Max(0.,FMath::Asin(Width/RingRadius)-.01/RingRadius):0;
    const double Extent=Span+2*Cap;
    const int32 Steps=FMath::Max(FMath::CeilToInt(RingSegments*Extent/UE_TWO_PI),FMath::CeilToInt((RingRadius+Width)*Extent/PieceSize));
    TArray<double> Angles;
    for (int32 S=0;S<=Steps;++S) Angles.Add(-Cap+Extent*S/Steps);
    Angles.AddUnique(0.);Angles.AddUnique(Span);Angles.Sort();
    const int32 Rows=FMath::Max(1,FMath::CeilToInt(2*Width/PieceSize));
    auto Interval=[&](double Along)
    {
        const double Outside=FMath::Max3(0.,-Along,Along-Span);
        const double Center=RingRadius*FMath::Cos(Outside);
        const double HalfWidth=FMath::Sqrt(FMath::Max(0.,Width*Width-FMath::Square(RingRadius*FMath::Sin(Outside))));
        return FVector2D(Center-HalfWidth,Center+HalfWidth);
    };
    for (int32 S=0;S<Angles.Num()-1 && !bBudgetExceeded;++S)
    {
        if (Angles[S+1]-Angles[S]<1.e-7) continue;
        const double A0=Start+Angles[S],A1=Start+Angles[S+1];
        const FVector A(FMath::Cos(A0),FMath::Sin(A0),0),B(FMath::Cos(A1),FMath::Sin(A1),0);
        const FVector2D RA=Interval(Angles[S]),RB=Interval(Angles[S+1]);
        for (int32 R=0;R<Rows && !bBudgetExceeded;++R)
        {
            const double AL=FMath::Lerp(RA.X,RA.Y,double(R)/Rows),AH=FMath::Lerp(RA.X,RA.Y,double(R+1)/Rows);
            const double BL=FMath::Lerp(RB.X,RB.Y,double(R)/Rows),BH=FMath::Lerp(RB.X,RB.Y,double(R+1)/Rows);
            const FVector Low(0,0,Z0),High(0,0,Z1);
            const FVector V[8]={A*AL+Low,A*AH+Low,B*BH+Low,B*BL+Low,
                A*AL+High,A*AH+High,B*BH+High,B*BL+High};
            AddPiece(V,true);
        }
    }
}

bool AChunkedDestructibleTorus::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max){V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min;};
    Clamp(RingRadius,400,10000);Clamp(TubeRadius,150,FMath::Min(4000.f,RingRadius-200));
    Clamp(OpeningAngle,0,300);OpeningDirection=FMath::IsFinite(OpeningDirection)?FMath::UnwindDegrees(OpeningDirection):-90;
    MeridianCount=FMath::Clamp(MeridianCount,3,96);ParallelCount=FMath::Clamp(ParallelCount,0,24);
    CurveSegments=FMath::Clamp(CurveSegments,16,128);RingSegments=FMath::Clamp(RingSegments,32,512);
    // Two rounded tips must not overlap when a very small opening is requested.
    const double Cap=bRoundedEnds && OpeningAngle>0?FMath::Asin(double(TubeRadius)/RingRadius):0;
    if (OpeningAngle>0) OpeningAngle=FMath::Max(double(OpeningAngle),Cap>0?FMath::RadiansToDegrees(2*Cap)+5:1.);
    const double Span=FMath::DegreesToRadians(360.-OpeningAngle);
    const double Start=FMath::DegreesToRadians(OpeningDirection+OpeningAngle*.5);
    Clamp(MeridianThickness,2,FMath::Min(100.,(RingRadius-TubeRadius)*(Span+2*Cap)/MeridianCount*.65));
    Clamp(ParallelThickness,2,FMath::Min(100.f,2*TubeRadius/(ParallelCount+1)*.3f));
    Clamp(PieceSize,60,600);Clamp(PieceGap,0,5);
    TArray<FVector2D> Shelves;
    for (int32 P=0;P<ParallelCount;++P)
    {
        const double Z=2.*TubeRadius*(P+1)/(ParallelCount+1);
        Shelves.Add(FVector2D(Z-ParallelThickness*.5,Z+ParallelThickness*.5));
    }
    TArray<FPiece> Previous=MoveTemp(Pieces);
    GenerationWarning.Reset();bBudgetExceeded=false;
    BuildMeridians(Start,Span,Shelves);
    const int32 MeridianPieces=Pieces.Num();
    for (const auto& Shelf:Shelves) BuildParallel(Start,Span,Shelf.X,Shelf.Y);
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce radius/counts/resolution. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);return false;
    }
    GeneratedHeight=2*TubeRadius;CenterOpeningWidth=2*(RingRadius-TubeRadius);
    GeneratedPieceCount=Pieces.Num();GeneratedMeridianPieces=MeridianPieces;GeneratedParallelPieces=Pieces.Num()-MeridianPieces;
    FloorMaterials={PanelMaterial,EdgeMaterial};
    for (auto& M:FloorMaterials) if (!M) M=UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount={Pieces.Num(),1};ChunkCount={FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1};
    EffectiveCellDimensions=FVector(PieceSize,PieceSize,ParallelThickness);CellDimensions=EffectiveCellDimensions;
    CellSpacing=FVector2D(PieceGap);EffectiveGridSize=FVector2D(2*(RingRadius+TubeRadius));GridLocalMin=-EffectiveGridSize*.5;AreaSize=EffectiveGridSize;
    return true;
}

bool AChunkedDestructibleTorus::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleTorus::GetCellLocalCenter(FIntPoint Cell) const
{
    if (!ShouldCellExistInGeneratedShape(Cell)) return FVector::ZeroVector;
    FVector Center=FVector::ZeroVector;for (const FVector& V:Pieces[Cell.X].V) Center+=V;
    return Center/8;
}
int32 AChunkedDestructibleTorus::EvaluatePatternMaterial(FIntPoint Cell) const { return 0; }
FTransform AChunkedDestructibleTorus::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructibleTorus::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const
{
    const FVector* V=Piece.V;
    const double UVScale=FMath::Max(1.f,UVWorldUnits);
    for (int32 Face=0;Face<6;++Face)
    {
        const auto& F=TorusFaces[Face];
        const FVector& A=V[F[0]];const FVector& B=V[F[1]];const FVector& C=V[F[2]];const FVector& D=V[F[3]];
        const FVector N=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        const FVector M=FVector::CrossProduct(C-A,D-A).GetSafeNormal();
        const int32 FaceMaterial=(Piece.bShelf?Face>=2:Face<2 || Face>=4)?1:Material;
        if (!N.IsNearlyZero()) AppendMeshTriangle(Mesh,A,B,C,FVector3f(N),FaceMaterial,FVector2f(0,0),FVector2f((B-A).Length()/UVScale,0),FVector2f((C-A).Length()/UVScale,(C-B).Length()/UVScale));
        if (!M.IsNearlyZero()) AppendMeshTriangle(Mesh,A,C,D,FVector3f(M),FaceMaterial,FVector2f(0,0),FVector2f((C-D).Length()/UVScale,(D-A).Length()/UVScale),FVector2f(0,(D-A).Length()/UVScale));
    }
}
void AChunkedDestructibleTorus::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
}
void AChunkedDestructibleTorus::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
{
    Collision.EmptyElements();if (!bGenerateCollision) return;
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            FKConvexElem& Convex=Collision.ConvexElems.Emplace_GetRef();
            for (const FVector& P:Pieces[I].V) Convex.VertexData.Add(P);
            Convex.UpdateElemBox();
        }
}
bool AChunkedDestructibleTorus::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
{
    Cell=FIntPoint::NoneValue;double Best=TNumericLimits<double>::Max();
    for (int32 I=0;I<Pieces.Num();++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists && Pieces[I].Contains(P,Tolerance))
        {
            const double Distance=FVector::DistSquared(P,GetCellLocalCenter(FIntPoint(I,0)));
            if (Distance<Best) {Best=Distance;Cell=FIntPoint(I,0);}
        }
    return Cell!=FIntPoint::NoneValue;
}
bool AChunkedDestructibleTorus::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructibleTorus::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    if (Hit.GetActor()!=this) return false;
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructibleTorus::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
    TFunctionRef<void(int32,float)> Function)
{
    if (!FMath::IsFinite(Radius) || Radius<=0 || P.ContainsNaN()) return 0;
    const FVector Local=GetActorTransform().InverseTransformPosition(P),Scale=GetActorScale3D().GetAbs();
    int32 Count=0;
    for (int32 I=0;I<Pieces.Num();++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            const FVector Point = Shape == EChunkedFloorBlastShape::Sphere
                ? Pieces[I].Bounds.GetClosestPointTo(Local) : GetCellLocalCenter(FIntPoint(I,0));
            const FVector D=((Point-Local)*Scale).GetAbs();
            const double Distance=Shape==EChunkedFloorBlastShape::Square?D.GetMax():
                (Shape==EChunkedFloorBlastShape::Diamond?D.X+D.Y+D.Z:D.Length());
            if (Distance<=Radius) {Function(I,float(Distance));++Count;}
        }
    return Count;
}
void AChunkedDestructibleTorus::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
