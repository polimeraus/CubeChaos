#include "ChunkedDestructibleZiggurat.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"
#include "Components/ChildActorComponent.h"

using namespace UE::Geometry;

AChunkedDestructibleZiggurat::AChunkedDestructibleZiggurat()
{
    FloorPattern = EChunkedFloorPattern::Solid;
    ChunkSideLength = 96;
    MaxGeneratedCells = 50000;
    bGenerateBottomFaces = true;
    bReactToMusic = false;
    DefaultCellHealth = 25;
    GroundLaunchPad=CreateDefaultSubobject<UChildActorComponent>(TEXT("GroundLaunchPad"));
    GroundLaunchPad->SetupAttachment(GetRootComponent());
    ZigguratTraversal::InitializeMaterials(Traversal);
}

void AChunkedDestructibleZiggurat::RebuildZiggurat() { GenerateFloor(false); }

#if WITH_EDITOR
bool AChunkedDestructibleZiggurat::CanEditChange(const FProperty* Property) const
{
    if (Property)
    {
        const FName Name = Property->GetFName();
        for (const TCHAR* Derived : {TEXT("AreaSize"), TEXT("CellDimensions"), TEXT("CellSpacing"),
            TEXT("FillRule"), TEXT("FloorPattern"), TEXT("FloorMaterials"), TEXT("PrimaryMaterialIndex"),
            TEXT("SecondaryMaterialIndex"), TEXT("PatternCellScale"), TEXT("PatternRandomSeed")})
            if (Name == FName(Derived)) return false;
    }
    return Super::CanEditChange(Property);
}
#endif

void AChunkedDestructibleZiggurat::TileBox(FVector Min, FVector Max, int32 Material)
{
    if (bBudgetExceeded || (Max-Min).GetMin() < 0.01) return;
    const FVector Size = Max-Min;
    const FIntVector Count(FMath::CeilToInt(Size.X/PieceSize), FMath::CeilToInt(Size.Y/PieceSize),
        FMath::CeilToInt(Size.Z/PieceSize));
    const int64 Requested = int64(Count.X)*Count.Y*Count.Z;
    if (Pieces.Num()+Requested > FMath::Clamp(MaxGeneratedCells, 1, 100000))
    {
        bBudgetExceeded = true;
        return;
    }
    const FVector Step(Size.X/Count.X, Size.Y/Count.Y, Size.Z/Count.Z);
    // Keep thin trim intact even with a large joint setting.
    const FVector Inset = Step.ComponentMin(FVector(PieceGap * 10)) * 0.05;
    for (int32 Z=0; Z<Count.Z; ++Z)
        for (int32 Y=0; Y<Count.Y; ++Y)
            for (int32 X=0; X<Count.X; ++X)
            {
                const FVector P = Min + FVector(X*Step.X,Y*Step.Y,Z*Step.Z);
                Pieces.Add({FBox(P+Inset,P+Step-Inset),Material});
            }
}

void AChunkedDestructibleZiggurat::TileSlab(FVector Min,FVector Max,int32 Material)
{
    const FVector2D Size(Max.X-Min.X,Max.Y-Min.Y),Center((Min.X+Max.X)*.5,(Min.Y+Max.Y)*.5);
    ZigguratTraversal::SolidSections(Size,Size,Min.Z,Max.Z,GenerationShaftWidth,
        [&](FVector2D B0,FVector2D B1,FVector2D,FVector2D,double Z0,double Z1)
        { TileBox(FVector(B0+Center,Z0),FVector(B1+Center,Z1),Material); });
}

void AChunkedDestructibleZiggurat::FacadeBox(int32 Face, double Offset, double U0, double U1,
    double Z0, double Z1, double Thickness, int32 Material)
{
    if (Face < 2)
    {
        const double Y = Face == 0 ? -Offset : Offset;
        TileBox(FVector(U0,Y-Thickness*.5,Z0),FVector(U1,Y+Thickness*.5,Z1),Material);
    }
    else
    {
        const double X = Face == 2 ? -Offset : Offset;
        TileBox(FVector(X-Thickness*.5,U0,Z0),FVector(X+Thickness*.5,U1,Z1),Material);
    }
}

