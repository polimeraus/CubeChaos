#if WITH_DEV_AUTOMATION_TESTS
#include "DestructibleRadialSculpture.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"

namespace
{
// Triangle IDs become sparse when pieces disappear. Compare the exact rendered
// attributes independently of IDs/order, including winding and both UV layers.
TArray<FString> TriangleSignatures(const UE::Geometry::FDynamicMesh3& Mesh)
{
    TArray<FString> Result;
    Result.Reserve(Mesh.TriangleCount());
    const auto* Attributes=Mesh.Attributes();
    for (const int32 T:Mesh.TriangleIndicesItr())
    {
        FSHA1 Hash;
        auto Append=[&](const auto& Value) { Hash.Update(reinterpret_cast<const uint8*>(&Value),sizeof(Value)); };
        Append(Mesh.GetTriangleGroup(T));
        Append(Attributes->GetMaterialID()->GetValue(T));
        const auto V=Mesh.GetTriangle(T),N=Attributes->PrimaryNormals()->GetTriangle(T);
        for (int32 K=0;K<3;++K)
        {
            Append(Mesh.GetVertex(V[K]));
            Append(Attributes->PrimaryNormals()->GetElement(N[K]));
            for (int32 Layer=0;Layer<2;++Layer)
            {
                const auto* UV=Attributes->GetUVLayer(Layer);
                Append(UV->GetElement(UV->GetTriangle(T)[K]));
            }
        }
        Hash.Final();FSHAHash Digest;Hash.GetHash(Digest.Hash);Result.Add(Digest.ToString());
    }
    Result.Sort();return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSculptureIncrementalGeometryTest,"MyProject6.Destructible.RadialSculptureIncrementalGeometry",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSculptureIncrementalGeometryTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* S=World->SpawnActor<ADestructibleRadialSculpture>();
    if (!TestNotNull(TEXT("Sculpture spawns"),S)) return false;
    S->FlashDuration=0;S->bPlayDestroyTickSound=false;S->bUseDestructionRipple=false;
    auto Verify=[&](const TCHAR* Stage)
    {
        for (int32 Chunk=0;Chunk<S->ChunkComponents.Num();++Chunk)
        {
            const auto* Actual=S->ChunkComponents[Chunk]->GetMesh();
            UE::Geometry::FDynamicMesh3 Reference;S->BuildFullChunkMesh(Chunk,Reference);
            if (!TestTrue(FString::Printf(TEXT("%s: chunk %d remains valid"),Stage,Chunk),Actual->CheckValidity()) ||
                !TestTrue(FString::Printf(TEXT("%s: chunk %d exactly matches full geometry, normals, UVs, glow and winding"),Stage,Chunk),
                    TriangleSignatures(*Actual)==TriangleSignatures(Reference))) return false;
        }
        return true;
    };
    TArray<int32> Targets;
    // Remove interior IDs from fin, hollow sphere and base chunks. Repeated
    // damage must also work on already sparse meshes.
    for (int32 Part=0;Part<3;++Part)
        for (int32 I=3;I<S->Pieces.Num();++I)
            if (int32(S->Pieces[I].Part)==Part) { Targets.Add(I);break; }
    TestEqual(TEXT("All three sculpture parts covered"),Targets.Num(),3);
    for (const int32 I:Targets) TestTrue(TEXT("Initial piece destroyed"),S->DestroyCell(FIntPoint(I,0)));
    if (!Verify(TEXT("First destruction"))) return false;
    for (const int32 I:Targets) TestTrue(TEXT("Adjacent piece destroyed from sparse mesh"),S->DestroyCell(FIntPoint(I+1,0)));
    if (!Verify(TEXT("Repeated destruction"))) return false;
    for (const int32 I:Targets) TestTrue(TEXT("Piece restored"),S->RestoreCell(FIntPoint(I,0)));
    if (!Verify(TEXT("Restoration"))) return false;
    S->SetGlowAnimationType(ERadialSculptureGlowAnimation::None);
    if (!Verify(TEXT("Glow disabled"))) return false;
    S->SetGlowAnimationType(ERadialSculptureGlowAnimation::SequentialFins);
    if (!Verify(TEXT("Glow restored"))) return false;
    S->ShatterSculpture(FVector::UpVector);
    return Verify(TEXT("Fully destroyed"));
}
#endif
