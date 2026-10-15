#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ZigguratTraversal.h"
#include "ChunkedDestructibleBabylonTower.generated.h"

/** Stepped sandstone tower with recessed facades, a monumental entrance and tall summit.
 * All visible architecture participates in the existing per-piece weapon damage system.
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Chunked Destructible Babylon Tower"))
class MYPROJECT6_API AChunkedDestructibleBabylonTower : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FBabylonTowerTest;
public:
    AChunkedDestructibleBabylonTower();
    virtual bool UsesPlanarAnimation() const override { return false; }

    /** Regenerates geometry and restores destroyed pieces, including at runtime. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Babylon Tower")
    void RebuildTower();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Traversal", meta=(ShowOnlyInnerProperties))
    FZigguratTraversalSettings Traversal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Babylon Tower|Traversal")
    TObjectPtr<UChildActorComponent> GroundLaunchPad;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Babylon Tower|Traversal", meta=(Units="cm"))
    float EffectiveOpeningWidth=0;

    /** All narrow facade windows are true holes, with no back panel or glass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade")
    bool bOpenFacadeRecesses=true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="600",ClampMax="16000",Units="cm"))
    float BaseWidth=3600;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="600",ClampMax="16000",Units="cm"))
    float BaseDepth=3000;
    /** Total storeys, INCLUDING the tall top storey. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="1",ClampMax="12"))
    int32 TierCount=6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="150",ClampMax="1800",Units="cm"))
    float TierHeight=420;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="150",ClampMax="3000",Units="cm"))
    float SummitHeight=1000;
    /** Highest tier width and depth relative to the base. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="0.2",ClampMax="1"))
    float TopScale=.30f;
    /** Inset of each wall edge across one storey, relative to its width/depth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="0",ClampMax="0.08"))
    float WallBatter=.018f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Pieces", meta=(ClampMin="30",ClampMax="500",Units="cm"))
    float PieceSize=130;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Pieces", meta=(ClampMin="10",ClampMax="180",Units="cm"))
    float WallThickness=65;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Pieces", meta=(ClampMin="0",ClampMax="5",Units="cm"))
    float PieceGap=0;

    /** Facade window count at the base; counts shrink on higher tiers. Open apertures by default. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(DisplayName="Front and Back Window Count",ClampMin="0",ClampMax="20"))
    int32 FrontRecessCount=9;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(DisplayName="Side Window Count",ClampMin="0",ClampMax="20"))
    int32 SideRecessCount=7;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(DisplayName="Facade Window Width",ClampMin="20",ClampMax="400",Units="cm"))
    float RecessWidth=125;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(EditCondition="!bOpenFacadeRecesses",ClampMin="0",ClampMax="100",Units="cm"))
    float RecessDepth=24;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(DisplayName="Facade Window Height Ratio",ClampMin="0.2",ClampMax="0.85"))
    float RecessHeightRatio=.68f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade")
    bool bUpperWindows=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="30",ClampMax="400",Units="cm",EditCondition="bUpperWindows"))
    float WindowWidth=125;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="40",ClampMax="800",Units="cm",EditCondition="bUpperWindows"))
    float WindowHeight=195;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="0",ClampMax="200",Units="cm"))
    float CorniceOverhang=45;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="10",ClampMax="150",Units="cm"))
    float CorniceHeight=42;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="10",ClampMax="150",Units="cm"))
    float FloorThickness=32;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade")
    bool bSummitCorbels=true;

    /** The entrance faces local -Y. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance")
    bool bEntrance=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="80",ClampMax="800",Units="cm",EditCondition="bEntrance"))
    float EntranceWidth=260;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="100",ClampMax="1500",Units="cm",EditCondition="bEntrance"))
    float EntranceHeight=340;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance")
    bool bEntranceFrame=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance")
    bool bEntranceStairs=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="1",ClampMax="32",EditCondition="bEntranceStairs"))
    int32 StairCount=9;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="20",ClampMax="120",Units="cm",EditCondition="bEntranceStairs"))
    float StairTreadDepth=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="80",ClampMax="1600",Units="cm",EditCondition="bEntranceStairs"))
    float StairWidth=480;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Foundation", meta=(ClampMin="0",ClampMax="600",Units="cm"))
    float FoundationHeight=180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Foundation", meta=(ClampMin="0",ClampMax="400",Units="cm"))
    float FoundationOverhang=140;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Materials")
    TObjectPtr<UMaterialInterface> StoneMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Materials")
    TObjectPtr<UMaterialInterface> CorniceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Materials")
    TObjectPtr<UMaterialInterface> TerraceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Materials")
    TObjectPtr<UMaterialInterface> PortalMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Materials")
    TObjectPtr<UMaterialInterface> FoundationMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Materials")
    TObjectPtr<UMaterialInterface> RecessMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Babylon Tower|Statistics")
    int32 GeneratedPieceCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Babylon Tower|Statistics")
    int32 GeneratedOpeningCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Babylon Tower|Statistics", meta=(Units="cm"))
    float GeneratedHeight=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Babylon Tower|Statistics")
    FString GenerationWarning;

    virtual bool WorldLocationToCell(FVector P,FIntPoint& Cell) const override;
#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* Property) const override;
#endif
protected:
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
        FVector2D BottomMin,BottomMax,TopMin,TopMax;
        double Z0=0,Z1=0;
        FBox Bounds;
        int32 Material=0;
        bool Contains(FVector P,double Tolerance) const;
        void Vertices(FVector (&V)[8]) const;
    };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded=false;
    double GenerationShaftWidth=0;
    void AddPrism(FVector2D BMin,FVector2D BMax,FVector2D TMin,FVector2D TMax,double Z0,double Z1,int32 Material);
    void TilePrism(FVector2D BMin,FVector2D BMax,FVector2D TMin,FVector2D TMax,double Z0,double Z1,int32 Material);
    void TileFrustum(FVector2D BottomSize,FVector2D TopSize,double Z0,double Z1,int32 Material,FVector2D Center=FVector2D::ZeroVector,bool bOpening=false);
    void BuildFacade(int32 Face,int32 Tier,double W,double D,double Z,double H);
    void FacadePrism(int32 Face,double U0B,double U1B,double U0T,double U1T,double OffsetB,double OffsetT,double Thickness,double Z0,double Z1,int32 Material);
    void BuildPortal(double Z,double H);
    bool FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const;
};
