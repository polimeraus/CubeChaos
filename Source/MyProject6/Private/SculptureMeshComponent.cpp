#include "SculptureMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Chaos/Convex.h"

namespace
{
    uint32 HullKey(const FKConvexElem& Hull)
    {
        uint32 Key = GetTypeHash(Hull.VertexData.Num());
        for (const FVector& Vertex : Hull.VertexData) Key = HashCombineFast(Key, GetTypeHash(Vertex));
        return Key;
    }
}

void USculptureMeshComponent::RebuildPhysicsData()
{
    // Never reuse for a changed hull or complex triangle collision. A hash is only
    // an index: exact vertices and transform must match before taking the fast path.
    bool bReusable = !bEnableComplexCollision && !CookedHulls.IsEmpty();
    for (const FKConvexElem& Hull : AggGeom.ConvexElems)
    {
        const FCachedHull* Cached = CookedHulls.Find(HullKey(Hull));
        if (!Cached || Cached->SourceVertices != Hull.VertexData || !Cached->SourceTransform.Equals(Hull.GetTransform())
            || !Cached->Cooked.GetChaosConvexMesh())
        {
            bReusable = false;
            break;
        }
    }
    UBodySetup* Body = GetBodySetup();
    if (bReusable && Body)
    {
        // Destroy the old shapes before replacing their aggregate. Shared Chaos hull
        // references stay alive in the cache; every removed cell loses collision now.
        DestroyPhysicsState();
        Body->RemoveSimpleCollision();
        Body->AggGeom = AggGeom;
        // FKConvexElem deliberately omits the physics pointer when copied.
        // Restore each shared hull explicitly AFTER copying the aggregate.
        for (int32 I = 0; I < AggGeom.ConvexElems.Num(); ++I)
        {
            const FCachedHull& Cached = CookedHulls.FindChecked(HullKey(AggGeom.ConvexElems[I]));
            Body->AggGeom.ConvexElems[I].SetConvexMeshObject(Chaos::FConvexPtr(Cached.Cooked.GetChaosConvexMesh()),
                FKConvexElem::EConvexDataUpdateMethod::UpdateConvexDataOnlyIfMissing);
        }
        Body->CollisionTraceFlag = CollisionType;
        Body->bCreatedPhysicsMeshes = true;
        Body->bFailedToCreatePhysicsMeshes = false;
        RecreatePhysicsState();
        bCollisionUpdatePending = false;
        ++CachedCollisionUpdates;
        return;
    }
    Super::RebuildPhysicsData();
    Body = GetBodySetup();
    if (Body && !bEnableComplexCollision && !bUseAsyncCooking)
    {
        // An edited/rebuilt sculpture gets a new component. Bound the cache as an
        // additional guard for callers that directly replace this component's mesh.
        if (CookedHulls.Num() > 512) CookedHulls.Reset();
        if (Body->AggGeom.ConvexElems.Num() == AggGeom.ConvexElems.Num())
            for (int32 I = 0; I < AggGeom.ConvexElems.Num(); ++I)
            {
                const FKConvexElem& Source = AggGeom.ConvexElems[I];
                const FKConvexElem& Cooked = Body->AggGeom.ConvexElems[I];
                // Chaos may reorder or simplify vertices when it cooks a hull.
                // Key by the original geometry, retain the resulting physics object.
                if (Cooked.GetChaosConvexMesh())
                {
                    FCachedHull& Entry = CookedHulls.FindOrAdd(HullKey(Source));
                    Entry.SourceVertices = Source.VertexData;
                    Entry.SourceTransform = Source.GetTransform();
                    Entry.Cooked.ResetChaosConvexMesh();
                    Entry.Cooked = Cooked;
                    Entry.Cooked.SetConvexMeshObject(Chaos::FConvexPtr(Cooked.GetChaosConvexMesh()),
                        FKConvexElem::EConvexDataUpdateMethod::UpdateConvexDataOnlyIfMissing);
                }
            }
    }
}
