#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "MyProject6Character.h"
#include "GameOverMenuTestController.h"
#include "RailgunWeapon.h"
#include "ThirdPersonWeaponPose.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerPerspectiveTest, "MyProject6.Player.Perspective",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlayerPerspectiveTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Class = LoadClass<AMyProject6Character>(nullptr,
		TEXT("/Game/Characters/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Saved player Blueprint"), Class)) return false;
	auto* Player = World->SpawnActor<AMyProject6Character>(Class, FVector(0,0,500), FRotator::ZeroRotator);
	auto* Controller = World->SpawnActor<AGameOverMenuTestController>();
	Controller->InitializeTestMenu();
	Controller->Possess(Player);
	Player->PawnClientRestart();
	Player->GetCharacterMovement()->DisableMovement();
	auto Tick = [&] { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f / 30.f); };
	Tick();
	auto* Binding = Player->InputComponent->KeyBindings.FindByPredicate([](const FInputKeyBinding& Key) {
		return Key.Chord.Key == EKeys::V && Key.KeyEvent == IE_Pressed;
	});
	if (!TestNotNull(TEXT("V press bound on playable character"), Binding)) return false;
	auto* Rig = Player->Railgun.Get();
	if (!TestNotNull(TEXT("Equipped weapon"), Rig)) return false;
	TestTrue(TEXT("Starts in first person"), Player->GetFirstPersonCameraComponent()->IsActive() && !Player->IsThirdPerson());
	Binding->KeyDelegate.Execute(EKeys::V);
	Tick();
	FMinimalViewInfo View;
	Player->CalcCamera(0.f, View);
	auto* Arm = Player->FindComponentByClass<USpringArmComponent>();
	if (!TestNotNull(TEXT("Third-person spring arm"), Arm)) return false;
	TestTrue(TEXT("View behind the body"), View.Location.X < Player->GetActorLocation().X - 200.f);
	TestTrue(TEXT("Full body visible"), !Player->GetMesh()->bOwnerNoSee && Player->GetMesh()->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::None);
	TestTrue(TEXT("Weapon hold layered over locomotion"), IsValid(Cast<UThirdPersonWeaponPose>(Player->GetMesh()->GetPostProcessInstance())));
	TestTrue(TEXT("Only one set of arms"), Rig->Arms->bHiddenInGame);

	auto* Wall = World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(8.f, 200.f, 200.f));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->RegisterComponent();
	Wall->SetActorLocation(Player->GetActorLocation() - FVector(115.f, 0, 0));
	Tick();
	Player->CalcCamera(0.f, View);
	TestTrue(TEXT("Camera retracts in front of wall"), Arm->IsCollisionFixApplied() && View.Location.X > Wall->GetActorLocation().X);
	Wall->SetActorEnableCollision(false);
	Tick();
	Tick();
	Player->CalcCamera(0.f, View);
	TestTrue(TEXT("Camera extends after obstruction clears"), !Arm->IsCollisionFixApplied()
		&& View.Location.Equals(Arm->GetUnfixedCameraPosition(), 1.f)
		&& FVector::Distance(View.Location, Arm->GetComponentLocation()) >= Arm->TargetArmLength);

	const FVector AimPoint = View.Location + View.Rotation.Vector() * 1800.f;
	Wall->SetActorLocation(AimPoint);
	Wall->SetActorEnableCollision(true);
	Player->EquipRailgun();
	Tick();
	Player->CalcCamera(0.f, View);
	TestTrue(TEXT("Railgun fires from third person"), Rig->Fire());
	FHitResult Crosshair;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PerspectiveTestAim), false, Player);
	TestTrue(TEXT("Crosshair sees the target"), World->LineTraceSingleByChannel(Crosshair,
		View.Location, View.Location + View.Rotation.Vector() * 2000.f, ECC_GameTraceChannel1, Query));
	TestEqual(TEXT("Shot reaches the crosshair from the player's muzzle"), Rig->LastShotEnd, Crosshair.ImpactPoint, 3.f);
	Wall->Destroy();

	Player->EquipRailgun(); Player->EquipPurpleRailgun(); Player->EquipBFG();
	Player->EquipBomb(); Player->EquipSleepSignal(); Player->EquipElectricTether();
	Player->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	for (const auto Slot : { EPlayerWeapon::Default, EPlayerWeapon::Railgun, EPlayerWeapon::BFG,
		EPlayerWeapon::ElectricTether, EPlayerWeapon::PurpleRailgun, EPlayerWeapon::SleepSignal, EPlayerWeapon::Bomb })
	{
		TestTrue(TEXT("Slot selects in third person"), Player->SelectWeapon(Slot));
		TestTrue(TEXT("World weapon attached to visible hand"), Rig->WeaponMesh->GetAttachParent() == Player->GetMesh()
			&& Rig->WeaponMesh->GetAttachSocketName() == TEXT("HandGrip_R") && Rig->WeaponMesh->GetStaticMesh());
		TestTrue(TEXT("World muzzle is unscaled"), Rig->GetVisualMuzzleLocation().Equals(Rig->Muzzle->GetComponentLocation(), .01f));
		Tick(); Tick();
		auto InWeaponSpace = [&](FName Bone)
		{
			return Rig->WeaponMesh->GetComponentTransform().InverseTransformPosition(Player->GetMesh()->GetSocketLocation(Bone));
		};
		const FVector Palm = InWeaponSpace(TEXT("middle_01_l"));
		if (Slot == EPlayerWeapon::SleepSignal || Slot == EPlayerWeapon::Bomb)
		{
			const FVector OtherPalm = InWeaponSpace(TEXT("middle_01_r"));
			TestTrue(TEXT("Throwable is centred between opposing palms"), FMath::Abs(Palm.X + OtherPalm.X) < 2. && Palm.X * OtherPalm.X < 0.);
		}
		else
		{
			TestTrue(TEXT("Support knuckle rests on the fore-end"), Palm.Y >= 22. && Palm.Y <= 33. && Palm.Z > -8. && Palm.Z < 3.);
			Controller->SetControlRotation(FRotator(45, 0, 0));
			Tick(); Tick();
			TestTrue(TEXT("Looking up preserves support hand contact"), InWeaponSpace(TEXT("middle_01_l")).Equals(Palm, 1.));
			Controller->SetControlRotation(FRotator::ZeroRotator);
		}
	}
	Binding->KeyDelegate.Execute(EKeys::V);
	Tick();
	TestTrue(TEXT("First-person rig restored with same item"), Player->GetFirstPersonCameraComponent()->IsActive()
		&& !Player->IsThirdPerson() && Player->GetMesh()->bOwnerNoSee && !Rig->Arms->bHiddenInGame
		&& Player->GetActiveWeapon() == EPlayerWeapon::Bomb && Rig->WeaponMesh->GetAttachParent() == Rig->GetRootComponent());
	return true;
}
#endif
