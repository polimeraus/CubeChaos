#pragma once

#include "CoreMinimal.h"
#include "Components/DynamicMeshComponent.h"
#include "SculptureMeshComponent.generated.h"

/** Sculpture cells never deform. Retain their cooked convex hulls as cells are removed. */
UCLASS()
class MYPROJECT6_API USculptureMeshComponent : public UDynamicMeshComponent
{
    GENERATED_BODY()
public:
    int32 GetCachedCollisionUpdates() const { return CachedCollisionUpdates; }
protected:
    virtual void RebuildPhysicsData() override;
private:
    struct FCachedHull
    {
        TArray<FVector> SourceVertices;
        FTransform SourceTransform;
        FKConvexElem Cooked;
    };
    TMap<uint32, FCachedHull> CookedHulls;
    int32 CachedCollisionUpdates = 0;
};
