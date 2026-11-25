#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedPanelStructure.generated.h"

/** Batched convex architectural panels. Rendering, weapon collision and damage use the same pieces. */
UCLASS(Abstract)
class MYPROJECT6_API AChunkedPanelStructure : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
public:
    virtual bool UsesPlanarAnimation() const override { return false; }
    virtual bool WorldLocationToCell(FVector Point, FIntPoint& Cell) const override;
#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* Property) const override;
#endif
protected:
    struct FPanel
    {
        FVector V[8], Center;
        FBox Bounds;
        TArray<FPlane,TInlineAllocator<12>> Planes;
        uint8 Slot;
        int32 EdgeTrimSlot = INDEX_NONE;
        bool Contains(const FVector& Point, double Tolerance) const;
    };
    struct FPanelLayout
    {
        TArray<FPanel> Result;
        FBox Bounds{ForceInit};
        bool bOverBudget = false;
        int32 Limit;
        double JointGap;
        FPanelLayout(int32 MaxPieces, double Gap) : Limit(MaxPieces), JointGap(Gap) {}
        void AddPanel(const FVector (&Lower)[4], const FVector (&Upper)[4], uint8 Slot, int32 Trim = INDEX_NONE);
    };
    void InstallLayout(FPanelLayout&& Layout, float FragmentSize);
    void DrawPanel(UE::Geometry::FDynamicMesh3& Mesh, const FPanel& Panel, int32 Material) const;
    TArray<FPanel> Panels;
    virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
    virtual void BuildChunkMesh(int32 Index, UE::Geometry::FDynamicMesh3& Mesh) const override;
    virtual void BuildChunkSimpleCollision(int32 Index, FKAggregateGeom& Collision) const override;
    virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
    virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
    virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& Cell) const override;
    virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
    virtual int32 SelectCellsInRadius(FVector Point, float Radius, EChunkedFloorBlastShape Shape,
        TFunctionRef<void(int32,float)> Visit) override;
    virtual void MarkCellChunksDirty(FIntPoint Cell) override;
private:
    int32 PanelsPerChunk = 48;
    TArray<FBox> ChunkBounds;
    bool FindPanel(const FVector& Local, FIntPoint& Cell, double Tolerance) const;
};
