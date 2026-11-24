#include "SculptureMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
    uint32 HullKey(const FKConvexElem& Hull)
    {
        uint32 Key = GetTypeHash(Hull.VertexData.Num());
        for (const FVector& Vertex : Hull.VertexData) Key = HashCombineFast(Key, GetTypeHash(Vertex));
        return Key;
    }
}

void USculptureMeshComponent::RebuildGeneratedCollision(TFunctionRef<void(FKAggregateGeom&)> BuildShapes)
{
    // Chunk replacement calls this while unregistered. Build into the component's
    // aggregate directly so thousands of convex vertex arrays are not copied twice.
    AggGeom.EmptyElements();
    BuildShapes(AggGeom);
    UpdateCollision(false);
}

void USculptureMeshComponent::RebuildPhysicsData()
{
    // Never reuse for a changed hull or complex triangle collision. A hash is only
    // an index: exact vertices and transform must match before taking the fast path.
    bool bReusable = !bEnableComplexCollision && !bUseAsyncCooking && !CookedHulls.IsEmpty();
    TArray<uint32, TInlineAllocator<64>> HullKeys;
    if (bReusable)
    {
        HullKeys.Reserve(AggGeom.ConvexElems.Num());
        for (const FKConvexElem& Hull : AggGeom.ConvexElems)
        {
            const uint32 Key = HullKey(Hull);
            const FCachedHull* Cached = CookedHulls.Find(Key);
            if (!Cached || Cached->SourceVertices != Hull.VertexData || !Cached->SourceTransform.Equals(Hull.GetTransform(), 0.)
                || !Cached->Cooked)
            {
                bReusable = false;
                break;
            }
            HullKeys.Add(Key);
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
            const FCachedHull& Cached = CookedHulls.FindChecked(HullKeys[I]);
            FKConvexElem& Hull = Body->AggGeom.ConvexElems[I];
            // With an empty index buffer, SetConvexMeshObject runs the hull builder again.
            if (Hull.IndexData.IsEmpty()) Hull.IndexData = Cached.GeneratedIndices;
            Hull.SetConvexMeshObject(Chaos::FConvexPtr(Cached.Cooked),
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
                    Entry.Cooked = Cooked.GetChaosConvexMesh();
                    Entry.GeneratedIndices.Reset();
                    if (Source.IndexData.IsEmpty() && Source.VertexData == Cooked.VertexData)
                    {
                        Entry.GeneratedIndices = Cooked.IndexData;
                    }
                }
            }
    }
}
