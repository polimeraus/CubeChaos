// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleHalloweenPumpkin2.h"
#include "ChunkedDestructibleHalloweenPumpkin3.generated.h"

/**
 * A smooth hollow jack-o'-lantern variant that keeps the destructible-cell system but removes the
 * external ribbing for a softer, more classic pumpkin silhouette.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Destructible Halloween Pumpkin 3 (Smooth Hollow)"))
class MYPROJECT6_API AChunkedDestructibleHalloweenPumpkin3 : public AChunkedDestructibleHalloweenPumpkin2
{
    GENERATED_BODY()

public:
    AChunkedDestructibleHalloweenPumpkin3();

    /** Rebuilds with a smoother, less ribbed outer shell while keeping the empty cavity intact. */
    UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 3|Generation")
    void ConfigureSmoothHollowPumpkin(
        FVector2D NewBodyRadiusBlocks,
        float NewBodyHalfHeightBlocks,
        float NewShellThicknessBlocks,
        float NewStemHeightBlocks,
        FVector NewBlockDimensions,
        FVector NewBlockSpacing,
        bool bPreserveCellState = false);

    /** Convenience toggle for the classic carved face. */
    UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 3|Face")
    void SetSmoothPumpkinFaceEnabled(bool bEnabled, bool bPreserveCellState = false);
};
