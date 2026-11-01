#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BombProjectile.h"
#include "DestructibleRadialSculpture.h"
#include "SculptureMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombSculptureBenchmark,"MyProject6.Performance.BombSculptureVolley",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBombSculptureBenchmark::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Class=LoadClass<ADestructibleRadialSculpture>(nullptr,TEXT("/Game/Structures/Destructible/RadialSculpture/BP_DestructibleRadialSculpture.BP_DestructibleRadialSculpture_C"));
    if (!TestNotNull(TEXT("Saved sculpture class loads"),Class)) return false;
    FString Runs;
    for (int32 Repeat=0;Repeat<3;++Repeat)
    {
        FMath::RandInit(61509);
        const FTransform Transform(FRotator::ZeroRotator,FVector::ZeroVector,FVector(5.75));
        auto* S=World->SpawnActorDeferred<ADestructibleRadialSculpture>(Class,Transform);
        S->FinishSpawning(Transform);
        const int32 Before=S->RemainingPieceCount;
        TestTrue(TEXT("Benchmark retains normal destruction flashes and ripple"),S->FlashDuration>0 && S->bUseDestructionRipple);
        TestTrue(TEXT("Benchmark retains the animated glow overlay"),S->SculptureGlowInstance!=nullptr);
        double MeshProbe=0,CollisionProbe=0;
        int32 Triangles=0,Hulls=0;
        for (int32 Chunk=0;Chunk<S->ChunkComponents.Num();++Chunk)
        {
            UE::Geometry::FDynamicMesh3 Mesh;
            double Start=FPlatformTime::Seconds(); S->BuildChunkMesh(Chunk,Mesh);
            MeshProbe+=(FPlatformTime::Seconds()-Start)*1000; Triangles+=Mesh.TriangleCount();
            FKAggregateGeom Collision;
            Start=FPlatformTime::Seconds(); S->BuildChunkSimpleCollision(Chunk,Collision);
            CollisionProbe+=(FPlatformTime::Seconds()-Start)*1000; Hulls+=Collision.ConvexElems.Num();
        }
        TArray<double> Frames;
        double BlastTotal=0,TickTotal=0,BlastMax=0;
        int32 Shots=0;
        FString Origins;
        for (int32 Frame=0;Frame<240;++Frame)
        {
            const double Start=FPlatformTime::Seconds();
            if (Shots<12 && Frame%12==0)
            {
                const double Angle=Shots<6 ? Shots*2.399963 : UE_TWO_PI*((Shots*3)%S->FinCount)/S->FinCount;
                const double R=Shots<6 ? S->SphereRadius*.7 : 230+(Shots%3)*60;
                const FVector Local(FMath::Cos(Angle)*R,FMath::Sin(Angle)*R,0);
                const FVector Above=Transform.TransformPosition(Local+FVector(0,0,1200));
                const FVector Below=Transform.TransformPosition(Local-FVector(0,0,10));
                FHitResult Hit;
                const FVector Impact=World->LineTraceSingleByChannel(Hit,Above,Below,ECC_Visibility)
                    ? Hit.ImpactPoint : Transform.TransformPosition(Local);
                const FVector Origin=Impact+FVector(0,0,35);
                Origins+=FString::Printf(TEXT("%s[%.3f,%.3f,%.3f]"),Shots?TEXT(","):TEXT(""),Origin.X,Origin.Y,Origin.Z);
                auto* B=World->SpawnActorDeferred<ABombProjectile>(ABombProjectile::StaticClass(),FTransform(Origin));
                B->LaunchVelocity=FVector::ZeroVector;
                B->FinishSpawning(FTransform(Origin));B->Movement->Deactivate();
                const double BlastStart=FPlatformTime::Seconds();B->Explode();
                const double BlastMs=(FPlatformTime::Seconds()-BlastStart)*1000;
                BlastTotal+=BlastMs;BlastMax=FMath::Max(BlastMax,BlastMs);++Shots;
            }
            const double TickStart=FPlatformTime::Seconds();
            ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60);
            TickTotal+=(FPlatformTime::Seconds()-TickStart)*1000;
            Frames.Add((FPlatformTime::Seconds()-Start)*1000);
        }
        int32 Updates=0; uint32 CellHash=0;
        for (auto* C : TInlineComponentArray<USculptureMeshComponent*>(S)) Updates+=C->GetCachedCollisionUpdates();
        for (const auto& Cell:S->Cells) CellHash=HashCombineFast(CellHash,GetTypeHash(Cell.bExists));
        Frames.Sort();
        const FString Row=FString::Printf(TEXT("{\"repeat\":%d,\"shots\":%d,\"initial_pieces\":%d,\"destroyed\":%d,\"cell_state_hash\":%u,\"triangles\":%d,\"convex_hulls\":%d,\"collision_updates\":%d,\"mesh_probe_ms\":%.3f,\"collision_probe_ms\":%.3f,\"blast_ms\":%.3f,\"blast_max_ms\":%.3f,\"tick_ms\":%.3f,\"frame_max_ms\":%.3f,\"frame_p95_ms\":%.3f,\"origins\":[%s]}"),
            Repeat,Shots,Before,Before-S->RemainingPieceCount,CellHash,Triangles,Hulls,Updates,MeshProbe,CollisionProbe,
            BlastTotal,BlastMax,TickTotal,Frames.Last(),Frames[FMath::FloorToInt(Frames.Num()*.95)],*Origins);
        Runs+=(Repeat?TEXT(","):TEXT(""))+Row; AddInfo(Row);
        TestEqual(TEXT("All twelve airbursts completed"),Shots,12);
        TestTrue(TEXT("Volley damages a substantial part of the sculpture"),Before-S->RemainingPieceCount>100);
        TestTrue(TEXT("Collision was updated for actual damage"),Updates>0);
        S->Destroy();
    }
    FString Tag=TEXT("Current");FParse::Value(FCommandLine::Get(),TEXT("SculptureBenchmarkTag="),Tag);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("SculptureBombPerformance");
    IFileManager::Get().MakeDirectory(*Dir,true);
    TestTrue(TEXT("Benchmark report saved"),FFileHelper::SaveStringToFile(TEXT("{\"runs\":[")+Runs+TEXT("]}"),*(Dir/(Tag+TEXT(".json")))));
    return true;
}
#endif
