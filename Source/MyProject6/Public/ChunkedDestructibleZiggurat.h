#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ZigguratTraversal.h"
#include "ChunkedDestructibleZiggurat.generated.h"

/** A hollow, terraced temple. Every wall, frame, stair and railing is made of damageable boxes. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Chunked Destructible Ziggurat"))
class MYPROJECT6_API AChunkedDestructibleZiggurat : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FDestructibleZigguratTest;
public:
    AChunkedDestructibleZiggurat();
    virtual bool UsesPlanarAnimation() const override { return false; }

    /** Rebuild after changing parameters at runtime. Also restores all destroyed pieces. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Ziggurat")
    void RebuildZiggurat();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Traversal", meta=(ShowOnlyInnerProperties))
    FZigguratTraversalSettings Traversal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ziggurat|Traversal")
    TObjectPtr<UChildActorComponent> GroundLaunchPad;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat|Statistics", meta=(Units="cm"))
    float EffectiveOpeningWidth = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Shape", meta=(ClampMin="400", ClampMax="20000", Units="cm"))
    float BaseWidth = 2400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Shape", meta=(ClampMin="400", ClampMax="20000", Units="cm"))
    float BaseDepth = 2100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Shape", meta=(ClampMin="1", ClampMax="12"))
    int32 TierCount = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Shape", meta=(ClampMin="150", ClampMax="2000", Units="cm"))
    float TierHeight = 440;
    /** Width/depth of the highest tier relative to the lowest. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Shape", meta=(ClampMin="0.15", ClampMax="1"))
    float TopScale = 0.28f;
    /** Successive tiers become shorter by this factor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Shape", meta=(ClampMin="0.65", ClampMax="1"))
    float TierHeightScale = 0.9f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Pieces", meta=(ClampMin="25", ClampMax="400", Units="cm"))
    float PieceSize = 90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Pieces", meta=(ClampMin="5", ClampMax="150", Units="cm"))
    float WallThickness = 40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Pieces", meta=(ClampMin="0", ClampMax="8", Units="cm"))
    float PieceGap = 0.6f;

    /** Requested openings on EACH front/back facade, before fitting to the available width. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Windows", meta=(ClampMin="0", ClampMax="24"))
    int32 FrontWindowCount = 6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Windows", meta=(ClampMin="0", ClampMax="24"))
    int32 SideWindowCount = 5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Windows")
    bool bReduceWindowsOnUpperTiers = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Windows", meta=(ClampMin="20", ClampMax="500", Units="cm"))
    float WindowWidth = 95;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Windows", meta=(ClampMin="20", ClampMax="1000", Units="cm"))
    float WindowHeight = 215;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Windows", meta=(ClampMin="0", ClampMax="1000", Units="cm"))
    float WindowSillHeight = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Windows")
    bool bWindowFrames = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Windows", meta=(ClampMin="4", ClampMax="50", Units="cm", EditCondition="bWindowFrames"))
    float FrameWidth = 14;

    /** Entrance faces local -Y. Upper doors open onto the surrounding terraces. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Entrance")
    bool bEntrance = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Entrance")
    bool bUpperTierDoors = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Entrance", meta=(ClampMin="50", ClampMax="600", Units="cm"))
    float EntranceWidth = 250;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Entrance", meta=(ClampMin="80", ClampMax="1000", Units="cm"))
    float EntranceHeight = 340;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Entrance")
    bool bEntranceStairs = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Entrance", meta=(ClampMin="1", ClampMax="32", EditCondition="bEntranceStairs"))
    int32 StairCount = 6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Entrance", meta=(ClampMin="20", ClampMax="100", Units="cm", EditCondition="bEntranceStairs"))
    float StairTreadDepth = 36;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Entrance", meta=(ClampMin="0", ClampMax="600", Units="cm"))
    float FoundationHeight = 120;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Terraces")
    bool bBalconies = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Terraces", meta=(ClampMin="0", ClampMax="500", Units="cm", EditCondition="bBalconies"))
    float BalconyOverhang = 95;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Terraces", meta=(ClampMin="10", ClampMax="150", Units="cm"))
    float TerraceThickness = 38;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Terraces")
    bool bRailings = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Terraces", meta=(ClampMin="30", ClampMax="200", Units="cm", EditCondition="bRailings"))
    float RailingHeight = 65;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Terraces", meta=(ClampMin="60", ClampMax="600", Units="cm", EditCondition="bRailings"))
    float RailingPostSpacing = 220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Details")
    bool bCornerPillars = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Details")
    bool bCrown = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Details", meta=(ClampMin="1", ClampMax="8", EditCondition="bCrown"))
    int32 CrownSteps = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Details", meta=(ClampMin="10", ClampMax="200", Units="cm", EditCondition="bCrown"))
    float CrownStepHeight = 50;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Materials")
    TObjectPtr<UMaterialInterface> WallMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Materials")
    TObjectPtr<UMaterialInterface> TrimMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Materials")
    TObjectPtr<UMaterialInterface> TerraceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Materials")
    TObjectPtr<UMaterialInterface> RailingMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Materials")
    TObjectPtr<UMaterialInterface> StairMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ziggurat|Materials")
    TObjectPtr<UMaterialInterface> CrownMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat|Statistics")
    int32 GeneratedPieceCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat|Statistics")
    int32 GeneratedWindowCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat|Statistics")
    float GeneratedHeight = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Ziggurat|Statistics")
    FString GenerationWarning;

    virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const override;
#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
protected:
    virtual bool CalculateLayout() override;
    virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
    virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& OutMesh) const override;
    virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const override;
    virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
    virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
    virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& OutCell) const override;
    virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
    virtual int32 SelectCellsInRadius(FVector WorldLocation, float Radius, EChunkedFloorBlastShape Shape,
        TFunctionRef<void(int32, float)> CellFunction) override;
    virtual void MarkCellChunksDirty(FIntPoint Cell) override;
private:
    struct FPiece { FBox Bounds; int32 Material; };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded = false;
    double GenerationShaftWidth = 0;
    void TileBox(FVector Min, FVector Max, int32 Material);
    void TileSlab(FVector Min, FVector Max, int32 Material);
    void BuildFacade(int32 Face, int32 Tier, double Width, double Depth, double Z, double Height);
    void FacadeBox(int32 Face, double Offset, double U0, double U1, double Z0, double Z1,
        double Thickness, int32 Material);
    void BuildRailings(double Width, double Depth, double Z);
    bool FindPiece(FVector LocalPoint, FIntPoint& OutCell, double Tolerance) const;
    void AppendBox(UE::Geometry::FDynamicMesh3& Mesh, const FBox& Box, int32 Material) const;
};
