#pragma once

#include "CoreMinimal.h"
#include "PlatformReferenceShape.generated.h"

/** Shared controls for the reference-piece silhouettes. Color and orientation do not create duplicates. */
USTRUCT(BlueprintType)
struct FPlatformReferenceShape
{
    GENERATED_BODY()

    /** Relative opening size for rings, holes, horseshoes and round notches. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reference Shape", meta=(ClampMin="0.25", ClampMax="0.7"))
    float OpeningRatio = .5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reference Shape", meta=(ClampMin="24", ClampMax="128"))
    int32 CurveSegments = 64;

    /** Rotates inside the final Shape Size bounds. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reference Shape", meta=(ClampMin="0", ClampMax="3"))
    int32 QuarterTurns = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reference Shape")
    bool bMirror = false;
};
