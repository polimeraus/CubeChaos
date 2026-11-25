#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricSpaceAgeHouse.h"
#include "BallProjectile.h"
#include "Chaos/Convex.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/BodySetup.h"
#include "TimerManager.h"
#include "UDynamicMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpaceAgeHouseTest,"MyProject6.Structures.SpaceAgeHomes.GeometryAndDestruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpaceAgeHouseTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Home=World->SpawnActor<AParametricSpaceAgeHouse>();
    if (!TestNotNull(TEXT("House spawns"),Home)) return false;
    Home->FlashDuration=0; Home->bUseDestructionRipple=false; Home->bPlayDestroyTickSound=false;
    Home->DispatchBeginPlay();
    auto Triangles=[&]()
    {
        int32 N=0;
        for (const auto& C:Home->ChunkComponents) N+=C->GetDynamicMesh()->GetTriangleCount();
        return N;
    };
    auto ClearDoor=[&]()
    {
        const double Z=Home->DeckHeight+99;
        return !World->SweepTestByChannel({0,-Home->Footprint.Y*.5-Home->PorchDepth-180,Z},{0,-95,Z},
            FQuat::Identity,ECC_Visibility,FCollisionShape::MakeCapsule(34,86));
    };
    const int32 Count=Home->GeneratedFragments, Tris=Triangles();
    TestTrue(TEXT("The full house builds within budget"),Count>700 && Home->GenerationWarning.IsEmpty() && Tris>Count*6);
    TestFalse(TEXT("No idle actor tick"),Home->IsActorTickEnabled());
    TestTrue(TEXT("Capsule passes from porch to living room"),ClearDoor());
    FIntPoint Cell;
    TestFalse(TEXT("Living room has hollow collision"),Home->WorldLocationToCell({0,0,Home->DeckHeight+110},Cell));
    TestTrue(TEXT("Living room floor supports a player"),World->LineTraceTestByChannel({50,15,Home->DeckHeight+32},{50,15,Home->DeckHeight-32},ECC_Visibility));
    int32 Hulls=0;
    for (const auto& C:Home->ChunkComponents)
        for (const auto& Hull:C->GetBodySetup()->AggGeom.ConvexElems)
        {
            if (!TestTrue(TEXT("Every breakable solid has valid cooked collision"),
                Hull.GetChaosConvexMesh() && Hull.GetChaosConvexMesh()->IsValidGeometry())) return false;
            ++Hulls;
        }
    TestEqual(TEXT("Every visible piece has a hull"),Hulls,Count);
    const auto* Pane=Home->Panels.FindByPredicate([&](const auto& P)
        {return P.Slot==3 && P.Center.Z>Home->DeckHeight+70 && P.Center.Z<Home->DeckHeight+Home->WallHeight-40;});
    if (!TestNotNull(TEXT("Facade contains a glazed module"),Pane)) return false;
    const FVector Center=Pane->Center, Normal=Center.GetSafeNormal2D();
    const FVector Start=Center+Normal*45, End=Center-Normal*21;
    FHitResult Hit;
    if (!TestTrue(TEXT("Weapon trace finds the pane"),World->LineTraceSingleByChannel(Hit,Start,End,ECC_Visibility))) return false;
    if (!TestTrue(TEXT("Impact maps to a visible panel"),Home->ResolveExistingCellFromHit(Hit,Cell))) return false;
    UGameplayStatics::ApplyPointDamage(Home,5,-Normal,Hit,nullptr,nullptr,nullptr);
    FChunkedFloorCellInfo Info;
    Home->GetCellInfo(Cell,Info);
    TestEqual(TEXT("Point damage consumes only panel health"),Info.Health,20.f);
    auto* Projectile=World->SpawnActor<ABallProjectile>(FVector(4000,0,4000),FRotator::ZeroRotator);
    static_cast<AActor*>(Projectile)->NotifyHit(nullptr,Home,Hit.GetComponent(),false,Hit.ImpactPoint,Hit.ImpactNormal,FVector::ZeroVector,Hit);
    TestFalse(TEXT("The standard projectile opens a hole in the collision"),World->LineTraceTestByChannel(Start,End,ECC_Visibility));
    TestTrue(TEXT("Destroyed pane is removed from visible geometry"),Triangles()<Tris);
    if (!TestEqual(TEXT("The pane releases one physical fragment"),Home->LoosePanels.Num(),1)) return false;
    auto* Loose=Home->LoosePanels[0]->FindComponentByClass<UDynamicMeshComponent>();
    TestTrue(TEXT("Loose glass tumbles with its own geometry and material"),
        Loose && Loose->IsSimulatingPhysics() && Loose->GetDynamicMesh()->GetTriangleCount()>0 &&
        Loose->GetMaterial(0)==Home->FloorMaterials[3] && Loose->GetCollisionEnabled()==ECollisionEnabled::PhysicsOnly);
    Home->RestoreCell(Cell);
    TestEqual(TEXT("Restoring the pane restores the original mesh"),Triangles(),Tris);
    const FTransform Moved(FRotator(0,31,0),FVector(271,-440,115),FVector(1.23,.76,1.14));
    Home->SetActorTransform(Moved);
    TestTrue(TEXT("Hit lookup respects transformed instances"),Home->WorldLocationToCell(Moved.TransformPosition(Center),Cell));
    const int32 Neighbours=Home->DestroyCellsInRadius(Moved.TransformPosition(Center),175,EChunkedFloorBlastShape::Sphere);
    TestTrue(TEXT("A local explosion removes a neighbourhood"),Neighbours>1 && Neighbours<Count/2 && Triangles()<Tris);
    Home->DestroyCellsInRadius(Moved.GetLocation(),10000,EChunkedFloorBlastShape::Sphere);
    TestEqual(TEXT("A fully demolished house has no triangles"),Triangles(),0);
    TestTrue(TEXT("Debris budget bounds full demolition"),Home->LoosePanels.Num()<=Home->LoosePanelLimit);
    TestTrue(TEXT("Empty house schedules removal"),Home->GetWorldTimerManager().TimerExists(Home->DemolitionTimer));
    for (const auto& C:Home->ChunkComponents)
        TestTrue(TEXT("Full demolition clears the collision"),!C->GetBodySetup() || C->GetBodySetup()->AggGeom.ConvexElems.IsEmpty());
    Home->SetActorTransform(FTransform::Identity);
    Home->RebuildHouse();
    TestEqual(TEXT("Rebuild recovers identical layout"),Triangles(),Tris);
    TestTrue(TEXT("Rebuilding clears debris and cancels removal"),Home->LoosePanels.IsEmpty() && !Home->GetWorldTimerManager().TimerExists(Home->DemolitionTimer));
    Home->RoofForm=ESpaceAgeRoof::Comet; Home->ScreenCourses=3; Home->bClerestory=false;
    Home->Footprint={1720,780}; Home->FacadeSeed=71; Home->EntranceWidth=123;
    Home->RebuildHouse();
    TestTrue(TEXT("Butterfly plan regenerates and remains passable"),Home->GenerationWarning.IsEmpty() && ClearDoor() && Triangles()!=Tris);
    const int32 ScreenCount=Home->GeneratedFragments;
    Home->ScreenCourses=0; Home->RebuildHouse();
    TestTrue(TEXT("Breeze screen has its own breakable solids"),Home->GeneratedFragments<ScreenCount);
    Home->Footprint={860,660}; Home->FacadeBays=24; Home->WallHeight=240; Home->EntranceWidth=310;
    Home->DeckHeight=34; Home->EaveDepth=35; Home->FragmentSpan=55; Home->PortholeSpacing=1;
    Home->RebuildHouse();
    TestTrue(TEXT("Smallest footprint with a wide door and dense facade stays traversable"),Home->GenerationWarning.IsEmpty() && ClearDoor());
    const int32 DenseTris=Triangles();
    AddExpectedMessage(TEXT("Fragment budget exceeded"),EAutomationExpectedMessageFlags::Contains,1);
    Home->MaxGeneratedCells=8; Home->RebuildHouse();
    TestEqual(TEXT("An over-budget edit keeps the last complete house"),Triangles(),DenseTris);
    AddInfo(FString::Printf(TEXT("Orbit: %d breakable panels, %d triangles; local blast hit %d panels."),Count,Tris,Neighbours));
    return true;
}
#endif