void AChunkedDestructibleZiggurat::BuildFacade(int32 Face, int32 Tier, double Width, double Depth,
    double Z, double Height)
{
    const double Span = Face < 2 ? Width : Depth-2*WallThickness;
    const double Offset = (Face < 2 ? Depth : Width)*.5-WallThickness*.5;
    const double Frame = bWindowFrames ? FrameWidth : 0;
    const double Margin = FMath::Max(30.0, Frame+WallThickness);
    const bool HasDoor = Face == 0 && (Tier == 0 ? bEntrance : bUpperTierDoors);
    const double DoorW = FMath::Min(double(EntranceWidth), Span*.45);
    const double DoorH = FMath::Min(double(EntranceHeight), Height-40);
    const double Sill = FMath::Clamp(double(WindowSillHeight), 20.0, Height*.45);
    const double WH = FMath::Clamp(double(WindowHeight), 20.0, Height-Sill-30);
    const double WW = FMath::Min(double(WindowWidth), Span*.22);
    TArray<FBox2D> Openings;
    if (HasDoor) Openings.Add(FBox2D(FVector2D(-DoorW*.5,Z),FVector2D(DoorW*.5,Z+DoorH)));
    int32 Count = Face < 2 ? FrontWindowCount : SideWindowCount;
    if (bReduceWindowsOnUpperTiers && Count>0)
        Count = FMath::Max(1, FMath::RoundToInt(Count * (Face < 2 ? Width/BaseWidth : Depth/BaseDepth)));
    const double Pitch = WW + FMath::Max(35.0, Frame*2+12);
    auto WindowsInSpan = [&](double Lo, double Hi, int32 Requested)
    {
        const int32 Fit = FMath::Min(Requested, FMath::Max(0, FMath::FloorToInt((Hi-Lo)/Pitch)));
        for (int32 I=0; I<Fit; ++I)
        {
            const double Center = Lo+(Hi-Lo)*(I+.5)/Fit;
            Openings.Add(FBox2D(FVector2D(Center-WW*.5,Z+Sill),FVector2D(Center+WW*.5,Z+Sill+WH)));
            ++GeneratedWindowCount;
        }
    };
    if (HasDoor)
    {
        WindowsInSpan(-Span*.5+Margin,-DoorW*.5-Margin,Count/2);
        WindowsInSpan(DoorW*.5+Margin,Span*.5-Margin,Count-Count/2);
    }
    else WindowsInSpan(-Span*.5+Margin,Span*.5-Margin,Count);

    // Partition at every opening edge. No solid box or collision crosses a window/door.
    TArray<double> Xs = {-Span*.5,Span*.5}, Zs = {Z,Z+Height};
    for (const FBox2D& O : Openings)
    {
        Xs.AddUnique(O.Min.X); Xs.AddUnique(O.Max.X);
        Zs.AddUnique(O.Min.Y); Zs.AddUnique(O.Max.Y);
    }
    Xs.Sort(); Zs.Sort();
    for (int32 X=0; X<Xs.Num()-1 && !bBudgetExceeded; ++X)
        for (int32 H=0; H<Zs.Num()-1 && !bBudgetExceeded; ++H)
        {
            const FVector2D Mid((Xs[X]+Xs[X+1])*.5,(Zs[H]+Zs[H+1])*.5);
            bool bOpening = false;
            for (const FBox2D& O : Openings) bOpening |= O.IsInside(Mid);
            if (!bOpening) FacadeBox(Face,Offset,Xs[X],Xs[X+1],Zs[H],Zs[H+1],WallThickness,0);
        }
    if (bWindowFrames)
        for (const FBox2D& O : Openings)
        {
            const double Outer = Offset+WallThickness*.5+Frame*.3;
            FacadeBox(Face,Outer,O.Min.X-Frame,O.Min.X,O.Min.Y,O.Max.Y,Frame*.6,1);
            FacadeBox(Face,Outer,O.Max.X,O.Max.X+Frame,O.Min.Y,O.Max.Y,Frame*.6,1);
            FacadeBox(Face,Outer,O.Min.X-Frame,O.Max.X+Frame,O.Max.Y,O.Max.Y+Frame,Frame*.6,1);
            if (O.Min.Y > Z+.1)
                FacadeBox(Face,Outer,O.Min.X-Frame,O.Max.X+Frame,O.Min.Y-Frame,O.Min.Y,Frame*.6,1);
        }
}

