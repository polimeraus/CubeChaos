#if WITH_DEV_AUTOMATION_TESTS
#include "DestructibleRadialSculpture.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadialSculpturePresentationTest,"MyProject6.Destructible.RadialSculpturePresentation",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRadialSculpturePresentationTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* A=World->SpawnActor<ADestructibleRadialSculpture>();
    if (!TestNotNull(TEXT("Scupture spawns"),A)) return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    if (!TestNotNull(TEXT("Shader glow loads on native/runtime spawns"),A->SculptureGlowInstance.Get())) return false;
    TestFalse(TEXT("Shader animation does not enable per-frame actor tick"),A->IsActorTickEnabled());
    const float InitialZ=A->GetSphereLocalCenter().Z;
    A->FinHeight+=160;A->RebuildSculpture();
    TestTrue(TEXT("Leg height raises sphere by the same amount"),FMath::IsNearlyEqual(A->GetSphereLocalCenter().Z,InitialZ+160));
    double Top=0;
    TSet<int32> LegIDs;
    for (const auto& P:A->Pieces) if(P.Part==ADestructibleRadialSculpture::EPart::Fin)
    {
        Top=FMath::Max(Top,P.Bounds.Max.Z);LegIDs.Add(P.FinIndex);
        const auto* UV=P.Mesh.Attributes()->GetUVLayer(1);
        for (int32 T:P.Mesh.TriangleIndicesItr())
        {
            const auto V=P.Mesh.GetTriangle(T), U=UV->GetTriangle(T);
            for (int32 K=0;K<3;++K)
                if (!TestTrue(TEXT("All leg faces carry exact identity and continuous height"),
                    UV->GetElement(U[K]).X==P.FinIndex && FMath::IsNearlyEqual(double(UV->GetElement(U[K]).Y),P.Mesh.GetVertex(V[K]).Z,.001))) return false;
        }
    }
    TestEqual(TEXT("Every leg participates once"),LegIDs.Num(),A->FinCount);
    TestTrue(TEXT("Leg geometry extends to requested height"),FMath::IsNearlyEqual(Top,double(A->BaseThickness+A->FinHeight),.01));
    A->SphereRadius=170;A->RebuildSculpture();
    TestEqual(TEXT("Sphere radius remains independent from leg height"),A->FinHeight,640.f);
    TestEqual(TEXT("Sphere radius controls hollow sphere size"),A->GetSphereInnerRadius(),155.f);
    TestTrue(TEXT("Glow reaches top of resized sphere"),FMath::IsNearlyEqual(double(A->GeneratedGlowHeight),A->GetSphereLocalCenter().Z+170,.01));
    A->DestroyCell(FIntPoint(0,0));
    const int32 Remaining=A->RemainingPieceCount;
    auto* Alternative=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Structures/Destructible/RadialSculpture/Materials/M_SculptureStage.M_SculptureStage"));
    if (!TestNotNull(TEXT("Alternate material exists"),Alternative)) return false;
    A->SetSculptureMaterial(Alternative);
    for (const auto& C:A->ChunkComponents) TestEqual(TEXT("User material reaches every chunk"),C->GetMaterial(0),Alternative);
    TestEqual(TEXT("Changing material preserves damage"),A->RemainingPieceCount,Remaining);
    A->SetGlowAnimationType(ERadialSculptureGlowAnimation::BottomToTop);
    TestEqual(TEXT("Shader switches to upward mode"),A->SculptureGlowInstance->K2_GetScalarParameterValue(TEXT("Mode")),2.f);
    A->GlowDuration=2;A->SculptureGlowIntensity=12;A->RefreshSculptureGlow();
    TestEqual(TEXT("Runtime duration changes apply"),A->SculptureGlowInstance->K2_GetScalarParameterValue(TEXT("Duration")),2.f);
    TestEqual(TEXT("Changing animation preserves damage"),A->RemainingPieceCount,Remaining);
    auto TriangleCount=[&](){int32 N=0;for(const auto& C:A->ChunkComponents) N+=C->GetDynamicMesh()->GetTriangleCount();return N;};
    const int32 WithGlow=TriangleCount();
    A->SetGlowAnimationType(ERadialSculptureGlowAnimation::None);
    TestNull(TEXT("None releases glow material"),A->SculptureGlowInstance.Get());
    TestEqual(TEXT("None removes just the additive skin"),TriangleCount()*2,WithGlow);
    A->RebuildSculpture();
    for (const auto& C:A->ChunkComponents) TestEqual(TEXT("Rebuild keeps selected material"),C->GetMaterial(0),Alternative);
    A->SetGlowAnimationType(ERadialSculptureGlowAnimation::SequentialFins);
    A->ShatterSculpture(FVector::UpVector);
    TestEqual(TEXT("Destruction removes both geometry and glow"),TriangleCount(),0);
    return true;
}
#endif
