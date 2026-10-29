#pragma once
#include "CoreMinimal.h"
#include "ParametricGround.h"
namespace UE::Geometry { class FDynamicMesh3; }
namespace ParametricGroundGeometry
{
// One closed slab; cost depends on perimeter segments, never area or a destruction grid.
void BuildSlab(UE::Geometry::FDynamicMesh3& Mesh, EParametricGroundShape Shape, double SafeSize,
    double SafeThickness, double SafeUV, int32 CircleSegments, bool bShowBoundary,
    float BoundaryInset, float BoundaryWidth);
}
