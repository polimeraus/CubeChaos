#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BombProjectile.h"
#include "BFGProjectile.h"
#include "MyProject6Character.h"
#include "RailgunWeapon.h"
#include "ParametricPickup.h"
#include "ChasingNPC.h"
#include "NPCHitReactionComponent.h"
#include "ChunkedDestructibleFloor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFuseBombTest,"MyProject6.Weapons.FuseBomb",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFuseBombTest::RunTest(const FString&)
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
    auto Wall=[&](FVector Location,FVector Extent)
    {
        auto* A=World->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);
        B->SetBoxExtent(Extent);B->SetCollisionProfileName(TEXT("BlockAll"));B->RegisterComponent();A->SetActorLocation(Location);return A;
    };
    auto* PlayerClass=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    auto* PickupClass=LoadClass<AParametricPickup>(nullptr,TEXT("/Game/GameplayActors/Pickups/FuseBomb/BP_BombPickup.BP_BombPickup_C"));
    if(!TestNotNull(TEXT("Saved player class"),PlayerClass)||!TestNotNull(TEXT("Saved bomb pickup class"),PickupClass))return false;
    auto* Player=World->SpawnActor<AMyProject6Character>(PlayerClass,FVector(0,0,500),FRotator::ZeroRotator);
    Player->GetCharacterMovement()->DisableMovement();
    auto* Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();Controller->Possess(Player);
    // Possess/Restart restores walking, so freeze only after possession.
    Player->GetCharacterMovement()->DisableMovement();
    auto* Gun=Player->Railgun.Get();
    TestEqual(TEXT("Production fuse is three seconds"),Gun->BombFuseSeconds,3.f);
    TestFalse(TEXT("Locked bomb cannot fire"),Gun->FireBomb());
    auto* Pickup=World->SpawnActor<AParametricPickup>(PickupClass,FVector(1200,0,500),FRotator::ZeroRotator);
    TestEqual(TEXT("Blueprint saves bomb reward"),Pickup->WeaponReward,EPickupWeapon::Bomb);
    TestEqual(TEXT("Blueprint saves round fuse-bomb icon"),Pickup->Icon,EPickupMotif::Bomb);
    Player->SetActorLocation(Pickup->GetActorLocation()+FVector(0,0,Pickup->Size*.5f));Advance(.05f);
    TestTrue(TEXT("Real overlap collects saved pickup"),Pickup->IsCollected());
    TestEqual(TEXT("Pickup immediately selects bomb"),Player->GetActiveWeapon(),EPlayerWeapon::Bomb);
    Player->SetActorLocation(FVector(0,0,500));
    auto* Camera=Player->GetFirstPersonCameraComponent();
    Camera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);Camera->bUsePawnControlRotation=false;
    Camera->SetWorldLocationAndRotation(FVector(0,0,600),FRotator::ZeroRotator);
    auto* BounceWall=Wall(FVector(200,0,600),FVector(10,500,500));
    Gun->BFGSplashDamage=137.f;
    TestTrue(TEXT("Bomb throws"),Gun->FireBomb());
    TestFalse(TEXT("Cooldown stops a duplicate throw"),Gun->FireBomb());
    ABombProjectile* Thrown=nullptr;for(TActorIterator<ABombProjectile> It(World);It;++It)Thrown=*It;
    if(!TestNotNull(TEXT("Physical bomb spawned"),Thrown))return false;
    TestEqual(TEXT("Damage uses twice live BFG tuning"),Thrown->BlastDamage,274.f);
    TestEqual(TEXT("Falloff radius matches BFG"),Thrown->BlastRadius,Gun->BFGSplashRadius);
    TestFalse(TEXT("Held model disappears during throw"),Gun->WeaponMesh->IsVisible());
    Advance(.25f);
    TestTrue(TEXT("Bomb bounces back from a wall"),Thrown->Movement->Velocity.X<0);
    TestFalse(TEXT("Contact does not detonate"),Thrown->bExploded);
    const float Remaining=Thrown->GetFuseRemaining();
    World->GetWorldSettings()->SetPauserPlayerState(World->SpawnActor<APlayerState>());
    Advance(.5f);
    TestEqual(TEXT("Pause freezes the fuse"),Thrown->GetFuseRemaining(),Remaining);
    TestFalse(TEXT("Paused player cannot throw"),Gun->FireBomb());
    World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    Advance(2.65f);TestFalse(TEXT("Bomb remains live before three seconds"),Thrown->bExploded);
    TestTrue(TEXT("Next held bomb returns after throw"),Gun->WeaponMesh->IsVisible());
    Advance(.15f);TestTrue(TEXT("Fuse detonates at three seconds"),Thrown->bExploded);
    BounceWall->Destroy();Thrown->Destroy();

    auto Target=[&](FVector Location)
    {
        const FTransform T(Location);
        auto* NPC=World->SpawnActorDeferred<AChasingNPC>(AChasingNPC::StaticClass(),T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FindFProperty<FFloatProperty>(AChasingNPC::StaticClass(),TEXT("MaxHealth"))->SetPropertyValue_InContainer(NPC,1000.f);
        FindFProperty<FBoolProperty>(AChasingNPC::StaticClass(),TEXT("bSpawnReplacementsOnDeath"))->SetPropertyValue_InContainer(NPC,false);
        FindFProperty<FFloatProperty>(AChasingNPC::StaticClass(),TEXT("ContactEnergyDrainPerSecond"))->SetPropertyValue_InContainer(NPC,0.f);
        NPC->FinishSpawning(T);NPC->GetCharacterMovement()->DisableMovement();NPC->SetActorTickEnabled(false);
        // Keep both measurements at the same distance: BFG stagger otherwise
        // pushes the fixture away during the three-second bomb countdown.
        NPC->FindComponentByClass<UNPCHitReactionComponent>()->StaggerDistance=0;
        return NPC;
    };
    const FVector Origin(4000,0,600);
    auto* Exposed=Target(Origin+FVector(0,170,0));
    auto* Covered=Target(Origin+FVector(0,-170,0));
    auto* Outside=Target(Origin+FVector(0,500,0));
    auto* Cover=Wall(Origin+FVector(0,-80,0),FVector(200,8,160));
    auto* BFG=World->SpawnActor<ABFGProjectile>(Origin,FRotator::ZeroRotator);
    BFG->Explode(FHitResult());
    const float BFGDamage=1000.f-Exposed->GetCurrentHealth();BFG->Destroy();
    TestTrue(TEXT("Reference BFG splash reaches exposed target"),BFGDamage>0);
    const float Before=Exposed->GetCurrentHealth();
    Player->SetActorLocation(Origin+FVector(170,0,0));
    const float PlayerBefore=Player->GetCurrentEnergy();
    const FTransform T(Origin);
    auto* Bomb=World->SpawnActorDeferred<ABombProjectile>(ABombProjectile::StaticClass(),T);
    Bomb->LaunchVelocity=FVector::ZeroVector;Bomb->FinishSpawning(T);Bomb->Movement->Deactivate();
    Advance(2.9f);TestEqual(TEXT("No damage before timed detonation"),Exposed->GetCurrentHealth(),Before);
    TestEqual(TEXT("Player energy is unchanged before the fuse expires"),Player->GetCurrentEnergy(),PlayerBefore);
    Advance(.15f);
    AddInfo(FString::Printf(TEXT("Same-distance damage: BFG %.3f, bomb %.3f"),BFGDamage,Before-Exposed->GetCurrentHealth()));
    TestTrue(TEXT("NPC blast damage includes the stronger NPC multiplier"),FMath::IsNearlyEqual(Before-Exposed->GetCurrentHealth(),2.f*Bomb->NPCDamageMultiplier*BFGDamage,.01f));
    TestTrue(TEXT("Airburst damages an NPC behind cover inside the sphere"),Covered->GetCurrentHealth()<1000.f);
    TestEqual(TEXT("Outside radius is safe"),Outside->GetCurrentHealth(),1000.f);
    const float After=Exposed->GetCurrentHealth();Bomb->Explode();
    TestEqual(TEXT("Repeated detonation cannot double apply damage"),Exposed->GetCurrentHealth(),After);
    const float PlayerLoss=PlayerBefore-Player->GetCurrentEnergy();
    AddInfo(FString::Printf(TEXT("Player energy before %.3f, after %.3f; NPC loss %.3f"),PlayerBefore,Player->GetCurrentEnergy(),Before-Exposed->GetCurrentHealth()));
    TestTrue(TEXT("Bomb harms its throwing player, but NPCs lose much more energy"),PlayerLoss>0 && PlayerLoss<=25.f && Before-Exposed->GetCurrentHealth()>PlayerLoss*5.f);
    Bomb->Destroy();
    Player->SetActorLocation(Origin+FVector(0,-200,0));
    const float CoveredEnergy=Player->GetCurrentEnergy();
    auto* CoveredBomb=World->SpawnActor<ABombProjectile>(Origin,FRotator::ZeroRotator);
    CoveredBomb->SetOwner(Player);CoveredBomb->Explode();
    TestTrue(TEXT("Solid cover does not cancel player energy damage inside the sphere"),Player->GetCurrentEnergy()<CoveredEnergy);
    const float EnergyAfterCoverBlast=Player->GetCurrentEnergy();
    CoveredBomb->Destroy();Cover->Destroy();
    Player->SetActorLocation(Origin+FVector(700,0,0));
    auto* FarBomb=World->SpawnActor<ABombProjectile>(Origin,FRotator::ZeroRotator);FarBomb->Explode();
    TestEqual(TEXT("Player outside the damage radius loses no energy"),Player->GetCurrentEnergy(),EnergyAfterCoverBlast);
    FarBomb->Destroy();

    const FTransform FloorT(FVector(8000,0,0));
    auto* Floor=World->SpawnActorDeferred<AChunkedDestructibleFloor>(AChunkedDestructibleFloor::StaticClass(),FloorT);
    Floor->ConfigureFloor(FVector2D(100,100),FVector(100,100,50),FVector2D::ZeroVector,false);Floor->FinishSpawning(FloorT);
    auto* FloorBomb=World->SpawnActor<ABombProjectile>(FVector(8000,0,70),FRotator::ZeroRotator);FloorBomb->Explode();
    FChunkedFloorCellInfo Cell;TestTrue(TEXT("Destructible cell queried"),Floor->GetCellInfo(FIntPoint(0,0),Cell));
    TestTrue(TEXT("Airburst applies lethal cell damage immediately"),Cell.Health<=0 && (!Cell.bExists || Cell.bPendingDestruction));
    // The destruction ripple now includes the 70 cm vertical travel distance.
    Advance(.1f);Floor->GetCellInfo(FIntPoint(0,0),Cell);
    TestFalse(TEXT("Timed area blast breaks destructible cells"),Cell.bExists);
    Player->DrainEnergy(100);TestFalse(TEXT("Dead player cannot throw"),Gun->FireBomb());
    return true;
}
#endif
