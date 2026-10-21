#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BombProjectile.h"
#include "BFGProjectile.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "MyProject6Character.h"
#include "Components/BoxComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombPressureTest,"MyProject6.Weapons.BombPressure",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FBombPressureTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto Advance=[&](float Seconds)
    {
        for(int32 I=0,N=FMath::CeilToInt(Seconds*60);I<N;++I){++GFrameCounter;World->Tick(LEVELTICK_All,Seconds/N);}
    };
    const FVector Origin(0,0,1000);
    auto Character=[&](UClass* Class,FVector Offset)
    {
        const FTransform T(Origin+Offset);
        auto* C=World->SpawnActorDeferred<ACharacter>(Class,T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!TestNotNull(TEXT("Pressure test character spawns"),C)) return C;
        if(auto* P=FindFProperty<FBoolProperty>(Class,TEXT("bSpawnReplacementsOnDeath")))P->SetPropertyValue_InContainer(C,false);
        C->FinishSpawning(T);C->SetActorTickEnabled(false);
        auto* Move=C->GetCharacterMovement();Move->bRunPhysicsWithNoController=true;
        Move->GravityScale=0;Move->SetMovementMode(MOVE_Falling);return C;
    };
    auto Box=[&](FVector Offset,FVector Extent,bool Physics)
    {
        auto* A=World->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);
        B->SetBoxExtent(Extent);B->SetCollisionProfileName(Physics?TEXT("PhysicsActor"):TEXT("BlockAll"));
        B->RegisterComponent();A->SetActorLocation(Origin+Offset);B->SetEnableGravity(false);B->SetSimulatePhysics(Physics);return B;
    };
    auto Bomb=[&](FVector Location,AActor* Owner,float Damage)
    {
        const FTransform T(Location);
        auto* B=World->SpawnActorDeferred<ABombProjectile>(ABombProjectile::StaticClass(),T,Owner);
        B->BlastDamage=Damage;B->LaunchVelocity=FVector::ZeroVector;B->FinishSpawning(T);B->Movement->Deactivate();return B;
    };
    auto* PlayerClass=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if (!TestNotNull(TEXT("Saved player Blueprint"),PlayerClass)) return false;
    auto* Player=Character(PlayerClass,FVector(180,0,0));
    auto* Near=Character(AChasingNPC::StaticClass(),FVector(-180,0,0));
    auto* Far=Character(AWhisperFollower::StaticClass(),FVector(0,450,0));
    auto* Covered=Character(AChasingNPC::StaticClass(),FVector(0,-300,0));
    auto* Outside=Character(AChasingNPC::StaticClass(),FVector(750,0,0));
    if (!Player || !Near || !Far || !Covered || !Outside) return false;
    Box(FVector(0,-150,0),FVector(90,8,150),false);
    auto* Prop=Box(FVector(180,220,0),FVector(20),true);
    auto* Shot=Bomb(Origin,Player,0);
    const float EnergyBefore=CastChecked<AMyProject6Character>(Player)->GetCurrentEnergy();
    Shot->Explode();
    const float EnergyAfter=CastChecked<AMyProject6Character>(Player)->GetCurrentEnergy();
    TestTrue(TEXT("Thrower loses a small amount of actual HUD energy"),EnergyBefore-EnergyAfter>5.f && EnergyBefore-EnergyAfter<=25.f);
    TestTrue(TEXT("Pressure tuning is at least five times the original"),Shot->ImpulseSpeed>=3250.f && Shot->ImpulseLift>=1100.f);
    const FVector OwnerKick=Player->GetCharacterMovement()->PendingLaunchVelocity;
    const FVector NearKick=Near->GetCharacterMovement()->PendingLaunchVelocity;
    const FVector FarKick=Far->GetCharacterMovement()->PendingLaunchVelocity;
    TestTrue(TEXT("Thrower is pushed outwards and upwards"),OwnerKick.X>2200 && OwnerKick.Z>750);
    TestTrue(TEXT("NPC on opposite side flies the opposite way"),NearKick.X< -2200 && NearKick.Z>750);
    TestTrue(TEXT("Pressure weakens with distance and reaches follower NPCs"),FarKick.Y>750 && FarKick.Size()<NearKick.Size());
    TestTrue(TEXT("Solid cover blocks the impulse"),Covered->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
    TestTrue(TEXT("Outside pressure radius receives no impulse"),Outside->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero());
    Shot->Explode();
    TestTrue(TEXT("Detonation applies pressure once despite duplicate overlaps/calls"),Player->GetCharacterMovement()->PendingLaunchVelocity.Equals(OwnerKick,.01));
    TestEqual(TEXT("Repeated detonation cannot drain player energy twice"),CastChecked<AMyProject6Character>(Player)->GetCurrentEnergy(),EnergyAfter);
    Advance(.15f);
    TestTrue(TEXT("Thrower actually moves away"),Player->GetActorLocation().X>450);
    TestTrue(TEXT("NPC actually moves away"),Near->GetActorLocation().X< -450);
    TestTrue(TEXT("Physics prop actually moves in the radial direction"),Prop->GetComponentLocation().X>190 && Prop->GetComponentLocation().Y>230);
    TestTrue(TEXT("Covered target remains in place"),Covered->GetActorLocation().Equals(Origin+FVector(0,-300,0),1));

    auto* Dying=Character(AChasingNPC::StaticClass(),FVector(4180,0,0));
    if (!Dying) return false;
    auto* Lethal=Bomb(Origin+FVector(4000,0,0),Player,10000);
    Lethal->Explode();
    TestTrue(TEXT("Lethal bomb enters normal NPC death"),CastChecked<AChasingNPC>(Dying)->IsDead());
    TestTrue(TEXT("Pressure is retained after lethal hit callbacks"),Dying->GetCharacterMovement()->PendingLaunchVelocity.X>600);
    Advance(.15f);
    TestTrue(TEXT("Killed NPC continues its outward flight"),Dying->GetActorLocation().X>4250);
    TWeakObjectPtr<ABombProjectile> Cleanup=Shot;
    Advance(1.5f);TestFalse(TEXT("Neon blast cleans up after its final light fragments"),Cleanup.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBFGVisualBudgetTest,"MyProject6.Weapons.BFGVisualBudget",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FBFGVisualBudgetTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Shot=World->SpawnActor<ABFGProjectile>(FVector(0,0,10000),FRotator::ZeroRotator);
    for(int32 I=0;I<120;++I){++GFrameCounter;World->Tick(LEVELTICK_All,1.f/120.f);}
    TestTrue(TEXT("Movement stays full rate while visual updates are bounded"),Shot->GetActorLocation().X>7900 && Shot->GetVisualUpdateCount()<=32);
    TestEqual(TEXT("Flight topology is allocated only once"),Shot->GetVisualTopologyBuildCount(),1);
    Shot->Explode(FHitResult());
    for(int32 I=0;I<36;++I){++GFrameCounter;World->Tick(LEVELTICK_All,1.f/120.f);}
    TestEqual(TEXT("Impact creates one new topology then reuses its buffers"),Shot->GetVisualTopologyBuildCount(),2);
    TestTrue(TEXT("Impact animation also stays bounded"),Shot->GetVisualUpdateCount()<=43);
    return true;
}
#endif
