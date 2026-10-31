#pragma once

#include "CoreMinimal.h"
#include "PlatformBlockShape.generated.h"

/** Logical shape blocks are independent of the destruction-cell grid and Tile Size. */
USTRUCT(BlueprintType)
struct FPlatformBlockShape
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block Shape", meta=(ClampMin="2", ClampMax="32"))
    int32 Columns = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block Shape", meta=(ClampMin="2", ClampMax="32"))
    int32 Rows = 3;
    /** Width of the arms in logical blocks. Clamped to leave the selected shape's opening. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block Shape", meta=(ClampMin="1", ClampMax="15"))
    int32 ArmThickness = 1;
    /** Stem position for T/Cross, from one side (0) to the other (1). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block Shape", meta=(ClampMin="0", ClampMax="1"))
    float BranchPosition = .5f;
    /** Rotates the layout inside the final Shape Size bounds. 0,1,2,3 = 0,90,180,270 degrees. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block Shape", meta=(ClampMin="0", ClampMax="3"))
    int32 QuarterTurns = 0;
    /** Mirroring produces J from L, Z from S, or reversed branches. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block Shape")
    bool bMirror = false;
    /** Used by Custom Blocks. First 256 entries; coordinates -64..64. Duplicates are ignored.
     * Blocks must form a single connected shape without enclosed holes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block Shape")
    TArray<FIntPoint> Blocks = {{0,0}, {1,0}, {2,0}, {0,1}, {0,2}};
    /** For Custom Blocks that touch only at a corner: add a square neck at that corner.
     * Zero requires edge-connected blocks; positive width supports diagonal joints in the reference. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Block Shape", meta=(ClampMin="0", Units="cm"))
    float DiagonalBridgeWidth = 0;
};
