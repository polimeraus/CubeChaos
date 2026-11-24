#pragma once

#include "CoreMinimal.h"

class AParametricPickup;
enum class EPickupShape : uint8;
namespace UE::Geometry { class FDynamicMesh3; }

namespace PickupRetro
{
    bool IsHousing(EPickupShape Shape);
    void Build(UE::Geometry::FDynamicMesh3& Mesh, const AParametricPickup& Pickup, double Size, double Depth);
}
