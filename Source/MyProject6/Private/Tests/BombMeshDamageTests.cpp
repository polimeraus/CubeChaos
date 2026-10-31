#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BombProjectile.h"
#include "DynamicMeshActor.h"
#include "SimpleHouseActor.h"
#include "ParametricFantasyHouse.h"
#include "ParametricLobbyPlatform.h"
#include "RoundedBoxActor.h"
#include "Components/BoxComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/SphereComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GeometryScript/MeshPrimitiveFunctions.h"
#include "Misc/AutomationTest.h"
#include "UDynamicMesh.h"

namespace BombMeshTest
{
struct FWorld
{
    UWorld* World;
    FWorld()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
        World->InitializeActorsForPlay(FURL()); World->BeginPlay();
    }
    ~FWorld() { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); }
    void Advance(float Seconds)
    {
        for (int32 I=0,N=FMath::CeilToInt(Seconds*120);I<N;++I) { ++GFrameCounter;World->Tick(LEVELTICK_All,Seconds/N); }
    }
    ABombProjectile* Bomb(FVector Position, float Radius=85.f)
    {
        const FTransform T(Position);
        auto* B=World->SpawnActorDeferred<ABombProjectile>(ABombProjectile::StaticClass(),T);
        B->LaunchVelocity=FVector::ZeroVector;B->BlastRadius=Radius;B->ImpulseSpeed=B->ImpulseLift=0;
        B->FinishSpawning(T);B->Movement->Deactivate();return B;
    }
    bool Trace(FVector Start,FVector End,FHitResult& Hit)
    { return World->LineTraceSingleByChannel(Hit,Start,End,ECC_GameTraceChannel1); }
    void Wall(FVector Position,FVector Extent)
    {
        auto* A=World->SpawnActor<AActor>();auto* C=NewObject<UBoxComponent>(A);A->SetRootComponent(C);
        C->SetBoxExtent(Extent);C->SetCollisionProfileName(TEXT("BlockAll"));C->RegisterComponent();A->SetActorLocation(Position);
    }
    ADynamicMeshActor* MeshBox(FTransform Transform,FVector Size=FVector(400,30,300))
    {
        auto* A=World->SpawnActor<ADynamicMeshActor>(ADynamicMeshActor::StaticClass(),Transform);
        auto* C=A->GetDynamicMeshComponent();
        UGeometryScriptLibrary_MeshPrimitiveFunctions::AppendBox(C->GetDynamicMesh(),
            FGeometryScriptPrimitiveOptions(),FTransform::Identity,Size.X,Size.Y,Size.Z,0,0,0,
            EGeometryScriptPrimitiveOriginMode::Center);
        C->SetCollisionProfileName(TEXT("BlockAll"));C->SetComplexAsSimpleCollisionEnabled(true,true);
        return A;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombWindowClearanceTest,"MyProject6.Weapons.BombWindowClearance",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBombWindowClearanceTest::RunTest(const FString&)
{
    BombMeshTest::FWorld T;
    // A real 18 x 18 cm window in a blocking frame. The previous 23 cm body cannot fit.
    const FVector Centre(300,0,1000);
    T.Wall(Centre+FVector(0,0,59),FVector(10,200,50));
    T.Wall(Centre-FVector(0,0,59),FVector(10,200,50));
    T.Wall(Centre+FVector(0,59,0),FVector(10,50,9));
    T.Wall(Centre-FVector(0,59,0),FVector(10,50,9));
    FHitResult Hit;
    TestTrue(TEXT("Regression window blocks the old oversized body"),T.World->SweepSingleByChannel(Hit,
        FVector(0,0,1000),FVector(600,0,1000),FQuat::Identity,ECC_GameTraceChannel1,FCollisionShape::MakeSphere(11.5f)));
    TArray<ABombProjectile*> Throws;
    for (int32 I=0;I<3;++I)
    {
        auto* B=T.World->SpawnActor<ABombProjectile>(FVector(0,0,1000),FRotator::ZeroRotator);
        B->Movement->ProjectileGravityScale=0;B->Movement->Velocity=FVector(1250,0,0);Throws.Add(B);T.Advance(.06f);
    }
    T.Advance(.4f);
    for (auto* B:Throws)
    {
        TestTrue(TEXT("Repeated thrown bombs pass through the narrow window"),B->GetActorLocation().X>450);
        TestTrue(TEXT("Window frame does not reverse the bomb"),B->Movement->Velocity.X>0);
        TestFalse(TEXT("Passing the window preserves the live fuse"),B->bExploded);
    }
    T.Wall(FVector(1100,0,1000),FVector(10,300,300));T.Advance(.55f);
    for (auto* B:Throws)TestTrue(TEXT("Smaller body still bounces off solid cover"),B->Movement->Velocity.X<0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombMeshDamageTest,"MyProject6.Weapons.BombDynamicMeshDamage",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBombMeshDamageTest::RunTest(const FString&)
{
    BombMeshTest::FWorld T;FHitResult Hit;
    // The real modular house: an airborne blast cuts only the intersecting facade.
    auto* House=T.World->SpawnActorDeferred<ASimpleHouseActor>(ASimpleHouseActor::StaticClass(),FTransform(FVector(0,3000,0)));
    House->Building.BaseWidth=600;House->Building.BaseDepth=600;House->Building.FloorCount=1;
    House->Building.FloorHeight=400;House->Building.WallThickness=20;
    for (auto* Face:{&House->FrontFacade,&House->BackFacade,&House->SideWall1Facade,&House->SideWall2Facade})Face->bEnableOpenings=false;
    House->FinishSpawning(FTransform(FVector(0,3000,0)));
    const FVector Face=House->FrontWallComponent->Bounds.Origin;
    const FVector Outward=(Face-FVector(House->GetActorLocation().X,House->GetActorLocation().Y,Face.Z)).GetSafeNormal();
    const FVector Tangent=FVector::CrossProduct(Outward,FVector::UpVector);
    TestTrue(TEXT("House facade initially blocks projectiles"),T.Trace(Face+Outward*150,Face-Outward*100,Hit));
    T.Bomb(Face+Outward*35)->Explode();
    TestFalse(TEXT("Airburst opens a hole through the dynamic house wall"),T.Trace(Face+Outward*150,Face-Outward*100,Hit));
    TestTrue(TEXT("Facade outside the blast sphere remains solid"),T.Trace(Face+Tangent*220+Outward*150,Face+Tangent*220-Outward*100,Hit));
    House->bEnableDamage=false;const int32 ProtectedHouseTriangles=House->FrontWallComponent->GetMesh()->TriangleCount();
    T.Bomb(Face+Tangent*220+Outward*35)->Explode();
    TestEqual(TEXT("House damage switch is respected"),House->FrontWallComponent->GetMesh()->TriangleCount(),ProtectedHouseTriangles);

    // A generic dynamic mesh with rotation and non-uniform scale, not a cell actor.
    const FTransform Transform(FRotator(15,30,0),FVector(4000,3000,1000),FVector(1.4,.7,1.2));
    auto* Mesh=T.MeshBox(Transform);const FVector Surface=Transform.TransformPosition(FVector(0,-15,0));
    const FVector Normal=Transform.TransformVectorNoScale(FVector(0,-1,0));
    TestTrue(TEXT("Transformed dynamic wall initially blocks"),T.Trace(Surface+Normal*120,Surface-Normal*100,Hit));
    T.Bomb(Surface+Normal*35)->Explode();
    TestFalse(TEXT("World-space blast removes transformed mesh collision immediately"),T.Trace(Surface+Normal*120,Surface-Normal*100,Hit));
    TestTrue(TEXT("Cut dynamic mesh remains valid"),Mesh->GetDynamicMeshComponent()->GetMesh()->CheckValidity());

    auto* QueryDisabled=T.MeshBox(FTransform(FVector(8000,3000,1000)));
    QueryDisabled->SetActorEnableCollision(false);
    const int32 Before=QueryDisabled->GetDynamicMeshComponent()->GetMesh()->TriangleCount();
    T.Bomb(FVector(8000,2950,1000))->Explode();
    TestNotEqual(TEXT("Complex/query-disabled meshes do not depend on overlap hits"),QueryDisabled->GetDynamicMeshComponent()->GetMesh()->TriangleCount(),Before);
    TestFalse(TEXT("Blast does not enable disabled actor collision"),QueryDisabled->GetActorEnableCollision());

    auto* Protected=T.MeshBox(FTransform(FVector(10000,3000,1000)));Protected->SetCanBeDamaged(false);
    const int32 ProtectedTriangles=Protected->GetDynamicMeshComponent()->GetMesh()->TriangleCount();
    T.Bomb(FVector(10000,2950,1000))->Explode();
    TestEqual(TEXT("CanBeDamaged false protects mesh geometry"),Protected->GetDynamicMeshComponent()->GetMesh()->TriangleCount(),ProtectedTriangles);
    auto* Distant=T.MeshBox(FTransform(FVector(12000,3000,1000)));
    const int32 FarTriangles=Distant->GetDynamicMeshComponent()->GetMesh()->TriangleCount();
    T.Bomb(FVector(12000,2800,1000))->Explode();
    TestEqual(TEXT("Meshes outside the sphere remain unchanged"),Distant->GetDynamicMeshComponent()->GetMesh()->TriangleCount(),FarTriangles);
    auto* ZeroDamage=T.Bomb(FVector(12000,2950,1000));ZeroDamage->BlastDamage=0;ZeroDamage->Explode();
    TestEqual(TEXT("Zero-damage blasts do not carve"),Distant->GetDynamicMeshComponent()->GetMesh()->TriangleCount(),FarTriangles);

    auto* Small=T.World->SpawnActor<ARoundedBoxActor>(FVector(14000,3000,0),FRotator::ZeroRotator);
    Small->Width=Small->Depth=Small->Height=40;Small->CornerRadius=2;Small->RebuildMesh();
    T.Bomb(FVector(14000,3000,20),100)->Explode();
    TestEqual(TEXT("A fully enclosed rounded box can be removed completely"),Small->GetDynamicMeshComponent()->GetMesh()->TriangleCount(),0);

    auto* Lobby=T.World->SpawnActor<AParametricLobbyPlatform>(FVector(20000,3000,1000),FRotator::ZeroRotator);
    auto* LobbyBomb=T.Bomb(FVector(20000,3000,1030),200);LobbyBomb->Explode();
    TestEqual(TEXT("Lobby destruction is dispatched once per explosion"),Lobby->DamageCutCount,1);
    LobbyBomb->Explode();TestEqual(TEXT("Repeated Explode does not damage twice"),Lobby->DamageCutCount,1);
    Lobby->RepairPlatform();Lobby->bDestructible=false;T.Bomb(FVector(20000,3000,1030),200)->Explode();
    TestEqual(TEXT("Lobby destruction switch remains respected"),Lobby->DamageCutCount,0);

    auto* Fantasy=T.World->SpawnActor<AParametricFantasyHouse>(FVector(30000,3000,0),FRotator::ZeroRotator);
    const FVector RoofProbe=Fantasy->GetActorLocation();
    if (TestTrue(TEXT("Fantasy house roof has real collision"),T.Trace(RoofProbe+FVector(0,0,5000),RoofProbe-FVector(0,0,100),Hit)))
    {
        const int32 RoofTriangles=Fantasy->HouseMesh->GetMesh()->TriangleCount();
        T.Bomb(Hit.ImpactPoint+Hit.ImpactNormal*20,120)->Explode();
        TestNotEqual(TEXT("Fantasy house dynamic geometry receives bomb damage"),Fantasy->HouseMesh->GetMesh()->TriangleCount(),RoofTriangles);
    }
    return true;
}
#endif
