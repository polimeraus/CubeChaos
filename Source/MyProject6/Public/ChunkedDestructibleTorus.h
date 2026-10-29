#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleTorus.generated.h"

/** Open, ribbed torus with horizontal shelves and optional spherical ends. Pivot is at ground level. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Chunked Destructible Torus"))
class MYPROJECT6_API AChunkedDestructibleTorus : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FDestructibleTorusTest;
public:
    AChunkedDestructibleTorus();
    virtual bool UsesPlanarAnimation() const override { return false; }
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Torus")
    void RebuildTorus();

    /** Distance from the actor center to the centerline of the tube. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Shape", meta=(ClampMin="400", ClampMax="10000", Units="cm"))
    float RingRadius=1800;
    /** Circular tube radius; overall height is twice this value. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Shape", meta=(ClampMin="150", ClampMax="4000", Units="cm"))
    float TubeRadius=750;
    /** Zero makes a closed torus. Rounded ends fit this angle to retain a clear entrance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Opening", meta=(ClampMin="0", ClampMax="300", Units="deg"))
    float OpeningAngle=90;
    /** Direction of the opening in local XY. -90 points toward -Y. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Opening", meta=(Units="deg"))
    float OpeningDirection=-90;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Opening")
    bool bRoundedEnds=true;
    /** Number of vertical radial partitions along the occupied arc, including its rounded ends. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Panels", meta=(ClampMin="3", ClampMax="96"))
    int32 MeridianCount=36;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Panels", meta=(ClampMin="0", ClampMax="24"))
    int32 ParallelCount=7;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Panels", meta=(ClampMin="2", ClampMax="100", Units="cm"))
    float MeridianThickness=12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Panels", meta=(ClampMin="2", ClampMax="100", Units="cm"))
    float ParallelThickness=12;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Quality", meta=(ClampMin="16", ClampMax="128"))
    int32 CurveSegments=40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Quality", meta=(ClampMin="32", ClampMax="512"))
    int32 RingSegments=160;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Pieces", meta=(ClampMin="60", ClampMax="600", Units="cm"))
    float PieceSize=220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Pieces", meta=(ClampMin="0", ClampMax="5", Units="cm"))
    float PieceGap=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Materials")
    TObjectPtr<UMaterialInterface> PanelMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Torus|Materials")
    TObjectPtr<UMaterialInterface> EdgeMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Torus|Statistics")
    int32 GeneratedPieceCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Torus|Statistics")
    int32 GeneratedMeridianPieces=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Torus|Statistics")
    int32 GeneratedParallelPieces=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Torus|Statistics", meta=(Units="cm"))
    float GeneratedHeight=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Torus|Statistics", meta=(Units="cm"))
    float CenterOpeningWidth=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Torus|Statistics")
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
        bool bShelf=false;
        bool Contains(FVector P,double Tolerance) const;
    };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded=false;
    void AddPiece(const FVector (&V)[8],bool bShelf);
    void BuildMeridians(double Start,double Span,const TArray<FVector2D>& Shelves);
    void BuildParallel(double Start,double Span,double Z0,double Z1);
    bool FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const;
};
