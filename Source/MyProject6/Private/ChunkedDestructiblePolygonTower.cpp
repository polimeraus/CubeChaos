#include "ChunkedDestructiblePolygonTower.h"
#include "Components/ChildActorComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"
using namespace UE::Geometry;

AChunkedDestructiblePolygonTower::AChunkedDestructiblePolygonTower()
{
    FloorPattern=EChunkedFloorPattern::Solid;ChunkSideLength=96;MaxGeneratedCells=50000;
    bGenerateBottomFaces=true;bReactToMusic=false;DefaultCellHealth=25;
    GroundLaunchPad=CreateDefaultSubobject<UChildActorComponent>(TEXT("GroundLaunchPad"));
    GroundLaunchPad->SetupAttachment(GetRootComponent());
    ZigguratTraversal::InitializeMaterials(Traversal);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stone(TEXT("/Game/Structures/Destructible/PolygonTower/Materials/MI_PolygonStone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Trim(TEXT("/Game/Structures/Destructible/PolygonTower/Materials/MI_PolygonCornice"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Floor(TEXT("/Game/Structures/Destructible/PolygonTower/Materials/MI_PolygonFloor"));
    StoneMaterial=Stone.Get();CorniceMaterial=Trim.Get();FloorMaterial=Floor.Get();
}
void AChunkedDestructiblePolygonTower::RebuildTower() { GenerateFloor(false); }
#if WITH_EDITOR
bool AChunkedDestructiblePolygonTower::CanEditChange(const FProperty* P) const
{
    if (P) for (const TCHAR* Derived:{TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
        TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
        TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
        if (P->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(P);
}
#endif
bool AChunkedDestructiblePolygonTower::FPiece::Contains(FVector P,double Tolerance) const
{
    if (!Bounds.ExpandBy(Tolerance).IsInsideOrOn(P)) return false;
    constexpr int32 Faces[6][4]={{4,5,6,7},{3,2,1,0},{0,1,5,4},{2,3,7,6},{1,2,6,5},{3,0,4,7}};
    for (const auto& F:Faces)
    {
        FVector N=FVector::CrossProduct(V[F[1]]-V[F[0]],V[F[3]]-V[F[0]]).GetSafeNormal();
        if (N.IsNearlyZero()) N=FVector::CrossProduct(V[F[2]]-V[F[0]],V[F[3]]-V[F[0]]).GetSafeNormal();
        if (FVector::DotProduct(P-V[F[0]],N)>Tolerance) return false;
    }
    return true;
}
void AChunkedDestructiblePolygonTower::AddPiece(const FVector (&V)[8],int32 Material)
{
    if (bBudgetExceeded) return;
    if (Pieces.Num()>=FMath::Clamp(MaxGeneratedCells,1,100000)) { bBudgetExceeded=true;return; }
    FPiece P;P.Material=Material;P.Bounds=FBox(ForceInit);
    FVector Center=FVector::ZeroVector;for (const auto& X:V) {Center+=X;P.Bounds+=X;}Center/=8;
    const FVector Size=P.Bounds.GetSize();if (Size.GetMin()<.001) return;
    const double Shrink=1-FMath::Min(double(PieceGap)/Size.GetMin(),.2);
    P.Bounds=FBox(ForceInit);
    for (int32 I=0;I<8;++I) { P.V[I]=Center+(V[I]-Center)*Shrink;P.Bounds+=P.V[I]; }
    Pieces.Add(P);
}
void AChunkedDestructiblePolygonTower::TileRing(double Radius,double InnerRadius,double Z0,double Z1,int32 Material)
{
    if (Z1-Z0<.001 || Radius<=InnerRadius+.001) return;
    const int32 Radial=FMath::Max(1,FMath::CeilToInt((Radius-InnerRadius)/PieceSize));
    const int32 Columns=FMath::Max(1,FMath::CeilToInt(2*Radius*FMath::Sin(UE_PI/SideCount)/PieceSize));
    const int32 Rows=FMath::Max(1,FMath::CeilToInt((Z1-Z0)/PieceSize));
    if (int64(Pieces.Num())+int64(SideCount)*Radial*Columns*Rows>FMath::Clamp(MaxGeneratedCells,1,100000))
    { bBudgetExceeded=true;return; }
    for (int32 Face=0;Face<SideCount;++Face)
    {
        const double Angle=-UE_HALF_PI+(Face*2-1)*UE_PI/SideCount;
        const FVector A(FMath::Cos(Angle),FMath::Sin(Angle),0),B(FMath::Cos(Angle+UE_TWO_PI/SideCount),FMath::Sin(Angle+UE_TWO_PI/SideCount),0);
        for (int32 C=0;C<Columns;++C) for (int32 R=0;R<Radial;++R) for (int32 H=0;H<Rows;++H)
        {
            const FVector L=FMath::Lerp(A,B,double(C)/Columns),U=FMath::Lerp(A,B,double(C+1)/Columns);
            const double R0=FMath::Lerp(InnerRadius,Radius,double(R)/Radial),R1=FMath::Lerp(InnerRadius,Radius,double(R+1)/Radial);
            const FVector Lo(0,0,FMath::Lerp(Z0,Z1,double(H)/Rows)),Hi(0,0,FMath::Lerp(Z0,Z1,double(H+1)/Rows));
            const FVector V[8]={L*R0+Lo,L*R1+Lo,U*R1+Lo,U*R0+Lo,L*R0+Hi,L*R1+Hi,U*R1+Hi,U*R0+Hi};
            AddPiece(V,Material);
        }
    }
}
void AChunkedDestructiblePolygonTower::BuildWall(int32 Face,int32 Tier,double Radius,double Z)
{
    const double Angle=-UE_HALF_PI+Face*UE_TWO_PI/SideCount;
    const FVector N(FMath::Cos(Angle),FMath::Sin(Angle),0),T(-N.Y,N.X,0);
    const double Apothem=Radius*FMath::Cos(UE_PI/SideCount),Tan=FMath::Tan(UE_PI/SideCount);
    const double TopApothem=Apothem*(1-WallBatter),ClearHalf=(TopApothem-WallThickness)*Tan;
    bool bDoor=false;
    for (int32 I=0;I<EntranceCount;++I) bDoor|=Tier==0 && Face==FMath::FloorToInt(double(I)*SideCount/EntranceCount);
    const int32 Count=bDoor?1:WindowsPerSide;
    const double ApertureWidth=FMath::Min(bDoor?double(EntranceWidth):double(WindowWidth),Count>0?ClearHalf*1.6/Count:0);
    const double Sill=bDoor?0:FMath::Min(double(WindowSillHeight),TierHeight*.4);
    const double H=FMath::Min(bDoor?double(EntranceHeight):double(WindowHeight),TierHeight-Sill-20);
    TArray<FBox2D> Openings;
    TArray<double> Us={-1,1},Zs={Z,Z+TierHeight};
    for (int32 I=0;I<Count;++I)
    {
        const double U=Count>1?1.5*((I+.5)/Count-.5):0;
        const double Half=ApertureWidth/(2*ClearHalf);
        Openings.Add(FBox2D({U-Half,Z+Sill},{U+Half,Z+Sill+H}));
        Us.AddUnique(U-Half);Us.AddUnique(U+Half);Zs.AddUnique(Z+Sill);Zs.AddUnique(Z+Sill+H);++GeneratedOpeningCount;
    }
    const int32 Cols=FMath::Max(1,FMath::CeilToInt(Apothem*Tan*2/PieceSize)),Rows=FMath::Max(1,FMath::CeilToInt(TierHeight/PieceSize));
    for (int32 I=1;I<Cols;++I) Us.AddUnique(-1+2.0*I/Cols);
    for (int32 I=1;I<Rows;++I) Zs.AddUnique(Z+double(TierHeight)*I/Rows);
    Us.Sort();Zs.Sort();
    for (int32 R=0;R<Zs.Num()-1 && !bBudgetExceeded;++R) for (int32 C=0;C<Us.Num()-1 && !bBudgetExceeded;++C)
    {
        const FVector2D Mid((Us[C]+Us[C+1])*.5,(Zs[R]+Zs[R+1])*.5);
        bool Open=false;for (const auto& O:Openings) Open|=O.IsInside(Mid);if (Open) continue;
        FVector V[8];
        for (int32 Layer=0;Layer<2;++Layer)
        {
            const double Height=Zs[R+Layer],Outer=FMath::Lerp(Apothem,TopApothem,(Height-Z)/TierHeight),Inner=Outer-WallThickness;
            auto Point=[&](double D,double U) {return N*D+T*(D*Tan*U)+FVector(0,0,Height);};
            V[Layer*4]=Point(Inner,Us[C]);V[Layer*4+1]=Point(Outer,Us[C]);
            V[Layer*4+2]=Point(Outer,Us[C+1]);V[Layer*4+3]=Point(Inner,Us[C+1]);
        }
        AddPiece(V,0);
    }
    if (bDoor && bEntranceStairs && FoundationHeight>0)
    {
        const double Base=(BaseRadius+FoundationOverhang)*FMath::Cos(UE_PI/SideCount);
        const double Half=FMath::Min(double(EntranceWidth)*.7,ClearHalf*.8);
        for (int32 I=0;I<StairCount;++I)
        {
            const double Outer=Base+(StairCount-I)*StairTreadDepth,Inner=Outer-StairTreadDepth;
            const double High=FoundationHeight*(I+1)/StairCount;
            const int32 ColsN=FMath::Max(1,FMath::CeilToInt(2*Half/PieceSize));
            for (int32 C=0;C<ColsN;++C)
            {
                const double L=FMath::Lerp(-Half,Half,double(C)/ColsN),U=FMath::Lerp(-Half,Half,double(C+1)/ColsN);
                const FVector V[8]={N*Inner+T*L,N*Outer+T*L,N*Outer+T*U,N*Inner+T*U,
                    N*Inner+T*L+FVector(0,0,High),N*Outer+T*L+FVector(0,0,High),N*Outer+T*U+FVector(0,0,High),N*Inner+T*U+FVector(0,0,High)};
                AddPiece(V,1);
            }
        }
    }
}
bool AChunkedDestructiblePolygonTower::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max){V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min;};
    SideCount=FMath::Clamp(SideCount,3,16);TierCount=FMath::Clamp(TierCount,1,20);
    Clamp(BaseRadius,300,10000);Clamp(TierHeight,150,1500);Clamp(TopScale,.1f,1);Clamp(WallBatter,0,.12f);
    Clamp(PieceSize,30,500);Clamp(WallThickness,10,200);Clamp(PieceGap,0,5);
    const double Cos=FMath::Cos(UE_PI/SideCount),TopRadius=BaseRadius*(TierCount>1?TopScale:1);
    WallThickness=FMath::Min(double(WallThickness),TopRadius*Cos*(1-WallBatter)*.25);
    WindowsPerSide=FMath::Clamp(WindowsPerSide,0,8);EntranceCount=FMath::Clamp(EntranceCount,0,SideCount);
    Clamp(WindowWidth,30,500);Clamp(WindowHeight,40,1000);Clamp(WindowSillHeight,0,500);
    Clamp(EntranceWidth,80,800);Clamp(EntranceHeight,100,1200);StairCount=FMath::Clamp(StairCount,1,32);
    Clamp(StairTreadDepth,20,120);Clamp(TerraceOverhang,0,250);Clamp(FloorThickness,10,150);Clamp(CorniceHeight,0,100);
    Clamp(FoundationHeight,0,600);Clamp(FoundationOverhang,0,400);
    const double Fitted=ZigguratTraversal::FitOpening(Traversal,2*(TopRadius*Cos*(1-WallBatter)-WallThickness));
    const double Inner=Traversal.bFloorOpenings?Fitted*.5/Cos:0;
    TArray<FPiece> Previous=MoveTemp(Pieces);const int32 OldOpenings=GeneratedOpeningCount;
    GeneratedOpeningCount=0;bBudgetExceeded=false;GenerationWarning.Reset();
    TileRing(BaseRadius+FoundationOverhang,0,0,FoundationHeight,2);
    double Z=FoundationHeight;
    for (int32 Tier=0;Tier<TierCount && !bBudgetExceeded;++Tier)
    {
        const double Scale=FMath::Lerp(1.0,double(TopScale),TierCount>1?double(Tier)/(TierCount-1):0.0),R=BaseRadius*Scale;
        for (int32 Face=0;Face<SideCount;++Face) BuildWall(Face,Tier,R,Z);
        Z+=TierHeight;
        const double Roof=R*(1-WallBatter)+TerraceOverhang;
        TileRing(Roof*.99,Inner,Z,Z+CorniceHeight,1);Z+=CorniceHeight;
        TileRing(Roof,Inner,Z,Z+FloorThickness,2);Z+=FloorThickness;
    }
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);GeneratedOpeningCount=OldOpenings;
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce dimensions/tier count. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);return false;
    }
    GeneratedHeight=Z;GeneratedPieceCount=Pieces.Num();EffectiveOpeningWidth=Traversal.bFloorOpenings?Fitted:0;
    FloorMaterials={StoneMaterial,CorniceMaterial,FloorMaterial};
    for (auto& M:FloorMaterials) if (!M) M=UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount={Pieces.Num(),1};ChunkCount={FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1};
    EffectiveCellDimensions=FVector(PieceSize,PieceSize,WallThickness);CellDimensions=EffectiveCellDimensions;
    CellSpacing=FVector2D(PieceGap);EffectiveGridSize=FVector2D(2*(BaseRadius+FoundationOverhang));GridLocalMin=-EffectiveGridSize*.5;AreaSize=EffectiveGridSize;
    ZigguratTraversal::UpdatePad(GroundLaunchPad,Traversal,Fitted,FoundationHeight,GeneratedHeight);
    return true;
}

