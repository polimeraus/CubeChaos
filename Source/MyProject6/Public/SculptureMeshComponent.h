#pragma once

#include "CoreMinimal.h"
#include "Chaos/Convex.h"
#include "Components/DynamicMeshComponent.h"
#include "SculptureMeshComponent.generated.h"

/** Keeps cooked collision for rigid cells as nearby pieces are removed or restored. */
UCLASS()
class MYPROJECT6_API USculptureMeshComponent : public UDynamicMeshComponent
{
    GENERATED_BODY()
public:
    int32 GetCachedCollisionUpdates() const { return CachedCollisionUpdates; }
    void RebuildGeneratedCollision(TFunctionRef<void(FKAggregateGeom&)> BuildShapes);
protected:
    virtual void RebuildPhysicsData() override;
private:
    struct FCachedHull
    {
        TArray<FVector> SourceVertices;
        FTransform SourceTransform;
        Chaos::FConvexPtr Cooked;
        TArray<int32> GeneratedIndices;
    };
    TMap<uint32, FCachedHull> CookedHulls;
    int32 CachedCollisionUpdates = 0;
};
