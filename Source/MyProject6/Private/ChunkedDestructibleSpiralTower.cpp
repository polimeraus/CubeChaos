#include "ChunkedDestructibleSpiralTower.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/ChildActorComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/World.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"
using namespace UE::Geometry;

AChunkedDestructibleSpiralTower::AChunkedDestructibleSpiralTower()
{
    FloorPattern=EChunkedFloorPattern::Solid;
    ChunkSideLength=96; MaxGeneratedCells=30000;
    bGenerateBottomFaces=true; bReactToMusic=false;
    DefaultCellHealth=25; bUseDestructionRipple=false;
    GroundLaunchPad=CreateDefaultSubobject<UChildActorComponent>(TEXT("GroundLaunchPad"));
    GroundLaunchPad->SetupAttachment(GetRootComponent());
    ZigguratTraversal::InitializeMaterials(Traversal);
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stone(TEXT("/Game/Structures/Destructible/SpiralTower/Materials/MI_SpiralTowerStone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stairs(TEXT("/Game/Structures/Destructible/SpiralTower/Materials/MI_SpiralTowerStairs"));
    StoneMaterial=Stone.Get(); StairMaterial=Stairs.Get();
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Glow(TEXT("/Game/Structures/Destructible/SpiralTower/Materials/M_SpiralTowerFlowGlow"));
    GlowMaterial=Glow.Get();
}
void AChunkedDestructibleSpiralTower::RebuildTower() { GenerateFloor(false); }
void AChunkedDestructibleSpiralTower::BeginPlay()
{
    GlowMaterialInstance=nullptr;
    Super::BeginPlay();
}
void AChunkedDestructibleSpiralTower::RefreshGlowAnimation()
{
    UMaterialInterface* Selected=GlowMaterial;
    if (!Selected) Selected=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Structures/Destructible/SpiralTower/Materials/M_SpiralTowerFlowGlow"));
    if (!Selected) return;
    if (!GlowMaterialInstance || GlowMaterialInstance->Parent!=Selected)
    {
        GlowMaterialInstance=UMaterialInstanceDynamic::Create(Selected,this);
        GlowMaterialInstance->SetScalarParameterValue(TEXT("StartTime"),GetWorld()?GetWorld()->GetTimeSeconds():0);
    }
    auto Safe=[](float V,float Min,float Max,float Default) { return FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Default; };
    GlowMaterialInstance->SetScalarParameterValue(TEXT("BandHeight"),Safe(GlowBandHeight,10,20000,900));
    GlowMaterialInstance->SetScalarParameterValue(TEXT("TrailLength"),Safe(GlowTrailLength,10,50000,600));
    GlowMaterialInstance->SetScalarParameterValue(TEXT("PathLength"),FMath::Max(1.f,GeneratedGlowPathLength));
    GlowMaterialInstance->SetScalarParameterValue(TEXT("Speed"),Safe(GlowSpeed,1,10000,500));
    GlowMaterialInstance->SetScalarParameterValue(TEXT("Pause"),Safe(GlowPause,0,60,1.2f));
    GlowMaterialInstance->SetScalarParameterValue(TEXT("Loop"),bLoopGlow?1:0);
    GlowMaterialInstance->SetScalarParameterValue(TEXT("Brightness"),bFlowingGlow?Safe(GlowBrightness,0,100,6):0);
    GlowMaterialInstance->SetVectorParameterValue(TEXT("Tint"),GlowColor);
    GlowMaterialInstance->SetScalarParameterValue(TEXT("PreviewTime"),FMath::IsFinite(GlowPreviewTime)?GlowPreviewTime:-1);
    if (FloorMaterials.IsValidIndex(2)) FloorMaterials[2]=GlowMaterialInstance;
    for (auto& Component:ChunkComponents) if (Component) Component->SetMaterial(2,GlowMaterialInstance);
}
#if WITH_EDITOR
bool AChunkedDestructibleSpiralTower::CanEditChange(const FProperty* P) const
{
    if (P && P->GetFName()==GET_MEMBER_NAME_CHECKED(AChunkedDestructibleSpiralTower,WalkwayWidth) && bFitStairsBetweenWalls) return false;
    if (P) for (const TCHAR* Derived:{TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
        TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
        TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
        if (P->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(P);
}
#endif

bool AChunkedDestructibleSpiralTower::FPiece::Contains(FVector P,double Tolerance) const
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
void AChunkedDestructibleSpiralTower::AddPiece(const FVector (&V)[8],int32 Material,bool SmoothInner,bool ReceivesGlow)
{
    if (bBudgetExceeded) return;
    if (Pieces.Num()>=FMath::Clamp(MaxGeneratedCells,1,100000)) { bBudgetExceeded=true;return; }
    FPiece P;P.Material=Material;P.bSmoothInnerFace=SmoothInner;P.Bounds=FBox(ForceInit);
    P.bReceivesGlow=ReceivesGlow;
    FVector Center=FVector::ZeroVector;for (const auto& X:V) {Center+=X;P.Bounds+=X;}Center/=8;
    const FVector Size=P.Bounds.GetSize();if (Size.GetMin()<.001) return;
    const double Shrink=1-FMath::Min(double(PieceGap)/Size.GetMin(),.2);
    P.Bounds=FBox(ForceInit);
    for (int32 I=0;I<8;++I) { P.V[I]=Center+(V[I]-Center)*Shrink;P.Bounds+=P.V[I]; }
    Pieces.Add(P);
}
double AChunkedDestructibleSpiralTower::InteriorRadiusAtHeight(double Z) const
{
    return FMath::Max(150.,InteriorBaseRadius-InteriorSlope*FMath::Max(0.,Z-InteriorFloorZ));
}
// Inner vertices share one continuous cone, while the outside follows the spiral.
// Both rings have planar side faces, suitable for the fragment's convex collision.
void AChunkedDestructibleSpiralTower::AddSector(double T0,double T1,double Inset0,double Inset1,double Z0,double Z1,int32 Material,
    double BottomScale,double TopScale,bool SmoothInner,double GlowTurnOffset)
{
    if (bBudgetExceeded || Z1<=Z0 || Inset1<=Inset0) return;
    if (SmoothInner && InteriorSlope>0)
    {
        const double Join=InteriorFloorZ+(InteriorBaseRadius-150)/InteriorSlope;
        if (Join>Z0+.001 && Join<Z1-.001)
        {
            const double Scale=FMath::Lerp(BottomScale,TopScale,(Join-Z0)/(Z1-Z0));
            AddSector(T0,T1,Inset0,Inset1,Z0,Join,Material,BottomScale,Scale,true,GlowTurnOffset);
            AddSector(T0,T1,Inset0,Inset1,Join,Z1,Material,Scale,TopScale,true,GlowTurnOffset);
            return;
        }
    }
    if (bClockwise) Swap(T0,T1);
    auto Point=[&](double T,double Inset,double Z,double Scale)
    {
        const double Angle=FMath::DegreesToRadians(double(StartAngle))+(bClockwise?-1.:1.)*UE_TWO_PI*T;
        const double R=SmoothInner && Inset==Inset0?InteriorRadiusAtHeight(Z):
            (FMath::Lerp(double(BaseRadius),double(TopRadius),T/Turns)-WalkwayWidth+Inset)*Scale;
        return FVector(FMath::Cos(Angle)*R,FMath::Sin(Angle)*R,Z);
    };
    const FVector V[8]={Point(T0,Inset0,Z0,BottomScale),Point(T0,Inset1,Z0,BottomScale),Point(T1,Inset1,Z0,BottomScale),Point(T1,Inset0,Z0,BottomScale),
                        Point(T0,Inset0,Z1,TopScale),Point(T0,Inset1,Z1,TopScale),Point(T1,Inset1,Z1,TopScale),Point(T1,Inset0,Z1,TopScale)};
    const int32 Before=Pieces.Num();
    AddPiece(V,Material,SmoothInner);
    if (Pieces.Num()>Before)
    {
        // Unwrapped path coordinates distinguish consecutive turns even at the same
        // angle/height. A wall column follows the tread it supports above it.
        FPiece& P=Pieces.Last();
        const float S0=StairDistanceAtTurn(T0+GlowTurnOffset),S1=StairDistanceAtTurn(T1+GlowTurnOffset);
        const double TreadZ=GeneratingTreadZ+HeightAtTurn(GeneratingTreadTurn+GlowTurnOffset)-HeightAtTurn(GeneratingTreadTurn);
        for (int32 I=0;I<8;++I)
            P.GlowUV[I]=FVector2f((I%4<2)?S0:S1,P.V[I].Z-TreadZ);
    }
}

void AChunkedDestructibleSpiralTower::AddFoundation()
{
    if (!bFoundation) return;
    const double Extent=BaseRadius+FoundationMargin,BodyExtent=Extent-25,Depth=FMath::Max(80.,double(WallThickness));
    const double End=BodyExtent-Depth,HalfOpening=FoundationHeight*.18;
    const double Sill=FoundationHeight*.2,Lintel=FoundationHeight*.65;
    constexpr int32 Openings=8;
    auto Block=[&](int32 Side,double X0,double X1,double Y0,double Y1,double Bottom0,double Bottom1,double Top)
    {
        const FQuat Rotation(FVector::UpVector,Side*UE_HALF_PI);
        FVector V[8]={{X0,Y0,Bottom0},{X1,Y0,Bottom1},{X1,Y1,Bottom1},{X0,Y1,Bottom0},
                      {X0,Y0,Top},{X1,Y0,Top},{X1,Y1,Top},{X0,Y1,Top}};
        for (FVector& P:V) P=Rotation.RotateVector(P);
        AddPiece(V,0,false,false);
    };
    TArray<double> Cuts={-End,End},Centers;
    for (int32 I=0;I<Openings;++I)
    {
        const double Center=-End+2*End*(I+.5)/Openings;Centers.Add(Center);
        Cuts.Add(Center-HalfOpening);Cuts.Add(Center+HalfOpening);
    }
    const int32 Divisions=FMath::CeilToInt(2*End/PieceSize);
    for (int32 I=1;I<Divisions;++I) Cuts.Add(-End+2*End*I/Divisions);
    Cuts.Sort();
    for (int32 Side=0;Side<4;++Side)
    {
        Block(Side,-BodyExtent,-End,-BodyExtent,-End,0,0,FoundationHeight-SlabThickness);
        for (int32 I=0;I+1<Cuts.Num();++I)
        {
            const double L=Cuts[I],R=Cuts[I+1],Mid=(L+R)*.5;
            bool Opening=false;
            for (double C:Centers) if (FMath::Abs(Mid-C)<HalfOpening) { Opening=true;break; }
            if (Opening)
            {
                Block(Side,L,R,-BodyExtent,-End,0,0,Sill);
                Block(Side,L,R,-BodyExtent,-End,Lintel,Lintel,FoundationHeight-SlabThickness);
            }
            else Block(Side,L,R,-BodyExtent,-End,0,0,FoundationHeight-SlabThickness);
            // Foot and recessed upper course frame the square plinth, retaining real vents.
            Block(Side,L,R,-Extent,-BodyExtent+10,0,0,FoundationHeight*.12);
        }
    }
    // Square terrace with a circular opening, never a solid plate across the hollow tower.
    // Include every square corner so each wedge stays convex with planar side faces.
    const int32 DeckSegments=8*FMath::DivideAndRoundUp(FMath::Max(StepsPerTurn,
        int32(FMath::CeilToInt(UE_TWO_PI*Extent*FMath::Sqrt(2.)/PieceSize))),8);
    const double Inner=BaseRadius-WalkwayWidth,Bottom=FoundationHeight-SlabThickness;
    const int32 DeckRows=FMath::Max(1,FMath::CeilToInt((Extent*FMath::Sqrt(2.)-Inner)/PieceSize));
    auto DeckPoint=[&](double Angle,double Fraction,double Z)
    {
        const double X=FMath::Cos(Angle),Y=FMath::Sin(Angle);
        const double Radius=FMath::Lerp(Inner,Extent/FMath::Max(FMath::Abs(X),FMath::Abs(Y)),Fraction);
        return FVector(X*Radius,Y*Radius,Z);
    };
    for (int32 I=0;I<DeckSegments;++I)
    {
        const double L=UE_TWO_PI*I/DeckSegments,R=UE_TWO_PI*(I+1)/DeckSegments;
        for (int32 Row=0;Row<DeckRows;++Row)
        {
            const double A=double(Row)/DeckRows,B=double(Row+1)/DeckRows;
            const FVector V[8]={DeckPoint(L,A,Bottom),DeckPoint(L,B,Bottom),DeckPoint(R,B,Bottom),DeckPoint(R,A,Bottom),
                DeckPoint(L,A,FoundationHeight),DeckPoint(L,B,FoundationHeight),DeckPoint(R,B,FoundationHeight),DeckPoint(R,A,FoundationHeight)};
            AddPiece(V,0,false,false);
        }
    }
}

double AChunkedDestructibleSpiralTower::HeightAtTurn(double T) const
{
    const double U=FMath::Clamp(T,0.,double(Turns));
    return RisePerTurn*(U-(1-TopRiseScale)*U*U/(2*Turns)+TopRiseScale*FMath::Max(T-Turns,0.));
}
double AChunkedDestructibleSpiralTower::WallHeightAtTurn(double T) const
{
    return FMath::Max(10.,(HeightAtTurn(T+1)-HeightAtTurn(T))*(WallHeight+SlabThickness)/RisePerTurn-SlabThickness);
}
double AChunkedDestructibleSpiralTower::StairDistanceAtTurn(double T) const
{
    // Arc length of the tapered, rising stair centerline. Simpson integration is
    // stable across angular seams and gives constant physical speed on every turn.
    const double RadialPitch=(TopRadius-BaseRadius)/Turns;
    auto Speed=[&](double U)
    {
        const double Radius=BaseRadius-(WalkwayWidth-WallThickness)*.5+RadialPitch*U;
        const double Rise=RisePerTurn*(1-(1-TopRiseScale)*U/Turns);
        return FMath::Sqrt(FMath::Square(RadialPitch)+FMath::Square(UE_TWO_PI*Radius)+FMath::Square(Rise));
    };
    constexpr int32 Intervals=16;
    const double U=FMath::Clamp(T,0.,double(Turns));
    const double H=U/Intervals;
    double Sum=Speed(0)+Speed(U);
    for (int32 I=1;I<Intervals;++I) Sum+=(I%2?4:2)*Speed(I*H);
    // After the last tread, continue around the open crown at the summit radius.
    // This keeps the final wall sweep positive and continuous for short towers too.
    const double SummitRadius=TopRadius-(WalkwayWidth-WallThickness)*.5;
    const double SummitSpeed=FMath::Sqrt(FMath::Square(UE_TWO_PI*SummitRadius)+FMath::Square(RisePerTurn*TopRiseScale));
    return Sum*H/3+FMath::Max(0.,T-Turns)*SummitSpeed;
}
bool AChunkedDestructibleSpiralTower::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max) { V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min; };
    Clamp(BaseRadius,600,6000); Clamp(Turns,1,12); Clamp(RisePerTurn,260,1200);
    Clamp(TopRiseScale,.25,1);
    Clamp(WallThickness,10,180);
    if (bFitStairsBetweenWalls)
    {
        const float MinimumTop=FMath::Max((BaseRadius+Turns*(WallThickness+150))/(Turns+1),BaseRadius-Turns*(900-WallThickness));
        Clamp(TopRadius,MinimumTop,BaseRadius);
        WalkwayWidth=(BaseRadius-TopRadius)/Turns+WallThickness;
    }
    else
    {
        Clamp(WalkwayWidth,80,FMath::Min(900.f,BaseRadius-250));
        Clamp(TopRadius,WalkwayWidth+150,BaseRadius);
        Clamp(WallThickness,10,FMath::Min(180.f,WalkwayWidth*.4f));
    }
    Clamp(SummitWallExtension,0,FMath::Min(1.f,Turns-.25f));
    Clamp(SlabThickness,10,80); Clamp(WallHeight,80,RisePerTurn-SlabThickness);
    Clamp(WallInset,0,FMath::Min(400.f,TopRadius-WalkwayWidth-150));
    Clamp(FoundationHeight,100,400); Clamp(FoundationMargin,0,300);
    Clamp(PieceSize,50,300); Clamp(PieceGap,0,3);
    StepsPerTurn=FMath::Clamp(StepsPerTurn,48,256);
    if (!FMath::IsFinite(StartAngle)) StartAngle=-90;
    const double StairTurns=Turns-SummitWallExtension;
    // The returning crown is the upper edge of the existing front wall, not a
    // second overlapping spiral. This also leaves a completely open rear.
    const bool ReturnCrown=bReverseSummitCrescent && SummitWallExtension>0;
    const double MeshTurns=ReturnCrown?StairTurns:double(Turns);
    const int32 Steps=FMath::CeilToInt(MeshTurns*StepsPerTurn);
    InteriorFloorZ=bFoundation?FoundationHeight:0;
    InteriorBaseRadius=BaseRadius-WalkwayWidth-WallInset;
    const double LastInner=FMath::Lerp(double(BaseRadius),double(TopRadius),MeshTurns/Turns)-WalkwayWidth;
    const double LastWallThickness=WallThickness*(1-WallInset/LastInner);
    // Keep at least 30 percent of the wall thickness at the narrowest stair join.
    // A straight inner cone avoids the shelves produced by stacking spiral slabs.
    InteriorSlope=FMath::Max(0.,(InteriorBaseRadius-(LastInner-WallInset+.7*LastWallThickness))/HeightAtTurn(MeshTurns));
    const int32 InnerDivisions=FMath::Max(4,FMath::CeilToInt(UE_TWO_PI*BaseRadius/StepsPerTurn/PieceSize));
    TArray<FPiece> Previous=MoveTemp(Pieces);
    bBudgetExceeded=false; GenerationWarning.Reset();
    for (int32 Step=0;Step<Steps && !bBudgetExceeded;++Step)
    {
        const double T0=double(Step)/StepsPerTurn,T1=FMath::Min(MeshTurns,double(Step+1)/StepsPerTurn);
        const int32 Columns=InnerDivisions;
        const double Base=bFoundation?FoundationHeight:0;
        const double Top=Base+SlabThickness+HeightAtTurn(T1),Bottom=Base+HeightAtTurn(T0);
        GeneratingTreadZ=Top;
        GeneratingTreadTurn=T1;
        for (int32 C=0;C<Columns && !bBudgetExceeded;++C)
        {
            const double L=FMath::Lerp(T0,T1,double(C)/Columns),U=FMath::Lerp(T0,T1,double(C+1)/Columns);
            // The final half turn is just the thick summit wall, with no projecting balcony.
            const bool IsStair=T0<StairTurns;
            const double Width=IsStair?WalkwayWidth:WallThickness;
            // The part below the inner wall is masonry. Stair fragments start at
            // its exterior face, so no tread surface extends into the room.
            AddSector(L,U,0,WallThickness,Bottom,Top,0,1,1,true);
            const double TreadWidth=Width-WallThickness;
            const int32 Radial=FMath::Max(1,FMath::CeilToInt(TreadWidth/PieceSize));
            for (int32 RIndex=0;RIndex<Radial && !bBudgetExceeded;++RIndex)
                AddSector(L,U,WallThickness+TreadWidth*RIndex/Radial,
                    WallThickness+TreadWidth*(RIndex+1)/Radial,Bottom,Top,1);
            const double InnerRadius=FMath::Lerp(double(BaseRadius),double(TopRadius),(L+U)*.5/Turns)-WalkwayWidth;
            // The first turn has no previous revolution underneath its outside edge.
            // Carry that edge down to the plinth with an upright, hollow skirt wall.
            // At one full turn it meets the inner wall at the staircase entrance.
            if (T0<1 && Bottom>Base)
            {
                const int32 SupportRows=FMath::CeilToInt((Bottom-Base)/PieceSize);
                for (int32 Row=0;Row<SupportRows && !bBudgetExceeded;++Row)
                {
                    const double A=double(Row)/SupportRows,B=double(Row+1)/SupportRows;
                    AddSector(L,U,0,WalkwayWidth,
                        FMath::Lerp(Base,Bottom,A),FMath::Lerp(Base,Bottom,B),0,1,1,true);
                }
            }
            // Wall and inner tread faces use the same angular divisions throughout.
            const int32 WallColumns=1;
            for (int32 W=0;W<WallColumns && !bBudgetExceeded;++W)
            {
                const double WL=FMath::Lerp(L,U,double(W)/WallColumns),WU=FMath::Lerp(L,U,double(W+1)/WallColumns);
                // Start exactly on the flat tread. Starting on the smooth rise
                // would bury the wall in the tread and duplicate its top face.
                const double WallBase=Top;
                const double CrownTurn=ReturnCrown && WL>=StairTurns-SummitWallExtension?2*StairTurns-WL:WL;
                double BandHeight=Base+SlabThickness+HeightAtTurn(CrownTurn)+WallHeightAtTurn(CrownTurn)-WallBase;
                // The hidden wall crown ends at the flat underside of the next tread.
                // Coplanar outer faces must not overlap, or the rim flickers in the renderer.
                if (bFitStairsBetweenWalls && T0+1<StairTurns && WallInset==0)
                    BandHeight=FMath::Min(BandHeight,Base+HeightAtTurn(T0+1)-WallBase);
                const int32 WallRows=FMath::Max(1,FMath::CeilToInt(BandHeight/PieceSize));
                for (int32 Row=0;Row<WallRows && !bBudgetExceeded;++Row)
                {
                    const double Lower=double(Row)/WallRows,Upper=double(Row+1)/WallRows;
                    AddSector(WL,WU,0,WallThickness,WallBase+BandHeight*Lower,WallBase+BandHeight*Upper,0,
                        1-WallInset*Lower/InnerRadius,1-WallInset*Upper/InnerRadius,true,1);
                }
            }
        }
    }
    AddFoundation();
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce Turns / Steps Per Turn. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);
        return false;
    }
    GeneratedPieceCount=Pieces.Num(); GeneratedStepCount=FMath::CeilToInt(StairTurns*StepsPerTurn);
    GeneratedHeight=(bFoundation?FoundationHeight:0)+SlabThickness+HeightAtTurn(Turns)+WallHeightAtTurn(Turns);
    // The entrance skirt is turn zero; every inner wall supports the next turn.
    // Include the last wall so it finishes before the wave pauses or loops.
    GeneratedGlowPathLength=StairDistanceAtTurn(MeshTurns+1);
    // The polygonal chord is slightly inside its endpoint radii.
    ClearShaftDiameter=2*InteriorRadiusAtHeight(GeneratedHeight)*FMath::Cos(UE_PI/(StepsPerTurn*InnerDivisions));
    RefreshGlowAnimation();
    FloorMaterials={StoneMaterial,StairMaterial,GlowMaterialInstance};
    for (auto& Material:FloorMaterials) if (!Material) Material=UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount={Pieces.Num(),1};
    ChunkCount={FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1};
    EffectiveCellDimensions=FVector(PieceSize,PieceSize,WallThickness); CellDimensions=EffectiveCellDimensions;
    CellSpacing=FVector2D(PieceGap); EffectiveGridSize=FVector2D(2*BaseRadius); GridLocalMin=-EffectiveGridSize*.5; AreaSize=EffectiveGridSize;
    // A square pad must fit inside the circular shaft even at its corners.
    // The pad stays on ground level; the optional plinth is hollow at the center.
    ZigguratTraversal::UpdatePad(GroundLaunchPad,Traversal,ClearShaftDiameter/FMath::Sqrt(2.),0,GeneratedHeight);
    return true;
}

bool AChunkedDestructibleSpiralTower::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleSpiralTower::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Bounds.GetCenter():FVector::ZeroVector; }
int32 AChunkedDestructibleSpiralTower::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Material:0; }
FTransform AChunkedDestructibleSpiralTower::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructibleSpiralTower::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material,bool GlowOverlay) const
{
    const FVector* V=Piece.V;
    // Counter-clockwise corners along each outward normal; AppendMeshQuad handles UE winding.
    constexpr int32 Faces[6][4]={{4,5,6,7},{3,2,1,0},{0,1,5,4},{2,3,7,6},{1,2,6,5},{3,0,4,7}};
    for (int32 FaceIndex=0;FaceIndex<6;++FaceIndex)
    {
        const auto& F=Faces[FaceIndex];
        FVector A=V[F[0]],B=V[F[1]],C=V[F[2]],D=V[F[3]];
        const FVector N=FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        const FVector M=FVector::CrossProduct(C-A,D-A).GetSafeNormal();
        if (GlowOverlay)
        {
            // Separate additive skin, with no collision. The source masonry is never replaced.
            const FVector Offset=(N+M).GetSafeNormal()*.15;
            A+=Offset;B+=Offset;C+=Offset;D+=Offset;
        }
        const int32 FirstTriangle=Mesh.MaxTriangleID();
        auto SetPathUV=[&](int32 I,int32 J,int32 K)
        {
            auto* UV=Mesh.Attributes()->GetUVLayer(1);
            // AppendMeshTriangle uses reversed (A,C,B) winding.
            const int32 U=UV->AppendElement(Piece.GlowUV[I]);
            const int32 W=UV->AppendElement(Piece.GlowUV[K]);
            const int32 VIndex=UV->AppendElement(Piece.GlowUV[J]);
            UV->SetTriangle(Mesh.MaxTriangleID()-1,FIndex3i(U,W,VIndex));
        };
        if (!N.IsNearlyZero())
        {
            AppendMeshTriangle(Mesh,A,B,C,FVector3f(N),Material,FVector2f(0,0),FVector2f((B-A).Length()/UVWorldUnits,0),FVector2f((C-A).Length()/UVWorldUnits,(C-B).Length()/UVWorldUnits));
            SetPathUV(F[0],F[1],F[2]);
        }
        if (!M.IsNearlyZero())
        {
            AppendMeshTriangle(Mesh,A,C,D,FVector3f(M),Material,FVector2f(0,0),FVector2f((C-D).Length()/UVWorldUnits,(D-A).Length()/UVWorldUnits),FVector2f(0,(D-A).Length()/UVWorldUnits));
            SetPathUV(F[0],F[2],F[3]);
        }
        if (Piece.bSmoothInnerFace && FaceIndex==5)
        {
            const double Slope=(FVector2D(V[4].X,V[4].Y).Size()-FVector2D(V[0].X,V[0].Y).Size())/(V[4].Z-V[0].Z);
            auto* Normals=Mesh.Attributes()->PrimaryNormals();
            for (int32 T=FirstTriangle;T<Mesh.MaxTriangleID();++T)
            {
                const FIndex3i Vertices=Mesh.GetTriangle(T),Elements=Normals->GetTriangle(T);
                for (int32 I=0;I<3;++I)
                {
                    const FVector P=Mesh.GetVertex(Vertices[I]);
                    const double Radius=FVector2D(P.X,P.Y).Size();
                    const FVector3f Normal=FVector3f(FVector(-P.X/Radius,-P.Y/Radius,Slope).GetSafeNormal());
                    Mesh.SetVertexNormal(Vertices[I],Normal);
                    Normals->SetElement(Elements[I],Normal);
                }
            }
        }
    }
}
void AChunkedDestructibleSpiralTower::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(2);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
            if (Pieces[I].bReceivesGlow && GlowMaterialInstance) AppendPiece(Mesh,Pieces[I],2,true);
        }
}
void AChunkedDestructibleSpiralTower::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
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
bool AChunkedDestructibleSpiralTower::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
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
bool AChunkedDestructibleSpiralTower::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructibleSpiralTower::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructibleSpiralTower::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
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
void AChunkedDestructibleSpiralTower::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
