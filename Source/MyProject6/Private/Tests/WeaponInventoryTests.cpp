#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "MyProject6Character.h"
#include "RailgunWeapon.h"
#include "ParametricPickup.h"
#include "BallProjectile.h"
#include "GameOverMenuTestController.h"
#include "EnergyHUDWidget.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponInventoryTest,"MyProject6.Weapons.Inventory",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWeaponInventoryTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Class=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if(!TestNotNull(TEXT("Saved player class"),Class))return false;
    auto* Player=World->SpawnActor<AMyProject6Character>(Class,FVector(0,0,500),FRotator::ZeroRotator);
    Player->GetCharacterMovement()->DisableMovement();
    auto* Controller=World->SpawnActor<AGameOverMenuTestController>();Controller->InitializeTestMenu();
    Controller->Possess(Player);Player->PawnClientRestart();
    auto Tick=[&]() {++GFrameCounter;World->Tick(LEVELTICK_All,.05f);};
    auto Key=[&](FKey K,EInputEvent E=IE_Pressed)
    {
        for(FInputKeyBinding& B:Player->InputComponent->KeyBindings)
            if(B.Chord.Key==K&&B.KeyEvent==E){B.KeyDelegate.Execute(K);return true;}
        AddError(TEXT("Missing input binding"));return false;
    };
    auto Collect=[&](EPickupWeapon Reward)
    {
        const FTransform T(FVector(1200,0,500));
        auto* P=World->SpawnActorDeferred<AParametricPickup>(AParametricPickup::StaticClass(),T);
        P->WeaponReward=Reward;P->FinishSpawning(T);
        Player->SetActorLocation(FVector(1200,0,530));Tick();
        TestTrue(TEXT("Physical overlap collects the configured weapon"),P->IsCollected());
        Player->SetActorLocation(FVector(0,0,500));P->Destroy();Tick();
    };
    auto Active=[&](EPlayerWeapon W){TestEqual(TEXT("Active weapon"),Player->GetActiveWeapon(),W);};
    Active(EPlayerWeapon::Default);
    TestFalse(TEXT("Cannot select uncollected BFG"),Player->SelectWeapon(EPlayerWeapon::BFG));
    Key(EKeys::MouseScrollUp);Active(EPlayerWeapon::Default);
    Collect(EPickupWeapon::BFG);Active(EPlayerWeapon::BFG);
    Key(EKeys::MouseScrollDown);Active(EPlayerWeapon::Default);
    TestEqual(TEXT("Original rifle model and grip restored"),Player->Railgun->WeaponMesh->GetStaticMesh()->GetName(),FString(TEXT("SM_Rifle")));
    TestTrue(TEXT("Default transform restored"),Player->Railgun->WeaponMesh->GetRelativeTransform().Equals(FTransform::Identity));
    Key(EKeys::MouseScrollDown);Active(EPlayerWeapon::BFG);
    Collect(EPickupWeapon::Railgun);Active(EPlayerWeapon::Railgun);
    Key(EKeys::MouseScrollUp);Active(EPlayerWeapon::BFG);
    Key(EKeys::MouseScrollUp);Active(EPlayerWeapon::Default);
    Key(EKeys::MouseScrollUp);Active(EPlayerWeapon::Railgun);
    Key(EKeys::Three);Active(EPlayerWeapon::BFG);
    Collect(EPickupWeapon::DefaultWeapon);Active(EPlayerWeapon::Default);
    TestTrue(TEXT("Picking original rifle retains both acquired weapons"),Player->HasWeapon(EPlayerWeapon::Railgun)&&Player->HasWeapon(EPlayerWeapon::BFG));
    Key(EKeys::LeftMouseButton);Tick();Key(EKeys::LeftMouseButton,IE_Released);
    int32 Balls=0;for(TActorIterator<ABallProjectile> It(World);It;++It)++Balls;
    TestTrue(TEXT("Restored default weapon fires its original projectiles"),Balls>0);
    Key(EKeys::Two);Active(EPlayerWeapon::Railgun);
    TestTrue(TEXT("Railgun can fire after switching"),Player->Railgun->Fire());
    Key(EKeys::Three);Key(EKeys::Two);
    TestFalse(TEXT("Weapon switching cannot reset railgun cooldown"),Player->Railgun->Fire());
    if(!GUsingNullRHI)
    {
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("WeaponUpgrade/Preview");IFileManager::Get().MakeDirectory(*Dir,true);
        FWidgetRenderer Renderer(true);
        for(EPlayerWeapon W:{EPlayerWeapon::Default,EPlayerWeapon::Railgun,EPlayerWeapon::BFG})
        {
            Player->SelectWeapon(W);
            Tick();
            // Each offscreen target needs its own Slate tree; retained element batches
            // belong to the previous render target, unlike the live viewport HUD.
            UEnergyHUDWidget* Preview=CreateWidget<UEnergyHUDWidget>(Controller,UEnergyHUDWidget::StaticClass());
            const TSharedRef<SWidget> Root=Preview->TakeWidget();
            auto* RT=Renderer.DrawWidget(Root,FVector2D(1280,720));
            FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
            if(TestNotNull(TEXT("HUD rendered"),RT)&&TestTrue(TEXT("HUD pixels readable"),RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))
            {
                TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(RT->SizeX,RT->SizeY,Pixels,PNG);
                TestTrue(TEXT("HUD image saved"),FFileHelper::SaveArrayToFile(PNG,*(Dir/FString::Printf(TEXT("HUD_%d.png"),int32(W)))));
            }
        }
    }
    UGameplayStatics::SetGamePaused(World,true);
    const auto Before=Player->GetActiveWeapon();Key(EKeys::MouseScrollDown);Active(Before);
    UGameplayStatics::SetGamePaused(World,false);Player->DrainEnergy(100);Key(EKeys::MouseScrollUp);Active(Before);
    return true;
}
#endif
