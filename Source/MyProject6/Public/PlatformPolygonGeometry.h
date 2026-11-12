#pragma once
#include "CoreMinimal.h"

/** Used by both the planner and actual mesh. Never fits polygons to catalog silhouettes. */
namespace PlatformPolygonGeometry
{
    MYPROJECT6_API bool BuildOutline(const TArray<FVector2D>& Source, float CornerRadius, int32 CornerSegments,
        TArray<FVector2D>& Out, FString& Error);
}
