#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleMayaPyramid.generated.h"

/** Terraced glass pyramid with a glazed podium and continuous concrete-framed inclined ribs. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Destructible Maya Pyramid"))
class MYPROJECT6_API AChunkedDestructibleMayaPyramid : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FMayaPyramidTest;
public:
    AChunkedDestructibleMayaPyramid();
    virtual bool UsesPlanarAnimation() const override { return false; }
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Maya Pyramid")
    void RebuildMayaPyramid();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Shape", meta=(ClampMin="1600",ClampMax="10000",Units="cm"))
    float BaseWidth=4200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Shape", meta=(ClampMin="3",ClampMax="16"))
    int32 TierCount=10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Shape", meta=(ClampMin="100",ClampMax="450",Units="cm"))
    float TierHeight=260;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Shape", meta=(ClampMin="0.12",ClampMax="0.35"))
    float SummitWidthRatio=0.17f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Pieces", meta=(ClampMin="60",ClampMax="300",Units="cm"))
    float PieceSize=180;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Inclined Ribs", meta=(DisplayName="Rib Width Ratio",ClampMin="0.07",ClampMax="0.18"))
    float StairWidthRatio=0.13f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Inclined Ribs", meta=(DisplayName="Steps On Side Ribs"))
    bool bSideStaircases=true;
    // Retained solely so older saved instances deserialize without losing their identity.
    UPROPERTY()
    bool bGenerateCrest=false;
    UPROPERTY()
    float CrestHeight=0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Podium", meta=(ClampMin="240",ClampMax="800",Units="cm"))
    float PodiumHeight=420;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Podium", meta=(ClampMin="1.1",ClampMax="1.6"))
    float PodiumWidthRatio=1.28f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Facade", meta=(ClampMin="12",ClampMax="65",Units="cm"))
    float SlabThickness=26;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Facade", meta=(ClampMin="140",ClampMax="400",Units="cm"))
    float WindowBayWidth=220;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Lighting")
    bool bAnimateInteriorLighting=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Lighting", meta=(ClampMin="0",ClampMax="5"))
    float InteriorBrightness=1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Lighting", meta=(ClampMin="2",ClampMax="60",Units="s"))
    float LightingCycleSeconds=8;
    UFUNCTION(BlueprintCallable, Category="Maya Pyramid|Lighting")
    void SetInteriorLighting(float Brightness,bool bAnimate);

    // Existing property names keep Blueprint references compatible; labels describe the new roles.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Materials", meta=(DisplayName="Glass Facade"))
    TObjectPtr<UMaterialInterface> WallMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Materials", meta=(DisplayName="Pale Concrete"))
    TObjectPtr<UMaterialInterface> LedgeMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Materials", meta=(DisplayName="Window Frames"))
    TObjectPtr<UMaterialInterface> AccentMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Materials", meta=(DisplayName="Dark Inclined Panel"))
    TObjectPtr<UMaterialInterface> StairMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Materials", meta=(DisplayName="Warm Interior Walls"))
    TObjectPtr<UMaterialInterface> ShrineMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Materials", meta=(DisplayName="Interior Light Strips"))
    TObjectPtr<UMaterialInterface> CrestMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Materials", meta=(DisplayName="Terrace Flooring"))
    TObjectPtr<UMaterialInterface> GoldMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maya Pyramid|Materials")
    TObjectPtr<UMaterialInterface> StepMaterial;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Maya Pyramid|Statistics")
    int32 GeneratedPieceCount=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Maya Pyramid|Statistics", meta=(Units="cm"))
    float GeneratedHeight=0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Maya Pyramid|Statistics")
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
        int32 Material=0,TopMaterial=0;
        bool Contains(FVector P,double Tolerance) const;
        void Vertices(FVector (&V)[8]) const;
    };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded=false;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> InteriorMaterialInstance;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> LightMaterialInstance;
    void TilePrism(FVector2D BMin,FVector2D BMax,FVector2D TMin,FVector2D TMax,double Z0,double Z1,int32 Material,int32 TopMaterial=-1);
    void Box(FVector2D Min,FVector2D Max,double Z0,double Z1,int32 Material,int32 TopMaterial=-1);
    void Ring(double Half,double Thickness,double Z0,double Z1,int32 Material);
    void RotatePieces(int32 FirstPiece,int32 Face);
    void BuildGlazing(double Half,double Z0,double Z1,double Scale,bool bPodium);
    void BuildSlab(double Half,double Z0,double Z1,double RibPlane,double RibHalfWidth,int32 TopMaterial);
    void BuildInclinedRib(int32 Face,double Z0,double Z1,double Start,double End,double Scale);
    void RefreshArchitectureMaterials();
    bool FindPiece(FVector P,FIntPoint& Cell,double Tolerance) const;
    void AppendPiece(UE::Geometry::FDynamicMesh3& Mesh,const FPiece& Piece,int32 Material) const;
};
