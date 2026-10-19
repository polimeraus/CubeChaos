#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ZigguratTraversal.h"
#include "ChunkedDestructibleSpiralTower.generated.h"

/** Hollow, tapering spiral tower. Every wall block and stair tread uses the project's weapon damage system. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Chunked Destructible Spiral Tower"))
class MYPROJECT6_API AChunkedDestructibleSpiralTower : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FSpiralTowerTest;
public:
    AChunkedDestructibleSpiralTower();
    virtual bool UsesPlanarAnimation() const override { return false; }

    /** Rebuilds mesh/collision and restores destroyed pieces, including during play. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Spiral Tower")
    void RebuildTower();

    /** The same centered, roof-height launch pad used by the cone structures. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Traversal", meta=(ShowOnlyInnerProperties))
    FZigguratTraversalSettings Traversal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Spiral Tower|Traversal")
    TObjectPtr<UChildActorComponent> GroundLaunchPad;

    /** A full-height vertical light strip follows the spiral staircase and its walls. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow")
    bool bFlowingGlow=true;
    /** Vertical extent passed to custom glow materials. The default strip always spans the full wall height. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(AdvancedDisplay,DisplayName="Custom Material Band Height",ClampMin="10",ClampMax="20000",Units="cm",EditCondition="bFlowingGlow"))
    float GlowBandHeight=900;
    /** Width of the vertical light strip along the spiral, including its fading edges, in local cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(ClampMin="10",ClampMax="50000",Units="cm",EditCondition="bFlowingGlow"))
    float GlowTrailLength=600;
    /** Travel speed along the staircase, not along the tower's vertical axis. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(ClampMin="1",ClampMax="10000",Units="cm/s",EditCondition="bFlowingGlow"))
    float GlowSpeed=500;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(ClampMin="0",ClampMax="60",Units="s",EditCondition="bFlowingGlow"))
    float GlowPause=1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(EditCondition="bFlowingGlow"))
    bool bLoopGlow=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(ClampMin="0",ClampMax="100",EditCondition="bFlowingGlow"))
    float GlowBrightness=6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(HideAlphaChannel,EditCondition="bFlowingGlow"))
    FLinearColor GlowColor=FLinearColor(.025f,.55f,1.f,1.f);
    /** Use M_SpiralTowerFlowGlow or a material instance of it to retain the wave/fade parameters.
     * UV channel 1 stores (distance along the spiral, height relative to the matching tread), in cm.
     * The default strip uses only distance, so the entire wall column pulses in unison.
     * Custom additive materials may use BandHeight, TrailLength, PathLength, Speed, Pause,
     * Loop, Brightness, Tint, StartTime and PreviewTime. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(EditCondition="bFlowingGlow"))
    TObjectPtr<UMaterialInterface> GlowMaterial;
    /** -1 animates normally. A nonnegative time freezes the wave for editor inspection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Glow", meta=(ClampMin="-1",Units="s",EditCondition="bFlowingGlow"))
    float GlowPreviewTime=-1;
    /** Apply changed glow parameters without rebuilding or restoring destroyed fragments. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Spiral Tower|Glow")
    void RefreshGlowAnimation();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Shape", meta=(ClampMin="600",ClampMax="6000",Units="cm"))
    float BaseRadius=1700;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Shape", meta=(ClampMin="250",ClampMax="6000",Units="cm"))
    float TopRadius=500;
    /** Fractional turns leave the characteristic open crescent at the summit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Shape", meta=(ClampMin="1",ClampMax="12"))
    float Turns=6.75f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Shape", meta=(ClampMin="260",ClampMax="1200",Units="cm"))
    float RisePerTurn=844;
    /** Upper turns have shorter walls and gentler risers, as in the reference. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Shape", meta=(ClampMin=".25",ClampMax="1"))
    float TopRiseScale=.48f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Shape", meta=(UIMin="-180",UIMax="180",Units="deg"))
    float StartAngle=-90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Shape")
    bool bClockwise=true;

    /** Match each stair's outer edge to the outer face of the supporting wall one turn below. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Stairs")
    bool bFitStairsBetweenWalls=true;
    /** Width including the inner wall; calculated from the radial pitch when Fit Stairs Between Walls is on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Stairs", meta=(ClampMin="80",ClampMax="900",Units="cm"))
    float WalkwayWidth=277.77778f;
    /** The last part of the inner wall forms the open summit crescent, without a projecting stair balcony. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Stairs", meta=(ClampMin="0",ClampMax="1"))
    float SummitWallExtension=.5f;
    /** The summit wall returns around the front, leaving the back of the crescent open. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Walls", meta=(EditCondition="SummitWallExtension > 0"))
    bool bReverseSummitCrescent=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Stairs", meta=(ClampMin="48",ClampMax="256"))
    int32 StepsPerTurn=80;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Stairs", meta=(ClampMin="10",ClampMax="80",Units="cm"))
    float SlabThickness=20;
    /** Tall inner spiral wall; there is no center pillar, filled core or solid cap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Walls", meta=(ClampMin="80",ClampMax="1140",Units="cm"))
    float WallHeight=824;
    /** Optional wall batter. The reference uses upright walls (zero). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Walls", meta=(ClampMin="0",ClampMax="400",Units="cm"))
    float WallInset=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Walls", meta=(ClampMin="10",ClampMax="180",Units="cm"))
    float WallThickness=100;
    /** Stepped square plinth with rectangular vents and an open central volume. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Foundation")
    bool bFoundation=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Foundation", meta=(ClampMin="100",ClampMax="400",Units="cm",EditCondition="bFoundation"))
    float FoundationHeight=220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Foundation", meta=(ClampMin="0",ClampMax="300",Units="cm",EditCondition="bFoundation"))
    float FoundationMargin=300;
    /** Maximum approximate fragment length; stair height remains controlled by Steps Per Turn. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Destruction", meta=(ClampMin="50",ClampMax="300",Units="cm"))
    float PieceSize=120;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Destruction", meta=(ClampMin="0",ClampMax="3",Units="cm"))
    float PieceGap=0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Materials")
    TObjectPtr<UMaterialInterface> StoneMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spiral Tower|Materials")
    TObjectPtr<UMaterialInterface> StairMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spiral Tower|Statistics")
    int32 GeneratedPieceCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spiral Tower|Statistics")
    int32 GeneratedStepCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spiral Tower|Statistics", meta=(Units="cm"))
    float GeneratedHeight=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spiral Tower|Statistics", meta=(Units="cm"))
    float GeneratedGlowPathLength=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spiral Tower|Statistics", meta=(Units="cm"))
    float ClearShaftDiameter=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Spiral Tower|Statistics")
    FString GenerationWarning;

    virtual bool WorldLocationToCell(FVector P,FIntPoint& Cell) const override;
#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* Property) const override;
#endif
protected:
    virtual void BeginPlay() override;
    virtual bool CalculateLayout() override;
    virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
    virtual void BuildChunkMesh(int32 ChunkIndex,UE::Geometry::FDynamicMesh3& Mesh) const override;
    virtual void BuildChunkSimpleCollision(int32 ChunkIndex,FKAggregateGeom& Collision) const override;
    virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
    virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
    virtual bool ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& Cell) const override;
    virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
    virtual int32 SelectCellsInRadius(FVector P,float Radius,EChunkedFloorBlastShape Shape,TFunctionRef<void(int32,float)> Function) override;
    virtual void MarkCellChunksDirty(FIntPoint Cell) override;
private:
    struct FPiece
    {
        FVector V[8];
        FVector2f GlowUV[8]{};
        FBox Bounds;
        int32 Material=0;
        bool bSmoothInnerFace=false;
        bool bReceivesGlow=true;
        bool Contains(FVector P,double Tolerance) const;
    };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded=false;
    double InteriorBaseRadius=0,InteriorSlope=0,InteriorFloorZ=0;
    double GeneratingTreadZ=0,GeneratingTreadTurn=0;
    double InteriorRadiusAtHeight(double Z) const;
    void AddPiece(const FVector (&V)[8],int32 Material,bool SmoothInner=false,bool ReceivesGlow=true);
    void AddSector(double T0,double T1,double Inset0,double Inset1,double Z0,double Z1,int32 Material,
        double BottomScale=1,double TopScale=1,bool SmoothInner=false,double GlowTurnOffset=0);
    void AddFoundation();
    double HeightAtTurn(double T) const;
    double StairDistanceAtTurn(double T) const;
    double WallHeightAtTurn(double T) const;
    bool FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material,bool GlowOverlay=false) const;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> GlowMaterialInstance;
};
