#include "ChunkedDestructibleBabylonTower.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/ChildActorComponent.h"

using namespace UE::Geometry;

AChunkedDestructibleBabylonTower::AChunkedDestructibleBabylonTower()
{
    FloorPattern=EChunkedFloorPattern::Solid;
    ChunkSideLength=96;
    MaxGeneratedCells=50000;
    bGenerateBottomFaces=true;
    bReactToMusic=false;
    DefaultCellHealth=25;
    GroundLaunchPad=CreateDefaultSubobject<UChildActorComponent>(TEXT("GroundLaunchPad"));
    GroundLaunchPad->SetupAttachment(GetRootComponent());
    ZigguratTraversal::InitializeMaterials(Traversal);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stone(TEXT("/Game/Structures/Destructible/BabylonTower/Materials/MI_BabylonStone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Cornice(TEXT("/Game/Structures/Destructible/BabylonTower/Materials/MI_BabylonCornice"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Terrace(TEXT("/Game/Structures/Destructible/BabylonTower/Materials/MI_BabylonTerrace"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Portal(TEXT("/Game/Structures/Destructible/BabylonTower/Materials/MI_BabylonPortal"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Foundation(TEXT("/Game/Structures/Destructible/BabylonTower/Materials/MI_BabylonFoundation"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Recess(TEXT("/Game/Structures/Destructible/BabylonTower/Materials/MI_BabylonRecess"));
    StoneMaterial=Stone.Get();CorniceMaterial=Cornice.Get();TerraceMaterial=Terrace.Get();
    PortalMaterial=Portal.Get();FoundationMaterial=Foundation.Get();RecessMaterial=Recess.Get();
}
void AChunkedDestructibleBabylonTower::RebuildTower() { GenerateFloor(false); }

#if WITH_EDITOR
bool AChunkedDestructibleBabylonTower::CanEditChange(const FProperty* Property) const
{
    if (Property)
        for (const TCHAR* Derived : {TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
            TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
            TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
            if (Property->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(Property);
}
#endif

bool AChunkedDestructibleBabylonTower::FPiece::Contains(FVector P,double Tolerance) const
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
void AChunkedDestructibleBabylonTower::AddPiece(const FVector (&V)[8],int32 Material)
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
// A regular polygon with one face towards -Y, stretched independently along X/Y.
namespace BabylonGeometry
{
FVector2D Corner(int32 Face,int32 Sides,FVector2D Size)
{
    const double Angle=-UE_DOUBLE_HALF_PI+(2*Face-1)*UE_DOUBLE_PI/Sides;
    return FVector2D(FMath::Cos(Angle)*Size.X,FMath::Sin(Angle)*Size.Y)*(.5/FMath::Cos(UE_DOUBLE_PI/Sides));
}
}
void AChunkedDestructibleBabylonTower::TilePolygon(FVector2D Size,double Z0,double Z1,int32 Material,bool bOpening)
{
    if (Z1-Z0<.001 || bBudgetExceeded) return;
    const double Half=bOpening?GenerationShaftWidth*.5:0;
    const int32 Rows=FMath::Max(1,FMath::CeilToInt((Z1-Z0)/PieceSize));
    for (int32 Face=0;Face<SideCount && !bBudgetExceeded;++Face)
    {
        const FVector2D A=BabylonGeometry::Corner(Face,SideCount,Size),B=BabylonGeometry::Corner(Face+1,SideCount,Size);
        TArray<double> Cuts={0,1};
        const int32 Columns=FMath::Max(1,FMath::CeilToInt((B-A).Length()/PieceSize));
        for (int32 I=1;I<Columns;++I) Cuts.Add(double(I)/Columns);
        // Split at the square shaft's corner rays, so no ring segment bridges its aperture.
        for (double Sign:{-1.,1.})
        {
            const double Start=A.X+Sign*A.Y,End=B.X+Sign*B.Y;
            if (Start*End<0) Cuts.AddUnique(Start/(Start-End));
        }
        Cuts.Sort();
        for (int32 I=Cuts.Num()-1;I>0;--I) if ((Cuts[I]-Cuts[I-1])*(B-A).Length()<.01) Cuts.RemoveAt(I);
        for (int32 C=0;C<Cuts.Num()-1 && !bBudgetExceeded;++C)
        {
            const FVector2D L=FMath::Lerp(A,B,Cuts[C]),R=FMath::Lerp(A,B,Cuts[C+1]);
            const FVector2D IL=Half>0?L*(Half/FMath::Max(FMath::Abs(L.X),FMath::Abs(L.Y))):FVector2D::ZeroVector;
            const FVector2D IR=Half>0?R*(Half/FMath::Max(FMath::Abs(R.X),FMath::Abs(R.Y))):FVector2D::ZeroVector;
            const int32 Radial=FMath::Max(1,FMath::CeilToInt(FMath::Max((L-IL).Length(),(R-IR).Length())/PieceSize));
            for (int32 I=0;I<Radial && !bBudgetExceeded;++I) for (int32 H=0;H<Rows && !bBudgetExceeded;++H)
            {
                const FVector2D P[4]={FMath::Lerp(IL,L,double(I)/Radial),FMath::Lerp(IL,L,double(I+1)/Radial),
                    FMath::Lerp(IR,R,double(I+1)/Radial),FMath::Lerp(IR,R,double(I)/Radial)};
                FVector V[8];
                for (int32 K=0;K<4;++K)
                {
                    V[K]=FVector(P[K],FMath::Lerp(Z0,Z1,double(H)/Rows));
                    V[K+4]=FVector(P[K],FMath::Lerp(Z0,Z1,double(H+1)/Rows));
                }
                AddPiece(V,Material);
            }
        }
    }
}
void AChunkedDestructibleBabylonTower::FaceBox(int32 Face,double U0,double U1,double D0,double D1,double Z0,double Z1,int32 Material)
{
    if (Z1-Z0<.001 || U1-U0<.001 || D1-D0<.001) return;
    const FVector2D A=BabylonGeometry::Corner(Face,SideCount,{BaseWidth,BaseDepth});
    const FVector2D B=BabylonGeometry::Corner(Face+1,SideCount,{BaseWidth,BaseDepth});
    const FVector2D T=(B-A).GetSafeNormal(),N(T.Y,-T.X),Center=(A+B)*.5;
    const int32 Columns=FMath::Max(1,FMath::CeilToInt((U1-U0)/PieceSize));
    const int32 Rows=FMath::Max(1,FMath::CeilToInt((Z1-Z0)/PieceSize));
    for (int32 C=0;C<Columns && !bBudgetExceeded;++C) for (int32 R=0;R<Rows && !bBudgetExceeded;++R)
    {
        const double L=FMath::Lerp(U0,U1,double(C)/Columns),U=FMath::Lerp(U0,U1,double(C+1)/Columns);
        const FVector2D P[4]={Center+T*L+N*D0,Center+T*L+N*D1,Center+T*U+N*D1,Center+T*U+N*D0};
        FVector V[8];
        for (int32 K=0;K<4;++K) {V[K]=FVector(P[K],FMath::Lerp(Z0,Z1,double(R)/Rows));V[K+4]=FVector(P[K],FMath::Lerp(Z0,Z1,double(R+1)/Rows));}
        AddPiece(V,Material);
    }
}
void AChunkedDestructibleBabylonTower::BuildPortal(int32 Face,double HalfWidth,double Z,double H)
{
    const double Frame=FMath::Min(35.,H*.12);
    FaceBox(Face,-HalfWidth-Frame,-HalfWidth,-WallThickness,40,Z,Z+H+Frame,3);
    FaceBox(Face,HalfWidth,HalfWidth+Frame,-WallThickness,40,Z,Z+H+Frame,3);
    FaceBox(Face,-HalfWidth,HalfWidth,-WallThickness,40,Z+H,Z+H+Frame,3);
}
void AChunkedDestructibleBabylonTower::BuildFacade(int32 Face,int32 Tier,double W,double D,double Z,double H)
{
    const FVector2D A=BabylonGeometry::Corner(Face,SideCount,{W,D}),B=BabylonGeometry::Corner(Face+1,SideCount,{W,D});
    const FVector2D Tangent=(B-A).GetSafeNormal(),Normal(Tangent.Y,-Tangent.X);
    const double Apothem=FVector2D::DotProduct(A,Normal),HalfSpan=(B-A).Length()*.5;
    const double TopFactor=1-2*WallBatter;
    const double InnerFactor=TopFactor-WallThickness/Apothem;
    const double ClearHalf=HalfSpan*InnerFactor;
    bool bDoor=false;
    const int32 Doors=FMath::Min(EntranceCount,SideCount);
    for (int32 I=0;I<Doors;++I) bDoor|=bEntrance && Tier==0 && Face==I*SideCount/Doors;
    int32 Count=TierWindowCounts.IsValidIndex(Tier) && TierWindowCounts[Tier]>=0?FMath::Clamp(TierWindowCounts[Tier],0,20):WindowsPerSide;
    Count=bDoor?1:(Tier>0 && bUpperWindows?Count:0);
    const double RequestedWidth=TierWindowWidths.IsValidIndex(Tier) && FMath::IsFinite(TierWindowWidths[Tier]) && TierWindowWidths[Tier]>0?
        FMath::Clamp(double(TierWindowWidths[Tier]),20.,400.):WindowWidth;
    const bool Proportional=!bDoor && bScaleWindowsWithTiers;
    const double WidthScale=Proportional?W/BaseWidth:1.;
    const double Width=Count>0?FMath::Min(bDoor?double(EntranceWidth):RequestedWidth*WidthScale,ClearHalf*1.5/Count):0;
    const double Sill=bDoor?0:(Proportional?
        FMath::Clamp(double(WindowSillHeight)/TierHeight,.025,FMath::Max(.025,1.-WindowHeightRatio-.05))*H:
        FMath::Clamp(double(WindowSillHeight),10.,H*.5));
    const double Height=Proportional?H*WindowHeightRatio:
        FMath::Min(bDoor?double(EntranceHeight):double(WindowHeight),H-Sill-20);
    const double Usable=HalfSpan*TopFactor;
    TArray<FBox2D> Openings;
    TArray<double> Us={-1,1},Zs={Z,Z+H};
    for (int32 I=0;I<Count;++I)
    {
        const double U=Count>1?1.5*((I+.5)/Count-.5):0;
        const double Half=Width/(2*Usable);
        Openings.Add(FBox2D({U-Half,Z+Sill},{U+Half,Z+Sill+Height}));
        Us.AddUnique(U-Half);Us.AddUnique(U+Half);Zs.AddUnique(Z+Sill);Zs.AddUnique(Z+Sill+Height);
        if (bDoor || bOpenFacadeRecesses) ++GeneratedOpeningCount;
    }
    const int32 Cols=FMath::Max(1,FMath::CeilToInt(HalfSpan*2/PieceSize)),Rows=FMath::Max(1,FMath::CeilToInt(H/PieceSize));
    for (int32 I=1;I<Cols;++I) Us.AddUnique(-1+2.*I/Cols);
    for (int32 I=1;I<Rows;++I) Zs.AddUnique(Z+H*I/Rows);
    Us.Sort();Zs.Sort();
    for (int32 I=Us.Num()-1;I>0;--I) if ((Us[I]-Us[I-1])*HalfSpan<.01) Us.RemoveAt(I);
    for (int32 I=Zs.Num()-1;I>0;--I) if (Zs[I]-Zs[I-1]<.01) Zs.RemoveAt(I);
    for (int32 R=0;R<Zs.Num()-1 && !bBudgetExceeded;++R) for (int32 C=0;C<Us.Num()-1 && !bBudgetExceeded;++C)
    {
        const FVector2D Mid((Us[C]+Us[C+1])*.5,(Zs[R]+Zs[R+1])*.5);
        bool Opening=false;for (const auto& O:Openings) Opening|=O.IsInside(Mid);
        if (Opening && (bDoor || bOpenFacadeRecesses)) continue;
        const double Recess=Opening?FMath::Min(double(RecessDepth),WallThickness*.65):0;
        FVector V[8];
        for (int32 Layer=0;Layer<2;++Layer)
        {
            const double ZP=Zs[R+Layer],Outer=1-2*WallBatter*(ZP-Z)/H,Inner=Outer-WallThickness/Apothem;
            const FVector2D L=FMath::Lerp(A,B,(Us[C]+1)*.5),U=FMath::Lerp(A,B,(Us[C+1]+1)*.5);
            V[Layer*4]=FVector(L*Inner,ZP);V[Layer*4+1]=FVector(L*(Outer-Recess/Apothem),ZP);
            V[Layer*4+2]=FVector(U*(Outer-Recess/Apothem),ZP);V[Layer*4+3]=FVector(U*Inner,ZP);
        }
        AddPiece(V,Opening?5:0);
    }
    if (bDoor && bEntranceFrame) BuildPortal(Face,Width*.5,Z,Height);
    if (bDoor && bEntranceStairs && FoundationHeight>0)
    {
        const double Half=FMath::Min(double(StairWidth)*.5,ClearHalf*.9);
        const FVector2D FA=BabylonGeometry::Corner(Face,SideCount,{BaseWidth+2*FoundationOverhang,BaseDepth+2*FoundationOverhang});
        const double Overhang=FVector2D::DotProduct(FA-A,Normal);
        for (int32 I=0;I<StairCount;++I)
            FaceBox(Face,-Half,Half,Overhang+(StairCount-I-1)*StairTreadDepth,Overhang+(StairCount-I)*StairTreadDepth,
                0,FoundationHeight*(I+1)/StairCount,4);
    }
}

bool AChunkedDestructibleBabylonTower::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max) { V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min; };
    SideCount=FMath::Clamp(SideCount,3,32);TierCount=FMath::Clamp(TierCount,1,32);
    Clamp(BaseWidth,600,16000);Clamp(BaseDepth,600,16000);
    Clamp(TierHeight,150,1800);Clamp(SummitHeight,150,3000);Clamp(TopScale,.05f,1);
    Clamp(WallBatter,0,.08f);Clamp(WallThickness,10,180);Clamp(PieceSize,30,500);Clamp(PieceGap,0,5);
    const double Smallest=FMath::Min(BaseWidth,BaseDepth)*(TierCount>1?TopScale:1)*(1-2*WallBatter);
    WallThickness=FMath::Min(double(WallThickness),Smallest*.12);
    WindowsPerSide=FMath::Clamp(WindowsPerSide,0,20);EntranceCount=FMath::Clamp(EntranceCount,1,32);
    Clamp(RecessDepth,0,100);Clamp(WindowWidth,30,400);Clamp(WindowHeight,40,800);Clamp(WindowSillHeight,10,500);
    Clamp(WindowHeightRatio,.05f,.9f);
    Clamp(CorniceOverhang,0,200);Clamp(CorniceHeight,0,150);Clamp(FloorThickness,10,150);
    Clamp(EntranceWidth,80,800);Clamp(EntranceHeight,100,1500);StairCount=FMath::Clamp(StairCount,1,32);
    Clamp(StairTreadDepth,20,120);Clamp(StairWidth,80,1600);Clamp(FoundationHeight,0,600);Clamp(FoundationOverhang,0,400);
    // Fit a SQUARE shaft inside every polygon wall, including diagonally facing walls.
    double RoomWidth=TNumericLimits<double>::Max();
    for (int32 Face=0;Face<SideCount;++Face)
    {
        const FVector2D A=BabylonGeometry::Corner(Face,SideCount,{BaseWidth,BaseDepth})*(TierCount>1?TopScale:1)*(1-2*WallBatter);
        const FVector2D B=BabylonGeometry::Corner(Face+1,SideCount,{BaseWidth,BaseDepth})*(TierCount>1?TopScale:1)*(1-2*WallBatter);
        const FVector2D T=(B-A).GetSafeNormal(),N(T.Y,-T.X);
        RoomWidth=FMath::Min(RoomWidth,2*(FVector2D::DotProduct(A,N)-WallThickness)/(FMath::Abs(N.X)+FMath::Abs(N.Y)));
    }
    const double FittedWidth=FMath::Min(ZigguratTraversal::FitOpening(Traversal,RoomWidth),FMath::Max(1.,RoomWidth-1));
    GenerationShaftWidth=Traversal.bFloorOpenings?FittedWidth:0;
    TArray<FPiece> Previous=MoveTemp(Pieces);const int32 PreviousOpenings=GeneratedOpeningCount;
    GeneratedOpeningCount=0;bBudgetExceeded=false;GenerationWarning.Reset();
    const FVector2D Foundation(BaseWidth+2*FoundationOverhang,BaseDepth+2*FoundationOverhang);
    TilePolygon(Foundation,0,FoundationHeight,4);
    double Z=FoundationHeight;
    for (int32 Tier=0;Tier<TierCount && !bBudgetExceeded;++Tier)
    {
        const bool bSummit=Tier==TierCount-1;
        const double Scale=FMath::Lerp(1.,double(TopScale),TierCount>1?double(Tier)/(TierCount-1):0.);
        const double H=TierHeights.IsValidIndex(Tier) && FMath::IsFinite(TierHeights[Tier]) && TierHeights[Tier]>0?
            FMath::Clamp(double(TierHeights[Tier]),150.,3000.):double(bSummit?SummitHeight:TierHeight);
        const double W=BaseWidth*Scale,D=BaseDepth*Scale;
        for (int32 Face=0;Face<SideCount && !bBudgetExceeded;++Face) BuildFacade(Face,Tier,W,D,Z,H);
        Z+=H;
        const FVector2D Top(W*(1-2*WallBatter),D*(1-2*WallBatter));
        const double Lip=bCornices?CorniceOverhang*FMath::Sqrt(Scale):0;
        if (bCornices)
        {
            TilePolygon(Top+FVector2D(Lip*2),Z,Z+CorniceHeight,1,true);
            Z+=CorniceHeight;
            if (bSummit && bSummitCorbels && Lip>0)
            {
                // Small, individually destructible brackets beneath the summit band.
                for (int32 Face=0;Face<SideCount;++Face)
                {
                    const FVector2D A=BabylonGeometry::Corner(Face,SideCount,Top),B=BabylonGeometry::Corner(Face+1,SideCount,Top);
                    const FVector2D T=(B-A).GetSafeNormal(),N(T.Y,-T.X);
                    const int32 Count=FMath::Max(1,FMath::FloorToInt((B-A).Length()/115));
                    for (int32 I=0;I<Count;++I)
                    {
                        const FVector2D C=FMath::Lerp(A,B,(I+.5)/Count);
                        const double Half=FMath::Min(25.,(B-A).Length()/Count*.25);
                        const FVector2D P[4]={C-T*Half-N*10,C-T*Half+N*Lip,C+T*Half+N*Lip,C+T*Half-N*10};
                        FVector V[8];for (int32 K=0;K<4;++K) {V[K]=FVector(P[K],Z-CorniceHeight-35);V[K+4]=FVector(P[K],Z-CorniceHeight);}
                        AddPiece(V,1);
                    }
                }
            }
        }
        TilePolygon(Top+FVector2D(Lip*2),Z,Z+FloorThickness,2,true);Z+=FloorThickness;
    }
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);GeneratedOpeningCount=PreviousOpenings;
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce dimensions/tier count. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);return false;
    }
    GeneratedHeight=Z;GeneratedPieceCount=Pieces.Num();
    FloorMaterials={StoneMaterial,CorniceMaterial,TerraceMaterial,PortalMaterial,FoundationMaterial,RecessMaterial};
    for (TObjectPtr<UMaterialInterface>& M:FloorMaterials)
        if (!M) M=StoneMaterial?StoneMaterial.Get():UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount=FIntPoint(Pieces.Num(),1);ChunkCount=FIntPoint(FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1);
    EffectiveCellDimensions=FVector(PieceSize,PieceSize,WallThickness);CellDimensions=EffectiveCellDimensions;CellSpacing=FVector2D(PieceGap);
    FBox Bounds(ForceInit);for (const auto& P:Pieces) Bounds+=P.Bounds;
    EffectiveGridSize=FVector2D(Bounds.GetSize());GridLocalMin=FVector2D(Bounds.Min);AreaSize=EffectiveGridSize;
    EffectiveOpeningWidth=GenerationShaftWidth;
    ZigguratTraversal::UpdatePad(GroundLaunchPad,Traversal,FittedWidth,FoundationHeight,GeneratedHeight);
    return true;
}

bool AChunkedDestructibleBabylonTower::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleBabylonTower::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Bounds.GetCenter():FVector::ZeroVector; }
int32 AChunkedDestructibleBabylonTower::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Material:0; }
FTransform AChunkedDestructibleBabylonTower::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructibleBabylonTower::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const
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
void AChunkedDestructibleBabylonTower::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
}
void AChunkedDestructibleBabylonTower::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
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
bool AChunkedDestructibleBabylonTower::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
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
bool AChunkedDestructibleBabylonTower::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructibleBabylonTower::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructibleBabylonTower::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
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
void AChunkedDestructibleBabylonTower::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
