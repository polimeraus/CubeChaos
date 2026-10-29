#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleGlobe.generated.h"

class UChildActorComponent;

/** Open spherical shelves and radial ribs. Pivot is at ground level; keep the shaft upright. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Chunked Destructible Globe"))
class MYPROJECT6_API AChunkedDestructibleGlobe : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FDestructibleGlobeTest;
public:
    AChunkedDestructibleGlobe();
    virtual void PostLoad() override;
    virtual bool UsesPlanarAnimation() const override { return false; }

    /** Rebuild also restores all destroyed fragments. Changes in Details rebuild automatically. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Globe")
    void RebuildGlobe();

    /** Number of radial half-disc partitions around the full 360 degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Shape", meta=(ClampMin="3", ClampMax="32"))
    int32 MeridianCount=16;
    /** Number of horizontal ring shelves, evenly distributed in height. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Shape", meta=(ClampMin="0", ClampMax="24"))
    int32 ParallelCount=5;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Shape", meta=(ClampMin="300", ClampMax="10000", Units="cm"))
    float SphereRadius=1600;
    /** Full clear diameter of the vertical opening through all ribs and shelves. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Shape", meta=(DisplayName="Center Opening Width", ClampMin="150", ClampMax="6000", Units="cm"))
    float ShaftWidth=480;
    /** Thickness of vertical meridian ribs, independent of the horizontal shelves. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Shape", meta=(ClampMin="4", ClampMax="100", Units="cm"))
    float MeridianThickness=36;
    /** Thickness of horizontal parallel shelves, independent of the vertical ribs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Shape", meta=(ClampMin="4", ClampMax="100", Units="cm"))
    float ParallelThickness=28;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Shape", meta=(ClampMin="16", ClampMax="128"))
    int32 CurveSegments=64;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Pieces", meta=(ClampMin="60", ClampMax="600", Units="cm"))
    float PieceSize=220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Pieces", meta=(ClampMin="0", ClampMax="5", Units="cm"))
    float PieceGap=0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Materials")
    TObjectPtr<UMaterialInterface> PanelMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Materials")
    TObjectPtr<UMaterialInterface> EdgeMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Launch Pad")
    bool bEnableLaunchPad=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Launch Pad", meta=(ClampMin="50", ClampMax="1000", Units="cm", EditCondition="bEnableLaunchPad"))
    float PadDiameter=280;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Launch Pad", meta=(EditCondition="bEnableLaunchPad"))
    bool bAutoLaunchHeight=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Launch Pad", meta=(ClampMin="50", ClampMax="3000", Units="cm", EditCondition="bEnableLaunchPad && bAutoLaunchHeight"))
    float HeightAboveTop=300;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Launch Pad", meta=(ClampMin="100", ClampMax="10000", Units="cm/s", EditCondition="bEnableLaunchPad && !bAutoLaunchHeight"))
    float LaunchUpSpeed=2700;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Launch Pad", meta=(EditCondition="bEnableLaunchPad"))
    bool bLaunchNPCs=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Launch Pad", meta=(HideAlphaChannel, EditCondition="bEnableLaunchPad"))
    FLinearColor LaunchPadColor=FLinearColor(.02f,.75f,1.f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Globe|Launch Pad", meta=(EditCondition="bEnableLaunchPad"))
    TObjectPtr<UMaterialInterface> LaunchPadMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Globe|Launch Pad")
    TObjectPtr<UChildActorComponent> GroundLaunchPad;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Globe|Statistics")
    int32 GeneratedPieceCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Globe|Statistics")
    int32 GeneratedMeridianPieces=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Globe|Statistics")
    int32 GeneratedParallelPieces=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Globe|Statistics", meta=(Units="cm"))
    float GeneratedHeight=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Globe|Statistics")
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
    // Read old saved overrides once; deprecated properties are not written to new assets.
    UPROPERTY()
    float ShaftRadius_DEPRECATED=-1;
    UPROPERTY()
    float PlateThickness_DEPRECATED=-1;
    void MigrateLegacyShapeSettings();

    struct FPiece
    {
        FVector V[8];
        FBox Bounds;
        bool bShelf=false;
        bool Contains(FVector P,double Tolerance) const;
    };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded=false;
    void AddPiece(const FVector (&V)[8],bool bShelf);
    void BuildMeridians(const TArray<double>& Heights,const TArray<FVector2D>& Shelves);
    void BuildParallel(double Z0,double Z1);
    bool FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const;
};
