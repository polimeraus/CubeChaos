#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RailgunWeapon.h"
#include "RailgunBeam.h"
#include "MyProject6Character.h"
#include "ChasingNPC.h"
#include "ChunkedDestructibleFloor.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRailgunTest, "MyProject6.Weapons.Railgun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRailgunTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    World->BeginPlay();
    auto Advance = [World](float Seconds)
    {
        for (int32 I = 0, N = FMath::CeilToInt(Seconds * 60); I < N; ++I)
        { ++GFrameCounter; World->Tick(LEVELTICK_All, Seconds / N); }
    };
    UClass* PlayerClass = LoadClass<AMyProject6Character>(nullptr,
        TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if (!TestNotNull(TEXT("Existing player Blueprint loads"), PlayerClass)) return false;
    auto* Player = World->SpawnActor<AMyProject6Character>(PlayerClass, FVector(0, 0, 500), FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Player spawns"), Player)) return false;
    Player->GetCharacterMovement()->DisableMovement();
    auto* Controller = World->SpawnActor<APlayerController>();
    Controller->SetAsLocalPlayerController();
    Controller->Possess(Player);
    Player->PawnClientRestart();
    Advance(.05f);
    ARailgunWeapon* Gun = Player->Railgun;
    if (!TestNotNull(TEXT("Player automatically equips railgun"), Gun)) return false;
    TestNotNull(TEXT("Visible rifle asset loads"), Gun->WeaponMesh->GetStaticMesh().Get());
    TestEqual(TEXT("Rifle attaches to the hand grip"), Gun->WeaponMesh->GetAttachSocketName(), FName(TEXT("HandGrip_R")));
    TestTrue(TEXT("Authored hand grip exists"), Gun->Arms->DoesSocketExist(TEXT("HandGrip_R")));
    TestEqual(TEXT("Weapon has no collision"), Gun->WeaponMesh->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    const FVector SupportHand = Gun->WeaponMesh->GetComponentTransform().InverseTransformPosition(Gun->Arms->GetSocketLocation(TEXT("hand_l")));
    TestTrue(TEXT("Left wrist stays at the fore-end instead of floating 13 cm away"), FMath::IsNearlyEqual(SupportHand.X, 4.8, .5));

    // Freeze the test camera so unrelated locomotion/head animation cannot move the aiming ray.
    auto* Camera = Player->GetFirstPersonCameraComponent();
    Camera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    Camera->bUsePawnControlRotation = false;
    Camera->SetWorldLocationAndRotation(FVector(0, 0, 600), FRotator::ZeroRotator);
    const FVector BarrelDirection = Gun->WeaponMesh->GetComponentTransform().TransformVectorNoScale(FVector::YAxisVector);
    TestTrue(TEXT("Held rifle points forward in the firing pose"), FVector::DotProduct(BarrelDirection, Camera->GetForwardVector()) > .8);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Target = World->SpawnActor<AChasingNPC>(AChasingNPC::StaticClass(), FVector(1000, 0, 600), FRotator::ZeroRotator, Spawn);
    Target->GetCharacterMovement()->DisableMovement();
    Target->SetActorTickEnabled(false);
    Gun->Damage = 40;
    Gun->Visuals.Color = FLinearColor(.01f, .3f, 1.f);
    Gun->Visuals.Intensity = 2.f;
    Gun->Visuals.Density = .4f;
    TestTrue(TEXT("Initial shot fires"), Gun->Fire());
    TestEqual(TEXT("Damage arrives instantly without advancing a frame"), Target->GetCurrentHealth(), 60.f);
    TestFalse(TEXT("Shared cooldown rejects a second simultaneous request"), Gun->Fire());
    TestEqual(TEXT("Rejected shot does not cause damage"), Target->GetCurrentHealth(), 60.f);
    TWeakObjectPtr<ARailgunBeam> Trail;
    for (TActorIterator<ARailgunBeam> It(World); It; ++It) Trail = *It;
    TestTrue(TEXT("Shot creates a world-space trail"), Trail.IsValid());
    if (Trail.IsValid())
    {
        auto* Material = Cast<UMaterialInstanceDynamic>(Trail->Trail->GetMaterial(0));
        TestNotNull(TEXT("Soft trail creates its material"), Material);
        if (Material)
        {
            TestTrue(TEXT("Shot color reaches the renderer"), Material->K2_GetVectorParameterValue(TEXT("Tint")).Equals(Gun->Visuals.Color));
            TestEqual(TEXT("Intensity reaches the renderer"), Material->K2_GetScalarParameterValue(TEXT("Brightness")), 2.f);
            TestEqual(TEXT("Density reaches the renderer"), Material->K2_GetScalarParameterValue(TEXT("Density")), .4f);
        }
    }
    Advance(.85f);
    TestFalse(TEXT("Trail cleans itself up"), Trail.IsValid());

    AActor* Wall = World->SpawnActor<AActor>();
    auto* Shape = NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(Shape);
    Shape->SetBoxExtent(FVector(15, 150, 150));
    Shape->SetCollisionProfileName(TEXT("BlockAll"));
    Shape->RegisterComponent();
    Wall->SetActorLocation(FVector(400, 0, 600));
    TestTrue(TEXT("Shot into cover fires"), Gun->Fire());
    TestEqual(TEXT("Solid wall blocks damage"), Target->GetCurrentHealth(), 60.f);
    TestTrue(TEXT("Beam terminates on the wall"), Gun->LastShotEnd.X < 401);
    Wall->Destroy();
    Advance(.85f);
    Gun->Range = 300;
    TestTrue(TEXT("Shot with short range fires"), Gun->Fire());
    TestEqual(TEXT("Out of range target is unharmed"), Target->GetCurrentHealth(), 60.f);
    TestTrue(TEXT("Miss ends at configured camera range"), Gun->LastShotEnd.Equals(FVector(300, 0, 600), .1));
    Advance(.85f);
    Gun->Range = 20000;

    // Exercise both actual player bindings, including their common cooldown.
    auto Press = [Player](FKey Key)
    {
        if (!Player->InputComponent) return false;
        for (FInputKeyBinding& Binding : Player->InputComponent->KeyBindings)
        {
            if (Binding.Chord.Key == Key && Binding.KeyEvent == IE_Pressed)
            { Binding.KeyDelegate.Execute(Key); return true; }
        }
        return false;
    };
    const int32 BeforeKeys = Gun->ShotsFired;
    TestTrue(TEXT("Middle mouse has a pressed binding"), Press(EKeys::MiddleMouseButton));
    TestEqual(TEXT("Middle mouse fires railgun"), Gun->ShotsFired, BeforeKeys + 1);
    TestTrue(TEXT("Number 1 has a pressed binding"), Press(EKeys::One));
    TestEqual(TEXT("Switching keys cannot bypass cooldown"), Gun->ShotsFired, BeforeKeys + 1);
    Advance(.85f);
    // Aim away so the final key press doesn't trigger NPC replacement spawning on death.
    Camera->SetWorldRotation(FRotator(0, 90, 0));
    TestTrue(TEXT("Number 1 remains bound"), Press(EKeys::One));
    TestEqual(TEXT("Number 1 also fires railgun after cooldown"), Gun->ShotsFired, BeforeKeys + 2);
    Advance(.85f);

    // The same hitscan also enters the existing destructible-cell damage path.
    const FTransform FloorTransform(FRotator::ZeroRotator, FVector(0, 500, 0));
    auto* Floor = World->SpawnActorDeferred<AChunkedDestructibleFloor>(AChunkedDestructibleFloor::StaticClass(), FloorTransform);
    Floor->ConfigureFloor(FVector2D(100, 100), FVector(100, 100, 50), FVector2D::ZeroVector, false);
    Floor->FinishSpawning(FloorTransform);
    Camera->SetWorldLocationAndRotation(FVector(0, 500, 500), FRotator(-90, 0, 0));
    TestTrue(TEXT("Shot at a destructible floor fires"), Gun->Fire());
    Advance(.85f);
    FChunkedFloorCellInfo Cell;
    TestTrue(TEXT("Test floor has a cell"), Floor->GetCellInfo(FIntPoint(0, 0), Cell));
    TestFalse(TEXT("Railgun destroys the struck floor cell"), Cell.bExists);

    World->GetWorldSettings()->SetPauserPlayerState(World->SpawnActor<APlayerState>());
    TestTrue(TEXT("Test world is paused"), UGameplayStatics::IsGamePaused(World));
    TestFalse(TEXT("Paused game cannot fire"), Gun->Fire());
    World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    Player->DrainEnergy(100);
    TestFalse(TEXT("A dead player cannot fire"), Gun->Fire());
    UGameplayStatics::SetGamePaused(World, false);
    Player->Destroy();
    TestTrue(TEXT("Player destruction cleans up held weapon"), Gun->IsActorBeingDestroyed());
    return true;
}
#endif
