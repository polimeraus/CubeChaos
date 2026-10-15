#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "SimpleHouseActor.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPaintOpeningTest,"MyProject6.Structures.PaintSplatter.OpeningsAndFacadeBounds",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPaintOpeningTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Editor,false);
    GEngine->CreateNewWorldContext(EWorldType::Editor).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* House=World->SpawnActor<ASimpleHouseActor>();
    House->Building.BaseWidth=540;House->Building.BaseDepth=540;
    House->Building.FloorCount=1;House->Building.FloorHeight=480;
    House->Building.WallThickness=20;
    auto Configure=[&](ESimpleHouseWindowType Shape,int32 Count,bool Door)
    {
        for(FSimpleHouseFacadeSettings* Face:{&House->FrontFacade,&House->BackFacade,&House->SideWall1Facade,&House->SideWall2Facade})
        {
            Face->bEnableOpenings=true;Face->Door.bHasDoor=Door;
            Face->FloorWindows.SetNum(1);auto& W=Face->FloorWindows[0];
            W.bHasWindows=true;W.WindowCount=Count;W.WindowWidth=120;W.WindowHeight=140;
            W.WindowSpacing=25;W.WindowType=Shape;W.RoundedDegree=35;
            W.WindowBorderStyle=ESimpleHouseWindowBorderStyle::PaintSplatter;
            W.PaintSpread=95;W.PaintSplatterAmount=100;W.PaintSplatterSeed=1337;
        }
        House->RebuildMesh();
    };
    auto CheckHoles=[&](ESimpleHouseWindowType Shape)
    {
        int32 Probes=0,Blocked=0,OutsideWall=0;
        for(UDynamicMeshComponent* Paint:House->WindowObjectComponents)
        {
            TestTrue(TEXT("Generated paint keeps its collision setting"),Paint->GetCollisionEnabled()==ECollisionEnabled::QueryAndPhysics);
            const FDynamicMesh3& Mesh=Paint->GetDynamicMesh()->GetMeshRef();
            TestTrue(TEXT("Paint geometry is non-empty"),Mesh.TriangleCount()>0);
            const FTransform Transform=Paint->GetComponentTransform();
            for(const FVector3d& Vertex:Mesh.VerticesItr())
            {
                const FVector Local=House->GetActorTransform().InverseTransformPosition(Transform.TransformPosition(FVector(Vertex)));
                if(FMath::Abs(Local.X)>270.5||FMath::Abs(Local.Y)>270.5||Local.Z<-.01||Local.Z>480.01)++OutsideWall;
            }
        }
        // Probe the final triangle meshes, including paint emitted by neighbouring
        // windows, rather than just checking the generated silhouette profiles.
        TArray<UDynamicMeshComponent*> Openings;
        for(UDynamicMeshComponent* C:House->WindowObjectComponents)Openings.Add(C);
        for(UDynamicMeshComponent* C:House->DoorObjectComponents)Openings.Add(C);
        for(UDynamicMeshComponent* Paint:House->WindowObjectComponents)
        {
            FDynamicMeshAABBTree3 Tree(&Paint->GetDynamicMesh()->GetMeshRef());
            for(auto* Opening:Openings)
            {
                const bool Door=House->DoorObjectComponents.Contains(Opening);
                const float Height=Door?220.f:Shape==ESimpleHouseWindowType::Circular?120.f:140.f;
                for(int32 IX=-5;IX<=5;++IX)for(int32 IZ=-5;IZ<=5;++IZ)
                {
                    const float X=IX*10.f,Z=IZ*(Height*.5f-10.f)/5.f;
                    if(!Door && Shape==ESimpleHouseWindowType::Circular && X*X+Z*Z>50.f*50.f)continue;
                    if(!Door && Shape==ESimpleHouseWindowType::Arched && Z>10 && FMath::Square(X/55.f)+FMath::Square((Z-10)/55.f)>1)continue;
                    // Points near rounded corners stay comfortably inside the cut.
                    if(!Door && Shape==ESimpleHouseWindowType::RoundedCorners && FMath::Abs(X)>45 && FMath::Abs(Z)>50)continue;
                    const FVector Start=Opening->GetComponentTransform().TransformPosition(FVector(X,-50,Z));
                    const FVector Dir=Opening->GetComponentTransform().TransformVectorNoScale(FVector(0,1,0));
                    const FVector LocalStart=Paint->GetComponentTransform().InverseTransformPosition(Start);
                    const FVector LocalDir=Paint->GetComponentTransform().InverseTransformVectorNoScale(Dir);
                    double Distance;int32 Triangle;
                    if(Tree.FindNearestHitTriangle(FRay3d(FVector3d(LocalStart),FVector3d(LocalDir)),Distance,Triangle)&&Distance<100)++Blocked;
                    ++Probes;
                }
            }
        }
        TestTrue(TEXT("Many positions inside each window/door were checked"),Probes>400);
        TestEqual(TEXT("No splatter triangles cover any window or door opening"),Blocked,0);
        TestEqual(TEXT("All splashes and drips remain on the facade"),OutsideWall,0);
    };
    for(ESimpleHouseWindowType Shape:{ESimpleHouseWindowType::Rectangular,ESimpleHouseWindowType::Arched,
        ESimpleHouseWindowType::RoundedCorners,ESimpleHouseWindowType::Circular})
    {
        Configure(Shape,1,false);CheckHoles(Shape);
    }
    Configure(ESimpleHouseWindowType::Rectangular,2,true);
    CheckHoles(ESimpleHouseWindowType::Rectangular);
    return true;
}
#endif
