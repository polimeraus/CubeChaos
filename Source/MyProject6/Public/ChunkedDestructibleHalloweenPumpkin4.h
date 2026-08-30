// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleHalloweenPumpkin3.h"
#include "ChunkedDestructibleHalloweenPumpkin4.generated.h"

/**
 * A smoother hollow jack-o'-lantern variant tuned for softer outer edges and a softer silhouette.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Destructible Halloween Pumpkin 4 (Soft Edge)"))
class MYPROJECT6_API AChunkedDestructibleHalloweenPumpkin4 : public AChunkedDestructibleHalloweenPumpkin3
{
    GENERATED_BODY()

public:
    AChunkedDestructibleHalloweenPumpkin4();

    /** Applies a softer pumpkin silhouette with reduced ribbing and gentler edge transitions. */
    UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 4|Generation")
    void ConfigureSoftEdgePumpkin(
        FVector2D NewBodyRadiusBlocks,
        float NewBodyHalfHeightBlocks,
        float NewShellThicknessBlocks,
        float NewStemHeightBlocks,
        FVector NewBlockDimensions,
        FVector NewBlockSpacing,
        bool bPreserveCellState = false);

    /** Convenience toggle for the carved face on the smooth pumpkin. */
    UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 4|Face")
    void SetSoftEdgePumpkinFaceEnabled(bool bEnabled, bool bPreserveCellState = false);

    /** Controls how gently the shell transitions around the outer silhouette. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 4|Surface")
    float SoftEdgeBlend = 0.75f;

    /** Lower values reduce angularity and make the outer rim less harsh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 4|Surface", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float EdgeRounding = 0.55f;
};
