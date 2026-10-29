#include "ChunkedDestructibleRoundTemple.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

AChunkedDestructibleRoundTemple::AChunkedDestructibleRoundTemple()
{
    FloorPattern = EChunkedFloorPattern::Solid;
    ChunkSideLength = 96;
    MaxGeneratedCells = 30000;
    DefaultCellHealth = 25;
    bReactToMusic = false;
    bGenerateBottomFaces = true;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stone(TEXT("/Game/Structures/Destructible/RoundTemple/Materials/MI_RoundTempleStone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Band(TEXT("/Game/Structures/Destructible/RoundTemple/Materials/MI_RoundTempleBand"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Roof(TEXT("/Game/Structures/Destructible/RoundTemple/Materials/MI_RoundTempleRoof"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Step(TEXT("/Game/Structures/Destructible/RoundTemple/Materials/MI_RoundTempleStep"));
    StoneMaterial=Stone.Get(); BandMaterial=Band.Get(); RoofMaterial=Roof.Get(); StepMaterial=Step.Get();
}

void AChunkedDestructibleRoundTemple::RebuildTemple() { GenerateFloor(false); }

void AChunkedDestructibleRoundTemple::Wedge(double A,double B,double R0,double R1,double Inner0,double Inner1,double Z0,double Z1,int32 Material)
{
    if (Z1-Z0<.01 || B-A<1.e-6 || FMath::Max(R0,R1)<.01) return;
    const FVector2D L(FMath::Cos(A),FMath::Sin(A)),U(FMath::Cos(B),FMath::Sin(B));
    const FVector V[8]={FVector(L*Inner0,Z0),FVector(L*R0,Z0),FVector(U*R0,Z0),FVector(U*Inner0,Z0),
        FVector(L*Inner1,Z1),FVector(L*R1,Z1),FVector(U*R1,Z1),FVector(U*Inner1,Z1)};
    AddPiece(V,Material);
}

void AChunkedDestructibleRoundTemple::Ring(double Outer,double Inner,double Z0,double Z1,int32 Material,double Arc)
{
    if (Outer-Inner<.01 || Z1-Z0<.01) return;
    const int32 Sides=FMath::Max(2,FMath::CeilToInt(CircleSegments*Arc/UE_DOUBLE_TWO_PI));
    const int32 Radial=FMath::Max(1,FMath::CeilToInt((Outer-Inner)/PieceSize));
    const int32 Rows=FMath::Max(1,FMath::CeilToInt((Z1-Z0)/PieceSize));
    const double Start=-UE_DOUBLE_HALF_PI-Arc*.5;
    for (int32 S=0;S<Sides && !bBudgetExceeded;++S)
    for (int32 R=0;R<Radial && !bBudgetExceeded;++R)
    for (int32 H=0;H<Rows && !bBudgetExceeded;++H)
    {
        const double IR=FMath::Lerp(Inner,Outer,double(R)/Radial),OR=FMath::Lerp(Inner,Outer,double(R+1)/Radial);
        Wedge(Start+S*Arc/Sides,Start+(S+1)*Arc/Sides,OR,OR,IR,IR,
            FMath::Lerp(Z0,Z1,double(H)/Rows),FMath::Lerp(Z0,Z1,double(H+1)/Rows),Material);
    }
}

void AChunkedDestructibleRoundTemple::Facade(double Angle,double Radius,double Left,double Right,
    double BottomLeft,double BottomRight,double Top,double Thickness,int32 Material)
{
    if (Right-Left<.01 || Top-FMath::Max(BottomLeft,BottomRight)<.01) return;
    const FVector N(FMath::Cos(Angle),FMath::Sin(Angle),0),T(-N.Y,N.X,0);
    const int32 Across=FMath::Max(1,FMath::CeilToInt((Right-Left)/PieceSize));
    const int32 Rows=FMath::Max(1,FMath::CeilToInt((Top-FMath::Min(BottomLeft,BottomRight))/PieceSize));
    for (int32 X=0;X<Across && !bBudgetExceeded;++X)
    for (int32 Z=0;Z<Rows && !bBudgetExceeded;++Z)
    {
        const double L=FMath::Lerp(Left,Right,double(X)/Across),R=FMath::Lerp(Left,Right,double(X+1)/Across);
        const double BL=FMath::Lerp(BottomLeft,BottomRight,double(X)/Across),BR=FMath::Lerp(BottomLeft,BottomRight,double(X+1)/Across);
        const double L0=FMath::Lerp(BL,Top,double(Z)/Rows),R0=FMath::Lerp(BR,Top,double(Z)/Rows);
        const double L1=FMath::Lerp(BL,Top,double(Z+1)/Rows),R1=FMath::Lerp(BR,Top,double(Z+1)/Rows);
        FVector V[8]={{L,0,L0},{R,0,R0},{R,Thickness,R0},{L,Thickness,L0},
            {L,0,L1},{R,0,R1},{R,Thickness,R1},{L,Thickness,L1}};
        // Tangential X, inward Y, up Z form a right-handed basis; all cells are convex prisms.
        for (FVector& P:V) P=T*P.X+N*(Radius-P.Y)+FVector(0,0,P.Z);
        AddPiece(V,Material);
    }
}

void AChunkedDestructibleRoundTemple::Arcade(double Radius,double Z,double Height,int32 Bays,bool bEntrance)
{
    const double Half=Radius*FMath::Sin(UE_DOUBLE_PI/Bays),Apothem=Radius*FMath::Cos(UE_DOUBLE_PI/Bays);
    const double Opening=FMath::Min(Half*ArchWidthRatio,Height*.34),Crown=Z+Height*.86,Spring=Crown-Opening;
    for (int32 Bay=0;Bay<Bays && !bBudgetExceeded;++Bay)
    {
        const double Angle=-UE_DOUBLE_HALF_PI+Bay*UE_DOUBLE_TWO_PI/Bays;
        Facade(Angle,Apothem,-Half,-Opening,Z,Z,Z+Height,WallThickness,0);
        Facade(Angle,Apothem,Opening,Half,Z,Z,Z+Height,WallThickness,0);
        if (bEntrance && Bay==0)
        {
            Facade(Angle,Apothem,-Opening,Opening,Crown,Crown,Z+Height,WallThickness,0);
            continue;
        }
        // A sampled semicircle creates real openings and simple rounded arch silhouettes.
        for (int32 S=0;S<ArchSegments && !bBudgetExceeded;++S)
        {
            const double A=UE_DOUBLE_PI-S*UE_DOUBLE_PI/ArchSegments,B=UE_DOUBLE_PI-(S+1)*UE_DOUBLE_PI/ArchSegments;
            Facade(Angle,Apothem,Opening*FMath::Cos(A),Opening*FMath::Cos(B),
                Spring+Opening*FMath::Sin(A),Spring+Opening*FMath::Sin(B),Z+Height,WallThickness,0);
        }
    }
}

bool AChunkedDestructibleRoundTemple::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max) {V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min;};
    Clamp(BodyRadius,400,3000);Clamp(LowerFloorHeight,220,1200);Clamp(UpperRadiusRatio,.65f,1);
    Clamp(UpperFloorHeight,160,1000);Clamp(WallThickness,20,100);Clamp(FloorBandHeight,15,80);
    LowerArchCount=FMath::Clamp(LowerArchCount,8,40);UpperArchCount=FMath::Clamp(UpperArchCount,8,40);
    Clamp(ArchWidthRatio,.35f,.8f);ArchSegments=FMath::Clamp(ArchSegments,4,20);
    Clamp(GalleryHeight,80,500);Clamp(GalleryPostWidth,12,60);
    Clamp(RoofHeight,300,3000);Clamp(RoofOverhang,20,300);Clamp(RoofThickness,10,100);RoofCourses=FMath::Clamp(RoofCourses,4,40);
    Clamp(PlatformHeight,40,600);Clamp(PlatformMargin,20,200);StepCount=FMath::Clamp(StepCount,3,30);
    Clamp(StepDepth,25,100);Clamp(StairArcDegrees,30,240);
    CircleSegments=FMath::Clamp(CircleSegments,32,160);Clamp(PieceSize,50,300);Clamp(PieceGap,0,3);

    TArray<FPiece> Previous=MoveTemp(Pieces);
    bBudgetExceeded=false;GenerationWarning.Reset();
    const double Base=BodyRadius+PlatformMargin,Upper=BodyRadius*UpperRadiusRatio,Roof=Upper+RoofOverhang;
    // Hollow plinth and a real walkable landing, without filling the whole building with cells.
    Ring(Base,Base-WallThickness,0,PlatformHeight-FloorBandHeight,3);
    Ring(Base,0,PlatformHeight-FloorBandHeight,PlatformHeight,3);
    const int32 BeforeSteps=Pieces.Num();
    if (bFrontSteps)
        for (int32 S=0;S<StepCount && !bBudgetExceeded;++S)
            Ring(Base+(S+1)*StepDepth,Base+S*StepDepth,0,PlatformHeight*(StepCount-S)/(StepCount+1.),3,FMath::DegreesToRadians(double(StairArcDegrees)));
    const int32 Steps=Pieces.Num()-BeforeSteps;

    double Z=PlatformHeight;
    Arcade(BodyRadius,Z,LowerFloorHeight,LowerArchCount,bRectangularEntrance);Z+=LowerFloorHeight;
    Ring(BodyRadius+18,0,Z,Z+FloorBandHeight,1);Z+=FloorBandHeight;
    Arcade(Upper,Z,UpperFloorHeight,UpperArchCount,false);Z+=UpperFloorHeight;
    Ring(Upper+18,0,Z,Z+FloorBandHeight,1);Z+=FloorBandHeight;
    for (int32 S=0;S<UpperArchCount && !bBudgetExceeded;++S)
        Facade(-UE_DOUBLE_HALF_PI+(S+.5)*UE_DOUBLE_TWO_PI/UpperArchCount,Upper-WallThickness*.5,
            -GalleryPostWidth*.5,GalleryPostWidth*.5,Z,Z,Z+GalleryHeight,GalleryPostWidth,1);
    Z+=GalleryHeight;
    Ring(Upper+18,Upper-WallThickness,Z,Z+FloorBandHeight*.5,1);Z+=FloorBandHeight*.5;

    const int32 BeforeRoof=Pieces.Num();
    // A plain eave soffit connects the overhanging cone to the gallery supports.
    Ring(Roof,Upper-WallThickness,Z-FloorBandHeight*.5,Z,2);
    for (int32 Row=0;Row<RoofCourses && !bBudgetExceeded;++Row)
    {
        const double R0=Roof*(1-double(Row)/RoofCourses),R1=Roof*(1-double(Row+1)/RoofCourses);
        for (int32 S=0;S<CircleSegments && !bBudgetExceeded;++S)
            Wedge(S*UE_DOUBLE_TWO_PI/CircleSegments,(S+1)*UE_DOUBLE_TWO_PI/CircleSegments,
                R0,R1,FMath::Max(0.,R0-RoofThickness),FMath::Max(0.,R1-RoofThickness),
                Z+RoofHeight*Row/RoofCourses,Z+RoofHeight*(Row+1)/RoofCourses,2);
    }
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce circle/arch/roof segments. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);return false;
    }
    GeneratedPieceCount=Pieces.Num();GeneratedStepPieces=Steps;GeneratedRoofPieces=Pieces.Num()-BeforeRoof;GeneratedHeight=Z+RoofHeight;
    FloorMaterials={StoneMaterial,BandMaterial,RoofMaterial,StepMaterial};
    for (TObjectPtr<UMaterialInterface>& M:FloorMaterials) if (!M) M=UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount={Pieces.Num(),1};ChunkCount={FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1};
    EffectiveCellDimensions={PieceSize,PieceSize,WallThickness};CellDimensions=EffectiveCellDimensions;CellSpacing=FVector2D(PieceGap);
    FBox Bounds(ForceInit);for (const FPiece& P:Pieces) Bounds+=P.Bounds;
    EffectiveGridSize=FVector2D(Bounds.GetSize());GridLocalMin=FVector2D(Bounds.Min);AreaSize=EffectiveGridSize;
    return true;
}

