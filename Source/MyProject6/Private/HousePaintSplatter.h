#pragma once
#include "CoreMinimal.h"
enum class ESimpleHouseWindowType : uint8;

namespace HousePaint
{
    // Independent simple polygons in window-local horizontal/vertical coordinates.
    // The caller clips each piece to the facade and subtracts every opening.
    TArray<TArray<FVector2D>> MakeSplatter(float Width, float Height,
        ESimpleHouseWindowType Shape, float CornerRadius, float Spread, float Amount, int32 Seed);
}
