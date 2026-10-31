#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "MyProject6Character.h"
#include "RailgunWeapon.h"
#include "RailgunBeam.h"
#include "ParametricPickup.h"
#include "ChasingNPC.h"
#include "GameOverMenuTestController.h"
#include "EnergyHUDWidget.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPurpleRailgunTest,"MyProject6.Weapons.PurpleRailgun",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FPurpleRailgunTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto Advance=[&](float Seconds)
    {
        for(int32 I=0,N=FMath::CeilToInt(Seconds*60);I<N;++I)
        { ++GFrameCounter;World->Tick(LEVELTICK_All,Seconds/N); }
    };
    UClass* PlayerClass=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if(!TestNotNull(TEXT("Saved player Blueprint loads"),PlayerClass))return false;
    auto* Player=World->SpawnActor<AMyProject6Character>(PlayerClass,FVector(0,0,500),FRotator::ZeroRotator);
    Player->GetCharacterMovement()->DisableMovement();
    auto* Controller=World->SpawnActor<AGameOverMenuTestController>();Controller->InitializeTestMenu();
    Controller->Possess(Player);Player->PawnClientRestart();Advance(.05f);
    auto Key=[&](FKey K,EInputEvent E=IE_Pressed)
    {
        for(FInputKeyBinding& Binding:Player->InputComponent->KeyBindings)
            if(Binding.Chord.Key==K&&Binding.KeyEvent==E){Binding.KeyDelegate.Execute(K);return true;}
        AddError(FString::Printf(TEXT("Missing key: %s"),*K.ToString()));return false;
    };
    ARailgunWeapon* Gun=Player->Railgun;
    if(!TestNotNull(TEXT("Weapon rig exists"),Gun))return false;
    TestFalse(TEXT("Purple weapon starts locked"),Player->HasWeapon(EPlayerWeapon::PurpleRailgun));
    Key(EKeys::Seven);
    TestEqual(TEXT("Hotkey cannot grant an uncollected weapon"),Player->GetActiveWeapon(),EPlayerWeapon::Default);
    UClass* PickupClass=LoadClass<AParametricPickup>(nullptr,TEXT("/Game/GameplayActors/Pickups/PurpleRailgun/BP_PurpleRailgunPickup.BP_PurpleRailgunPickup_C"));
    if(!TestNotNull(TEXT("Saved purple pickup loads"),PickupClass))return false;
    auto* Pickup=World->SpawnActor<AParametricPickup>(PickupClass,FVector(1200,0,500),FRotator::ZeroRotator);
    TestEqual(TEXT("New pickup inherits reward"),Pickup->WeaponReward,EPickupWeapon::PurpleRailgun);
    TestEqual(TEXT("New pickup inherits rifle icon"),Pickup->Icon,EPickupMotif::PurpleRailgun);
    Player->SetActorLocation(FVector(1200,0,530));Advance(.05f);
    TestTrue(TEXT("Real overlap collects pickup"),Pickup->IsCollected());
    TestFalse(TEXT("Pickup cannot grant twice"),Pickup->TryCollect(Player));
    TestTrue(TEXT("Purple weapon is owned after overlap"),Player->HasWeapon(EPlayerWeapon::PurpleRailgun));
    TestFalse(TEXT("Purple pickup does not unlock the blue railgun"),Player->HasWeapon(EPlayerWeapon::Railgun));
    TestEqual(TEXT("Collection equips purple railgun"),Player->GetActiveWeapon(),EPlayerWeapon::PurpleRailgun);
    if(!TestNotNull(TEXT("Imported mesh exists"),Gun->WeaponMesh->GetStaticMesh().Get()))return false;
    TestEqual(TEXT("Fab model is equipped"),Gun->WeaponMesh->GetStaticMesh()->GetName(),FString(TEXT("SM_PurpleRailgun")));
    TestEqual(TEXT("Weapon follows grip"),Gun->WeaponMesh->GetAttachSocketName(),FName(TEXT("HandGrip_R")));
    TestEqual(TEXT("Baked fit animation runs"),Gun->Arms->GetSingleNodeInstance()->GetCurrentAsset()->GetName(),FString(TEXT("A_PurpleRailgunHold")));
    TestTrue(TEXT("Mesh keeps imported PBR body material"),Gun->WeaponMesh->GetMaterial(0)->GetName().Contains(TEXT("PurpleRailgun")));
    const FVector LocalMuzzle=Gun->WeaponMesh->GetSocketTransform(TEXT("Muzzle"),RTS_Component).GetLocation();
    TestTrue(TEXT("Muzzle is on the barrel face"),LocalMuzzle.Y>55 && LocalMuzzle.Y<60 && LocalMuzzle.Z>8 && LocalMuzzle.Z<14);
    for(int32 I=0;I<8;++I)
    {
        Advance(.1f);
        const FVector Wrist=Gun->WeaponMesh->GetComponentTransform().InverseTransformPosition(Gun->Arms->GetSocketLocation(TEXT("hand_l")));
        TestTrue(TEXT("Support wrist remains fitted throughout idle motion"),Wrist.Equals(FVector(5.2,31.5,-2),.25));
    }
    Player->SetActorLocation(FVector(0,0,500));
    auto* Camera=Player->GetFirstPersonCameraComponent();Camera->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    Camera->bUsePawnControlRotation=false;Camera->SetWorldLocationAndRotation(FVector(0,0,600),FRotator::ZeroRotator);
    TestTrue(TEXT("Barrel points with the crosshair"),FVector::DotProduct(Gun->WeaponMesh->GetComponentTransform().TransformVectorNoScale(FVector::YAxisVector),Camera->GetForwardVector())>.98);
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Target=World->SpawnActor<AChasingNPC>(AChasingNPC::StaticClass(),FVector(1000,0,600),FRotator::ZeroRotator,Spawn);
    Target->GetCharacterMovement()->DisableMovement();Target->SetActorTickEnabled(false);Gun->Damage=25;
    Key(EKeys::LeftMouseButton);Key(EKeys::LeftMouseButton,IE_Released);
    TestEqual(TEXT("Primary fire shoots exactly once"),Gun->ShotsFired,1);
    TestEqual(TEXT("Purple rail hits instantly"),Target->GetCurrentHealth(),75.f);
    TestTrue(TEXT("HUD charge starts empty"),Gun->GetRailgunCharge()<.01f);
    for(TActorIterator<ARailgunBeam> It(World);It;++It)
    {
        TestTrue(TEXT("Purple core settings reach beam"),It->Visuals.Color.Equals(Player->PurpleRailgunVisuals.Color));
        auto* MID=Cast<UMaterialInstanceDynamic>(It->Trail->GetMaterial(0));
        if(TestNotNull(TEXT("Beam renderer material"),MID))TestTrue(TEXT("Rendered laser tint is purple"),MID->K2_GetVectorParameterValue(TEXT("Tint")).Equals(Player->PurpleRailgunVisuals.Color));
        auto* SpiralMID=Cast<UMaterialInstanceDynamic>(It->Spiral->GetMaterial(0));
        if(TestNotNull(TEXT("Spiral renderer material"),SpiralMID))TestTrue(TEXT("Spiral tint is purple"),SpiralMID->K2_GetVectorParameterValue(TEXT("Tint")).Equals(Player->PurpleRailgunVisuals.SpiralColor));
    }
    Key(EKeys::MiddleMouseButton);TestEqual(TEXT("Middle and primary share cooldown"),Gun->ShotsFired,1);
    Player->EquipRailgun();TestTrue(TEXT("Blue railgun retains its blue tint"),Gun->Visuals.Color.Equals(Player->RailgunVisuals.Color));
    TestFalse(TEXT("Switching railgun cannot reset cooldown"),Gun->Fire());
    Key(EKeys::Seven);TestTrue(TEXT("Switching back restores purple"),Gun->Visuals.Color.Equals(Player->PurpleRailgunVisuals.Color));
    Advance(1.6f);Key(EKeys::MiddleMouseButton);TestEqual(TEXT("Middle fires the selected purple weapon"),Gun->ShotsFired,2);
    Key(EKeys::MouseScrollUp);TestEqual(TEXT("Wheel wraps from seventh slot"),Player->GetActiveWeapon(),EPlayerWeapon::Default);
    Key(EKeys::MouseScrollDown);TestEqual(TEXT("Wheel selects owned purple railgun"),Player->GetActiveWeapon(),EPlayerWeapon::PurpleRailgun);
    Advance(1.6f);TestTrue(TEXT("Charge returns to ready"),Gun->GetRailgunCharge()>.99f);
    if(!GUsingNullRHI)
    {
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("PurpleRailgun/Preview");IFileManager::Get().MakeDirectory(*Dir,true);
        UEnergyHUDWidget* Preview=CreateWidget<UEnergyHUDWidget>(Controller,UEnergyHUDWidget::StaticClass());
        FWidgetRenderer Renderer(true);auto* RT=Renderer.DrawWidget(Preview->TakeWidget(),FVector2D(1280,720));
        FlushRenderingCommands();TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
        if(TestTrue(TEXT("HUD pixels readable"),RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)))
        {
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(RT->SizeX,RT->SizeY,Pixels,PNG);
            TestTrue(TEXT("Purple HUD preview saved"),FFileHelper::SaveArrayToFile(PNG,*(Dir/TEXT("HUD_PurpleRailgun.png"))));
        }
    }
    UGameplayStatics::SetGamePaused(World,true);TestFalse(TEXT("Paused shots rejected"),Gun->Fire());
    Key(EKeys::One);TestEqual(TEXT("Paused selection rejected"),Player->GetActiveWeapon(),EPlayerWeapon::PurpleRailgun);
    UGameplayStatics::SetGamePaused(World,false);Player->DrainEnergy(100);
    TestFalse(TEXT("Depleted player cannot shoot"),Gun->Fire());
    return true;
}
#endif
