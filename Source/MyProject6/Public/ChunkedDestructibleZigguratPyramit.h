#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ZigguratTraversal.h"
#include "ChunkedDestructibleZigguratPyramit.generated.h"

/** Battered sandstone temple matching the three-storey reference, with a stepped pyramid crown.
 * Each fragment is a six-quad convex block, including sloping walls and their exposed cut faces.
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="ChunkedDestructibleZigguratPyramit"))
class MYPROJECT6_API AChunkedDestructibleZigguratPyramit : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FZigguratPyramitTest;
public:
    AChunkedDestructibleZigguratPyramit();
    virtual bool UsesPlanarAnimation() const override { return false; }
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Ziggurat Pyramit")
    void RebuildZigguratPyramit();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Traversal", meta=(ShowOnlyInnerProperties))
    FZigguratTraversalSettings Traversal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ziggurat Pyramit|Traversal")
    TObjectPtr<UChildActorComponent> GroundLaunchPad;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat Pyramit|Statistics", meta=(Units="cm"))
    float EffectiveOpeningWidth = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Shape", meta=(ClampMin="600",ClampMax="16000",Units="cm"))
    float BaseWidth=2400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Shape", meta=(ClampMin="600",ClampMax="16000",Units="cm"))
    float BaseDepth=2100;
    /** Main occupied levels, excluding the stepped crown. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Shape", meta=(ClampMin="1",ClampMax="10"))
    int32 TierCount=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Shape", meta=(ClampMin="180",ClampMax="2000",Units="cm"))
    float GroundTierHeight=650;
    /** Each higher level is this fraction of the preceding level's height. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Shape", meta=(ClampMin="0.5",ClampMax="1"))
    float TierHeightScale=.72f;
    /** Highest occupied tier width / ground tier width. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Shape", meta=(ClampMin="0.2",ClampMax="1"))
    float TopTierScale=.42f;
    /** Inset of EACH edge at the top of a wall, as a fraction of that tier's width/depth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Shape", meta=(ClampMin="0",ClampMax="0.16"))
    float WallBatter=.065f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Pieces", meta=(ClampMin="10",ClampMax="200",Units="cm"))
    float WallThickness=85;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Pieces", meta=(ClampMin="30",ClampMax="400",Units="cm"))
    float PieceSize=100;
    /** Zero produces the continuous stone surface in the reference. Damage still removes individual blocks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Pieces", meta=(ClampMin="0",ClampMax="5",Units="cm"))
    float PieceGap=0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Ground Openings", meta=(ClampMin="0",ClampMax="16"))
    int32 GroundFrontOpenings=3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Ground Openings", meta=(ClampMin="0",ClampMax="16"))
    int32 GroundSideOpenings=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Ground Openings", meta=(ClampMin="50",ClampMax="800",Units="cm"))
    float GroundOpeningWidth=285;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Ground Openings", meta=(ClampMin="0.3",ClampMax="1"))
    float GroundOpeningHeightRatio=.91f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Ground Openings")
    bool bBackOpenings=true;

    /** Per upper tier; the final entry repeats if more tiers are added. Empty arrays disable windows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows", meta=(ClampMin="0",ClampMax="16"))
    TArray<int32> FrontWindowCounts={3,3};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows", meta=(ClampMin="0",ClampMax="16"))
    TArray<int32> SideWindowCounts={3,2};
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows", meta=(ClampMin="20",ClampMax="400",Units="cm"))
    float WindowWidth=125;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows", meta=(ClampMin="0.2",ClampMax="0.9"))
    float WindowHeightRatio=.74f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows", meta=(ClampMin="0",ClampMax="0.4"))
    float WindowSillRatio=.08f;
    /** Width of the facade region occupied by evenly spaced openings. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows", meta=(ClampMin="0.35",ClampMax="0.95"))
    float OpeningSpread=.78f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows")
    bool bWindowLintels=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows", meta=(ClampMin="5",ClampMax="70",Units="cm",EditCondition="bWindowLintels"))
    float LintelHeight=26;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Upper Windows", meta=(ClampMin="0",ClampMax="70",Units="cm",EditCondition="bWindowLintels"))
    float LintelOverhang=20;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Terraces", meta=(ClampMin="0",ClampMax="400",Units="cm"))
    float TerraceOverhang=150;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Terraces", meta=(ClampMin="10",ClampMax="150",Units="cm"))
    float TerraceThickness=38;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Terraces")
    bool bCorniceBands=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Terraces", meta=(ClampMin="5",ClampMax="100",Units="cm",EditCondition="bCorniceBands"))
    float CorniceHeight=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Terraces")
    bool bRailings=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Terraces", meta=(ClampMin="30",ClampMax="160",Units="cm",EditCondition="bRailings"))
    float RailingHeight=70;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Foundation", meta=(ClampMin="0",ClampMax="400",Units="cm"))
    float FoundationHeight=110;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Foundation", meta=(ClampMin="0",ClampMax="400",Units="cm"))
    float FoundationOverhang=130;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Foundation")
    bool bEntranceStairs=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Foundation", meta=(ClampMin="1",ClampMax="24",EditCondition="bEntranceStairs"))
    int32 StairCount=6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Crown")
    bool bPyramidCrown=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Crown", meta=(ClampMin="1",ClampMax="12",EditCondition="bPyramidCrown"))
    int32 CrownStepCount=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Crown", meta=(ClampMin="40",ClampMax="1500",Units="cm",EditCondition="bPyramidCrown"))
    float CrownHeight=360;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Crown", meta=(ClampMin="0.3",ClampMax="0.95",EditCondition="bPyramidCrown"))
    float CrownBaseScale=.78f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Crown", meta=(ClampMin="0.15",ClampMax="0.8",EditCondition="bPyramidCrown"))
    float CrownTopScale=.32f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Materials")
    TObjectPtr<UMaterialInterface> StoneMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Materials")
    TObjectPtr<UMaterialInterface> CorniceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Materials")
    TObjectPtr<UMaterialInterface> TerraceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Materials")
    TObjectPtr<UMaterialInterface> LintelMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Materials")
    TObjectPtr<UMaterialInterface> FoundationMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat Pyramit|Materials")
    TObjectPtr<UMaterialInterface> CrownMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat Pyramit|Statistics")
    int32 GeneratedPieceCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat Pyramit|Statistics")
    int32 GeneratedOpeningCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat Pyramit|Statistics")
    float GeneratedHeight=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat Pyramit|Statistics")
    FString GenerationWarning;

    virtual bool WorldLocationToCell(FVector WorldLocation,FIntPoint& OutCell) const override;
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
    virtual bool ResolveExistingCellFromHit(const FHitResult& Hit,FIntPoint& OutCell) const override;
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
    void BuildRailings(double W,double D,double Z);
    bool FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const;
};
