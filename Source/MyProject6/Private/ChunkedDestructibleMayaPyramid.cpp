#include "ChunkedDestructibleMayaPyramid.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/DynamicMeshComponent.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "PhysicsEngine/ConvexElem.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

AChunkedDestructibleMayaPyramid::AChunkedDestructibleMayaPyramid()
{
    FloorPattern=EChunkedFloorPattern::Solid;
    ChunkSideLength=96;
    MaxGeneratedCells=40000;
    bGenerateBottomFaces=true;
    bReactToMusic=false;
    DefaultCellHealth=25;
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Wall(TEXT("/Game/Structures/Destructible/MayaPyramid/Materials/MI_Maya_ArchitecturalGlass"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Ledge(TEXT("/Game/Structures/Destructible/MayaPyramid/Materials/MI_Maya_IvoryConcrete"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Accent(TEXT("/Game/Structures/Destructible/MayaPyramid/Materials/MI_Maya_BronzeFrames"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Stair(TEXT("/Game/Structures/Destructible/MayaPyramid/Materials/MI_Maya_BlueGraphite"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Shrine(TEXT("/Game/Structures/Destructible/MayaPyramid/Materials/MI_Maya_WarmRooms"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Crest(TEXT("/Game/Structures/Destructible/MayaPyramid/Materials/MI_Maya_WarmLight"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Gold(TEXT("/Game/Structures/Destructible/MayaPyramid/Materials/MI_Maya_TerraceStone"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Steps(TEXT("/Game/Structures/Destructible/MayaPyramid/Materials/MI_Maya_CharcoalSteps"));
    StepMaterial=Steps.Get();
    WallMaterial=Wall.Get();LedgeMaterial=Ledge.Get();AccentMaterial=Accent.Get();StairMaterial=Stair.Get();
    ShrineMaterial=Shrine.Get();CrestMaterial=Crest.Get();GoldMaterial=Gold.Get();
}

void AChunkedDestructibleMayaPyramid::RebuildMayaPyramid() { GenerateFloor(false); }

#if WITH_EDITOR
bool AChunkedDestructibleMayaPyramid::CanEditChange(const FProperty* Property) const
{
    if (Property)
        for (const TCHAR* Derived : {TEXT("AreaSize"),TEXT("CellDimensions"),TEXT("CellSpacing"),TEXT("FillRule"),
            TEXT("FloorPattern"),TEXT("FloorMaterials"),TEXT("PrimaryMaterialIndex"),TEXT("SecondaryMaterialIndex"),
            TEXT("PatternCellScale"),TEXT("PatternRandomSeed")})
            if (Property->GetFName()==FName(Derived)) return false;
    return Super::CanEditChange(Property);
}
#endif

bool AChunkedDestructibleMayaPyramid::FPiece::Contains(FVector P,double Tolerance) const
{
    if (!Bounds.ExpandBy(Tolerance).IsInsideOrOn(P)) return false;
    const double Alpha=FMath::Clamp((P.Z-Z0)/(Z1-Z0),0.0,1.0);
    const FVector2D Min=FMath::Lerp(BottomMin,TopMin,Alpha),Max=FMath::Lerp(BottomMax,TopMax,Alpha);
    return P.X>=Min.X-Tolerance && P.X<=Max.X+Tolerance && P.Y>=Min.Y-Tolerance && P.Y<=Max.Y+Tolerance;
}
void AChunkedDestructibleMayaPyramid::FPiece::Vertices(FVector (&V)[8]) const
{
    V[0]={BottomMin.X,BottomMin.Y,Z0}; V[1]={BottomMax.X,BottomMin.Y,Z0};
    V[2]={BottomMax.X,BottomMax.Y,Z0}; V[3]={BottomMin.X,BottomMax.Y,Z0};
    V[4]={TopMin.X,TopMin.Y,Z1}; V[5]={TopMax.X,TopMin.Y,Z1};
    V[6]={TopMax.X,TopMax.Y,Z1}; V[7]={TopMin.X,TopMax.Y,Z1};
}

void AChunkedDestructibleMayaPyramid::TilePrism(FVector2D BMin,FVector2D BMax,FVector2D TMin,
    FVector2D TMax,double Z0,double Z1,int32 Material,int32 TopMaterial)
{
    const FVector2D Bottom=BMax-BMin,Top=TMax-TMin;
    if (bBudgetExceeded || Z1-Z0<.001 || Bottom.GetMin()<.001 || Top.GetMin()<.001) return;
    const int32 NX=FMath::Max(1,FMath::CeilToInt(FMath::Max(Bottom.X,Top.X)/PieceSize));
    const int32 NY=FMath::Max(1,FMath::CeilToInt(FMath::Max(Bottom.Y,Top.Y)/PieceSize));
    const int32 NZ=FMath::Max(1,FMath::CeilToInt((Z1-Z0)/PieceSize));
    if (int64(Pieces.Num())+int64(NX)*NY*NZ>FMath::Clamp(MaxGeneratedCells,1,100000)) { bBudgetExceeded=true;return; }
    for (int32 Z=0;Z<NZ;++Z)
        for (int32 Y=0;Y<NY;++Y)
            for (int32 X=0;X<NX;++X)
            {
                const FVector2D S0=FMath::Lerp(Bottom,Top,double(Z)/NZ),S1=FMath::Lerp(Bottom,Top,double(Z+1)/NZ);
                const FVector2D O0=FMath::Lerp(BMin,TMin,double(Z)/NZ),O1=FMath::Lerp(BMin,TMin,double(Z+1)/NZ);
                const FVector2D Min(double(X)/NX,double(Y)/NY),Max(double(X+1)/NX,double(Y+1)/NY);
                FPiece P;
                P.BottomMin=O0+Min*S0;P.BottomMax=O0+Max*S0;P.TopMin=O1+Min*S1;P.TopMax=O1+Max*S1;
                P.Z0=FMath::Lerp(Z0,Z1,double(Z)/NZ);P.Z1=FMath::Lerp(Z0,Z1,double(Z+1)/NZ);
                P.Material=Material;P.TopMaterial=(Z==NZ-1 && TopMaterial>=0)?TopMaterial:Material;
                P.Bounds=FBox(FVector(FMath::Min(P.BottomMin.X,P.TopMin.X),FMath::Min(P.BottomMin.Y,P.TopMin.Y),P.Z0),
                    FVector(FMath::Max(P.BottomMax.X,P.TopMax.X),FMath::Max(P.BottomMax.Y,P.TopMax.Y),P.Z1));
                Pieces.Add(P);
            }
}
void AChunkedDestructibleMayaPyramid::Box(FVector2D Min,FVector2D Max,double Z0,double Z1,int32 Material,int32 TopMaterial)
{ TilePrism(Min,Max,Min,Max,Z0,Z1,Material,TopMaterial); }

void AChunkedDestructibleMayaPyramid::Ring(double Half,double Thickness,double Z0,double Z1,int32 Material)
{
    Box({-Half,-Half},{Half,-Half+Thickness},Z0,Z1,Material);
    Box({-Half,Half-Thickness},{Half,Half},Z0,Z1,Material);
    Box({-Half,-Half+Thickness},{-Half+Thickness,Half-Thickness},Z0,Z1,Material);
    Box({Half-Thickness,-Half+Thickness},{Half,Half-Thickness},Z0,Z1,Material);
}

void AChunkedDestructibleMayaPyramid::RotatePieces(int32 FirstPiece,int32 Face)
{
    if (Face==0) return;
    auto Rotate=[Face](FVector2D Min,FVector2D Max,FVector2D& OutMin,FVector2D& OutMax)
    {
        if (Face==1) { OutMin={-Max.X,-Max.Y};OutMax={-Min.X,-Min.Y}; }
        else if (Face==2) { OutMin={Min.Y,-Max.X};OutMax={Max.Y,-Min.X}; }
        else { OutMin={-Max.Y,Min.X};OutMax={-Min.Y,Max.X}; }
    };
    for (int32 I=FirstPiece;I<Pieces.Num();++I)
    {
        FPiece& P=Pieces[I];
        Rotate(P.BottomMin,P.BottomMax,P.BottomMin,P.BottomMax);
        Rotate(P.TopMin,P.TopMax,P.TopMin,P.TopMax);
        P.Bounds=FBox(FVector(FMath::Min(P.BottomMin.X,P.TopMin.X),FMath::Min(P.BottomMin.Y,P.TopMin.Y),P.Z0),
            FVector(FMath::Max(P.BottomMax.X,P.TopMax.X),FMath::Max(P.BottomMax.Y,P.TopMax.Y),P.Z1));
    }
}

void AChunkedDestructibleMayaPyramid::BuildGlazing(double Half,double Z0,double Z1,double Scale,bool bPodium)
{
    const double Frame=7*Scale,GlassDepth=5*Scale,RoomDepth=245*Scale;
    for (int32 Face=0;Face<4;++Face)
    {
        const int32 FirstPiece=Pieces.Num();
        const double Span=Half-(Face<2?0:Frame);
        auto WindowRun=[&](double Start,double End)
        {
            const int32 Bays=FMath::Max(1,FMath::RoundToInt((End-Start)/WindowBayWidth));
            for (int32 I=0;I<Bays;++I)
            {
                const double A=FMath::Lerp(Start,End,double(I)/Bays),B=FMath::Lerp(Start,End,double(I+1)/Bays);
                // Real transparent panes, continuous dark mullions and recessed rooms give actual depth.
                Box({A+Frame*.5,-Half},{B-Frame*.5,-Half+GlassDepth},Z0+Frame,Z1-Frame,0);
                Box({A-Frame*.5,-Half-2*Scale},{A+Frame*.5,-Half+Frame},Z0,Z1,2);
                const double Recess=RoomDepth*(I%3==0?1.05:.8);
                Box({A+Frame*.5,-Half+Recess},{B-Frame*.5,-Half+Recess+8*Scale},Z0+10*Scale,Z1-15*Scale,4);
                // Narrow warm ceiling light, supported by the same destructible geometry system.
                Box({A+20*Scale,-Half+25*Scale},{B-20*Scale,-Half+32*Scale},Z1-10*Scale,Z1-6*Scale,5);
                if (I%3==0)
                {
                    Box({A+Frame*.5,-Half+45*Scale},{A+Frame*.5+6*Scale,-Half+Recess},Z0+10*Scale,Z1-15*Scale,6);
                    const double Mid=(A+B)*.5,DeskHalf=FMath::Min((B-A)*.28,70*Scale);
                    // A low recessed table provides depth behind the glass without a texture pattern.
                    Box({Mid-DeskHalf,-Half+Recess-85*Scale},{Mid+DeskHalf,-Half+Recess-30*Scale},Z0+65*Scale,Z0+74*Scale,6);
                    Box({Mid-DeskHalf+8*Scale,-Half+Recess-75*Scale},{Mid-DeskHalf+16*Scale,-Half+Recess-40*Scale},Z0,Z0+65*Scale,2);
                    Box({Mid+DeskHalf-16*Scale,-Half+Recess-75*Scale},{Mid+DeskHalf-8*Scale,-Half+Recess-40*Scale},Z0,Z0+65*Scale,2);
                }
            }
            Box({End-Frame*.5,-Half-2*Scale},{End+Frame*.5,-Half+Frame},Z0,Z1,2);
            Box({Start,-Half-2*Scale},{End,-Half+Frame},Z0,Z0+Frame,2);
            Box({Start,-Half-2*Scale},{End,-Half+Frame},Z1-Frame,Z1,2);
        };
        if (bPodium && Face==0)
        {
            // Clear, standing-height main entrance through the glazed podium.
            WindowRun(-Span,-155*Scale);WindowRun(155*Scale,Span);
            Box({-155*Scale,-Half},{155*Scale,-Half+Frame},Z1-40*Scale,Z1,1);
        }
        else WindowRun(-Span,Span);
        RotatePieces(FirstPiece,Face);
    }
}

void AChunkedDestructibleMayaPyramid::BuildSlab(double Half,double Z0,double Z1,double RibPlane,double RibHalfWidth,int32 TopMaterial)
{
    // Trim only the tiny terrace portions that would poke through the continuous inclined ribs.
    TArray<double> Edges={-Half,-RibHalfWidth,RibHalfWidth,Half};
    if (RibPlane<Half) { Edges.Add(-RibPlane);Edges.Add(RibPlane); }
    for (double& E:Edges) E=FMath::Clamp(E,-Half,Half);
    Edges.Sort();
    for (int32 X=0;X<Edges.Num()-1;++X)
        for (int32 Y=0;Y<Edges.Num()-1;++Y)
        {
            const double CX=(Edges[X]+Edges[X+1])*.5,CY=(Edges[Y]+Edges[Y+1])*.5;
            if ((FMath::Abs(CX)<RibHalfWidth && FMath::Abs(CY)>RibPlane) ||
                (FMath::Abs(CY)<RibHalfWidth && FMath::Abs(CX)>RibPlane)) continue;
            Box({Edges[X],Edges[Y]},{Edges[X+1],Edges[Y+1]},Z0,Z1,1,TopMaterial);
        }
}

void AChunkedDestructibleMayaPyramid::BuildInclinedRib(int32 Face,double Z0,double Z1,double Start,double End,double Scale)
{
    const int32 FirstPiece=Pieces.Num();
    const double Width=BaseWidth*StairWidthRatio,TopWidth=Width*.7,Rail=68*Scale;
    const double Height=Z1-Z0,Run=Start-End;
    const int32 Segments=FMath::CeilToInt(Height/PieceSize);
    for (int32 I=0;I<Segments;++I)
    {
        const double A=double(I)/Segments,B=double(I+1)/Segments;
        const double YA=-FMath::Lerp(Start,End,A),YB=-FMath::Lerp(Start,End,B);
        const double WA=FMath::Lerp(Width,TopWidth,A)*.5,WB=FMath::Lerp(Width,TopWidth,B)*.5;
        const double ZA=Z0+Height*A,ZB=Z0+Height*B;
        // Monolithic off-white flanking beams. Their inward depth gives a recessed dark center.
        TilePrism({-WA-Rail,YA-35*Scale},{-WA,YA+105*Scale},{-WB-Rail,YB-35*Scale},{-WB,YB+105*Scale},ZA,ZB,1);
        TilePrism({WA,YA-35*Scale},{WA+Rail,YA+105*Scale},{WB,YB-35*Scale},{WB+Rail,YB+105*Scale},ZA,ZB,1);
        if (Face<2 || !bSideStaircases)
            TilePrism({-WA,YA+15*Scale},{WA,YA+50*Scale},{-WB,YB+15*Scale},{WB,YB+50*Scale},ZA,ZB,3);
    }
    if (Face>=2 && bSideStaircases)
    {
        const int32 Steps=FMath::CeilToInt(Height/25);
        for (int32 I=0;I<Steps;++I)
        {
            const double A=double(I)/Steps,B=double(I+1)/Steps;
            const double Y=-Start+Run*A,W=FMath::Lerp(Width,TopWidth,B)*.5;
            const double Z=Z0+Height*B;
            Box({-W,Y+15*Scale},{W,Y+Run/Steps+15*Scale},FMath::Max(Z0,Z-65*Scale),Z,7);
        }
    }
    Box({-Width*.5-Rail,-Start-28*Scale},{Width*.5+Rail,-Start+145*Scale},Z0-30*Scale,Z0,1,6);
    RotatePieces(FirstPiece,Face);
}

void AChunkedDestructibleMayaPyramid::RefreshArchitectureMaterials()
{
    auto Update=[&](TObjectPtr<UMaterialInstanceDynamic>& Instance,UMaterialInterface* Parent)
    {
        if (!Parent) { Instance=nullptr;return; }
        if (!Instance || Instance->Parent!=Parent) Instance=UMaterialInstanceDynamic::Create(Parent,this);
        Instance->SetScalarParameterValue(TEXT("InteriorBrightness"),InteriorBrightness);
        Instance->SetScalarParameterValue(TEXT("AnimateLighting"),bAnimateInteriorLighting?1.f:0.f);
        Instance->SetScalarParameterValue(TEXT("LightingCycle"),LightingCycleSeconds);
    };
    Update(InteriorMaterialInstance,ShrineMaterial);Update(LightMaterialInstance,CrestMaterial);
    FloorMaterials={WallMaterial,LedgeMaterial,AccentMaterial,StairMaterial,
        InteriorMaterialInstance?static_cast<UMaterialInterface*>(InteriorMaterialInstance.Get()):ShrineMaterial.Get(),
        LightMaterialInstance?static_cast<UMaterialInterface*>(LightMaterialInstance.Get()):CrestMaterial.Get(),GoldMaterial,StepMaterial};
    for (TObjectPtr<UMaterialInterface>& M:FloorMaterials) if (!M) M=UMaterial::GetDefaultMaterial(MD_Surface);
}

void AChunkedDestructibleMayaPyramid::SetInteriorLighting(float Brightness,bool bAnimate)
{
    InteriorBrightness=FMath::IsFinite(Brightness)?FMath::Clamp(Brightness,0.f,5.f):0.f;
    bAnimateInteriorLighting=bAnimate;
    RefreshArchitectureMaterials();
    for (const auto& Component:ChunkComponents) ConfigureChunkComponent(Component);
}

bool AChunkedDestructibleMayaPyramid::CalculateLayout()
{
    auto Clamp=[](float& V,float Min,float Max) { V=FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Min; };
    Clamp(BaseWidth,1600,10000);TierCount=FMath::Clamp(TierCount,3,16);Clamp(TierHeight,100,450);
    Clamp(SummitWidthRatio,.12f,.35f);Clamp(PieceSize,60,300);Clamp(StairWidthRatio,.07f,.18f);
    Clamp(PodiumHeight,240,800);Clamp(PodiumWidthRatio,1.1f,1.6f);Clamp(SlabThickness,12,65);
    Clamp(WindowBayWidth,140,400);Clamp(InteriorBrightness,0,5);Clamp(LightingCycleSeconds,2,60);
    // The top opening must fit the converging ribs and the selected wall/slab sizes.
    StairWidthRatio=FMath::Min(StairWidthRatio, float((SummitWidthRatio-136.0/4200.)/.7));
    const double Scale=BaseWidth/4200.,Half=BaseWidth*.5,PodiumHalf=Half*PodiumWidthRatio;
    const double Summit=Half*SummitWidthRatio,BodyTop=PodiumHeight+TierCount*TierHeight;
    const double RibStart=Half+180*Scale,RibEnd=Summit+40*Scale;
    TArray<FPiece> Previous=MoveTemp(Pieces);
    const float PreviousHeight=GeneratedHeight;
    bBudgetExceeded=false;GenerationWarning.Reset();
    // The wide glazed ground storey and terrace are part of the actor, not preview dressing.
    Box({-PodiumHalf,-PodiumHalf},{PodiumHalf,PodiumHalf},0,36*Scale,1,6);
    BuildGlazing(PodiumHalf-55*Scale,36*Scale,PodiumHeight-38*Scale,Scale,true);
    Box({-PodiumHalf,-PodiumHalf},{PodiumHalf,PodiumHalf},PodiumHeight-38*Scale,PodiumHeight,1,6);
    // Small projecting entrance canopy and two off-white jambs frame the real opening.
    Box({-235*Scale,-PodiumHalf-170*Scale},{235*Scale,-PodiumHalf+40*Scale},PodiumHeight-50*Scale,PodiumHeight,1,6);
    for (double Sign:{-1.,1.})
    {
        const double X=Sign*215*Scale;
        Box({X-20*Scale,-PodiumHalf-170*Scale},{X+20*Scale,-PodiumHalf-120*Scale},0,PodiumHeight-50*Scale,1);
    }
    for (int32 I=0;I<TierCount;++I)
    {
        const double B=FMath::Lerp(Half,Summit,double(I)/(TierCount-1));
        const double Z=PodiumHeight+I*TierHeight;
        const double Slab=FMath::Min(double(SlabThickness)*Scale,TierHeight*.25);
        const double GlassInset=FMath::Min(55*Scale,B*.12);
        BuildGlazing(B-GlassInset,Z,Z+TierHeight-Slab,Scale,false);
        const double Alpha=double(I+1)/TierCount;
        const double RibPlane=FMath::Lerp(RibStart,RibEnd,Alpha)-65*Scale;
        const double RibHalf=BaseWidth*StairWidthRatio*FMath::Lerp(1.,.7,Alpha)*.5;
        BuildSlab(B,Z+TierHeight-Slab,Z+TierHeight,RibPlane,RibHalf+4*Scale,6);
    }
    for (int32 Face=0;Face<4;++Face) BuildInclinedRib(Face,PodiumHeight,BodyTop,RibStart,RibEnd,Scale);
    // Small flat crown with a recessed blue skylight, no separate shrine or symbol.
    Ring(Summit+18*Scale,65*Scale,BodyTop,BodyTop+45*Scale,1);
    Box({-Summit+47*Scale,-Summit+47*Scale},{Summit-47*Scale,Summit-47*Scale},BodyTop,BodyTop+8*Scale,3);
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous);GeneratedHeight=PreviousHeight;
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce dimensions. Previous geometry retained.");
        UE_LOG(LogTemp,Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);
        return false;
    }
    GeneratedHeight=BodyTop+45*Scale;GeneratedPieceCount=Pieces.Num();
    RefreshArchitectureMaterials();
    CellCount=FIntPoint(Pieces.Num(),1);
    ChunkCount=FIntPoint(FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1);
    EffectiveCellDimensions=FVector(PieceSize,PieceSize,SlabThickness);CellDimensions=EffectiveCellDimensions;
    CellSpacing=FVector2D::ZeroVector;
    EffectiveGridSize=FVector2D(PodiumHalf*2+400*Scale);GridLocalMin=-EffectiveGridSize*.5;AreaSize=EffectiveGridSize;
    return true;
}

bool AChunkedDestructibleMayaPyramid::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleMayaPyramid::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Bounds.GetCenter():FVector::ZeroVector; }
int32 AChunkedDestructibleMayaPyramid::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell)?Pieces[Cell.X].Material:0; }
FTransform AChunkedDestructibleMayaPyramid::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell)?FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)):FTransform::Identity;
}
void AChunkedDestructibleMayaPyramid::AppendPiece(FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const
{
    FVector V[8];Piece.Vertices(V);
    // Counter-clockwise corners along each outward normal; AppendMeshQuad handles UE winding.
    constexpr int32 Faces[6][4]={{4,5,6,7},{3,2,1,0},{0,1,5,4},{2,3,7,6},{1,2,6,5},{3,0,4,7}};
    for (int32 FaceIndex=0;FaceIndex<6;++FaceIndex)
    {
        const auto& F=Faces[FaceIndex];
        const FVector& A=V[F[0]];const FVector& B=V[F[1]];const FVector& C=V[F[2]];const FVector& D=V[F[3]];
        const FVector Normal=FVector::CrossProduct(B-A,D-A).GetSafeNormal();
        AppendMeshQuad(Mesh,A,B,C,D,FVector3f(Normal),FaceIndex==0?Piece.TopMaterial:Material,(B-A).Length(),(D-A).Length(),UVWorldUnits);
    }
}
void AChunkedDestructibleMayaPyramid::BuildChunkMesh(int32 ChunkIndex,FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ());Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1);Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First;I<End;++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendPiece(Mesh,Pieces[I],Cells[I].MaterialIndex);
}
void AChunkedDestructibleMayaPyramid::BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const
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
bool AChunkedDestructibleMayaPyramid::FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const
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
bool AChunkedDestructibleMayaPyramid::WorldLocationToCell(FVector P,FIntPoint& Cell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.01); }
bool AChunkedDestructibleMayaPyramid::ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const
{
    const FVector P=Hit.ImpactPoint.ContainsNaN()?Hit.Location:Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),Cell,.25);
}
int32 AChunkedDestructibleMayaPyramid::SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,
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
void AChunkedDestructibleMayaPyramid::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
