#include "ChunkedDestructibleZigguratPyramit.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/ChildActorComponent.h"

using namespace UE::Geometry;

AChunkedDestructibleZigguratPyramit::AChunkedDestructibleZigguratPyramit()
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
    // Native placement and runtime native spawns share the saved reference palette too.
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stone(TEXT("/Game/Structures/Destructible/ZigguratPyramit/Materials/MI_PyramitStone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Cornice(TEXT("/Game/Structures/Destructible/ZigguratPyramit/Materials/MI_PyramitCornice"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Terrace(TEXT("/Game/Structures/Destructible/ZigguratPyramit/Materials/MI_PyramitTerrace"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Lintel(TEXT("/Game/Structures/Destructible/ZigguratPyramit/Materials/MI_PyramitLintel"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Foundation(TEXT("/Game/Structures/Destructible/ZigguratPyramit/Materials/MI_PyramitFoundation"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Crown(TEXT("/Game/Structures/Destructible/ZigguratPyramit/Materials/MI_PyramitCrown"));
    StoneMaterial=Stone.Get();CorniceMaterial=Cornice.Get();TerraceMaterial=Terrace.Get();
    LintelMaterial=Lintel.Get();FoundationMaterial=Foundation.Get();CrownMaterial=Crown.Get();
}
void AChunkedDestructibleZigguratPyramit::RebuildZigguratPyramit() { GenerateFloor(false); }

#if WITH_EDITOR
bool AChunkedDestructibleZigguratPyramit::CanEditChange(const FProperty* Property) const
{
    if (Property)
        for (const TCHAR* Derived : {TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
            TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
            TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
            if (Property->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(Property);
}
#endif

bool AChunkedDestructibleZigguratPyramit::FPiece::Contains(FVector P,double Tolerance) const
{
    if (!Bounds.ExpandBy(Tolerance).IsInsideOrOn(P)) return false;
    const double Alpha=FMath::Clamp((P.Z-Z0)/(Z1-Z0),0.0,1.0);
    const FVector2D Min=FMath::Lerp(BottomMin,TopMin,Alpha),Max=FMath::Lerp(BottomMax,TopMax,Alpha);
    return P.X>=Min.X-Tolerance && P.X<=Max.X+Tolerance && P.Y>=Min.Y-Tolerance && P.Y<=Max.Y+Tolerance;
}
void AChunkedDestructibleZigguratPyramit::FPiece::Vertices(FVector (&V)[8]) const
{
    V[0]={BottomMin.X,BottomMin.Y,Z0}; V[1]={BottomMax.X,BottomMin.Y,Z0};
    V[2]={BottomMax.X,BottomMax.Y,Z0}; V[3]={BottomMin.X,BottomMax.Y,Z0};
    V[4]={TopMin.X,TopMin.Y,Z1}; V[5]={TopMax.X,TopMin.Y,Z1};
    V[6]={TopMax.X,TopMax.Y,Z1}; V[7]={TopMin.X,TopMax.Y,Z1};
}
void AChunkedDestructibleZigguratPyramit::AddPrism(FVector2D BMin,FVector2D BMax,FVector2D TMin,
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
void AChunkedDestructibleZigguratPyramit::TileFrustum(FVector2D Bottom,FVector2D Top,double Z0,double Z1,
    int32 Material,FVector2D Center,bool bOpening)
{
    ZigguratTraversal::SolidSections(Bottom,Top,Z0,Z1,bOpening?GenerationShaftWidth:0,
        [&](FVector2D B0,FVector2D B1,FVector2D T0,FVector2D T1,double Lo,double Hi)
        { TilePrism(B0+Center,B1+Center,T0+Center,T1+Center,Lo,Hi,Material); });
}
void AChunkedDestructibleZigguratPyramit::TilePrism(FVector2D BMin,FVector2D BMax,FVector2D TMin,
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
void AChunkedDestructibleZigguratPyramit::FacadePrism(int32 Face,double U0B,double U1B,double U0T,double U1T,
    double OB,double OT,double T,double Z0,double Z1,int32 Material)
{
    if (Face==0) AddPrism({U0B,-OB},{U1B,-OB+T},{U0T,-OT},{U1T,-OT+T},Z0,Z1,Material);
    if (Face==1) AddPrism({U0B,OB-T},{U1B,OB},{U0T,OT-T},{U1T,OT},Z0,Z1,Material);
    if (Face==2) AddPrism({-OB,U0B},{-OB+T,U1B},{-OT,U0T},{-OT+T,U1T},Z0,Z1,Material);
    if (Face==3) AddPrism({OB-T,U0B},{OB,U1B},{OT-T,U0T},{OT,U1T},Z0,Z1,Material);
}
void AChunkedDestructibleZigguratPyramit::BuildFacade(int32 Face,int32 Tier,double W,double D,double Z,double H)
{
    const double HalfSpan=(Face<2?W:D)*.5, HalfDepth=(Face<2?D:W)*.5;
    const double SpanInset=(Face<2?W:D)*WallBatter, DepthInset=(Face<2?D:W)*WallBatter;
    const double CornerTrim=Face<2?0:WallThickness;
    const double TopHalf=HalfSpan-SpanInset-CornerTrim;
    const double Core=TopHalf-FMath::Min(double(WallThickness),TopHalf*.2);
    int32 Count=0;
    if (Tier==0) Count=Face<2?GroundFrontOpenings:GroundSideOpenings;
    else
    {
        const TArray<int32>& Counts=Face<2?FrontWindowCounts:SideWindowCounts;
        if (!Counts.IsEmpty()) Count=Counts[FMath::Min(Tier-1,Counts.Num()-1)];
    }
    if (Face==1 && !bBackOpenings) Count=0;
    Count=FMath::Clamp(Count,0,16);
    const double Spread=Core*2*OpeningSpread;
    const double ApertureWidth=FMath::Min(Tier==0?double(GroundOpeningWidth):WindowWidth*FMath::Sqrt(W/BaseWidth),
        Count>0 ? Spread/Count*.64 : 0.0);
    const double Sill=Tier==0?0:H*WindowSillRatio;
    const double ApertureHeight=Tier==0?H*GroundOpeningHeightRatio:
        FMath::Min(H*WindowHeightRatio,H-Sill-LintelHeight-8);
    TArray<FBox2D> Openings;
    for (int32 I=0;I<Count;++I)
    {
        const double U=Spread*((I+.5)/Count-.5);
        Openings.Add(FBox2D({U-ApertureWidth*.5,Z+Sill},{U+ApertureWidth*.5,Z+Sill+ApertureHeight}));
        ++GeneratedOpeningCount;
    }
    TArray<double> Xs={-Core,Core}, Zs={Z,Z+H};
    auto Grid=[](TArray<double>& Cuts,double Min,double Max,double Size)
    {
        const int32 N=FMath::Max(1,FMath::CeilToInt((Max-Min)/Size));
        for (int32 I=1;I<N;++I) Cuts.AddUnique(FMath::Lerp(Min,Max,double(I)/N));
    };
    Grid(Xs,-Core,Core,PieceSize); Grid(Zs,Z,Z+H,PieceSize);
    for (const auto& O:Openings) { Xs.AddUnique(O.Min.X);Xs.AddUnique(O.Max.X);Zs.AddUnique(O.Min.Y);Zs.AddUnique(O.Max.Y); }
    Xs.Sort();Zs.Sort();
    for (int32 R=0;R<Zs.Num()-1 && !bBudgetExceeded;++R)
    {
        const double A=(Zs[R]-Z)/H,B=(Zs[R+1]-Z)/H;
        const double OB=HalfDepth-DepthInset*A,OT=HalfDepth-DepthInset*B;
        for (int32 C=0;C<Xs.Num()-1 && !bBudgetExceeded;++C)
        {
            const FVector2D Mid((Xs[C]+Xs[C+1])*.5,(Zs[R]+Zs[R+1])*.5);
            bool bOpening=false;
            for (const auto& O:Openings) bOpening|=O.IsInside(Mid);
            if (!bOpening) FacadePrism(Face,Xs[C],Xs[C+1],Xs[C],Xs[C+1],OB,OT,WallThickness,Zs[R],Zs[R+1],0);
        }
        // Corner strips taper with the building, while window jambs stay vertical in elevation.
        // The spine stops short of the top corners, so every corner fragment remains a QUAD.
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
    if (Tier>0 && bWindowLintels)
        for (const auto& O:Openings)
        {
            const double Bottom=O.Max.Y,Top=FMath::Min(Z+H,Bottom+LintelHeight);
            const double OB=HalfDepth-DepthInset*(Bottom-Z)/H+18,OT=HalfDepth-DepthInset*(Top-Z)/H+18;
            const double Lo=FMath::Max(-Core,O.Min.X-LintelOverhang),Hi=FMath::Min(Core,O.Max.X+LintelOverhang);
            const int32 N=FMath::Max(1,FMath::CeilToInt((Hi-Lo)/PieceSize));
            for (int32 I=0;I<N;++I)
            {
                const double A=FMath::Lerp(Lo,Hi,double(I)/N),B=FMath::Lerp(Lo,Hi,double(I+1)/N);
                FacadePrism(Face,A,B,A,B,OB,OT,18,Bottom,Top,3);
            }
        }
}
void AChunkedDestructibleZigguratPyramit::BuildRailings(double W,double D,double Z)
{
    const double Post=20;
    for (int32 Face=0;Face<4;++Face)
    {
        const double Span=(Face<2?W:D)-Post*2,Offset=(Face<2?D:W)*.5-Post;
        const int32 N=FMath::Max(1,FMath::CeilToInt(Span/PieceSize));
        for (int32 I=0;I<N;++I)
        {
            const double A=-Span*.5+Span*I/N,B=-Span*.5+Span*(I+1)/N;
            FacadePrism(Face,A,B,A,B,Offset,Offset,Post,Z+RailingHeight-15,Z+RailingHeight,1);
        }
        const int32 Bays=FMath::Max(1,FMath::CeilToInt(Span/220));
        for (int32 I=Face<2?0:1;I<=Bays-(Face<2?0:1);++I)
        {
            const double U=-Span*.5+Span*I/Bays;
            FacadePrism(Face,U-10,U+10,U-10,U+10,Offset,Offset,Post,Z,Z+RailingHeight-15,1);
        }
    }
}

bool AChunkedDestructibleZigguratPyramit::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max) { V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min; };
    Clamp(BaseWidth,600,16000);Clamp(BaseDepth,600,16000);TierCount=FMath::Clamp(TierCount,1,10);
    Clamp(GroundTierHeight,180,2000);Clamp(TierHeightScale,.5f,1);Clamp(TopTierScale,.2f,1);
    Clamp(WallBatter,0,.16f);Clamp(WallThickness,10,200);Clamp(PieceSize,30,400);Clamp(PieceGap,0,5);
    // Keep the top room hollow even with narrow dimensions and thick walls.
    WallThickness=FMath::Min(WallThickness,FMath::Min(BaseWidth,BaseDepth)*TopTierScale*(1-2*WallBatter)*.18f);
    GroundFrontOpenings=FMath::Clamp(GroundFrontOpenings,0,16);GroundSideOpenings=FMath::Clamp(GroundSideOpenings,0,16);
    for (int32& N:FrontWindowCounts) N=FMath::Clamp(N,0,16);
    for (int32& N:SideWindowCounts) N=FMath::Clamp(N,0,16);
    Clamp(GroundOpeningWidth,50,800);Clamp(GroundOpeningHeightRatio,.3f,1);
    Clamp(WindowWidth,20,400);Clamp(WindowHeightRatio,.2f,.9f);Clamp(WindowSillRatio,0,.4f);
    Clamp(OpeningSpread,.35f,.95f);Clamp(LintelHeight,5,70);Clamp(LintelOverhang,0,70);
    Clamp(TerraceOverhang,0,400);Clamp(TerraceThickness,10,150);Clamp(CorniceHeight,5,100);
    Clamp(RailingHeight,30,160);Clamp(FoundationHeight,0,400);Clamp(FoundationOverhang,0,400);
    StairCount=FMath::Clamp(StairCount,1,24);CrownStepCount=FMath::Clamp(CrownStepCount,1,12);
    Clamp(CrownHeight,40,1500);Clamp(CrownBaseScale,.3f,.95f);Clamp(CrownTopScale,.15f,.8f);
    const double RoomWidth=FMath::Min(BaseWidth,BaseDepth)*(TierCount>1?TopTierScale:1)*(1-2*WallBatter)-2*WallThickness;
    const double FittedWidth=ZigguratTraversal::FitOpening(Traversal,RoomWidth);
    GenerationShaftWidth=Traversal.bFloorOpenings?FittedWidth:0;

    TArray<FPiece> Previous=MoveTemp(Pieces);
    const int32 PreviousOpenings=GeneratedOpeningCount;
    GeneratedOpeningCount=0;bBudgetExceeded=false;GenerationWarning.Reset();
    const FVector2D Foundation(BaseWidth+2*FoundationOverhang,BaseDepth+2*FoundationOverhang);
    TileFrustum(Foundation,Foundation-FVector2D(35),0,FoundationHeight,4);
    if (bEntranceStairs && GroundFrontOpenings>0 && FoundationHeight>0)
    {
        const double Width=FMath::Min(double(GroundOpeningWidth)*1.7,BaseWidth*.7);
        for (int32 I=0;I<StairCount;++I)
        {
            const FVector2D Center(0,-Foundation.Y*.5-(StairCount-I-.5)*36);
            TileFrustum({Width,36},{Width,36},0,FoundationHeight*(I+1)/StairCount,4,Center);
        }
    }
    double Z=FoundationHeight,RoofW=0,RoofD=0,HighestRailing=0;
    for (int32 Tier=0;Tier<TierCount && !bBudgetExceeded;++Tier)
    {
        const double Scale=FMath::Lerp(1.0,double(TopTierScale),TierCount>1?double(Tier)/(TierCount-1):0.0);
        const double W=BaseWidth*Scale,D=BaseDepth*Scale;
        const double H=FMath::Max(180.0,GroundTierHeight*FMath::Pow(double(TierHeightScale),Tier));
        for (int32 Face=0;Face<4;++Face) BuildFacade(Face,Tier,W,D,Z,H);
        Z+=H;
        const FVector2D WallTop(W*(1-2*WallBatter),D*(1-2*WallBatter));
        const double Overhang=TerraceOverhang*Scale;
        const double Band=bCorniceBands?CorniceHeight*FMath::Sqrt(Scale):0;
        // A chamfered lower fascia and a second thin lip reproduce the stepped stone cornice.
        if (bCorniceBands)
        {
            TileFrustum(WallTop+FVector2D(Overhang*.35),WallTop+FVector2D(Overhang*1.35),Z,Z+Band*.65,1,FVector2D::ZeroVector,true);
            Z+=Band*.65;
            TileFrustum(WallTop+FVector2D(Overhang*1.35),WallTop+FVector2D(Overhang*1.65),Z,Z+Band*.35,1,FVector2D::ZeroVector,true);
            Z+=Band*.35;
        }
        RoofW=WallTop.X+2*Overhang;RoofD=WallTop.Y+2*Overhang;
        TileFrustum({RoofW,RoofD},{RoofW,RoofD},Z,Z+TerraceThickness,2,FVector2D::ZeroVector,true);
        Z+=TerraceThickness;
        if (bRailings) { BuildRailings(RoofW,RoofD,Z);HighestRailing=Z+RailingHeight; }
    }
    if (bPyramidCrown)
    {
        const FVector2D CrownBase(RoofW*CrownBaseScale,RoofD*CrownBaseScale);
        double Weight=0;
        for (int32 I=0;I<CrownStepCount;++I) Weight+=FMath::Pow(.78,I);
        for (int32 I=0;I<CrownStepCount && !bBudgetExceeded;++I)
        {
            const double Scale=FMath::Lerp(1.0,double(CrownTopScale),CrownStepCount>1?double(I)/(CrownStepCount-1):0.0);
            const double Height=CrownHeight*FMath::Pow(.78,I)/Weight;
            TileFrustum(CrownBase*Scale,CrownBase*Scale*.95,Z,Z+Height,5,FVector2D::ZeroVector,true);
            Z+=Height;
        }
    }
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);GeneratedOpeningCount=PreviousOpenings;
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce the building dimensions. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);
        return false;
    }
    GeneratedHeight=FMath::Max(Z,HighestRailing);GeneratedPieceCount=Pieces.Num();
    FloorMaterials={StoneMaterial,CorniceMaterial,TerraceMaterial,LintelMaterial,FoundationMaterial,CrownMaterial};
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

bool AChunkedDestructibleZigguratPyramit::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleZigguratPyramit::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Bounds.GetCenter():FVector::ZeroVector; }
int32 AChunkedDestructibleZigguratPyramit::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Material:0; }
FTransform AChunkedDestructibleZigguratPyramit::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructibleZigguratPyramit::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const
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
void AChunkedDestructibleZigguratPyramit::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
}
void AChunkedDestructibleZigguratPyramit::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
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
bool AChunkedDestructibleZigguratPyramit::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
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
bool AChunkedDestructibleZigguratPyramit::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructibleZigguratPyramit::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructibleZigguratPyramit::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
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
void AChunkedDestructibleZigguratPyramit::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