void AChunkedDestructibleZiggurat::BuildRailings(double Width, double Depth, double Z)
{
    constexpr double Post = 22;
    for (int32 Face=0; Face<4; ++Face)
    {
        const double Span = (Face<2 ? Width : Depth)-Post*2;
        const double Offset = (Face<2 ? Depth : Width)*.5-Post;
        FacadeBox(Face,Offset,-Span*.5,Span*.5,Z+RailingHeight-Post*.7,Z+RailingHeight,Post,3);
        const int32 Bays = FMath::Max(1,FMath::CeilToInt(Span/RailingPostSpacing));
        // Side rails omit duplicate corner posts owned by front/back rails.
        for (int32 I=(Face<2 ? 0 : 1); I<=Bays-(Face<2 ? 0 : 1); ++I)
        {
            const double U = -Span*.5+Span*I/Bays;
            FacadeBox(Face,Offset,U-Post*.5,U+Post*.5,Z,Z+RailingHeight-Post*.7,Post,3);
        }
    }
}

bool AChunkedDestructibleZiggurat::CalculateLayout()
{
    auto Clamp = [](float& Value, float Min, float Max)
    { Value = FMath::IsFinite(Value) ? FMath::Clamp(Value,Min,Max) : Min; };
    Clamp(BaseWidth,400,20000); Clamp(BaseDepth,400,20000);
    TierCount=FMath::Clamp(TierCount,1,12); Clamp(TierHeight,150,2000);
    Clamp(TopScale,.15f,1); Clamp(TierHeightScale,.65f,1);
    Clamp(PieceSize,25,400); Clamp(WallThickness,5,150); Clamp(PieceGap,0,8);
    // Even the smallest top chamber keeps a real interior and non-overlapping corners.
    TopScale=FMath::Max(TopScale, float(FMath::Min(1.0,(WallThickness*2+120)/FMath::Min(BaseWidth,BaseDepth))));
    FrontWindowCount=FMath::Clamp(FrontWindowCount,0,24); SideWindowCount=FMath::Clamp(SideWindowCount,0,24);
    Clamp(WindowWidth,20,500); Clamp(WindowHeight,20,1000); Clamp(WindowSillHeight,0,1000);
    Clamp(FrameWidth,4,50); Clamp(EntranceWidth,50,600); Clamp(EntranceHeight,80,1000);
    StairCount=FMath::Clamp(StairCount,1,32); Clamp(StairTreadDepth,20,100); Clamp(FoundationHeight,0,600);
    Clamp(BalconyOverhang,0,500); Clamp(TerraceThickness,10,150);
    Clamp(RailingHeight,30,200); Clamp(RailingPostSpacing,60,600);
    CrownSteps=FMath::Clamp(CrownSteps,1,8); Clamp(CrownStepHeight,10,200);
    const double RoomWidth=FMath::Min(BaseWidth,BaseDepth)*(TierCount>1?TopScale:1)-2*WallThickness;
    const double FittedWidth=ZigguratTraversal::FitOpening(Traversal,RoomWidth);
    GenerationShaftWidth=Traversal.bFloorOpenings?FittedWidth:0;

    TArray<FPiece> Previous = MoveTemp(Pieces);
    const int32 PreviousWindows = GeneratedWindowCount;
    const float PreviousHeight = GeneratedHeight;
    GeneratedWindowCount=0; bBudgetExceeded=false; GenerationWarning.Reset();
    const double Overhang = bBalconies ? BalconyOverhang : 0;
    const double FoundationOverhang = FMath::Max(60.0,Overhang);
    TileBox(FVector(-BaseWidth*.5-FoundationOverhang,-BaseDepth*.5-FoundationOverhang,0),
        FVector(BaseWidth*.5+FoundationOverhang,BaseDepth*.5+FoundationOverhang,FoundationHeight),4);
    if (bEntrance && bEntranceStairs && FoundationHeight>0)
    {
        const double StairW=FMath::Min(double(EntranceWidth)+180,BaseWidth*.8);
        const double Front=-BaseDepth*.5-FoundationOverhang;
        for (int32 I=0; I<StairCount; ++I)
        {
            const double Near=Front-(StairCount-1-I)*StairTreadDepth;
            TileBox(FVector(-StairW*.5,Near-StairTreadDepth,0),
                FVector(StairW*.5,Near,FoundationHeight*(I+1)/StairCount),4);
        }
    }
    double Z=FoundationHeight;
    double LastWidth=BaseWidth, LastDepth=BaseDepth;
    for (int32 Tier=0; Tier<TierCount && !bBudgetExceeded; ++Tier)
    {
        const double Scale=FMath::Lerp(1.0,double(TopScale),TierCount>1 ? double(Tier)/(TierCount-1) : 0.0);
        const double W=BaseWidth*Scale, D=BaseDepth*Scale;
        const double H=FMath::Max(150.0,TierHeight*FMath::Pow(double(TierHeightScale),Tier));
        for (int32 Face=0; Face<4; ++Face) BuildFacade(Face,Tier,W,D,Z,H);
        if (bCornerPillars)
        {
            const double P=FMath::Min(65.0,WallThickness+20.0);
            for (int32 X : {-1,1}) for (int32 Y : {-1,1})
            {
                // External L-shaped corner cladding, leaving the wall cells independently exposed.
                FacadeBox(Y<0?0:1,D*.5+10,X*W*.5-P*.5,X*W*.5+P*.5,Z,Z+H,20,1);
                FacadeBox(X<0?2:3,W*.5+10,FMath::Max(-D*.5,Y*D*.5-P*.5),
                    FMath::Min(D*.5,Y*D*.5+P*.5),Z,Z+H,20,1);
            }
        }
        // The solid foundation is excluded; every upper slab shares the same clear shaft.
        TileSlab(FVector(-W*.5-Overhang,-D*.5-Overhang,Z+H),
            FVector(W*.5+Overhang,D*.5+Overhang,Z+H+TerraceThickness),2);
        Z += H+TerraceThickness;
        if (bBalconies && bRailings && Overhang>40) BuildRailings(W+2*Overhang,D+2*Overhang,Z);
        LastWidth=W; LastDepth=D;
    }
    double Top=Z+(bBalconies && bRailings && Overhang>40 ? RailingHeight : 0);
    if (bCrown)
        for (int32 I=0; I<CrownSteps && !bBudgetExceeded; ++I)
        {
            const double Scale=.72*(1.0-.65*I/CrownSteps);
            TileSlab(FVector(-LastWidth*.5*Scale,-LastDepth*.5*Scale,Z),
                FVector(LastWidth*.5*Scale,LastDepth*.5*Scale,Z+CrownStepHeight),5);
            Z+=CrownStepHeight;
        }
    if (bBudgetExceeded || Pieces.IsEmpty())
    {
        Pieces=MoveTemp(Previous); GeneratedWindowCount=PreviousWindows; GeneratedHeight=PreviousHeight;
        GenerationWarning=TEXT("Piece budget exceeded. Increase Piece Size or reduce the building dimensions. Previous geometry retained.");
        UE_LOG(LogTemp, Warning,TEXT("%s: %s"),*GetName(),*GenerationWarning);
        return false;
    }
    GeneratedHeight=FMath::Max(Z,Top); GeneratedPieceCount=Pieces.Num();
    FloorMaterials={WallMaterial,TrimMaterial,TerraceMaterial,RailingMaterial,StairMaterial,CrownMaterial};
    for (TObjectPtr<UMaterialInterface>& Material : FloorMaterials)
        if (!Material) Material=WallMaterial ? WallMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface);
    CellCount=FIntPoint(Pieces.Num(),1);
    ChunkCount=FIntPoint(FMath::DivideAndRoundUp(Pieces.Num(),FMath::Clamp(ChunkSideLength,4,128)),1);
    EffectiveCellDimensions=FVector(PieceSize,PieceSize,WallThickness);
    CellDimensions=EffectiveCellDimensions; CellSpacing=FVector2D(PieceGap);
    EffectiveGridSize=FVector2D(BaseWidth+2*FoundationOverhang,BaseDepth+2*FoundationOverhang);
    GridLocalMin=-EffectiveGridSize*.5; AreaSize=EffectiveGridSize;
    EffectiveOpeningWidth=GenerationShaftWidth;
    ZigguratTraversal::UpdatePad(GroundLaunchPad,Traversal,FittedWidth,FoundationHeight,GeneratedHeight);
    return true;
}

