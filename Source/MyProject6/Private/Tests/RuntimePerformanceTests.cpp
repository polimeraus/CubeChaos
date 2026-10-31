#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "RailgunBeam.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRailgunVisualBenchmark,"MyProject6.Performance.RailgunVisualUpdates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRailgunVisualBenchmark::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Beam=World->SpawnActor<ARailgunBeam>();
    FString Rows;
    for (bool Smooth:{false,true})
    {
        Beam->Visuals.bSpiral=true;Beam->Visuals.bSmoothSpiral=Smooth;
        Beam->InitializeBeam(FVector(100,200,300),FVector(20100,200,300),.4f);
        Beam->SetImpact(FVector(20100,200,300));
        Beam->FaceCamera(FVector(-300,-500,500));
        TArray<double> Trials;
        for (int32 Trial=0;Trial<3;++Trial)
        {
            const uint32 BuildsBefore=Beam->GetParticleTopologyBuildCount();
            const double Start=FPlatformTime::Seconds();
            for (int32 Frame=0;Frame<120;++Frame) Beam->SetPreviewAge(.1f+Frame/1200.f);
            Trials.Add((FPlatformTime::Seconds()-Start)*1000.);
            TestTrue(TEXT("120 visual updates reuse topology apart from the initial flash expiry"),
                Beam->GetParticleTopologyBuildCount()-BuildsBefore<=1);
        }
        Trials.Sort();
        // Capture exact presentation signatures across topology changes and late fade.
        for (float Age:{0.f,.12f,.3f,.8f,1.1f,4.8f})
        {
            Beam->SetPreviewAge(Age);
            const auto& Mesh=Beam->Spiral->GetDynamicMesh()->GetMeshRef();
            uint32 Hash=0;
            for (int32 ID:Mesh.VertexIndicesItr())
            {
                Hash=HashCombine(Hash,GetTypeHash(Mesh.GetVertex(ID)));
                Hash=HashCombine(Hash,GetTypeHash(Mesh.Attributes()->PrimaryColors()->GetElement(ID)));
                Hash=HashCombine(Hash,GetTypeHash(Mesh.Attributes()->PrimaryUV()->GetElement(ID)));
                Hash=HashCombine(Hash,GetTypeHash(Mesh.Attributes()->PrimaryNormals()->GetElement(ID)));
            }
            TestTrue(TEXT("Visual mesh and overlays remain valid"),Mesh.CheckValidity());
            if(!Rows.IsEmpty())Rows+=TEXT(",");
            Rows+=FString::Printf(TEXT("{\"smooth\":%s,\"age\":%.2f,\"vertices\":%d,\"triangles\":%d,\"signature\":%u,\"median_120_updates_ms\":%.3f}"),
                Smooth?TEXT("true"):TEXT("false"),Age,Mesh.VertexCount(),Mesh.TriangleCount(),Hash,Trials[1]);
        }
    }
    FString Tag=TEXT("Current");FParse::Value(FCommandLine::Get(),TEXT("RuntimeBenchmarkTag="),Tag);
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("RuntimePerformance");IFileManager::Get().MakeDirectory(*Dir,true);
    const FString Report=TEXT("[")+Rows+TEXT("]");
    TestTrue(TEXT("Benchmark report written"),FFileHelper::SaveStringToFile(Report,*(Dir/(TEXT("Railgun_")+Tag+TEXT(".json")))));
    AddInfo(Report);
    return true;
}
#endif
