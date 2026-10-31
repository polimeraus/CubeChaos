#pragma once
#include "CoreMinimal.h"

namespace BlastGeometry
{
// Exact surface/volume distance for a solid triangle extruded between two faces.
// World-space vertices preserve a spherical blast under rotated, nonuniform scale.
inline double TrianglePrismDistanceSquared(const FVector& Point, const FVector (&Vertices)[6], FVector* OutClosestPoint = nullptr)
{
    static constexpr int32 Faces[8][3] = {
        {0,1,2}, {3,5,4}, {0,3,4}, {0,4,1},
        {1,4,5}, {1,5,2}, {2,5,3}, {2,3,0}};
    FVector Center = FVector::ZeroVector;
    for (const FVector& Vertex : Vertices) Center += Vertex/6.;
    double DistanceSquared = TNumericLimits<double>::Max();
    bool bInside = true;
    for (const auto& Face : Faces)
    {
        const FVector& A = Vertices[Face[0]];
        const FVector& B = Vertices[Face[1]];
        const FVector& C = Vertices[Face[2]];
        FVector Normal = FVector::CrossProduct(B-A,C-A).GetSafeNormal();
        if (Normal.IsNearlyZero()) continue;
        if (Normal.Dot(Center-A) > 0) Normal *= -1;
        if (Normal.Dot(Point-A) > UE_KINDA_SMALL_NUMBER) bInside = false;
        const FVector Closest = FMath::ClosestPointOnTriangleToPoint(Point,A,B,C);
        const double FaceDistance = FVector::DistSquared(Point,Closest);
        if (FaceDistance < DistanceSquared)
        {
            DistanceSquared = FaceDistance;
            if (OutClosestPoint) *OutClosestPoint = Closest;
        }
    }
    if (bInside && DistanceSquared < TNumericLimits<double>::Max())
    {
        if (OutClosestPoint) *OutClosestPoint = Point;
        return 0.;
    }
    return DistanceSquared;
}
}
