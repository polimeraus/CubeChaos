#include "ChunkedDestructibleGlobe.h"
#include "Components/ChildActorComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"
#include "ZigguratTraversal.h"

using namespace UE::Geometry;
namespace
{
    // Counter-clockwise corners along outward normals. Shared mesh helpers reverse UE winding.
    constexpr int32 GlobeFaces[6][4]={{4,5,6,7},{3,2,1,0},{0,1,5,4},{2,3,7,6},{1,2,6,5},{3,0,4,7}};
}

AChunkedDestructibleGlobe::AChunkedDestructibleGlobe()
{
    FloorPattern=EChunkedFloorPattern::Solid;ChunkSideLength=96;MaxGeneratedCells=50000;
    bGenerateBottomFaces=true;bReactToMusic=false;DefaultCellHealth=25;
    GroundLaunchPad=CreateDefaultSubobject<UChildActorComponent>(TEXT("GroundLaunchPad"));
    GroundLaunchPad->SetupAttachment(GetRootComponent());
    FZigguratTraversalSettings Pad;
    ZigguratTraversal::InitializeMaterials(Pad);LaunchPadMaterial=Pad.RingsMaterial;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Panel(TEXT("/Game/Structures/Destructible/Globe/Materials/MI_GlobePanel"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Edge(TEXT("/Game/Structures/Destructible/Globe/Materials/MI_GlobeEdge"));
    PanelMaterial=Panel.Get();EdgeMaterial=Edge.Get();
}

void AChunkedDestructibleGlobe::RebuildGlobe() { GenerateFloor(false); }

void AChunkedDestructibleGlobe::PostLoad()
{
    Super::PostLoad();
    MigrateLegacyShapeSettings();
}

void AChunkedDestructibleGlobe::MigrateLegacyShapeSettings()
{
    if (ShaftRadius_DEPRECATED>=0)
    {
        ShaftWidth=2*ShaftRadius_DEPRECATED;
        ShaftRadius_DEPRECATED=-1;
    }
    if (PlateThickness_DEPRECATED>=0)
    {
        MeridianThickness=ParallelThickness=PlateThickness_DEPRECATED;
        PlateThickness_DEPRECATED=-1;
    }
}

#if WITH_EDITOR
bool AChunkedDestructibleGlobe::CanEditChange(const FProperty* P) const
{
    if (P) for (const TCHAR* Derived:{TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
        TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
        TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
        if (P->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(P);
}
#endif

bool AChunkedDestructibleGlobe::FPiece::Contains(FVector P,double Tolerance) const
{
    if (!Bounds.ExpandBy(Tolerance).IsInsideOrOn(P)) return false;
    for (const auto& F:GlobeFaces)
    {
        const FVector N=FVector::CrossProduct(V[F[1]]-V[F[0]],V[F[3]]-V[F[0]]).GetSafeNormal();
        if (FVector::DotProduct(P-V[F[0]],N)>Tolerance) return false;
    }
    return true;
}

void AChunkedDestructibleGlobe::AddPiece(const FVector (&V)[8],bool bShelf)
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

void AChunkedDestructibleGlobe::BuildMeridians(const TArray<double>& Heights,const TArray<FVector2D>& Shelves)
{
    const double Half=MeridianThickness*.5,ShaftRadius=ShaftWidth*.5;
    auto RadiusAt=[&](double Z)
    {
        return FMath::Sqrt(FMath::Max(0.0,FMath::Square(double(SphereRadius))-FMath::Square(Z-SphereRadius)-Half*Half));
    };
    for (int32 M=0;M<MeridianCount && !bBudgetExceeded;++M)
    {
        const double Angle=UE_TWO_PI*M/MeridianCount;
        const FVector N(FMath::Cos(Angle),FMath::Sin(Angle),0),T(-N.Y,N.X,0);
        for (int32 H=0;H<Heights.Num()-1 && !bBudgetExceeded;++H)
        {
            const double Z0=Heights[H],Z1=Heights[H+1],Mid=(Z0+Z1)*.5;
            if (Z1-Z0<.001) continue;
            bool bInsideShelf=false;
            for (const auto& S:Shelves) bInsideShelf|=Mid>S.X && Mid<S.Y;
            if (bInsideShelf) continue; // Shelves own intersections: no hidden overlapping colliders.
            const double R0=RadiusAt(Z0),R1=RadiusAt(Z1);
            const int32 Columns=FMath::Max(1,FMath::CeilToInt((FMath::Max(R0,R1)-ShaftRadius)/PieceSize));
            for (int32 C=0;C<Columns && !bBudgetExceeded;++C)
            {
                FVector V[8];
                for (int32 Layer=0;Layer<2;++Layer)
                {
                    const double Outer=Layer?R1:R0,Z=Layer?Z1:Z0;
                    const double Inner=FMath::Lerp(double(ShaftRadius),Outer,double(C)/Columns);
                    const double Far=FMath::Lerp(double(ShaftRadius),Outer,double(C+1)/Columns);
                    auto Point=[&](double R,double Side){return N*R+T*Side+FVector(0,0,Z);};
                    V[Layer*4]=Point(Inner,-Half);V[Layer*4+1]=Point(Far,-Half);
                    V[Layer*4+2]=Point(Far,Half);V[Layer*4+3]=Point(Inner,Half);
                }
                AddPiece(V,false);
            }
        }
    }
}

void AChunkedDestructibleGlobe::BuildParallel(double Z0,double Z1)
{
    const double ShaftRadius=ShaftWidth*.5;
    const double ZDistance=FMath::Max(FMath::Abs(Z0-SphereRadius),FMath::Abs(Z1-SphereRadius));
    const double Outer=FMath::Sqrt(FMath::Square(double(SphereRadius))-ZDistance*ZDistance);
    // Whole subdivisions per meridian give a smooth ring independent of the number of ribs.
    const int32 Segments=MeridianCount*FMath::Max(4,FMath::CeilToInt(UE_TWO_PI*Outer/(PieceSize*MeridianCount)));
    const double Delta=UE_TWO_PI/Segments;
    // Circumscribe the clear bore; an inscribed polygon would intrude into the shaft at its chords.
    const double Inner=ShaftRadius/FMath::Cos(Delta*.5);
    const int32 Rows=FMath::Max(1,FMath::CeilToInt((Outer-Inner)/PieceSize));
    for (int32 S=0;S<Segments && !bBudgetExceeded;++S)
    {
        const FVector A(FMath::Cos(S*Delta),FMath::Sin(S*Delta),0);
        const FVector B(FMath::Cos((S+1)*Delta),FMath::Sin((S+1)*Delta),0);
        for (int32 R=0;R<Rows && !bBudgetExceeded;++R)
        {
            const double Lo=FMath::Lerp(Inner,Outer,double(R)/Rows),Hi=FMath::Lerp(Inner,Outer,double(R+1)/Rows);
            const FVector Low(0,0,Z0),High(0,0,Z1);
            const FVector V[8]={A*Lo+Low,A*Hi+Low,B*Hi+Low,B*Lo+Low,
                A*Lo+High,A*Hi+High,B*Hi+High,B*Lo+High};
            AddPiece(V,true);
        }
    }
}

bool AChunkedDestructibleGlobe::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max){V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min;};
    MeridianCount=FMath::Clamp(MeridianCount,3,32);ParallelCount=FMath::Clamp(ParallelCount,0,24);
    CurveSegments=FMath::Clamp(CurveSegments,16,128);
    Clamp(SphereRadius,300,10000);Clamp(ShaftWidth,150,FMath::Min(6000.f,SphereRadius*1.2f));
    const double ShaftRadius=ShaftWidth*.5;
    Clamp(MeridianThickness,4,FMath::Min(100.f,float(ShaftRadius*.25)));
    // Dense meridians must still have separate inner edges at the bore.
    MeridianThickness=FMath::Min(double(MeridianThickness),1.6*ShaftRadius*FMath::Tan(UE_PI/MeridianCount));
    Clamp(PieceSize,60,600);Clamp(PieceGap,0,5);
    // Also enforce enough air between adjacent shelves at the smallest supported radius.
    Clamp(ParallelThickness,4,FMath::Min(100.f,2*SphereRadius/(ParallelCount+1)*.3f));
    const double EndRadius=ShaftRadius+1;
    const double Polar=FMath::Asin(FMath::Sqrt(EndRadius*EndRadius+FMath::Square(MeridianThickness*.5))/SphereRadius);
    const double Bottom=SphereRadius*(1-FMath::Cos(Polar)),Top=2*SphereRadius-Bottom;
    TArray<double> Heights;
    TArray<FVector2D> Shelves;
    for (int32 H=0;H<=CurveSegments;++H)
        Heights.Add(SphereRadius*(1-FMath::Cos(FMath::Lerp(Polar,UE_PI-Polar,double(H)/CurveSegments))));
    for (int32 P=0;P<ParallelCount;++P)
    {
        const double Z=FMath::Lerp(Bottom,Top,double(P+1)/(ParallelCount+1));
        Shelves.Add(FVector2D(Z-ParallelThickness*.5,Z+ParallelThickness*.5));
        Heights.Add(Shelves.Last().X);Heights.Add(Shelves.Last().Y);
    }
    Heights.Sort();
    TArray<FPiece> Previous=MoveTemp(Pieces);
    GenerationWarning.Reset();bBudgetExceeded=false;
    BuildMeridians(Heights,Shelves);
    const int32 MeridianPieces=Pieces.Num();
    for (const auto& Shelf:Shelves) BuildParallel(Shelf.X,Shelf.Y);
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce radius/counts. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);return false;
    }
    GeneratedHeight=Top;GeneratedPieceCount=Pieces.Num();GeneratedMeridianPieces=MeridianPieces;
    GeneratedParallelPieces=Pieces.Num()-MeridianPieces;
    FloorMaterials={PanelMaterial,EdgeMaterial};
    for (auto& M:FloorMaterials) if (!M) M=UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount={Pieces.Num(),1};ChunkCount={FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1};
    EffectiveCellDimensions=FVector(PieceSize,PieceSize,ParallelThickness);CellDimensions=EffectiveCellDimensions;
    CellSpacing=FVector2D(PieceGap);EffectiveGridSize=FVector2D(2*SphereRadius);GridLocalMin=-EffectiveGridSize*.5;AreaSize=EffectiveGridSize;

    FZigguratTraversalSettings Pad;
    Pad.bGroundLaunchPad=bEnableLaunchPad;Pad.PadWidth=PadDiameter;
    Pad.bAutoLaunchHeight=bAutoLaunchHeight;Pad.HeightAboveRoof=HeightAboveTop;Pad.LaunchUpSpeed=LaunchUpSpeed;
    Pad.bAffectNPCs=bLaunchNPCs;Pad.Pattern=ELaunchPadPattern::ConvergingRings;
    Pad.Color=LaunchPadColor;Pad.RingsMaterial=LaunchPadMaterial;
    ZigguratTraversal::UpdatePad(GroundLaunchPad,Pad,ShaftWidth,0,GeneratedHeight);
    if (auto* Launch=Cast<ALaunchPad>(GroundLaunchPad->GetChildActor()))
    {
        Launch->Shape=ELaunchPadShape::Circle;Launch->RefreshPad();
    }
    return true;
}

bool AChunkedDestructibleGlobe::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleGlobe::GetCellLocalCenter(FIntPoint Cell) const
{
    if (!ShouldCellExistInGeneratedShape(Cell)) return FVector::ZeroVector;
    FVector Center=FVector::ZeroVector;for (const FVector& V:Pieces[Cell.X].V) Center+=V;
    return Center/8;
}
int32 AChunkedDestructibleGlobe::EvaluatePatternMaterial(FIntPoint Cell) const { return 0; }
FTransform AChunkedDestructibleGlobe::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructibleGlobe::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const
{
    const FVector* V=Piece.V;
    const double UVScale=FMath::Max(1.f,UVWorldUnits);
    for (int32 Face=0;Face<6;++Face)
    {
        const auto& F=GlobeFaces[Face];
        const FVector& A=V[F[0]];const FVector& B=V[F[1]];const FVector& C=V[F[2]];const FVector& D=V[F[3]];
        const FVector N=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        const FVector M=FVector::CrossProduct(C-A,D-A).GetSafeNormal();
        const int32 FaceMaterial=(Piece.bShelf?Face>=2:Face<2 || Face>=4)?1:Material;
        if (!N.IsNearlyZero()) AppendMeshTriangle(Mesh,A,B,C,FVector3f(N),FaceMaterial,FVector2f(0,0),FVector2f((B-A).Length()/UVScale,0),FVector2f((C-A).Length()/UVScale,(C-B).Length()/UVScale));
        if (!M.IsNearlyZero()) AppendMeshTriangle(Mesh,A,C,D,FVector3f(M),FaceMaterial,FVector2f(0,0),FVector2f((C-D).Length()/UVScale,(D-A).Length()/UVScale),FVector2f(0,(D-A).Length()/UVScale));
    }
}
void AChunkedDestructibleGlobe::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
}
void AChunkedDestructibleGlobe::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
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
bool AChunkedDestructibleGlobe::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
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
bool AChunkedDestructibleGlobe::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructibleGlobe::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    if (Hit.GetActor()!=this) return false;
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructibleGlobe::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
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
void AChunkedDestructibleGlobe::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
