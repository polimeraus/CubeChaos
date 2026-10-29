#if WITH_DEV_AUTOMATION_TESTS
#include "ChunkedDestructibleMayaPyramid.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMayaPyramidTest,"MyProject6.Destructible.MayaPyramid",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMayaPyramidTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* A=World->SpawnActor<AChunkedDestructibleMayaPyramid>();
    if (!TestNotNull(TEXT("Native pyramid spawns"),A)) return false;
    A->FlashDuration=0;A->bPlayDestroyTickSound=false;A->bUseDestructionRipple=false;
    const int32 Count=A->GeneratedPieceCount;
    auto Triangles=[&]() { int32 N=0;for (auto& C:A->ChunkComponents) N+=C->GetDynamicMesh()->GetTriangleCount();return N; };
    TestTrue(TEXT("Complete preset fits budget"),Count>1000 && Count<35000 && A->GenerationWarning.IsEmpty());
    TestEqual(TEXT("All parts have closed six-sided geometry"),Triangles(),Count*12);
    int32 Slabs=0,Glass=0,Stairs=0,Panels=0;
    for (const auto& P:A->Pieces)
    {
        if (P.Material==1) ++Slabs;
        if (P.Material==0) ++Glass;
        if (P.Material==7) ++Stairs;
        if (P.Material==3) ++Panels;
        if (!TestTrue(TEXT("Every prism contains its center"),P.Contains(P.Bounds.GetCenter(),.001))) return false;
        UE::Geometry::FDynamicMesh3 M;M.EnableVertexNormals(FVector3f::UnitZ());M.EnableAttributes();
        M.Attributes()->SetNumUVLayers(1);M.Attributes()->EnableMaterialID();A->AppendPiece(M,P,P.Material);
        for (int32 T:M.TriangleIndicesItr())
            if (!TestTrue(TEXT("No degenerate geometry"),M.GetTriArea(T)>1.e-8)) return false;
        TestEqual(TEXT("Top triangles carry their dedicated material"),M.Attributes()->GetMaterialID()->GetValue(0),P.TopMaterial);
        TestEqual(TEXT("Side triangles retain the wall material"),M.Attributes()->GetMaterialID()->GetValue(4),P.Material);
    }
    TestTrue(TEXT("Concrete, real glass panes, recessed panels and side stairs generated"),Slabs>100 && Glass>100 && Stairs>100 && Panels>100);
    TestEqual(TEXT("Roof ends at the building, without a detached shrine or icon"),A->GeneratedHeight,A->PodiumHeight+A->TierCount*A->TierHeight+45.f);
    FHitResult Hit;
    const FVector Start(1000,-2750,200),End(1000,-2600,200);
    TestFalse(TEXT("Main entrance remains open to a standing player"),World->LineTraceSingleByChannel(Hit,{0,-2800,160},{0,-2400,160},ECC_Visibility));
    if (!TestTrue(TEXT("Weapon ray hits a real podium glass pane"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility))) return false;
    FIntPoint Cell;
    TestTrue(TEXT("Weapon hit resolves to a fragment"),A->ResolveExistingCellFromHit(Hit,Cell));
    TestTrue(TEXT("Nonlethal shot applies health"),A->DamageCellFromHit(Hit,5));
    TestEqual(TEXT("Nonlethal damage retains mesh"),Triangles(),Count*12);
    TestTrue(TEXT("Lethal shot destroys a fragment"),A->DamageCellFromHit(Hit,100));
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestEqual(TEXT("Exactly one fragment disappears"),Triangles(),Count*12-12);
    TestFalse(TEXT("Collision is removed with the destroyed fragment"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    A->RestoreCell(Cell);
    TestTrue(TEXT("Restoring the fragment restores collision"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility));
    const FTransform Transform(FRotator(0,37,0),FVector(400,-700,100),FVector(1.3,.8,1.15));
    A->SetActorTransform(Transform);
    const FVector Center=A->Pieces[Cell.X].Bounds.GetCenter();
    FIntPoint Mapped;
    TestTrue(TEXT("Rotation and nonuniform scale preserve hit identity"),A->WorldLocationToCell(Transform.TransformPosition(Center),Mapped));
    TestEqual(TEXT("Transformed cell identity"),Mapped,Cell);
    const int32 Removed=A->DestroyCellsInRadius(Transform.TransformPosition(Center),350,EChunkedFloorBlastShape::Sphere);
    A->ProcessQueuedDestructions(1.e10);A->FlushDirtyChunks();
    TestTrue(TEXT("Sphere explosion removes multiple pieces"),Removed>3);
    TestEqual(TEXT("Explosion removes exactly the affected geometry"),Triangles(),(Count-Removed)*12);
    A->SetActorTransform(FTransform::Identity);A->RebuildMayaPyramid();
    TestEqual(TEXT("Rebuild restores the whole pyramid"),Triangles(),Count*12);
    const int32 BeforeLighting=Triangles();
    A->SetInteriorLighting(.35f,false);
    TestNotNull(TEXT("Interior has an actor-owned lighting material"),A->InteriorMaterialInstance.Get());
    TestEqual(TEXT("Lighting changes preserve damaged geometry"),Triangles(),BeforeLighting);
    if (A->InteriorMaterialInstance)
    {
        TestEqual(TEXT("Brightness reaches the shader"),A->InteriorMaterialInstance->K2_GetScalarParameterValue(TEXT("InteriorBrightness")),.35f);
        TestEqual(TEXT("Animation can be frozen without ticking the actor"),A->InteriorMaterialInstance->K2_GetScalarParameterValue(TEXT("AnimateLighting")),0.f);
    }
    A->bSideStaircases=false;A->TierCount=5;A->RebuildMayaPyramid();
    TestTrue(TEXT("Simpler editable preset rebuilds"),A->GeneratedPieceCount<Count && A->GenerationWarning.IsEmpty());
    for (const auto& P:A->Pieces) if (!TestTrue(TEXT("Side stairs can be replaced by smooth inclined panels"),P.Material!=7)) return false;
    A->RebuildMayaPyramid();
    const int32 Previous=A->GeneratedPieceCount;
    AddExpectedMessage(TEXT("Piece budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    A->MaxGeneratedCells=5;A->RebuildMayaPyramid();
    TestEqual(TEXT("Budget failure preserves existing geometry"),A->GeneratedPieceCount,Previous);
    AddInfo(FString::Printf(TEXT("Architectural Maya pyramid: %d pieces / %d triangles"),Count,Count*12));
    return true;
}
#endif
