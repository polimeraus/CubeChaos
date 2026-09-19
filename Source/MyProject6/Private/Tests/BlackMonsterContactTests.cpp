#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WhisperFollower.h"
#include "ChasingAIController.h"
#include "MyProject6Character.h"
#include "NPCHealthBarComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlackMonsterContactTest, "MyProject6.NPC.BlackMonsterFollowAndClaws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBlackMonsterContactTest::RunTest(const FString& Parameters)
{
	UClass* NPCClass = LoadClass<AWhisperFollower>(nullptr,
		TEXT("/Game/Characters/black-monster/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C"));
	if (!TestNotNull(TEXT("Drag-and-drop Black Monster Blueprint loads"), NPCClass)) { return false; }
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->BeginPlay();
	auto Advance = [World](float Seconds)
	{
		const int32 Frames = FMath::CeilToInt(Seconds * 60.0f);
		for (int32 Frame = 0; Frame < Frames; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All, Seconds / Frames); }
	};
	AActor* Floor = World->SpawnActor<AActor>();
	UBoxComponent* Shape = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Shape);
	Shape->SetBoxExtent(FVector(5000, 5000, 50));
	Shape->SetCollisionProfileName(TEXT("BlockAll"));
	Shape->RegisterComponent();
	Floor->SetActorLocation(FVector(0, 0, -50));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* PlayerClass = LoadClass<AMyProject6Character>(nullptr,
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	AMyProject6Character* Player = World->SpawnActor<AMyProject6Character>(
		PlayerClass, FVector(750, 0, 100), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Playable character spawns"), Player)) { return false; }
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);
	Player->GetCharacterMovement()->DisableMovement();
	AWhisperFollower* NPC = World->SpawnActor<AWhisperFollower>(NPCClass, FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Black Monster spawns"), NPC)) { return false; }
	TestNotNull(TEXT("Spawned monster automatically gets the chasing AI"), Cast<AChasingAIController>(NPC->GetController()));
	USkeletalMeshComponent* Mesh = NPC->GetMesh();
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	TestEqual(TEXT("Monster has the imported mesh"), Mesh->GetSkinnedAsset()->GetFName(), FName(TEXT("SK_BlackMonster")));
	Advance(0.6f);
	TestTrue(TEXT("Monster follows the player without a NavMesh"), NPC->GetActorLocation().X > 100);
	TestTrue(TEXT("Locomotion is driven by movement"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->IsA<UBlendSpace>());
	TestEqual(TEXT("Player loses no energy outside claw range"), Player->GetCurrentEnergy(), Player->GetMaxEnergy());
	const double Height = Mesh->GetSocketLocation(TEXT("Head")).Z - Mesh->GetSocketLocation(TEXT("LeftToeBase")).Z;
	TestTrue(TEXT("Running pose stays upright and correctly scaled"), Height > 90 && Height < 250);
	const FVector Head = Mesh->GetSocketLocation(TEXT("Head"));
	UNPCHealthBarComponent* Bar = NPC->FindComponentByClass<UNPCHealthBarComponent>();
	TestTrue(TEXT("Health bar clears the monster's large head"), Bar->GetComponentLocation().Z > Head.Z + 50);
	const FVector Face = Mesh->GetSocketLocation(TEXT("headfront")) - Head;
	TestTrue(TEXT("Monster faces its direction of travel"), FVector::DotProduct(Face.GetSafeNormal2D(), NPC->GetActorForwardVector()) > 0.6f);
	NPC->GetCharacterMovement()->DisableMovement();
	const TArray<UMaterialInterface*> Materials = Mesh->GetMaterials();
	Player->SetActorLocation(NPC->GetActorLocation() + FVector(120, 0, 0));
	Advance(0.1f);
	TestTrue(TEXT("Close player triggers claws and pauses following"), NPC->ShouldPauseFollow());
	UAnimSingleNodeInstance* Instance = Mesh->GetSingleNodeInstance();
	UAnimSequence* Attack = Cast<UAnimSequence>(Instance->GetCurrentAsset());
	if (!TestNotNull(TEXT("Claw sequence plays"), Attack)) { return false; }
	TestEqual(TEXT("Uses Black Monster's own skeleton and claw sequence"), Attack->GetFName(), FName(TEXT("Anim_BlackMonster_Claw")));
	const FVector Hand = Mesh->GetSocketLocation(TEXT("RightHand"));
	const FVector LeftHand = Mesh->GetSocketLocation(TEXT("LeftHand"));
	const FVector PlantedLocation = NPC->GetActorLocation();
	const float Energy = Player->GetCurrentEnergy();
	Advance(0.45f);
	TestTrue(TEXT("Claw really sweeps through space"), FVector::Dist(Hand, Mesh->GetSocketLocation(TEXT("RightHand"))) > 35);
	TestTrue(TEXT("The second arm participates in the combo"), FVector::Dist(LeftHand, Mesh->GetSocketLocation(TEXT("LeftHand"))) > 35);
	TestTrue(TEXT("Attack stays planted"), NPC->GetActorLocation().Equals(PlantedLocation, 0.1));
	TestTrue(TEXT("Contact drains the same energy rate as other enemies"), FMath::IsNearlyEqual(Player->GetCurrentEnergy(), Energy - 9.0f, 0.05f));
	Advance(Attack->GetPlayLength() + 0.1f);
	TestEqual(TEXT("Attack loops while player stays close"), Instance->GetCurrentAsset(), static_cast<UAnimationAsset*>(Attack));
	Player->SetActorLocation(NPC->GetActorLocation() + FVector(650, 0, 0));
	const float EnergyAfterContact = Player->GetCurrentEnergy();
	Advance(0.1f);
	TestFalse(TEXT("Leaving range releases the attack"), NPC->ShouldPauseFollow());
	TestEqual(TEXT("Leaving range stops energy drain"), Player->GetCurrentEnergy(), EnergyAfterContact);
	TestTrue(TEXT("Leaving range restores locomotion"), Instance->GetCurrentAsset()->IsA<UBlendSpace>());
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
	{
		TestEqual(TEXT("Original monster material is restored"), Mesh->GetMaterial(Index), Materials[Index]);
	}
	NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Advance(0.4f);
	TestTrue(TEXT("Monster resumes following"), NPC->GetVelocity().Size2D() > 50);
	NPC->GetCharacterMovement()->DisableMovement();
	Player->SetActorLocation(NPC->GetActorLocation() + FVector(120, 0, 0));
	Advance(0.1f);
	UGameplayStatics::ApplyDamage(NPC, NPC->GetCurrentHealth(), nullptr, nullptr, nullptr);
	const float EnergyAfterDeath = Player->GetCurrentEnergy();
	Advance(0.1f);
	TestTrue(TEXT("Monster supports existing projectile damage and death"), NPC->IsDead());
	TestEqual(TEXT("Death replaces the claw loop"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(), FName(TEXT("Anim_BlackMonster_Death")));
	TestEqual(TEXT("Dead monster stops damaging the player"), Player->GetCurrentEnergy(), EnergyAfterDeath);
	return true;
}
#endif