bool AChunkedDestructiblePolygonTower::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructiblePolygonTower::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Bounds.GetCenter():FVector::ZeroVector; }
int32 AChunkedDestructiblePolygonTower::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Material:0; }
FTransform AChunkedDestructiblePolygonTower::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructiblePolygonTower::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const
{
    const FVector* V=Piece.V;
    // Counter-clockwise corners along each outward normal; AppendMeshQuad handles UE winding.
    constexpr int32 Faces[6][4]={{4,5,6,7},{3,2,1,0},{0,1,5,4},{2,3,7,6},{1,2,6,5},{3,0,4,7}};
    for (const auto& F:Faces)
    {
        const FVector& A=V[F[0]];const FVector& B=V[F[1]];const FVector& C=V[F[2]];const FVector& D=V[F[3]];
        const FVector N=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        const FVector M=FVector::CrossProduct(C-A,D-A).GetSafeNormal();
        if (!N.IsNearlyZero()) AppendMeshTriangle(Mesh,A,B,C,FVector3f(N),Material,FVector2f(0,0),FVector2f((B-A).Length()/UVWorldUnits,0),FVector2f((C-A).Length()/UVWorldUnits,(C-B).Length()/UVWorldUnits));
        if (!M.IsNearlyZero()) AppendMeshTriangle(Mesh,A,C,D,FVector3f(M),Material,FVector2f(0,0),FVector2f((C-D).Length()/UVWorldUnits,(D-A).Length()/UVWorldUnits),FVector2f(0,(D-A).Length()/UVWorldUnits));
    }
}
void AChunkedDestructiblePolygonTower::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
}
void AChunkedDestructiblePolygonTower::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
{
    Collision.EmptyElements();if (!bGenerateCollision) return;
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            const FPiece& P=Pieces[I];
            FKConvexElem& Convex=Collision.ConvexElems.Emplace_GetRef();
            for (const FVector& Point:P.V) Convex.VertexData.AddUnique(Point);
            Convex.UpdateElemBox();
        }
}
bool AChunkedDestructiblePolygonTower::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
{
    Cell=FIntPoint::NoneValue;double Best=TNumericLimits<double>::Max();
    for (int32 I=0;I<Pieces.Num();++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists && Pieces[I].Contains(P,Tolerance))
        {
            const double Distance=FVector::DistSquared(P,Pieces[I].Bounds.GetCenter());
            if (Distance<Best) { Best=Distance;Cell=FIntPoint(I,0); }
        }
    return Cell!=FIntPoint::NoneValue;
}
bool AChunkedDestructiblePolygonTower::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructiblePolygonTower::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructiblePolygonTower::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
    TFunctionRef<void(int32,float)> Function)
{
    if (Radius<=0) return 0;
    const FVector Local=GetActorTransform().InverseTransformPosition(P),Scale=GetActorScale3D().GetAbs();
    int32 Count=0;
    for (int32 I=0;I<Pieces.Num();++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            const FVector D=((Pieces[I].Bounds.GetCenter()-Local)*Scale).GetAbs();
            const double Distance=Shape==EChunkedFloorBlastShape::Square?D.GetMax():
                (Shape==EChunkedFloorBlastShape::Diamond?D.X+D.Y+D.Z:D.Length());
            if (Distance<=Radius) { Function(I,float(Distance));++Count; }
        }
    return Count;
}
void AChunkedDestructiblePolygonTower::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
