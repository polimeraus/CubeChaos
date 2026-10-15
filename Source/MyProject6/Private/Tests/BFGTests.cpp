#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BFGProjectile.h"
#include "RailgunWeapon.h"
#include "ParametricPickup.h"
#include "BallProjectile.h"
#include "MyProject6Character.h"
#include "ChasingNPC.h"
#include "ChunkedDestructibleFloor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBFGTest,"MyProject6.Weapons.BFG",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FBFGTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    World->BeginPlay();
    auto Advance=[World](float Seconds)
    {
        for(int32 I=0,N=FMath::CeilToInt(Seconds*60);I<N;++I)
        { ++GFrameCounter;World->Tick(LEVELTICK_All,Seconds/N); }
    };
    auto* PlayerClass=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    auto* BFGPickupClass=LoadClass<AParametricPickup>(nullptr,TEXT("/Game/GameplayActors/Pickups/BFG/BP_BFGPickup.BP_BFGPickup_C"));
    auto* RailPickupClass=LoadClass<AParametricPickup>(nullptr,TEXT("/Game/GameplayActors/Pickups/ParametricPickup/BP_ParametricPickup.BP_ParametricPickup_C"));
    if(!TestNotNull(TEXT("Player BP"),PlayerClass)||!TestNotNull(TEXT("BFG pickup BP"),BFGPickupClass)||!TestNotNull(TEXT("Rail pickup BP"),RailPickupClass))return false;
    auto* Player=World->SpawnActor<AMyProject6Character>(PlayerClass,FVector(0,0,500),FRotator::ZeroRotator);
    Player->GetCharacterMovement()->DisableMovement();
    auto* Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();Controller->Possess(Player);Player->PawnClientRestart();
    Advance(.05f);
    auto* Gun=Player->Railgun.Get();
    if(!TestNotNull(TEXT("Shared hands/weapon rig"),Gun))return false;
    TestFalse(TEXT("BFG starts locked"),Player->bBFGEquipped);
    TestFalse(TEXT("Cannot bypass BFG pickup"),Gun->FireBFG());
    TestEqual(TEXT("BFG Q3 firing interval"),Gun->BFGFireInterval,.2f);
    TestEqual(TEXT("BFG Q3 direct damage"),Gun->BFGDamage,100.f);
    TestEqual(TEXT("BFG Q3 splash damage"),Gun->BFGSplashDamage,100.f);
    TestEqual(TEXT("BFG Q3 radius scaled to centimetres"),Gun->BFGSplashRadius,300.f);
    auto Collect=[&](UClass* Class)
    {
        auto* Pickup=World->SpawnActor<AParametricPickup>(Class,FVector(2000,2000,500),FRotator::ZeroRotator);
        Player->SetActorLocation(Pickup->GetActorLocation()+FVector(0,0,Pickup->Size*.5));
        Advance(.05f);
        TestTrue(TEXT("Pickup collected by real overlap"),Pickup->IsCollected());
        Player->SetActorLocation(FVector(0,0,500));
    };
    Collect(BFGPickupClass);
    TestTrue(TEXT("Saved BFG pickup equips BFG"),Player->bBFGEquipped);
    TestFalse(TEXT("BFG pickup unequips railgun"),Player->bRailgunEquipped);
    TestFalse(TEXT("Rail input cannot fire while BFG is equipped"),Gun->Fire());
    TestEqual(TEXT("BFG mesh is actually displayed"),Gun->WeaponMesh->GetStaticMesh()->GetName(),FString(TEXT("SM_Mini_Gun")));
    auto* Camera=Player->GetFirstPersonCameraComponent();
    Camera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    Camera->bUsePawnControlRotation=false;
    Camera->SetWorldLocationAndRotation(FVector(0,0,600),FRotator::ZeroRotator);
    auto SpawnTarget=[&](FVector Location)
    {
        const FTransform Transform(Location);
        auto* NPC=World->SpawnActorDeferred<AChasingNPC>(AChasingNPC::StaticClass(),Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        FindFProperty<FFloatProperty>(AChasingNPC::StaticClass(),TEXT("MaxHealth"))->SetPropertyValue_InContainer(NPC,500.f);
        NPC->FinishSpawning(Transform);NPC->GetCharacterMovement()->DisableMovement();NPC->SetActorTickEnabled(false);return NPC;
    };
    auto Wall=[&](FVector Location,FVector Extent)
    {
        auto* Actor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Actor);Actor->SetRootComponent(Box);
        Box->SetBoxExtent(Extent);Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Actor->SetActorLocation(Location);return Actor;
    };
    auto* Direct=SpawnTarget(FVector(1000,0,600));
    auto* Nearby=SpawnTarget(FVector(1000,170,600));
    auto* Protected=SpawnTarget(FVector(1000,-180,600));
    auto* SplashCover=Wall(FVector(1000,-90,600),FVector(200,8,160));
    TestTrue(TEXT("First BFG shot fires"),Gun->FireBFG());
    TestFalse(TEXT("Repeated request respects cooldown"),Gun->FireBFG());
    TestEqual(TEXT("BFG is a travelling projectile, not instant damage"),Direct->GetCurrentHealth(),500.f);
    TWeakObjectPtr<ABFGProjectile> Shot;
    for(TActorIterator<ABFGProjectile> It(World);It;++It)Shot=*It;
    if(!TestTrue(TEXT("BFG projectile exists"),Shot.IsValid()))return false;
    TestEqual(TEXT("Q3 projectile speed scaled to cm/s"),Shot->Speed,5000.f);
    Advance(.24f);
    TestTrue(TEXT("Projectile hits and explodes"),Shot->bExploded);
    TestEqual(TEXT("Direct damage applied once without additional splash"),Direct->GetCurrentHealth(),400.f);
    TestTrue(TEXT("Nearby target takes radial falloff damage"),Nearby->GetCurrentHealth()<500.f&&Nearby->GetCurrentHealth()>400.f);
    TestEqual(TEXT("Wall occludes splash damage"),Protected->GetCurrentHealth(),500.f);
    const float NearbyAfter=Nearby->GetCurrentHealth();Shot->Explode(FHitResult());
    TestEqual(TEXT("Explosion cannot be applied twice"),Nearby->GetCurrentHealth(),NearbyAfter);
    Advance(.6f);TestFalse(TEXT("Explosion cleans up"),Shot.IsValid());
    auto* FrontCover=Wall(FVector(400,0,600),FVector(2,150,150));
    TestTrue(TEXT("Shot into thin cover fires"),Gun->FireBFG());
    Advance(.3f);
    TestEqual(TEXT("Fast projectile cannot tunnel through a thin wall"),Direct->GetCurrentHealth(),400.f);
    FrontCover->Destroy();SplashCover->Destroy();

    Collect(RailPickupClass);
    TestTrue(TEXT("Other pickups switch back to railgun"),Player->bRailgunEquipped);
    TestFalse(TEXT("Switch clears BFG equipped state"),Player->bBFGEquipped);
    TestFalse(TEXT("BFG cannot fire after switching away"),Gun->FireBFG());
    TestEqual(TEXT("Rail mesh restored"),Gun->WeaponMesh->GetStaticMesh()->GetName(),FString(TEXT("SM_QuakeRailgun")));
    TestEqual(TEXT("Railgun damage increased to 200"),Gun->Damage,200.f);
    TestTrue(TEXT("Powered up rail fires"),Gun->Fire());
    TestEqual(TEXT("Actual rail hit removes 200 health"),Direct->GetCurrentHealth(),200.f);
    Collect(BFGPickupClass);
    Camera->SetWorldRotation(FRotator(0,90,0));
    auto Key=[&](EInputEvent Event)
    {
        for(FInputKeyBinding& B:Player->InputComponent->KeyBindings)
            if(B.Chord.Key==EKeys::LeftMouseButton&&B.KeyEvent==Event){B.KeyDelegate.Execute(EKeys::LeftMouseButton);return true;}
        return false;
    };
    const int32 Before=Gun->BFGShotsFired;
    TestTrue(TEXT("BFG uses primary fire binding"),Key(IE_Pressed));Advance(.45f);
    TestEqual(TEXT("Held BFG fires every 200ms"),Gun->BFGShotsFired,Before+3);
    TestTrue(TEXT("BFG release binding"),Key(IE_Released));Advance(.3f);
    TestEqual(TEXT("Release stops firing"),Gun->BFGShotsFired,Before+3);
    int32 Balls=0;for(TActorIterator<ABallProjectile> It(World);It;++It)++Balls;
    TestEqual(TEXT("BFG never also fires the old ball projectile"),Balls,0);

    const FTransform FloorTransform(FRotator::ZeroRotator,FVector(0,500,0));
    auto* Floor=World->SpawnActorDeferred<AChunkedDestructibleFloor>(AChunkedDestructibleFloor::StaticClass(),FloorTransform);
    Floor->ConfigureFloor(FVector2D(100,100),FVector(100,100,50),FVector2D::ZeroVector,false);Floor->FinishSpawning(FloorTransform);
    Camera->SetWorldLocationAndRotation(FVector(0,500,500),FRotator(-90,0,0));
    TestTrue(TEXT("BFG fires at destructible floor"),Gun->FireBFG());Advance(.3f);
    FChunkedFloorCellInfo Cell;TestTrue(TEXT("Floor cell queried"),Floor->GetCellInfo(FIntPoint(0,0),Cell));
    TestFalse(TEXT("BFG blast damages destructible cells"),Cell.bExists);
    World->GetWorldSettings()->SetPauserPlayerState(World->SpawnActor<APlayerState>());
    TestFalse(TEXT("Paused BFG cannot fire"),Gun->FireBFG());World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    Player->DrainEnergy(100);TestFalse(TEXT("Dead player cannot fire BFG"),Gun->FireBFG());
    UGameplayStatics::SetGamePaused(World,false);Player->Destroy();
    TestTrue(TEXT("Player cleans up shared weapon"),Gun->IsActorBeingDestroyed());
    return true;
}
#endif
