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
	FMinimalViewInfo View;
	Player->CalcCamera(0.f, View);
	const FVector EyeLocation = View.Location;
	const FVector Start = Player->GetActorLocation();
	auto Press = [&] { Binding->KeyDelegate.Execute(EKeys::V); Player->CalcCamera(0.f, View); };
	auto Step = [&](int32 Count) { for (int32 I = 0; I < Count; ++I) Tick(); Player->CalcCamera(0.f, View); };
	auto Settle = [&] { Step(24); };
	auto Cycle = [&] { Press(); Settle(); };
	Press();
	TestTrue(TEXT("The first V starts at the current eye position"), Player->IsCameraTransitioning() && View.Location.Equals(EyeLocation, 1.));
	Step(1);
	const double FirstStep = FVector::Distance(View.Location, EyeLocation);
	Step(9);
	const FVector Midpoint = View.Location;
	TestTrue(TEXT("Camera eases out of the eyes and travels through intermediate positions"),
		FirstStep < 12. && FVector::Distance(Midpoint, EyeLocation) > 95. && FVector::Distance(Midpoint, EyeLocation) < 370.);
	const FVector AimOrigin = Player->GetWeaponEye();
	const FVector VisibleAim = (View.Location + View.Rotation.Vector() * 100000. - AimOrigin).GetSafeNormal();
	TestTrue(TEXT("Aim follows the moving viewpoint"), Player->GetWeaponAim(AimOrigin).Equals(VisibleAim, .001));
	Settle();
	auto* Arm = Player->FindComponentByClass<USpringArmComponent>();
	if (!TestNotNull(TEXT("Third-person spring arm"), Arm)) return false;
	TestTrue(TEXT("First V lands directly at the previous wide preset"),
		!Player->IsCameraTransitioning() && FMath::IsNearlyEqual(View.Location.X - Start.X, -545., 1.)
		&& FMath::IsNearlyEqual(View.Location.Z - Start.Z, 121., 1.) && FMath::IsNearlyEqual(View.Rotation.Pitch, -5., .1));
	TestTrue(TEXT("Full body visible"), !Player->GetMesh()->bOwnerNoSee && Player->GetMesh()->FirstPersonPrimitiveType == EFirstPersonPrimitiveType::None);
	TestTrue(TEXT("Weapon hold layered over locomotion"), IsValid(Cast<UThirdPersonWeaponPose>(Player->GetMesh()->GetPostProcessInstance())));
	TestTrue(TEXT("Only one set of arms"), Rig->Arms->bHiddenInGame);
	const FVector WideLocation = View.Location;
	const FRotator WideRotation = View.Rotation;
	Press();
	TestTrue(TEXT("The second V keeps the previous pose on the first frame"), View.Location.Equals(WideLocation, 1.));
	Step(10);
	TestTrue(TEXT("Pullback interpolates height, distance and pitch"), View.Location.X < WideLocation.X - 45.
		&& View.Location.X > WideLocation.X - 240. && View.Location.Z > WideLocation.Z + 20. && View.Rotation.Pitch < WideRotation.Pitch - 1.);
	Settle();
	const FVector PanoramicLocation = View.Location;
	TestTrue(TEXT("Second V settles at the panoramic preset"), Player->IsThirdPerson()
		&& PanoramicLocation.X < WideLocation.X - 250. && PanoramicLocation.Z > WideLocation.Z + 130. && View.Rotation.Pitch < WideRotation.Pitch - 6.);
	Press();
	Step(10);
	TestTrue(TEXT("Return to the eyes also passes through an intermediate position"),
		Player->IsCameraTransitioning() && View.Location.X > PanoramicLocation.X + 120. && View.Location.X < EyeLocation.X - 200.);
	Settle();
	TestTrue(TEXT("Third V returns to first person and stops probing"),
		!Player->IsThirdPerson() && Player->GetFirstPersonCameraComponent()->IsActive() && !Arm->IsComponentTickEnabled());
	Press(); Step(7);
	const FVector BeforeRetarget = View.Location;
	Press();
	TestTrue(TEXT("Rapid V starts the new move at the displayed pose"), View.Location.Equals(BeforeRetarget, 1.));
	Step(5);
	const FVector BeforeReverse = View.Location;
	Press();
	TestTrue(TEXT("Interrupting a pullback preserves position"), View.Location.Equals(BeforeReverse, 1.));
	Settle();
	TestTrue(TEXT("Rapid cycling reaches first person"), !Player->IsThirdPerson() && View.Location.Equals(EyeLocation, 2.));
	Cycle();

	auto* Wall = World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(8.f, 200.f, 200.f));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->RegisterComponent();
	Wall->SetActorLocation(Start - FVector(430.f, 0, 0));
	Tick();
	Player->CalcCamera(0.f, View);
	TestTrue(TEXT("With useful clearance the outside camera retracts in front of a wall"),
		Player->IsThirdPerson() && Arm->IsCollisionFixApplied() && View.Location.X > Wall->GetActorLocation().X);
	Wall->SetActorLocation(Start - FVector(115.f, 0, 0));
	Step(1);
	TestTrue(TEXT("The travelling lens stays on the player's side of the wall"),
		Player->IsCameraTransitioning() && View.Location.X > Wall->GetActorLocation().X + Box->GetScaledBoxExtent().X);
	Settle();
	TestTrue(TEXT("Tight clearance restores the eye camera and first-person presentation"),
		!Player->IsThirdPerson() && Player->GetFirstPersonCameraComponent()->IsActive() && Player->GetMesh()->bOwnerNoSee
		&& !Rig->Arms->bHiddenInGame && Rig->WeaponMesh->GetAttachParent() == Rig->Arms.Get() && Arm->IsComponentTickEnabled());
	Wall->SetActorLocation(Start - FVector(385.f, 0, 0));
	Tick();
	TestFalse(TEXT("Small changes at a doorway do not repeatedly flip the camera"), Player->IsThirdPerson());
	Player->SetActorLocation(Start + FVector(600, 0, 0));
	Step(8);
	TestTrue(TEXT("Automatic return outside is animated"), Player->IsCameraTransitioning() &&
		View.Location.X > WideLocation.X + 600. + 100. && View.Location.X < EyeLocation.X + 600. - 35.);
	Settle();
	TestTrue(TEXT("Leaving the wall automatically restores the wide view"),
		Player->IsThirdPerson() && View.Location.Equals(WideLocation + FVector(600, 0, 0), 1.));

	Wall->SetActorLocation(Start - FVector(115.f, 0, 0));
	Player->SetActorLocation(Start);
	Tick();
	Cycle();
	TestFalse(TEXT("Selecting panoramic indoors keeps the camera at the eyes"), Player->IsThirdPerson());
	Player->SetActorLocation(Start + FVector(600, 0, 0));
	Settle();
	TestTrue(TEXT("Leaving restores the panoramic view selected indoors"),
		Player->IsThirdPerson() && View.Location.Equals(PanoramicLocation + FVector(600, 0, 0), 1.));
	Player->SetActorLocation(Start);
	Settle();
	TestFalse(TEXT("Panoramic also falls back on re-entry"), Player->IsThirdPerson());
	Cycle();
	Wall->SetActorEnableCollision(false);
	Tick();
	TestTrue(TEXT("First person chosen indoors remains selected outside"),
		!Player->IsThirdPerson() && !Arm->IsComponentTickEnabled());
	Cycle();
	Cycle();
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
	TestTrue(TEXT("Railgun fires from wide third person"), Rig->Fire());
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
	Cycle();
	TestTrue(TEXT("First-person rig restored with same item"), Player->GetFirstPersonCameraComponent()->IsActive()
		&& !Player->IsThirdPerson() && Player->GetMesh()->bOwnerNoSee && !Rig->Arms->bHiddenInGame
		&& Player->GetActiveWeapon() == EPlayerWeapon::Bomb && Rig->WeaponMesh->GetAttachParent() == Rig->GetRootComponent());
	return true;
}
#endif
