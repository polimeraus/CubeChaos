#if WITH_DEV_AUTOMATION_TESTS
#include "ParametricLobbyPlatformDestructable.h"
#include "BombProjectile.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLobbyDestructableTest,"MyProject6.LobbyDestructable.CellsCollisionSpawnsAndSharedAnimations",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLobbyDestructableTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* A=World->SpawnActor<AParametricLobbyPlatformDestructable>();
    if (!TestNotNull(TEXT("New cell-based actor spawns"),A)) { return false; }
    TestTrue(TEXT("Original class is an independent actor"),!A->IsA<AParametricLobbyPlatform>());
    TestTrue(TEXT("New actor uses existing floor damage/animation family"),A->IsA<AChunkedDestructibleFloor>());
    TestTrue(TEXT("Defaults match triangle's fixed wave and equalizer"), A->PlanarAnimationType==EPlanarAnimationType::FlowingWave && A->MusicAnimationType==EMusicAnimationType::Equalizer && A->bReactToMusic);
    A->Width=1200; A->Depth=1000; A->CornerRadius=180; A->ChunkSideLength=4;
    A->FlashDuration=0; A->bPlayDestroyTickSound=false; A->bUseDestructionRipple=false;
    auto Alive=[&]() { int32 N=0; for (const auto& Cell:A->Cells) { N+=Cell.bExists; } return N; };
    auto Trace=[&](FVector P) { FHitResult Hit; return World->LineTraceSingleByChannel(Hit,P+FVector(0,0,200),P-FVector(0,0,200),ECC_Pawn)&&Hit.GetActor()==A; };
    for (const auto Shape:{ELobbyPlatformShape::RoundedRectangle,ELobbyPlatformShape::Rectangle,ELobbyPlatformShape::Triangle,ELobbyPlatformShape::Hexagon,ELobbyPlatformShape::Ellipse,ELobbyPlatformShape::RegularPolygon})
    {
        A->Shape=Shape; A->GenerateFloor(false);
        TestTrue(TEXT("All silhouettes contain cells"),Alive()>0);
        bool Valid=true;
        for (UDynamicMeshComponent* C:A->ChunkComponents) { Valid&=C->GetMesh()->CheckValidity(); for (int32 T:C->GetMesh()->TriangleIndicesItr()) { Valid&=C->GetMesh()->GetTriArea(T)>1.e-8; } }
        TestTrue(TEXT("Clipped cells retain valid nondegenerate triangles"),Valid);
        for (FName Id:A->GetPadIds()) { FVector P; TestTrue(TEXT("Named pads stay supported in every shape"),A->GetPadWorldCenter(Id,P)&&Trace(P)); }
    }
    A->Shape=ELobbyPlatformShape::RoundedRectangle; A->GenerateFloor(false);
    const FIntPoint Cell(5,5); const FVector P=A->GetCellWorldCenter(Cell);
    const int32 Before=Alive();
    TArray<UDynamicMesh*> Previous;
    for (UDynamicMeshComponent* C:A->ChunkComponents) { Previous.Add(C->GetDynamicMesh()); }
    TestTrue(TEXT("A single cell can be destroyed"),A->DestroyCell(Cell));
    TestEqual(TEXT("One cell disappeared, not a boolean volume"),Alive(),Before-1);
    TestFalse(TEXT("Destroyed cell no longer collides"),Trace(P));
    TestTrue(TEXT("Adjacent cell retains collision"),Trace(A->GetCellWorldCenter(Cell+FIntPoint(1,0))));
    int32 Changed=0; for (int32 I=0;I<Previous.Num();++I) { Changed+=Previous[I]!=A->ChunkComponents[I]->GetDynamicMesh(); }
    TestTrue(TEXT("Damage rebuilds only affected chunks"),Changed>0 && Changed<A->ChunkComponents.Num());
    bool Interior=false, InteriorWinding=true, InteriorUVs=true;
    for (UDynamicMeshComponent* C:A->ChunkComponents) for (int32 T:C->GetMesh()->TriangleIndicesItr()) {
        const auto* Mesh=C->GetMesh();
        if (Mesh->Attributes()->GetMaterialID()->GetValue(T)!=4) { continue; }
        Interior=true;
        const auto V=Mesh->GetTriangle(T), N=Mesh->Attributes()->PrimaryNormals()->GetTriangle(T), U=Mesh->Attributes()->PrimaryUV()->GetTriangle(T);
        for (int32 K=0; K<3; ++K) {
            InteriorWinding &= Mesh->GetTriNormal(T).Dot(FVector3d(Mesh->Attributes()->PrimaryNormals()->GetElement(N[K])))>.99;
            InteriorUVs &= FMath::IsNearlyEqual(double(Mesh->Attributes()->PrimaryUV()->GetElement(U[K]).Y),-Mesh->GetVertex(V[K]).Z/A->Thickness,1.e-5);
        }
    }
    TestTrue(TEXT("Hole exposes purple/neon interior material"),Interior);
    TestTrue(TEXT("Cut walls face outward so all four sides render"),InteriorWinding);
    TestTrue(TEXT("Cut wall neon stays at the original layer height"),InteriorUVs);
    A->RebuildPlatform(); TestEqual(TEXT("Rebuild preserves destroyed cells"),Alive(),Before-1);
    A->RepairPlatform(); TestEqual(TEXT("Repair restores cells"),Alive(),Before);
    A->bDestructible=false;
    TestFalse(TEXT("Master flag blocks inherited direct destruction"),A->DestroyCell(Cell));
    TestFalse(TEXT("Master flag blocks inherited point damage"),A->DamageCellAtWorldLocation(P,100));
    TestFalse(TEXT("Master flag blocks radial destruction"),A->RemovePlatformSphere(P,250));
    TestFalse(TEXT("Master flag blocks complete destruction"),A->DestroyPlatformCompletely());
    TestTrue(TEXT("Protected collision remains"),Trace(P));
    A->bDestructible=true;
    A->QueueCellDestruction(A->ToCellIndex(Cell),.5f,true); A->bDestructible=false;
    A->ProcessQueuedDestructions(World->GetTimeSeconds()+1);
    TestEqual(TEXT("Disabling while a ripple is queued protects cells"),Alive(),Before);
    A->bDestructible=true;
    auto* Bomb=World->SpawnActor<ABombProjectile>(P+FVector(0,0,90),FRotator::ZeroRotator);
    Bomb->BlastRadius=180; Bomb->BlastDamage=100; Bomb->Explode();
    TestTrue(TEXT("Existing bomb routes into shared cell destruction"),Alive()<Before);
    A->RepairPlatform(); A->DispatchBeginPlay();
    auto Surfaces=[&]() { TArray<UDynamicMeshComponent*> Result; A->GetComponents(Result); Result.RemoveAll([](auto* C){return !C->ComponentHasTag(TEXT("PlanarAnimationSurface"));}); return Result; };
    TestEqual(TEXT("Fixed wave uses shared renderer, one surface per chunk"),Surfaces().Num(),A->ChunkComponents.Num());
    for (int32 Frame=0; Frame<36; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); }
    FLinearColor TimedPhases;
    TestTrue(TEXT("Shared timer advances fixed waves in game time"), !Surfaces().IsEmpty() &&
        Surfaces()[0]->GetMaterial(0)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Waves0")),TimedPhases) && TimedPhases.R>.05f);
    TArray<UDynamicMesh*> FixedMeshes; for (auto* C:Surfaces()) { FixedMeshes.Add(C->GetDynamicMesh()); }
    const float Phases[]={.2f,.7f}; A->RenderFixedAnimation(Phases);
    int32 I=0; for (auto* C:Surfaces()) { TestTrue(TEXT("Flowing animation never rebuilds geometry"),FixedMeshes[I++]==C->GetDynamicMesh()); }
    for (auto Type:{EMusicAnimationType::Equalizer,EMusicAnimationType::TravelingColumns})
    {
        A->SetMusicAnimationType(Type); A->TriggerMusicReaction(1,true);
        A->UpdateMusicEqualizer(A->GetMusicAnimationTime()+.15);
        bool MusicVisible=false;
        for (auto* C:Surfaces()) { float Enabled=0; MusicVisible|=C->GetMaterial(0)->GetScalarParameterValue(FMaterialParameterInfo(TEXT("MusicEnabled")),Enabled)&&Enabled>0; }
        TestTrue(TEXT("Both music modes write the shared renderer mask"),MusicVisible);
    }
    TestTrue(TEXT("Cell destruction works with both effects active"),A->DestroyCell(Cell));
    int32 SourceTriangles=0,EffectTriangles=0;
    for (UDynamicMeshComponent* C:A->ChunkComponents) { SourceTriangles+=C->GetMesh()->TriangleCount(); }
    for (const auto* C:Surfaces()) { EffectTriangles+=C->GetMesh()->TriangleCount(); TestTrue(TEXT("Animation has no collision"),C->GetCollisionEnabled()==ECollisionEnabled::NoCollision); }
    TestEqual(TEXT("Effect follows actual destruction holes"),EffectTriangles,SourceTriangles);
    A->SetReactToMusic(false); A->StopFixedAnimation();
    TestEqual(TEXT("Stopping both releases shared surfaces"),Surfaces().Num(),0);
    TestFalse(TEXT("Idle actor stops ticking"),A->IsActorTickEnabled());
    A->RepairPlatform(); A->Pads[0].SpawnOffset=FVector(0,0,130);
    AActor* Spawn=A->SpawnActorAtPad(TEXT("Center"),ACharacter::StaticClass());
    TestNotNull(TEXT("Named spawn works on surviving cells"),Spawn);
    TestTrue(TEXT("Repeated spawn is idempotent"),A->SpawnActorAtPad(TEXT("Center"),ACharacter::StaticClass())==Spawn);
    A->ClearSpawnedActors();
    FVector Center; A->GetPadWorldCenter(TEXT("Center"),Center); FIntPoint CenterCell; A->WorldLocationToCell(Center,CenterCell); A->DestroyCell(CenterCell);
    TestNull(TEXT("Destroyed pad cannot spawn an actor"),A->SpawnActorAtPad(TEXT("Center"),ACharacter::StaticClass()));
    A->RepairPlatform();
    A->SetActorTransform(FTransform(FRotator(15,35,0),FVector(3000,1000,500),FVector(1.5,.8,1.2)));
    TestTrue(TEXT("World-space sphere works with transformed platform"),A->RemovePlatformSphere(A->GetCellWorldCenter(Cell),180));
    TestTrue(TEXT("Master flag allows complete destruction when enabled"),A->DestroyPlatformCompletely());
    return true;
}
#endif