bool AChunkedDestructibleZiggurat::ShouldCellExistInGeneratedShape(FIntPoint Cell) const
{ return Cell.Y==0 && Pieces.IsValidIndex(Cell.X); }
FVector AChunkedDestructibleZiggurat::GetCellLocalCenter(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell) ? Pieces[Cell.X].Bounds.GetCenter() : FVector::ZeroVector; }
int32 AChunkedDestructibleZiggurat::EvaluatePatternMaterial(FIntPoint Cell) const
{ return ShouldCellExistInGeneratedShape(Cell) ? Pieces[Cell.X].Material : 0; }
FTransform AChunkedDestructibleZiggurat::GetCellFlashTransform(FIntPoint Cell) const
{
    return ShouldCellExistInGeneratedShape(Cell) ? FTransform(FQuat::Identity,GetCellLocalCenter(Cell),
        Pieces[Cell.X].Bounds.GetSize()/100*FMath::Max(FlashScale,.01f)) : FTransform::Identity;
}

void AChunkedDestructibleZiggurat::AppendBox(FDynamicMesh3& Mesh, const FBox& B, int32 Material) const
{
    const double X=B.Min.X,Y=B.Min.Y,Z=B.Min.Z, U=B.Max.X,V=B.Max.Y,W=B.Max.Z;
    AppendMeshQuad(Mesh,{X,Y,W},{U,Y,W},{U,V,W},{X,V,W},FVector3f::UnitZ(),Material,U-X,V-Y,UVWorldUnits);
    AppendMeshQuad(Mesh,{X,V,Z},{U,V,Z},{U,Y,Z},{X,Y,Z},-FVector3f::UnitZ(),Material,U-X,V-Y,UVWorldUnits);
    AppendMeshQuad(Mesh,{X,Y,Z},{U,Y,Z},{U,Y,W},{X,Y,W},-FVector3f::UnitY(),Material,U-X,W-Z,UVWorldUnits);
    AppendMeshQuad(Mesh,{U,V,Z},{X,V,Z},{X,V,W},{U,V,W},FVector3f::UnitY(),Material,U-X,W-Z,UVWorldUnits);
    AppendMeshQuad(Mesh,{U,Y,Z},{U,V,Z},{U,V,W},{U,Y,W},FVector3f::UnitX(),Material,V-Y,W-Z,UVWorldUnits);
    AppendMeshQuad(Mesh,{X,V,Z},{X,Y,Z},{X,Y,W},{X,V,W},-FVector3f::UnitX(),Material,V-Y,W-Z,UVWorldUnits);
}

