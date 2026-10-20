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
#include "Components/PointLightComponent.h"
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
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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
    TestEqual(TEXT("BFG arena firing interval"),Gun->BFGFireInterval,.3f);
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
    TestEqual(TEXT("New fusion cannon mesh is actually displayed"),Gun->WeaponMesh->GetStaticMesh()->GetName(),FString(TEXT("SM_BFGFusion")));
    TestTrue(TEXT("New gun's forward barrel and muzzle align"),Gun->Muzzle->GetRelativeLocation().Equals(FVector(0,65,12.5f),.01));
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
    const FTransform AimBefore=Camera->GetComponentTransform();
    FString Samples(TEXT("time,x,y,z,pitch,yaw,roll,light\n"));
    auto Sample=[&](float Time)
    {
        const FVector P=Gun->GetRootComponent()->GetRelativeLocation();
        const FRotator R=Gun->GetRootComponent()->GetRelativeRotation();
        Samples+=FString::Printf(TEXT("%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.1f\n"),
            Time,P.X,P.Y,P.Z,R.Pitch,R.Yaw,R.Roll,Gun->BFGMuzzleLight->Intensity);
    };
    Sample(-1.f);
    TestTrue(TEXT("First BFG shot fires"),Gun->FireBFG());
    TestTrue(TEXT("Discharge brightly illuminates the surroundings"),Gun->BFGMuzzleLight->Intensity>=18000
        && Gun->BFGMuzzleLight->AttenuationRadius>=650);
    const FLinearColor FlashColor=Gun->BFGMuzzleLight->GetLightColor();
    TestTrue(TEXT("Discharge is saturated neon green"),FlashColor.G>.99f && FlashColor.R<.04f && FlashColor.B<.01f);
    TestFalse(TEXT("Repeated request respects cooldown"),Gun->FireBFG());
    TestEqual(TEXT("BFG is a travelling projectile, not instant damage"),Direct->GetCurrentHealth(),500.f);
    TWeakObjectPtr<ABFGProjectile> Shot;
    for(TActorIterator<ABFGProjectile> It(World);It;++It)Shot=*It;
    if(!TestTrue(TEXT("BFG projectile exists"),Shot.IsValid()))return false;
    TestEqual(TEXT("Fusion projectile is 60 percent faster"),Shot->Speed,8000.f);
    TestTrue(TEXT("Projectile illuminates nearby geometry"),Shot->PlasmaLight->Intensity>3000 && Shot->PlasmaLight->AttenuationRadius>=420);
    Sample(0.f);
    float PeakPitch=0.f;
    for(int32 Frame=1;Frame<=15;++Frame)
    {
        Advance(.016f);
        PeakPitch=FMath::Max(PeakPitch,float(Gun->GetRootComponent()->GetRelativeRotation().Pitch));
        Sample(Frame*.016f);
        if(Frame==3) TestTrue(TEXT("Flash decays quickly but remains visible through recoil"),
            Gun->BFGMuzzleLight->Intensity>4000 && Gun->BFGMuzzleLight->Intensity<18000);
    }
    TestTrue(TEXT("Heavy BFG discharge exceeds the previous 2.5 degree kick"),PeakPitch>3.3f);
    TestTrue(TEXT("Weapon recoil leaves the camera and aim unchanged"),Camera->GetComponentTransform().Equals(AimBefore,.001f));
    const FString PreviewDir=FPaths::ProjectSavedDir()/TEXT("BFGFeedback");
    IFileManager::Get().MakeDirectory(*PreviewDir,true);
    TestTrue(TEXT("Actual firing presentation samples saved"),FFileHelper::SaveStringToFile(Samples,*(PreviewDir/TEXT("Samples.csv"))));
    TestEqual(TEXT("Muzzle flash turns off after discharge"),Gun->BFGMuzzleLight->Intensity,0.f);
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
    TestEqual(TEXT("Held BFG fires every 300ms"),Gun->BFGShotsFired,Before+2);
    TestTrue(TEXT("BFG release binding"),Key(IE_Released));Advance(.3f);
    TestEqual(TEXT("Release stops firing"),Gun->BFGShotsFired,Before+2);
    int32 Balls=0;for(TActorIterator<ABallProjectile> It(World);It;++It)++Balls;
    TestEqual(TEXT("BFG never also fires the old ball projectile"),Balls,0);

    const FTransform FloorTransform(FRotator::ZeroRotator,FVector(0,500,0));
    auto* Floor=World->SpawnActorDeferred<AChunkedDestructibleFloor>(AChunkedDestructibleFloor::StaticClass(),FloorTransform);
    Floor->ConfigureFloor(FVector2D(100,100),FVector(100,100,50),FVector2D::ZeroVector,false);Floor->FinishSpawning(FloorTransform);
    Camera->SetWorldLocationAndRotation(FVector(0,500,500),FRotator(-90,0,0));
    TestTrue(TEXT("BFG fires at destructible floor"),Gun->FireBFG());Advance(.3f);
    FChunkedFloorCellInfo Cell;TestTrue(TEXT("Floor cell queried"),Floor->GetCellInfo(FIntPoint(0,0),Cell));
    TestFalse(TEXT("BFG blast damages destructible cells"),Cell.bExists);
    TestTrue(TEXT("Fresh discharge before switching weapons"),Gun->FireBFG());
    TestTrue(TEXT("Switch to the owned railgun during discharge"),Player->SelectWeapon(EPlayerWeapon::Railgun));
    TestEqual(TEXT("Switching weapons immediately clears the green flash"),Gun->BFGMuzzleLight->Intensity,0.f);
    TestTrue(TEXT("Switch back to BFG"),Player->SelectWeapon(EPlayerWeapon::BFG));
    World->GetWorldSettings()->SetPauserPlayerState(World->SpawnActor<APlayerState>());
    TestFalse(TEXT("Paused BFG cannot fire"),Gun->FireBFG());World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    Player->DrainEnergy(100);TestFalse(TEXT("Dead player cannot fire BFG"),Gun->FireBFG());
    UGameplayStatics::SetGamePaused(World,false);Player->Destroy();
    TestTrue(TEXT("Player cleans up shared weapon"),Gun->IsActorBeingDestroyed());
    return true;
}
#endif
