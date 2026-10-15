#include "ChunkedDestructibleBabylonTower.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
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
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stone(TEXT("/Game/Structures/BabylonTower/MI_BabylonStone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Cornice(TEXT("/Game/Structures/BabylonTower/MI_BabylonCornice"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Terrace(TEXT("/Game/Structures/BabylonTower/MI_BabylonTerrace"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Portal(TEXT("/Game/Structures/BabylonTower/MI_BabylonPortal"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Foundation(TEXT("/Game/Structures/BabylonTower/MI_BabylonFoundation"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Recess(TEXT("/Game/Structures/BabylonTower/MI_BabylonRecess"));
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
    const double Alpha=FMath::Clamp((P.Z-Z0)/(Z1-Z0),0.0,1.0);
    const FVector2D Min=FMath::Lerp(BottomMin,TopMin,Alpha),Max=FMath::Lerp(BottomMax,TopMax,Alpha);
    return P.X>=Min.X-Tolerance && P.X<=Max.X+Tolerance && P.Y>=Min.Y-Tolerance && P.Y<=Max.Y+Tolerance;
}
void AChunkedDestructibleBabylonTower::FPiece::Vertices(FVector (&V)[8]) const
{
    V[0]={BottomMin.X,BottomMin.Y,Z0}; V[1]={BottomMax.X,BottomMin.Y,Z0};
    V[2]={BottomMax.X,BottomMax.Y,Z0}; V[3]={BottomMin.X,BottomMax.Y,Z0};
    V[4]={TopMin.X,TopMin.Y,Z1}; V[5]={TopMax.X,TopMin.Y,Z1};
    V[6]={TopMax.X,TopMax.Y,Z1}; V[7]={TopMin.X,TopMax.Y,Z1};
}
void AChunkedDestructibleBabylonTower::AddPrism(FVector2D BMin,FVector2D BMax,FVector2D TMin,
    FVector2D TMax,double Z0,double Z1,int32 Material)
{
    if (bBudgetExceeded || Z1-Z0<.001 || (BMax-BMin).GetMin()<.001 || (TMax-TMin).GetMin()<.001) return;
    if (Pieces.Num()>=FMath::Clamp(MaxGeneratedCells,1,100000)) { bBudgetExceeded=true; return; }
    // Interpolate before trimming Z so joints follow the actual sloped surface.
    const double InsetZ=FMath::Min(double(PieceGap)*.5,(Z1-Z0)*.1);
    const double Alpha=InsetZ/(Z1-Z0);
    FPiece P;
    P.BottomMin=FMath::Lerp(BMin,TMin,Alpha); P.BottomMax=FMath::Lerp(BMax,TMax,Alpha);
    P.TopMin=FMath::Lerp(BMin,TMin,1-Alpha); P.TopMax=FMath::Lerp(BMax,TMax,1-Alpha);
    const FVector2D Small=(P.BottomMax-P.BottomMin).ComponentMin(P.TopMax-P.TopMin);
    const FVector2D Gap(FMath::Min(double(PieceGap)*.5,Small.X*.1),FMath::Min(double(PieceGap)*.5,Small.Y*.1));
    P.BottomMin+=Gap; P.BottomMax-=Gap; P.TopMin+=Gap; P.TopMax-=Gap;
    P.Z0=Z0+InsetZ; P.Z1=Z1-InsetZ; P.Material=Material;
    P.Bounds=FBox(FVector(FMath::Min(P.BottomMin.X,P.TopMin.X),FMath::Min(P.BottomMin.Y,P.TopMin.Y),P.Z0),
        FVector(FMath::Max(P.BottomMax.X,P.TopMax.X),FMath::Max(P.BottomMax.Y,P.TopMax.Y),P.Z1));
    Pieces.Add(P);
}
void AChunkedDestructibleBabylonTower::TileFrustum(FVector2D Bottom,FVector2D Top,double Z0,double Z1,
    int32 Material,FVector2D Center,bool bOpening)
{
    ZigguratTraversal::SolidSections(Bottom,Top,Z0,Z1,bOpening?GenerationShaftWidth:0,
        [&](FVector2D B0,FVector2D B1,FVector2D T0,FVector2D T1,double Lo,double Hi)
        { TilePrism(B0+Center,B1+Center,T0+Center,T1+Center,Lo,Hi,Material); });
}
void AChunkedDestructibleBabylonTower::TilePrism(FVector2D BMin,FVector2D BMax,FVector2D TMin,
    FVector2D TMax,double Z0,double Z1,int32 Material)
{
    if (bBudgetExceeded || Z1-Z0<.001) return;
    const FVector2D Bottom=BMax-BMin,Top=TMax-TMin;
    const int32 NX=FMath::Max(1,FMath::CeilToInt(FMath::Max(Bottom.X,Top.X)/PieceSize));
    const int32 NY=FMath::Max(1,FMath::CeilToInt(FMath::Max(Bottom.Y,Top.Y)/PieceSize));
    const int32 NZ=FMath::Max(1,FMath::CeilToInt((Z1-Z0)/PieceSize));
    if (int64(Pieces.Num())+int64(NX)*NY*NZ>FMath::Clamp(MaxGeneratedCells,1,100000)) { bBudgetExceeded=true; return; }
    for (int32 Z=0;Z<NZ;++Z)
        for (int32 Y=0;Y<NY;++Y)
            for (int32 X=0;X<NX;++X)
            {
                const FVector2D S0=FMath::Lerp(Bottom,Top,double(Z)/NZ),S1=FMath::Lerp(Bottom,Top,double(Z+1)/NZ);
                const FVector2D O0=FMath::Lerp(BMin,TMin,double(Z)/NZ),O1=FMath::Lerp(BMin,TMin,double(Z+1)/NZ);
                const FVector2D Min(double(X)/NX,double(Y)/NY),Max(double(X+1)/NX,double(Y+1)/NY);
                AddPrism(O0+Min*S0,O0+Max*S0,O1+Min*S1,O1+Max*S1,
                    FMath::Lerp(Z0,Z1,double(Z)/NZ),FMath::Lerp(Z0,Z1,double(Z+1)/NZ),Material);
            }
}
void AChunkedDestructibleBabylonTower::FacadePrism(int32 Face,double U0B,double U1B,double U0T,double U1T,
    double OB,double OT,double T,double Z0,double Z1,int32 Material)
{
    if (Face==0) AddPrism({U0B,-OB},{U1B,-OB+T},{U0T,-OT},{U1T,-OT+T},Z0,Z1,Material);
    if (Face==1) AddPrism({U0B,OB-T},{U1B,OB},{U0T,OT-T},{U1T,OT},Z0,Z1,Material);
    if (Face==2) AddPrism({-OB,U0B},{-OB+T,U1B},{-OT,U0T},{-OT+T,U1T},Z0,Z1,Material);
    if (Face==3) AddPrism({OB-T,U0B},{OB,U1B},{OT-T,U0T},{OT,U1T},Z0,Z1,Material);
}
void AChunkedDestructibleBabylonTower::BuildFacade(int32 Face,int32 Tier,double W,double D,double Z,double H)
{
    const bool bSummit=Tier==TierCount-1;
    const double HalfSpan=(Face<2?W:D)*.5,HalfDepth=(Face<2?D:W)*.5;
    const double SpanInset=(Face<2?W:D)*WallBatter,DepthInset=(Face<2?D:W)*WallBatter;
    const double CornerTrim=Face<2?0:WallThickness;
    const double Core=HalfSpan-SpanInset-CornerTrim-FMath::Min(double(WallThickness),HalfSpan*.08);
    const double Scale=W/BaseWidth;
    const bool bDoor=Face==0 && Tier==0 && bEntrance;
    const bool bWindow=Tier>0 && bUpperWindows && (Face==0 || Face==1);
    TArray<FBox2D> Openings,Recesses;
    const double ApertureWidth=FMath::Min(bDoor?double(EntranceWidth):WindowWidth*(bSummit?.7:1),Core*.8);
    const double ApertureHeight=FMath::Min(bDoor?double(EntranceHeight):WindowHeight*(bSummit?.65:1),H*.82);
    const double Sill=bDoor?0:(bSummit?H*.18:H*.15);
    if (bDoor || bWindow)
    {
        Openings.Add(FBox2D({-ApertureWidth*.5,Z+Sill},{ApertureWidth*.5,Z+Sill+ApertureHeight}));
        ++GeneratedOpeningCount;
    }
    int32 Count=bSummit?0:FMath::RoundToInt((Face<2?FrontRecessCount:SideRecessCount)*Scale);
    Count=FMath::Clamp(Count,0,20);
    const double Spread=Core*1.82;
    const double NicheWidth=Count>0?FMath::Min(double(RecessWidth),Spread/Count*.5):0;
    for (int32 I=0;I<Count;++I)
    {
        const double U=Spread*((I+.5)/Count-.5);
        // Reserve a broad, solid surround for the entrance/window, including its projecting frame.
        if ((bDoor || bWindow) && FMath::Abs(U)<ApertureWidth*.5+NicheWidth*.5+(bDoor?150:35)) continue;
        Recesses.Add(FBox2D({U-NicheWidth*.5,Z+H*.12},{U+NicheWidth*.5,Z+H*(.12+RecessHeightRatio)}));
        if (bOpenFacadeRecesses) ++GeneratedOpeningCount;
    }
    TArray<double> Xs={-Core,Core},Zs={Z,Z+H};
    auto Grid=[](TArray<double>& Cuts,double Min,double Max,double Size)
    {
        const int32 N=FMath::Max(1,FMath::CeilToInt((Max-Min)/Size));
        for (int32 I=1;I<N;++I) Cuts.AddUnique(FMath::Lerp(Min,Max,double(I)/N));
    };
    Grid(Xs,-Core,Core,PieceSize);Grid(Zs,Z,Z+H,PieceSize);
    for (const auto& List:{Openings,Recesses})
        for (const auto& O:List) { Xs.AddUnique(O.Min.X);Xs.AddUnique(O.Max.X);Zs.AddUnique(O.Min.Y);Zs.AddUnique(O.Max.Y); }
    Xs.Sort();Zs.Sort();
    const double Recess=FMath::Min(double(RecessDepth),WallThickness*.65);
    for (int32 R=0;R<Zs.Num()-1 && !bBudgetExceeded;++R)
    {
        const double A=(Zs[R]-Z)/H,B=(Zs[R+1]-Z)/H;
        const double OB=HalfDepth-DepthInset*A,OT=HalfDepth-DepthInset*B;
        for (int32 C=0;C<Xs.Num()-1 && !bBudgetExceeded;++C)
        {
            const FVector2D Mid((Xs[C]+Xs[C+1])*.5,(Zs[R]+Zs[R+1])*.5);
            bool bOpening=false,bRecess=false;
            for (const auto& O:Openings) bOpening|=O.IsInside(Mid);
            for (const auto& O:Recesses) bRecess|=O.IsInside(Mid);
            const double Inset=bRecess?Recess:0;
            if (!bOpening && !(bRecess && bOpenFacadeRecesses)) FacadePrism(Face,Xs[C],Xs[C+1],Xs[C],Xs[C+1],OB-Inset,OT-Inset,
                WallThickness-Inset,Zs[R],Zs[R+1],bRecess?5:0);
        }
        // Side walls stop at front/back walls; no indestructible shared hull spans the room.
        const double EdgeB=HalfSpan-CornerTrim-SpanInset*A,EdgeT=HalfSpan-CornerTrim-SpanInset*B;
        const int32 N=FMath::Max(1,FMath::CeilToInt((HalfSpan-CornerTrim-Core)/PieceSize));
        for (int32 C=0;C<N && !bBudgetExceeded;++C)
        {
            const double L0=FMath::Lerp(Core,EdgeB,double(C)/N),L1=FMath::Lerp(Core,EdgeB,double(C+1)/N);
            const double T0=FMath::Lerp(Core,EdgeT,double(C)/N),T1=FMath::Lerp(Core,EdgeT,double(C+1)/N);
            FacadePrism(Face,L0,L1,T0,T1,OB,OT,WallThickness,Zs[R],Zs[R+1],0);
            FacadePrism(Face,-L1,-L0,-T1,-T0,OB,OT,WallThickness,Zs[R],Zs[R+1],0);
        }
    }
    // A small lintel above the actual opening, never across its clear aperture.
    if (bWindow)
    {
        const double Z0=Z+Sill+ApertureHeight,Z1=FMath::Min(Z+H,Z0+22);
        const double OB=HalfDepth-DepthInset*(Z0-Z)/H+14,OT=HalfDepth-DepthInset*(Z1-Z)/H+14;
        const double L=FMath::Min(Core,ApertureWidth*.5+25);
        const int32 N=FMath::Max(1,FMath::CeilToInt(2*L/PieceSize));
        for (int32 I=0;I<N;++I)
        {
            const double A=FMath::Lerp(-L,L,double(I)/N),B=FMath::Lerp(-L,L,double(I+1)/N);
            FacadePrism(Face,A,B,A,B,OB,OT,14,Z0,Z1,3);
        }
    }
}

void AChunkedDestructibleBabylonTower::BuildPortal(double Z,double H)
{
    const double Core=BaseWidth*.5-BaseWidth*WallBatter-FMath::Min(double(WallThickness),BaseWidth*.04);
    const double Half=FMath::Min(double(EntranceWidth),Core*.8)*.5;
    const double DoorH=FMath::Min(double(EntranceHeight),H*.82);
    const double Frame=FMath::Min3(48.0,BaseWidth*.025,(H-DoorH)*.45);
    const double Front=-BaseDepth*.5;
    // Two nested rectangular surrounds, made from the same damageable blocks as the walls.
    for (int32 Ring=0;Ring<2;++Ring)
    {
        const double Inner=Half+Ring*Frame,Top=Z+DoorH+Ring*Frame;
        const double Y0=Front-75+Ring*22,Y1=Front+BaseDepth*WallBatter;
        for (int32 Side:{-1,1})
        {
            const double X0=Side<0?-Inner-Frame:Inner,X1=Side<0?-Inner:Inner+Frame;
            TilePrism({X0,Y0},{X1,Y1},{X0,Y0},{X1,Y1},Z,Top,3);
        }
        TilePrism({-Inner-Frame,Y0},{Inner+Frame,Y1},{-Inner-Frame,Y0},{Inner+Frame,Y1},Top,Top+Frame,3);
    }
    // Tall flanking pilasters evoke the monumental entrance in the reference.
    const double PillarX=Half+Frame*3.4;
    for (int32 Side:{-1,1})
    {
        const FVector2D Center(Side*PillarX,Front-32);
        TileFrustum({65,110},{48,82},Z,Z+H*.9,3,Center);
        TileFrustum({100,140},{100,140},Z,Z+38,1,Center);
        TileFrustum({70,100},{92,130},Z+H*.9,Z+H*.9+FMath::Min(28.0,H*.08),1,Center);
    }
}

bool AChunkedDestructibleBabylonTower::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max) { V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min; };
    Clamp(BaseWidth,600,16000);Clamp(BaseDepth,600,16000);TierCount=FMath::Clamp(TierCount,1,12);
    Clamp(TierHeight,150,1800);Clamp(SummitHeight,150,3000);Clamp(TopScale,.2f,1);
    Clamp(WallBatter,0,.08f);Clamp(WallThickness,10,180);Clamp(PieceSize,30,500);Clamp(PieceGap,0,5);
    WallThickness=FMath::Min(WallThickness,FMath::Min(BaseWidth,BaseDepth)*(TierCount>1?TopScale:1)*(1-2*WallBatter)*.16f);
    FrontRecessCount=FMath::Clamp(FrontRecessCount,0,20);SideRecessCount=FMath::Clamp(SideRecessCount,0,20);
    Clamp(RecessWidth,20,400);Clamp(RecessDepth,0,100);Clamp(RecessHeightRatio,.2f,.85f);
    Clamp(WindowWidth,30,400);Clamp(WindowHeight,40,800);Clamp(CorniceOverhang,0,200);
    Clamp(CorniceHeight,10,150);Clamp(FloorThickness,10,150);
    Clamp(EntranceWidth,80,800);Clamp(EntranceHeight,100,1500);StairCount=FMath::Clamp(StairCount,1,32);
    Clamp(StairTreadDepth,20,120);Clamp(StairWidth,80,1600);
    Clamp(FoundationHeight,0,600);Clamp(FoundationOverhang,0,400);
    const double RoomWidth=FMath::Min(BaseWidth,BaseDepth)*(TierCount>1?TopScale:1)*(1-2*WallBatter)-2*WallThickness;
    const double FittedWidth=ZigguratTraversal::FitOpening(Traversal,RoomWidth);
    GenerationShaftWidth=Traversal.bFloorOpenings?FittedWidth:0;
    TArray<FPiece> Previous=MoveTemp(Pieces);
    const int32 PreviousOpenings=GeneratedOpeningCount;
    GeneratedOpeningCount=0;bBudgetExceeded=false;GenerationWarning.Reset();
    const FVector2D Foundation(BaseWidth+2*FoundationOverhang,BaseDepth+2*FoundationOverhang);
    TileFrustum(Foundation,Foundation,0,FoundationHeight,4);
    if (bEntrance && bEntranceStairs && FoundationHeight>0)
    {
        const double Width=FMath::Min(double(StairWidth),BaseWidth*.8);
        for (int32 I=0;I<StairCount;++I)
        {
            const FVector2D Center(0,-Foundation.Y*.5-(StairCount-I-.5)*StairTreadDepth);
            TileFrustum({Width,StairTreadDepth},{Width,StairTreadDepth},0,FoundationHeight*(I+1)/StairCount,4,Center);
        }
    }
    double Z=FoundationHeight;
    for (int32 Tier=0;Tier<TierCount && !bBudgetExceeded;++Tier)
    {
        const bool bSummit=Tier==TierCount-1;
        const double Scale=FMath::Lerp(1.0,double(TopScale),TierCount>1?double(Tier)/(TierCount-1):0.0);
        const double W=BaseWidth*Scale,D=BaseDepth*Scale,H=bSummit?SummitHeight:TierHeight;
        for (int32 Face=0;Face<4;++Face) BuildFacade(Face,Tier,W,D,Z,H);
        if (Tier==0 && bEntrance && bEntranceFrame) BuildPortal(Z,H);
        Z+=H;
        const FVector2D Top(W*(1-2*WallBatter),D*(1-2*WallBatter));
        const double Lip=CorniceOverhang*FMath::Sqrt(Scale);
        if (bSummit && bSummitCorbels && Lip>0)
        {
            for (int32 Face=0;Face<4;++Face)
            {
                const double Span=(Face<2?Top.X:Top.Y)-80,Offset=(Face<2?Top.Y:Top.X)*.5;
                const int32 Count=FMath::Clamp(FMath::FloorToInt(Span/115),1,128);
                for (int32 I=0;I<Count;++I)
                {
                    const double U=Span*((I+.5)/Count-.5),Half=FMath::Min(25.0,Span/Count*.25);
                    FacadePrism(Face,U-Half,U+Half,U-Half,U+Half,Offset+Lip*.4,Offset+Lip,
                        Lip*.8,Z-48,Z,1);
                }
            }
        }
        TileFrustum(Top,Top+FVector2D(Lip*1.4),Z,Z+CorniceHeight*.65,1,FVector2D::ZeroVector,true);
        Z+=CorniceHeight*.65;
        TileFrustum(Top+FVector2D(Lip*1.4),Top+FVector2D(Lip*2),Z,Z+CorniceHeight*.35,1,FVector2D::ZeroVector,true);
        Z+=CorniceHeight*.35;
        TileFrustum(Top+FVector2D(Lip*2),Top+FVector2D(Lip*2),Z,Z+FloorThickness,2,FVector2D::ZeroVector,true);
        Z+=FloorThickness;
    }
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);GeneratedOpeningCount=PreviousOpenings;
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce dimensions/tier count. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);
        return false;
    }
    GeneratedHeight=Z;GeneratedPieceCount=Pieces.Num();
    FloorMaterials={StoneMaterial,CorniceMaterial,TerraceMaterial,PortalMaterial,FoundationMaterial,RecessMaterial};
    for (TObjectPtr<UMaterialInterface>& M:FloorMaterials)
        if (!M) M=StoneMaterial?StoneMaterial.Get():UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount=FIntPoint(Pieces.Num(),1);
    ChunkCount=FIntPoint(FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1);
    EffectiveCellDimensions=FVector(PieceSize,PieceSize,WallThickness);
    CellDimensions=EffectiveCellDimensions;CellSpacing=FVector2D(PieceGap);
    EffectiveGridSize=Foundation;GridLocalMin=-Foundation*.5;AreaSize=Foundation;
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
    FVector V[8];Piece.Vertices(V);
    // Counter-clockwise corners along each outward normal; AppendMeshQuad handles UE winding.
    constexpr int32 Faces[6][4]={{4,5,6,7},{3,2,1,0},{0,1,5,4},{2,3,7,6},{1,2,6,5},{3,0,4,7}};
    for (const auto& F:Faces)
    {
        const FVector& A=V[F[0]];const FVector& B=V[F[1]];const FVector& C=V[F[2]];const FVector& D=V[F[3]];
        const FVector Normal=FVector::CrossProduct(B-A,D-A).GetSafeNormal();
        AppendMeshQuad(Mesh,A,B,C,D,FVector3f(Normal),Material,(B-A).Length(),(D-A).Length(),UVWorldUnits);
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
            if (P.BottomMin.Equals(P.TopMin,.001) && P.BottomMax.Equals(P.TopMax,.001))
            {
                const FVector Size=P.Bounds.GetSize();
                FKBoxElem& Box=Collision.BoxElems.Emplace_GetRef(Size.X,Size.Y,Size.Z);Box.Center=P.Bounds.GetCenter();
            }
            else
            {
                FKConvexElem& Convex=Collision.ConvexElems.Emplace_GetRef();
                FVector V[8];P.Vertices(V);for (const FVector& Point:V) Convex.VertexData.Add(Point);
                Convex.UpdateElemBox();
            }
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
