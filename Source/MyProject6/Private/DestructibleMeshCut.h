#pragma once

#include "CoreMinimal.h"

class UDynamicMesh;
class UDynamicMeshComponent;

namespace DestructibleMesh
{
	/** Subtract a world-space sphere and publish the resulting collision immediately. */
	void SubtractSphere(UDynamicMeshComponent& Component, const FVector& Center, double Radius,
		TFunctionRef<UDynamicMesh*()> GetSphereMesh);
}
