#pragma once
#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ZigguratTraversal.h"
#include "ChunkedDestructiblePolygonTower.generated.h"

/** Hollow terraced polygon temple; walls, roofs, cornices and stairs are damageable fragments. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Chunked Destructible Polygon Tower"))
class MYPROJECT6_API AChunkedDestructiblePolygonTower : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FPolygonTowerTest;
public:
    AChunkedDestructiblePolygonTower();
    virtual bool UsesPlanarAnimation() const override { return false; }
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Polygon Tower")
    void RebuildTower();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Shape", meta=(ClampMin="3",ClampMax="16"))
    int32 SideCount=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Shape", meta=(ClampMin="1",ClampMax="20"))
    int32 TierCount=12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Shape", meta=(ClampMin="300",ClampMax="10000",Units="cm"))
    float BaseRadius=1800;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Shape", meta=(ClampMin="150",ClampMax="1500",Units="cm"))
    float TierHeight=260;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Shape", meta=(ClampMin="0.1",ClampMax="1"))
    float TopScale=.16f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Shape", meta=(ClampMin="0",ClampMax="0.12"))
    float WallBatter=.025f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Pieces", meta=(ClampMin="30",ClampMax="500",Units="cm"))
    float PieceSize=110;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Pieces", meta=(ClampMin="10",ClampMax="200",Units="cm"))
    float WallThickness=38;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Pieces", meta=(ClampMin="0",ClampMax="5",Units="cm"))
    float PieceGap=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Windows", meta=(ClampMin="0",ClampMax="8"))
    int32 WindowsPerSide=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Windows", meta=(ClampMin="30",ClampMax="500",Units="cm"))
    float WindowWidth=110;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Windows", meta=(ClampMin="40",ClampMax="1000",Units="cm"))
    float WindowHeight=165;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Windows", meta=(ClampMin="0",ClampMax="500",Units="cm"))
    float WindowSillHeight=45;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Entrance", meta=(ClampMin="0",ClampMax="16"))
    int32 EntranceCount=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Entrance", meta=(ClampMin="80",ClampMax="800",Units="cm"))
    float EntranceWidth=210;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Entrance", meta=(ClampMin="100",ClampMax="1200",Units="cm"))
    float EntranceHeight=225;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Entrance")
    bool bEntranceStairs=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Entrance", meta=(ClampMin="1",ClampMax="32"))
    int32 StairCount=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Entrance", meta=(ClampMin="20",ClampMax="120",Units="cm"))
    float StairTreadDepth=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Terraces", meta=(ClampMin="0",ClampMax="250",Units="cm"))
    float TerraceOverhang=55;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Terraces", meta=(ClampMin="10",ClampMax="150",Units="cm"))
    float FloorThickness=28;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Terraces", meta=(ClampMin="0",ClampMax="100",Units="cm"))
    float CorniceHeight=20;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Foundation", meta=(ClampMin="0",ClampMax="600",Units="cm"))
    float FoundationHeight=90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Foundation", meta=(ClampMin="0",ClampMax="400",Units="cm"))
    float FoundationOverhang=70;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Materials")
    TObjectPtr<UMaterialInterface> StoneMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Materials")
    TObjectPtr<UMaterialInterface> CorniceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Materials")
    TObjectPtr<UMaterialInterface> FloorMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Polygon Tower|Traversal", meta=(ShowOnlyInnerProperties))
    FZigguratTraversalSettings Traversal;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Polygon Tower|Traversal")
    TObjectPtr<UChildActorComponent> GroundLaunchPad;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Polygon Tower|Statistics")
    int32 GeneratedPieceCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Polygon Tower|Statistics")
    int32 GeneratedOpeningCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Polygon Tower|Statistics", meta=(Units="cm"))
    float GeneratedHeight=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Polygon Tower|Statistics", meta=(Units="cm"))
    float EffectiveOpeningWidth=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Polygon Tower|Statistics")
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
    void AddPiece(const FVector (&V)[8],int32 Material);
    void BuildWall(int32 Face,int32 Tier,double Radius,double Z);
    void TileRing(double Radius,double InnerRadius,double Z0,double Z1,int32 Material);
    bool FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const;
};
