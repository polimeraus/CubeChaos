#pragma once

#include "CoreMinimal.h"

struct FDestructibleFaceUVs
{
    FVector2f A, B, C, D;
};

// Project in actor space so coplanar fragments share a texture origin. Both
// triangles use the same plane even when the four corners are slightly bent.
inline FDestructibleFaceUVs ProjectDestructibleFaceUVs(
    const FVector& A, const FVector& B, const FVector& C, const FVector& D,
    const FVector& Normal, float UnitsPerTile)
{
    const FVector AbsNormal = Normal.GetAbs();
    int32 UAxis = 0;
    int32 VAxis = 1;
    double Facing = Normal.Z;
    if (AbsNormal.X > AbsNormal.Z && AbsNormal.X >= AbsNormal.Y)
    {
        UAxis = 1;
        VAxis = 2;
        Facing = Normal.X;
    }
    else if (AbsNormal.Y > AbsNormal.Z)
    {
        VAxis = 2;
        Facing = -Normal.Y;
    }

    const double VScale = 1.0 / FMath::Max(1.0f, UnitsPerTile);
    const double UScale = Facing < 0 ? -VScale : VScale;
    auto Project = [&](const FVector& Point)
    {
        return FVector2f(Point[UAxis] * UScale, Point[VAxis] * VScale);
    };
    return {Project(A), Project(B), Project(C), Project(D)};
}
