#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ZigguratTraversal.h"
#include "ChunkedDestructibleBabylonTower.generated.h"

/** Parametric polygonal stepped tower with ground-floor entrances and upper windows.
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

    /** Number of polygon edges. Equal width/depth produces a regular polygon. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="3",ClampMax="32"))
    int32 SideCount=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="600",ClampMax="16000",Units="cm"))
    float BaseWidth=3600;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="600",ClampMax="16000",Units="cm"))
    float BaseDepth=3600;
    /** Total storeys, INCLUDING the tall top storey. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="1",ClampMax="32"))
    int32 TierCount=15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="150",ClampMax="1800",Units="cm"))
    float TierHeight=240;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="150",ClampMax="3000",Units="cm"))
    float SummitHeight=300;
    /** Highest tier width and depth relative to the base. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="0.05",ClampMax="1"))
    float TopScale=.12f;
    /** Inset of each wall edge across one storey, relative to its width/depth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Shape", meta=(ClampMin="0",ClampMax="0.08"))
    float WallBatter=.006f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Pieces", meta=(ClampMin="30",ClampMax="500",Units="cm"))
    float PieceSize=130;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Pieces", meta=(ClampMin="10",ClampMax="180",Units="cm"))
    float WallThickness=35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Pieces", meta=(ClampMin="0",ClampMax="5",Units="cm"))
    float PieceGap=0;

    // Retained for loading older assets. All polygon faces now use WindowsPerSide/WindowWidth.
    UPROPERTY()
    int32 FrontRecessCount=9;
    UPROPERTY()
    int32 SideRecessCount=7;
    UPROPERTY()
    float RecessWidth=125;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(EditCondition="!bOpenFacadeRecesses",ClampMin="0",ClampMax="100",Units="cm"))
    float RecessDepth=24;
    UPROPERTY()
    float RecessHeightRatio=.68f;
    /** Window count on each face of each upper tier; zero closes the windows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="0",ClampMax="20",EditCondition="bUpperWindows"))
    int32 WindowsPerSide=1;
    /** Optional per-tier counts PER FACE. Index 0 is the ground floor (no windows).
     * Missing entries or -1 use WindowsPerSide. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="-1",ClampMax="20",EditCondition="bUpperWindows"))
    TArray<int32> TierWindowCounts;
    /** Optional per-tier widths in cm. Missing entries or zero use WindowWidth. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="0",ClampMax="400",EditCondition="bUpperWindows"))
    TArray<float> TierWindowWidths;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade")
    bool bUpperWindows=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="30",ClampMax="400",Units="cm",EditCondition="bUpperWindows"))
    float WindowWidth=125;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="40",ClampMax="800",Units="cm",EditCondition="bUpperWindows"))
    float WindowHeight=150;
    /** A positive sill keeps upper windows distinct from ground-floor doorways. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="10",ClampMax="500",Units="cm",EditCondition="bUpperWindows"))
    float WindowSillHeight=35;
    /** Extra projecting bands. Turning these off retains the normal stepped terraces. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade")
    bool bCornices=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="0",ClampMax="200",Units="cm",EditCondition="bCornices"))
    float CorniceOverhang=25;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="0",ClampMax="150",Units="cm",EditCondition="bCornices"))
    float CorniceHeight=20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(ClampMin="10",ClampMax="150",Units="cm"))
    float FloorThickness=18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Facade", meta=(EditCondition="bCornices"))
    bool bSummitCorbels=false;

    /** The entrance faces local -Y. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance")
    bool bEntrance=true;
    /** Evenly distributed around the ground floor only; limited to SideCount. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="1",ClampMax="32",EditCondition="bEntrance"))
    int32 EntranceCount=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="80",ClampMax="800",Units="cm",EditCondition="bEntrance"))
    float EntranceWidth=220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="100",ClampMax="1500",Units="cm",EditCondition="bEntrance"))
    float EntranceHeight=185;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance")
    bool bEntranceFrame=false;
    /** Add steps up to the foundation. Requires FoundationHeight greater than zero. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance")
    bool bEntranceStairs=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="1",ClampMax="32",EditCondition="bEntranceStairs"))
    int32 StairCount=9;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="20",ClampMax="120",Units="cm",EditCondition="bEntranceStairs"))
    float StairTreadDepth=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Entrance", meta=(ClampMin="80",ClampMax="1600",Units="cm",EditCondition="bEntranceStairs"))
    float StairWidth=480;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Foundation", meta=(ClampMin="0",ClampMax="600",Units="cm"))
    float FoundationHeight=30;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Babylon Tower|Foundation", meta=(ClampMin="0",ClampMax="400",Units="cm"))
    float FoundationOverhang=0;

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
        FVector V[8];
        FBox Bounds;
        int32 Material=0;
        bool Contains(FVector P,double Tolerance) const;
    };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded=false;
    double GenerationShaftWidth=0;
    void AddPiece(const FVector (&Vertices)[8],int32 Material);
    void TilePolygon(FVector2D Size,double Z0,double Z1,int32 Material,bool bOpening=false);
    void FaceBox(int32 Face,double U0,double U1,double D0,double D1,double Z0,double Z1,int32 Material);
    void BuildFacade(int32 Face,int32 Tier,double W,double D,double Z,double H);
    void BuildPortal(int32 Face,double HalfWidth,double Z,double H);
    bool FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const;
};
