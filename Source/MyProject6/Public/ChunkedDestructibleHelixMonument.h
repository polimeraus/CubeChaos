#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleHelixMonument.generated.h"

/** Tapering open-air promenade with a pierced lantern and a pointed crown. The pivot is the centre of the base at ground level. */
UCLASS(Blueprintable, BlueprintType, meta=(DisplayName="Destructible Helix Monument"))
class MYPROJECT6_API AChunkedDestructibleHelixMonument : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
public:
    AChunkedDestructibleHelixMonument();

    /** Rebuild with the current settings and restore all fragments. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Helix Monument")
    void RebuildMonument();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Silhouette", meta=(ClampMin="900", ClampMax="7500", Units="cm"))
    float Height = 3050;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Silhouette", meta=(ClampMin="400", ClampMax="3000", Units="cm"))
    float BaseRadius = 1030;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Silhouette", meta=(ClampMin=".10", ClampMax=".48", ToolTip="Radius at the foot of the crown as a fraction of the base radius."))
    float NeckRadiusRatio = .22f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Silhouette", meta=(ClampMin=".65", ClampMax="1.8"))
    float TaperExponent = .96f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(ClampMin="1", ClampMax="7"))
    float Turns = 4.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade")
    bool bClockwise = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(Units="deg", ToolTip="Angle of the start of the walking deck relative to the entrance."))
    float StartOffset = 40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(ClampMin="0", ClampMax=".75", ToolTip="How much of each turn's climb is concentrated on the entrance side. Zero gives an even pitch."))
    float RiseVariation = .48f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(ClampMin="80", ClampMax="650", Units="cm"))
    float WalkwayWidth = 225;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(ClampMin="40", ClampMax="420", Units="cm", ToolTip="Total height of the stone fascia, including its dark inset. Limited to leave daylight between turns."))
    float BandHeight = 244;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(ClampMin=".4", ClampMax="1"))
    float UpperBandScale = .83f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(ClampMin="10", ClampMax="80", Units="cm"))
    float ParapetThickness = 34;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(ClampMin="0", ClampMax=".54", ToolTip="Fraction of the fascia occupied by the recessed dark band. Zero gives a continuous stone face."))
    float InsetBandFraction = .31f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Promenade", meta=(ClampMin="0", ClampMax="8", Units="cm", ToolTip="Width of the recessed bronze joint between the promenade deck and its parapet."))
    float FasciaJointWidth = 3.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Entrance", meta=(ClampMin="50", ClampMax="480", Units="cm"))
    float PlinthHeight = 228;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Entrance", meta=(Units="deg"))
    float EntranceAngle = -90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Entrance")
    bool bEntranceStairs = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Entrance", meta=(ClampMin="3", ClampMax="28", EditCondition="bEntranceStairs"))
    int32 StairCount = 12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Entrance", meta=(ClampMin="180", ClampMax="2000", Units="cm", EditCondition="bEntranceStairs"))
    float EntranceWidth = 880;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Entrance", meta=(ClampMin="90", ClampMax="950", Units="cm", EditCondition="bEntranceStairs"))
    float StairRun = 350;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Entrance", meta=(ClampMin="0", ClampMax="24"))
    int32 ButtressCount = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Frame")
    bool bCentralColumn = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Frame", meta=(ClampMin="35", ClampMax="270", Units="cm", EditCondition="bCentralColumn"))
    float ColumnRadius = 101;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Frame", meta=(ClampMin="6", ClampMax="32", EditCondition="bCentralColumn"))
    int32 ColumnFlutes = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Frame", meta=(ClampMin="0", ClampMax="8", EditCondition="bCentralColumn"))
    int32 BracesPerTurn = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Frame", meta=(ClampMin="0", ClampMax="16", ToolTip="Freestanding stone piers beneath the first revolution of the promenade."))
    int32 LowerPierCount = 6;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Frame", meta=(ClampMin="45", ClampMax="280", Units="cm", EditCondition="LowerPierCount > 0"))
    float LowerPierWidth = 116;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Crown")
    bool bCrown = true;
    /** Share of the total height reserved for the crown. Without the crown the promenade ends at this shoulder. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Crown", meta=(ClampMin=".15", ClampMax=".45"))
    float CrownHeightRatio = .345f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Crown", meta=(ClampMin=".85", ClampMax="2", EditCondition="bCrown"))
    float CrownTaper = 1.42f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Crown", meta=(ClampMin="2", ClampMax="8", EditCondition="bCrown"))
    int32 LanternArches = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Crown", meta=(ClampMin="0", ClampMax="16", EditCondition="bCrown"))
    int32 LancetCount = 9;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Fragments", meta=(ClampMin="65", ClampMax="380", Units="cm"))
    float FragmentSize = 156;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Fragments", meta=(ClampMin="24", ClampMax="108", ToolTip="Minimum angular resolution of one turn. Larger radii also respect the fragment size."))
    int32 SegmentsPerTurn = 64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Fragments", meta=(ClampMin="0", ClampMax="3", Units="cm"))
    float JointGap = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Materials")
    TObjectPtr<UMaterialInterface> AggregateMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Materials")
    TObjectPtr<UMaterialInterface> LimestoneMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Materials")
    TObjectPtr<UMaterialInterface> InsetMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Helix Monument|Materials")
    TObjectPtr<UMaterialInterface> BronzeMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Helix Monument|Statistics")
    int32 GeneratedFragments = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Helix Monument|Statistics", meta=(Units="cm"))
    float GeneratedHeight = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Helix Monument|Statistics")
    FString GenerationWarning;

    virtual bool UsesPlanarAnimation() const override { return false; }
    virtual bool WorldLocationToCell(FVector Point, FIntPoint& Cell) const override;
#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* Property) const override;
#endif
protected:
    virtual bool CalculateLayout() override;
    virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
    virtual void BuildChunkMesh(int32 Index, UE::Geometry::FDynamicMesh3& Mesh) const override;
    virtual void BuildChunkSimpleCollision(int32 Index, FKAggregateGeom& Collision) const override;
    virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
    virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
    virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& Cell) const override;
    virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
    virtual int32 SelectCellsInRadius(FVector Point, float Radius, EChunkedFloorBlastShape Shape, TFunctionRef<void(int32,float)> Visit) override;
    virtual void MarkCellChunksDirty(FIntPoint Cell) override;
private:
    friend class FHelixMonumentTest;
    struct FBuilder;
    struct FStone
    {
        FVector V[8];
        FVector Center;
        FBox Bounds;
        TArray<FPlane,TInlineAllocator<12>> Planes;
        uint8 Slot;
        bool bMullion;
        bool Contains(const FVector& Point, double Tolerance) const;
    };
    TArray<FStone> Stones;
    TArray<FBox> ChunkBounds;
    int32 StonesPerChunk = 48;
    bool FindStone(const FVector& Local, FIntPoint& Cell, double Tolerance) const;
    void DrawStone(UE::Geometry::FDynamicMesh3& Mesh, const FStone& Stone, int32 Material) const;
};
