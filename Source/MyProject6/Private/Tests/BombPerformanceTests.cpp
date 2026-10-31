#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BombProjectile.h"
#include "ChunkedDestructibleHouse.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/FileManager.h"
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombSphericalVisualTest,"MyProject6.Weapons.BombSphericalVisual",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBombSphericalVisualTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Bomb=World->SpawnActor<ABombProjectile>();
    Bomb->SetActorRotation(FRotator(21,45,13));
    const FVector Views[]={FVector(-1000,-1000,400),FVector(0,0,1200),FVector(1000,0,0)};
    for (FVector View:Views)
    {
        double PreviousRadius=0.;float PreviousOpacity=1.f;
        for (float Age:{.05f,.25f,.6f})
        {
            Bomb->PreviewEffect(Age,true,View);
            const auto& Mesh=Bomb->Effect->GetDynamicMesh()->GetMeshRef();
            TestEqual(TEXT("Soft orb has only two triangles, with no wire grid"),Mesh.TriangleCount(),2);
            const FVector ViewNormal=View.GetSafeNormal();
            for (int32 ID:Mesh.VertexIndicesItr())
            {
                const FVector V=Bomb->GetActorTransform().TransformPosition(FVector(Mesh.GetVertex(ID)));
                TestFalse(TEXT("All vertices stay finite, including overhead view"),V.ContainsNaN());
                TestTrue(TEXT("Orb faces the viewer from every angle"),FMath::Abs(FVector::DotProduct(V,ViewNormal))<.001);
            }
            const FVector EdgeX(Mesh.GetVertex(1)-Mesh.GetVertex(0)),EdgeY(Mesh.GetVertex(2)-Mesh.GetVertex(0));
            TestTrue(TEXT("Round mask remains circular, never stretched"),FMath::IsNearlyEqual(EdgeX.Size(),EdgeY.Size(),.001)
                && FMath::Abs(FVector::DotProduct(EdgeX.GetSafeNormal(),EdgeY.GetSafeNormal()))<.001);
            const double Radius=EdgeX.Size()*.5;
            TestTrue(TEXT("Glow expands continuously"),Radius>PreviousRadius);PreviousRadius=Radius;
            const float Opacity=Mesh.Attributes()->PrimaryColors()->GetElement(0).W;
            TestTrue(TEXT("Opacity decreases throughout the glow"),Opacity<PreviousOpacity);PreviousOpacity=Opacity;
        }
    }
    Bomb->PreviewEffect(1.4f,true,Views[0]);
    TestEqual(TEXT("Glow reaches zero opacity before actor cleanup"),
        Bomb->Effect->GetDynamicMesh()->GetMeshRef().Attributes()->PrimaryColors()->GetElement(0).W,0.f);
    TestEqual(TEXT("Topology reused for every age and viewing angle"),Bomb->GetVisualTopologyBuildCount(),1);
    Bomb->Explode();
    const int32 Before=Bomb->GetVisualUpdateCount();
    for(int32 Frame=0;Frame<120;++Frame)Bomb->Tick(1.f/120.f);
    const int32 Updates=Bomb->GetVisualUpdateCount()-Before;
    TestTrue(TEXT("120 FPS does not multiply visual geometry updates"),Updates>=29 && Updates<=31);
    TestEqual(TEXT("Tick preserves topology"),Bomb->GetVisualTopologyBuildCount(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombBuildingBenchmark,"MyProject6.Performance.BombBuildingVolley",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBombBuildingBenchmark::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    FMath::RandInit(83017);
    auto* House=World->SpawnActor<AChunkedDestructibleHouse>();
    House->ConfigureHouse(FIntVector(48,40,36),8,1,FVector(25),FVector::ZeroVector,true);
    TInlineComponentArray<UDynamicMeshComponent*> Chunks;
    House->GetComponents(Chunks);
    TArray<TObjectPtr<UDynamicMesh>> Sources;
    for (auto* C:Chunks)Sources.Add(C->GetDynamicMesh());
    int32 ChangedChunkFrames=0,Shots=0;double BlastTotal=0,BlastMax=0,TickTotal=0;
    TArray<double> Frames;
    for (int32 Frame=0;Frame<300;++Frame)
    {
        const double Start=FPlatformTime::Seconds();
        if (Frame<216 && Frame%18==0)
        {
            const int32 Side=Shots%4;const float Height=Shots<4?780.f:Shots<8?480.f:180.f;
            const FVector Position=Side==0?FVector(-200,-580,Height):Side==1?FVector(680,-100,Height)
                :Side==2?FVector(200,580,Height):FVector(-680,100,Height);
            auto* Bomb=World->SpawnActor<ABombProjectile>(Position,FRotator::ZeroRotator);
            const double BlastStart=FPlatformTime::Seconds();Bomb->Explode();
            const double Ms=(FPlatformTime::Seconds()-BlastStart)*1000;BlastTotal+=Ms;BlastMax=FMath::Max(BlastMax,Ms);++Shots;
        }
        const double TickStart=FPlatformTime::Seconds();
        ++GFrameCounter;World->Tick(LEVELTICK_All,1.f/60.f);
        TickTotal+=(FPlatformTime::Seconds()-TickStart)*1000;
        Frames.Add((FPlatformTime::Seconds()-Start)*1000);
        for (int32 I=0;I<Sources.Num();++I)
            if (Sources[I]!=Chunks[I]->GetDynamicMesh())
            { ++ChangedChunkFrames;Sources[I]=Chunks[I]->GetDynamicMesh(); }
    }
    int32 Destroyed=0;const auto Metrics=House->GetHouseMetrics();
    for(int32 Z=0;Z<Metrics.BackingGridBlockCount.Z;++Z)
        for(int32 XY=0;XY<Metrics.BackingGridBlockCount.X*Metrics.BackingGridBlockCount.Y;++XY)
        { FChunkedFloorCellInfo Info;if(House->GetCellInfo(FIntPoint(XY,Z),Info) && Info.Health<=0)++Destroyed; }
    Frames.Sort();const double Max=Frames.Last(),P95=Frames[FMath::FloorToInt(Frames.Num()*.95)];
    FString Tag=TEXT("Current");FParse::Value(FCommandLine::Get(),TEXT("BombBenchmarkTag="),Tag);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("BombPerformance");IFileManager::Get().MakeDirectory(*Dir,true);
    const FString Report=FString::Printf(TEXT("{\"shots\":%d,\"frames\":%d,\"cell_capacity\":%d,\"chunks\":%d,\"changed_chunk_frames\":%d,\"blast_total_ms\":%.3f,\"blast_max_ms\":%.3f,\"world_tick_total_ms\":%.3f,\"frame_max_ms\":%.3f,\"frame_p95_ms\":%.3f,\"destroyed_or_empty_cells\":%d}"),
        Shots,Frames.Num(),Metrics.LogicalCellCapacity,Chunks.Num(),ChangedChunkFrames,BlastTotal,BlastMax,TickTotal,Max,P95,Destroyed);
    FFileHelper::SaveStringToFile(Report,*(Dir/(TEXT("Benchmark_")+Tag+TEXT(".json"))));
    AddInfo(Report);TestEqual(TEXT("Volley completed"),Shots,12);TestTrue(TEXT("Stress scenario actually rebuilds damaged chunks"),ChangedChunkFrames>10);
    return true;
}
#endif