void AChunkedDestructibleZiggurat::BuildChunkMesh(int32 ChunkIndex, FDynamicMesh3& Mesh) const
{
    Mesh.EnableVertexNormals(FVector3f::UnitZ()); Mesh.EnableAttributes();
    Mesh.Attributes()->SetNumUVLayers(1); Mesh.Attributes()->EnableMaterialID();
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First; I<End; ++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists) AppendBox(Mesh,Pieces[I].Bounds,Cells[I].MaterialIndex);
}
void AChunkedDestructibleZiggurat::BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& Collision) const
{
    Collision.EmptyElements();
    if (!bGenerateCollision) return;
    const int32 First=ChunkIndex*FMath::Clamp(ChunkSideLength,4,128);
    const int32 End=FMath::Min(First+FMath::Clamp(ChunkSideLength,4,128),Pieces.Num());
    for (int32 I=First; I<End; ++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            const FBox& B=Pieces[I].Bounds; const FVector S=B.GetSize();
            FKBoxElem& Box=Collision.BoxElems.Emplace_GetRef(S.X,S.Y,S.Z);
            Box.Center=B.GetCenter();
        }
}
bool AChunkedDestructibleZiggurat::FindPiece(FVector P, FIntPoint& OutCell, double Tolerance) const
{
    OutCell=FIntPoint::NoneValue;
    double Best=TNumericLimits<double>::Max();
    for (int32 I=0; I<Pieces.Num(); ++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists && Pieces[I].Bounds.ExpandBy(Tolerance).IsInsideOrOn(P))
        {
            const double D=FVector::DistSquared(P,Pieces[I].Bounds.GetCenter());
            if (D<Best) { Best=D; OutCell=FIntPoint(I,0); }
        }
    return OutCell!=FIntPoint::NoneValue;
}
bool AChunkedDestructibleZiggurat::WorldLocationToCell(FVector P, FIntPoint& OutCell) const
{ return FindPiece(GetActorTransform().InverseTransformPosition(P),OutCell,0.01); }
bool AChunkedDestructibleZiggurat::ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& OutCell) const
{
    const FVector P=Hit.ImpactPoint.ContainsNaN() ? Hit.Location : Hit.ImpactPoint;
    return FindPiece(GetActorTransform().InverseTransformPosition(P),OutCell,.25);
}
int32 AChunkedDestructibleZiggurat::SelectCellsInRadius(FVector P, float Radius, EChunkedFloorBlastShape Shape,
    TFunctionRef<void(int32,float)> Function)
{
    if (Radius<=0) return 0;
    const FVector Local=GetActorTransform().InverseTransformPosition(P), Scale=GetActorScale3D().GetAbs();
    int32 Count=0;
    for (int32 I=0; I<Pieces.Num(); ++I)
        if (Cells.IsValidIndex(I) && Cells[I].bExists)
        {
            const FVector Point = Shape == EChunkedFloorBlastShape::Sphere
                ? Pieces[I].Bounds.GetClosestPointTo(Local) : Pieces[I].Bounds.GetCenter();
            const FVector D=((Point-Local)*Scale).GetAbs();
            const double Distance=Shape==EChunkedFloorBlastShape::Square ? D.GetMax() :
                (Shape==EChunkedFloorBlastShape::Diamond ? D.X+D.Y+D.Z : D.Length());
            if (Distance<=Radius) { Function(I,float(Distance)); ++Count; }
        }
    return Count;
}
void AChunkedDestructibleZiggurat::MarkCellChunksDirty(FIntPoint Cell)
{
    if (ShouldCellExistInGeneratedShape(Cell)) DirtyChunkIndices.Add(Cell.X/FMath::Clamp(ChunkSideLength,4,128));
}
