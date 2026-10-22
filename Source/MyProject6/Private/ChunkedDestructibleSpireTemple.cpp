#include "ChunkedDestructibleSpireTemple.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

AChunkedDestructibleSpireTemple::AChunkedDestructibleSpireTemple()
{
    FloorPattern = EChunkedFloorPattern::Solid;
    ChunkSideLength = 96;
    MaxGeneratedCells = 50000;
    DefaultCellHealth = 25;
    bReactToMusic = false;
    bGenerateBottomFaces = true;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stone(TEXT("/Game/Structures/Destructible/SpireTemple/Materials/MI_SpireStone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Trim(TEXT("/Game/Structures/Destructible/SpireTemple/Materials/MI_SpireTrim"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Spire(TEXT("/Game/Structures/Destructible/SpireTemple/Materials/MI_SpireCone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Floor(TEXT("/Game/Structures/Destructible/SpireTemple/Materials/MI_SpireFloor"));
    StoneMaterial = Stone.Get(); TrimMaterial = Trim.Get(); SpireMaterial = Spire.Get(); FloorMaterial = Floor.Get();
}

void AChunkedDestructibleSpireTemple::RebuildTemple() { GenerateFloor(false); }

namespace SpireTempleGeometry
{
void SortCuts(TArray<double>& Cuts)
{
    Cuts.Sort();
    for (int32 I = Cuts.Num()-1; I > 0; --I)
        if (Cuts[I]-Cuts[I-1] < 1.e-5) Cuts.RemoveAt(I);
}
}

void AChunkedDestructibleSpireTemple::Box(FVector Min, FVector Max, int32 Material, int32 Face)
{
    if ((Max-Min).GetMin() < .01 || bBudgetExceeded) return;
    const FIntVector Count(FMath::Max(1,FMath::CeilToInt((Max.X-Min.X)/PieceSize)),
        FMath::Max(1,FMath::CeilToInt((Max.Y-Min.Y)/PieceSize)),FMath::Max(1,FMath::CeilToInt((Max.Z-Min.Z)/PieceSize)));
    for (int32 X=0; X<Count.X && !bBudgetExceeded; ++X)
    for (int32 Y=0; Y<Count.Y && !bBudgetExceeded; ++Y)
    for (int32 Z=0; Z<Count.Z && !bBudgetExceeded; ++Z)
    {
        const FVector L=Min+(Max-Min)*FVector(double(X)/Count.X,double(Y)/Count.Y,double(Z)/Count.Z);
        const FVector U=Min+(Max-Min)*FVector(double(X+1)/Count.X,double(Y+1)/Count.Y,double(Z+1)/Count.Z);
        FVector V[8]={{L.X,L.Y,L.Z},{U.X,L.Y,L.Z},{U.X,U.Y,L.Z},{L.X,U.Y,L.Z},
            {L.X,L.Y,U.Z},{U.X,L.Y,U.Z},{U.X,U.Y,U.Z},{L.X,U.Y,U.Z}};
        if (Face>=0)
        {
            // Local facade coordinates: X along wall, Y inward. Rotations preserve winding.
            const FQuat Rotation(FVector::UpVector,Face*UE_DOUBLE_HALF_PI);
            const double Depth=(Face%2==0?CourtyardDepth:CourtyardWidth)*.5;
            for (FVector& P:V) P=Rotation.RotateVector(P-FVector(0,Depth,0));
        }
        AddPiece(V,Material);
    }
}

void AChunkedDestructibleSpireTemple::Wedge(double A,double B,double R0,double R1,double Inner0,double Inner1,double Z0,double Z1,int32 Material)
{
    if (Z1-Z0<.01 || B-A<1.e-6 || FMath::Max(R0,R1)<.01) return;
    const FVector2D L(FMath::Cos(A),FMath::Sin(A)),U(FMath::Cos(B),FMath::Sin(B));
    const FVector V[8]={FVector(L*Inner0,Z0),FVector(L*R0,Z0),FVector(U*R0,Z0),FVector(U*Inner0,Z0),
        FVector(L*Inner1,Z1),FVector(L*R1,Z1),FVector(U*R1,Z1),FVector(U*Inner1,Z1)};
    AddPiece(V,Material);
}

void AChunkedDestructibleSpireTemple::Ring(double Outer,double Inner,double Z0,double Z1,int32 Material)
{
    if (Outer-Inner<.01 || Z1-Z0<.01) return;
    const int32 Radial=FMath::Max(1,FMath::CeilToInt((Outer-Inner)/PieceSize));
    const int32 Rows=FMath::Max(1,FMath::CeilToInt((Z1-Z0)/PieceSize));
    for (int32 S=0; S<DrumSides && !bBudgetExceeded; ++S)
    for (int32 R=0; R<Radial && !bBudgetExceeded; ++R)
    for (int32 H=0; H<Rows && !bBudgetExceeded; ++H)
    {
        const double IR=FMath::Lerp(Inner,Outer,double(R)/Radial),OR=FMath::Lerp(Inner,Outer,double(R+1)/Radial);
        Wedge(S*UE_DOUBLE_TWO_PI/DrumSides,(S+1)*UE_DOUBLE_TWO_PI/DrumSides,OR,OR,IR,IR,
            FMath::Lerp(Z0,Z1,double(H)/Rows),FMath::Lerp(Z0,Z1,double(H+1)/Rows),Material);
    }
}

void AChunkedDestructibleSpireTemple::Drum(double Radius,double Z,double Height,bool bUpper)
{
    // Unwrap the circle around the entrance, so the -Y opening never crosses a seam.
    const double Start=-UE_DOUBLE_HALF_PI-UE_DOUBLE_PI,End=Start+UE_DOUBLE_TWO_PI;
    TArray<double> Angles={Start,End},Heights={Z,Z+Height};
    TArray<FBox2D> Holes;
    for (int32 S=1; S<DrumSides; ++S) Angles.Add(Start+S*UE_DOUBLE_TWO_PI/DrumSides);
    const int32 Rows=FMath::Max(1,FMath::CeilToInt(Height/PieceSize));
    for (int32 H=1; H<Rows; ++H) Heights.Add(Z+Height*H/Rows);
    if (bUpper && bDrumWindows)
    {
        const double Bay=UE_DOUBLE_TWO_PI/DrumWindowsAround,Row=Height/DrumWindowRows;
        for (int32 C=0; C<DrumWindowsAround; ++C) for (int32 R=0; R<DrumWindowRows; ++R)
        {
            const double Center=Start+(C+.5)*Bay,MidZ=Z+(R+.5)*Row;
            const FBox2D Hole({Center-Bay*DrumWindowWidthRatio*.5,MidZ-Row*DrumWindowHeightRatio*.5},
                {Center+Bay*DrumWindowWidthRatio*.5,MidZ+Row*DrumWindowHeightRatio*.5});
            Holes.Add(Hole);Angles.Add(Hole.Min.X);Angles.Add(Hole.Max.X);Heights.Add(Hole.Min.Y);Heights.Add(Hole.Max.Y);
        }
    }
    if (!bUpper && bDrumEntrance)
    {
        const double Half=FMath::Asin(FMath::Min(double(DrumEntranceWidth)/(2*(Radius-DrumWallThickness)),.8));
        Holes.Add(FBox2D({-UE_DOUBLE_HALF_PI-Half,Z},{-UE_DOUBLE_HALF_PI+Half,Z+FMath::Min(double(DrumEntranceHeight),Height-20)}));
        Angles.Add(Holes.Last().Min.X);Angles.Add(Holes.Last().Max.X);Heights.Add(Holes.Last().Max.Y);
    }
    GeneratedOpeningCount+=Holes.Num();
    SpireTempleGeometry::SortCuts(Angles);SpireTempleGeometry::SortCuts(Heights);
    for (int32 C=0; C<Angles.Num()-1 && !bBudgetExceeded; ++C)
    for (int32 R=0; R<Heights.Num()-1 && !bBudgetExceeded; ++R)
    {
        const FVector2D Mid((Angles[C]+Angles[C+1])*.5,(Heights[R]+Heights[R+1])*.5);
        bool bHole=false;for (const FBox2D& Hole:Holes) bHole|=Hole.IsInside(Mid);
        if (!bHole) Wedge(Angles[C],Angles[C+1],Radius,Radius,Radius-DrumWallThickness,Radius-DrumWallThickness,Heights[R],Heights[R+1],0);
    }
}

void AChunkedDestructibleSpireTemple::EnclosureFace(int32 Face)
{
    // Front/back own the corner blocks; side faces stop at their inside edges.
    const double Half=(Face%2==0?CourtyardWidth:CourtyardDepth)*.5-(Face%2?EnclosureThickness:0);
    const double CapHalf=Half+(Face%2?-8:8);
    const double H=EnclosureHeight,T=EnclosureThickness;
    const bool HasGate=bGate && (Face==0 || (Face==2 && bRearGate));
    const double GateHalf=GateWidth*.5,Frame=GateFrameWidth,GH=GateHeight;
    auto Panel=[&](double Left,double Right,int32 Bays)
    {
        if (Right-Left<1) return;
        const double Bay=(Right-Left)/Bays,Opening=Bay*EnclosureOpeningRatio;
        if (!bEnclosureOpenings) {Box({Left,0,0},{Right,T,H},0,Face);return;}
        for (int32 I=0; I<Bays && !bBudgetExceeded; ++I)
        {
            const double L=Left+I*Bay,R=L+Bay,C=(L+R)*.5;
            const double OL=C-Opening*.5,OR=C+Opening*.5;
            // Pier, foot slot, tall slit and small clerestory opening.
            Box({L,0,0},{OL,T,H},0,Face);Box({OR,0,0},{R,T,H},0,Face);
            Box({OL,0,0},{OR,T,H*.045},1,Face);
            Box({OL,0,H*.105},{OR,T,H*.20},0,Face);
            Box({OL,0,H*.73},{OR,T,H*.79},0,Face);
            Box({OL,0,H*.91},{OR,T,H},0,Face);
            GeneratedOpeningCount+=3;
            if (EnclosurePierProjection>0)
            {
                const double PW=(Bay-Opening)*.32;
                Box({L,-EnclosurePierProjection,H*.16},{L+PW,0,H*.94},1,Face);
                Box({R-PW,-EnclosurePierProjection,H*.16},{R,0,H*.94},1,Face);
            }
        }
    };
    if (HasGate)
    {
        const double End=GateHalf+Frame;
        const int32 Bays=FMath::Max(1,FMath::RoundToInt(FrontBays*(Half-End)/(2*Half)));
        Panel(-Half,-End,Bays);Panel(End,Half,Bays);
        Box({-End,-GateProjection,0},{-GateHalf,T,H},1,Face);
        Box({GateHalf,-GateProjection,0},{End,T,H},1,Face);
        Box({-GateHalf,-GateProjection,GH},{GateHalf,T,H},0,Face);
        Box({-End,-GateProjection-8,H},{End,T+8,H+CorniceHeight},1,Face);
        Box({-CapHalf,-EnclosurePierProjection-8,H},{-End,T+8,H+CorniceHeight},1,Face);
        Box({End,-EnclosurePierProjection-8,H},{CapHalf,T+8,H+CorniceHeight},1,Face);
        ++GeneratedOpeningCount;
    }
    else
    {
        Panel(-Half,Half,Face%2==0?FrontBays:SideBays);
        Box({-CapHalf,-EnclosurePierProjection-8,H},{CapHalf,T+8,H+CorniceHeight},1,Face);
    }
}

bool AChunkedDestructibleSpireTemple::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max) { V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min; };
    Clamp(SpireRadius,100,4000);Clamp(SpireHeight,200,12000);Clamp(SpireTipRatio,0,.8f);
    SpireSides=FMath::Clamp(SpireSides,8,128);SpireCourses=FMath::Clamp(SpireCourses,3,128);
    Clamp(SpireShellThickness,5,150);Clamp(SpireCollarHeight,0,150);
    Clamp(LowerDrumRadius,200,5000);Clamp(UpperDrumRadius,150,5000);
    Clamp(LowerDrumHeight,150,2000);Clamp(UpperDrumHeight,150,2500);
    Clamp(DrumWallThickness,10,120);DrumSides=FMath::Clamp(DrumSides,16,128);
    Clamp(CorniceOverhang,0,100);Clamp(CorniceHeight,5,120);Clamp(PlinthHeight,0,150);
    UpperDrumRadius=FMath::Min(UpperDrumRadius,LowerDrumRadius);
    SpireRadius=FMath::Min(SpireRadius,UpperDrumRadius);
    DrumWallThickness=FMath::Min(DrumWallThickness,UpperDrumRadius*.3f);
    Clamp(DrumEntranceWidth,80,600);Clamp(DrumEntranceHeight,100,1500);
    DrumWindowsAround=FMath::Clamp(DrumWindowsAround,4,64);DrumWindowRows=FMath::Clamp(DrumWindowRows,1,6);
    Clamp(DrumWindowWidthRatio,.1f,.8f);Clamp(DrumWindowHeightRatio,.1f,.85f);
    Clamp(CourtyardWidth,800,16000);Clamp(CourtyardDepth,800,16000);
    Clamp(EnclosureHeight,200,2000);Clamp(EnclosureThickness,15,180);Clamp(EnclosurePierProjection,0,50);
    // Keep the optional wall clear of the central structure even at extreme settings.
    const float MinCourtyard=2*(LowerDrumRadius+CorniceOverhang+EnclosureThickness+100);
    CourtyardWidth=FMath::Max(CourtyardWidth,MinCourtyard);CourtyardDepth=FMath::Max(CourtyardDepth,MinCourtyard);
    FrontBays=FMath::Clamp(FrontBays,3,60);SideBays=FMath::Clamp(SideBays,3,60);
    Clamp(EnclosureOpeningRatio,.15f,.75f);Clamp(CourtyardFloorThickness,5,100);
    Clamp(GateWidth,100,1600);Clamp(GateHeight,150,1800);Clamp(GateFrameWidth,10,150);Clamp(GateProjection,0,180);
    GateWidth=FMath::Min(GateWidth,CourtyardWidth*.6f);GateHeight=FMath::Min(GateHeight,EnclosureHeight-30);
    Clamp(PieceSize,40,500);Clamp(PieceGap,0,5);

    TArray<FPiece> Previous=MoveTemp(Pieces);
    const int32 PreviousOpenings=GeneratedOpeningCount;
    GeneratedOpeningCount=0;bBudgetExceeded=false;GenerationWarning.Reset();
    if (bCourtyardFloor) Box({-CourtyardWidth*.5,-CourtyardDepth*.5,-CourtyardFloorThickness},{CourtyardWidth*.5,CourtyardDepth*.5,0},3);
    const int32 BeforeEnclosure=Pieces.Num();
    if (bRectangularEnclosure) for (int32 Face=0;Face<4 && !bBudgetExceeded;++Face) EnclosureFace(Face);
    const int32 EnclosureCount=Pieces.Num()-BeforeEnclosure;
    Ring(LowerDrumRadius+CorniceOverhang,0,0,PlinthHeight,1);
    double Z=PlinthHeight;
    Drum(LowerDrumRadius,Z,LowerDrumHeight,false);Z+=LowerDrumHeight;
    Ring(LowerDrumRadius+CorniceOverhang,bInteriorFloors?0:UpperDrumRadius-DrumWallThickness,Z,Z+CorniceHeight,1);Z+=CorniceHeight;
    Drum(UpperDrumRadius,Z,UpperDrumHeight,true);Z+=UpperDrumHeight;
    Ring(UpperDrumRadius+CorniceOverhang,bInteriorFloors?0:SpireRadius-SpireShellThickness,Z,Z+CorniceHeight,1);Z+=CorniceHeight;
    const int32 BeforeSpire=Pieces.Num();
    if (bSpire)
    {
        Ring(SpireRadius+16,FMath::Max(0.,double(SpireRadius-SpireShellThickness)),Z,Z+SpireCollarHeight,1);Z+=SpireCollarHeight;
        for (int32 Row=0;Row<SpireCourses && !bBudgetExceeded;++Row)
        {
            const double R0=FMath::Lerp(double(SpireRadius),double(SpireRadius*SpireTipRatio),double(Row)/SpireCourses);
            const double R1=FMath::Lerp(double(SpireRadius),double(SpireRadius*SpireTipRatio),double(Row+1)/SpireCourses);
            const double Offset=(bStaggerSpireJoints && Row%2)?.5:0;
            for (int32 S=0;S<SpireSides && !bBudgetExceeded;++S)
                Wedge((S+Offset)*UE_DOUBLE_TWO_PI/SpireSides,(S+1+Offset)*UE_DOUBLE_TWO_PI/SpireSides,
                    R0,R1,FMath::Max(0.,R0-SpireShellThickness),FMath::Max(0.,R1-SpireShellThickness),
                    Z+SpireHeight*Row/SpireCourses,Z+SpireHeight*(Row+1)/SpireCourses,2);
        }
        Z+=SpireHeight;
        if (SpireTipRatio>0) Ring(SpireRadius*SpireTipRatio,0,Z-FMath::Min(10.f,SpireHeight/SpireCourses),Z,2);
    }
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);GeneratedOpeningCount=PreviousOpenings;
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size, reduce courses/bays, or increase Max Generated Cells. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);return false;
    }
    GeneratedHeight=FMath::Max(Z,bRectangularEnclosure?double(EnclosureHeight+CorniceHeight):0.);
    GeneratedEnclosurePieces=EnclosureCount;GeneratedSpirePieces=Pieces.Num()-BeforeSpire;GeneratedPieceCount=Pieces.Num();
    FloorMaterials={StoneMaterial,TrimMaterial,SpireMaterial,FloorMaterial};
    for (TObjectPtr<UMaterialInterface>& M:FloorMaterials) if (!M) M=StoneMaterial?StoneMaterial.Get():UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount={Pieces.Num(),1};ChunkCount={FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1};
    EffectiveCellDimensions={PieceSize,PieceSize,DrumWallThickness};CellDimensions=EffectiveCellDimensions;CellSpacing=FVector2D(PieceGap);
    FBox Bounds(ForceInit);for (const FPiece& P:Pieces) Bounds+=P.Bounds;
    EffectiveGridSize=FVector2D(Bounds.GetSize());GridLocalMin=FVector2D(Bounds.Min);AreaSize=EffectiveGridSize;
    return true;
}

#if WITH_EDITOR
bool AChunkedDestructibleSpireTemple::CanEditChange(const FProperty* Property) const
{
    if (Property)
        for (const TCHAR* Derived : {TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
            TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
            TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
            if (Property->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(Property);
}
#endif

bool AChunkedDestructibleSpireTemple::FPiece::Contains(FVector P,double Tolerance) const
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
void AChunkedDestructibleSpireTemple::AddPiece(const FVector (&V)[8],int32 Material)
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
bool AChunkedDestructibleSpireTemple::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleSpireTemple::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Bounds.GetCenter():FVector::ZeroVector; }
int32 AChunkedDestructibleSpireTemple::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Material:0; }
FTransform AChunkedDestructibleSpireTemple::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructibleSpireTemple::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const
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
void AChunkedDestructibleSpireTemple::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
}
void AChunkedDestructibleSpireTemple::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
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
bool AChunkedDestructibleSpireTemple::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
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
bool AChunkedDestructibleSpireTemple::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructibleSpireTemple::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructibleSpireTemple::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
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
void AChunkedDestructibleSpireTemple::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