#if WITH_EDITOR
bool AChunkedDestructibleRoundTemple::CanEditChange(const FProperty* Property) const
{
    if (Property)
        for (const TCHAR* Derived : {TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
            TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
            TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
            if (Property->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(Property);
}
#endif

bool AChunkedDestructibleRoundTemple::FPiece::Contains(FVector P,double Tolerance) const
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
void AChunkedDestructibleRoundTemple::AddPiece(const FVector (&V)[8],int32 Material)
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
bool AChunkedDestructibleRoundTemple::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleRoundTemple::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Bounds.GetCenter():FVector::ZeroVector; }
int32 AChunkedDestructibleRoundTemple::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Material:0; }
FTransform AChunkedDestructibleRoundTemple::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructibleRoundTemple::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const
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
void AChunkedDestructibleRoundTemple::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
}
void AChunkedDestructibleRoundTemple::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
{
    Collision.EmptyElements();if (!bGenerateCollision) return;
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            const FPiece& P=Pieces[I];
            FKConvexElem& Convex=Collision.ConvexElems.Emplace_GetRef();
            // Cook near the origin: Chaos hull tolerances otherwise lose thin, distant wedges.
            const FVector Center=P.Bounds.GetCenter();
            for (const FVector& Point:P.V) Convex.VertexData.AddUnique(Point-Center);
            Convex.SetTransform(FTransform(Center));
            Convex.UpdateElemBox();
        }
}
bool AChunkedDestructibleRoundTemple::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
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
bool AChunkedDestructibleRoundTemple::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructibleRoundTemple::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructibleRoundTemple::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
    TFunctionRef<void(int32,float)> Function)
{
    if (Radius<=0) return 0;
    const FVector Local=GetActorTransform().InverseTransformPosition(P),Scale=GetActorScale3D().GetAbs();
    int32 Count=0;
    for (int32 I=0;I<Pieces.Num();++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            const FVector Point = Shape == EChunkedFloorBlastShape::Sphere
                ? Pieces[I].Bounds.GetClosestPointTo(Local) : Pieces[I].Bounds.GetCenter();
            const FVector D=((Point-Local)*Scale).GetAbs();
            const double Distance=Shape==EChunkedFloorBlastShape::Square?D.GetMax():
                (Shape==EChunkedFloorBlastShape::Diamond?D.X+D.Y+D.Z:D.Length());
            if (Distance<=Radius) { Function(I,float(Distance));++Count; }
        }
    return Count;
}
void AChunkedDestructibleRoundTemple::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
