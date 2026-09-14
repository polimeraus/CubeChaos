#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "ChasingNPC.h"
#include "MyProject6Character.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
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
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGhoulContactTest, "MyProject6.NPC.GhoulContactClaws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGhoulContactTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
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
	UClass* PlayerClass = LoadClass<AMyProject6Character>(nullptr,
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMyProject6Character* Player = World->SpawnActor<AMyProject6Character>(PlayerClass, FVector(600, 0, 100), FRotator::ZeroRotator, Spawn);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);
	Player->GetCharacterMovement()->DisableMovement();
	AChasingNPC* NPC = World->SpawnActor<AChasingNPC>(FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
	FindFProperty<FBoolProperty>(NPC->GetClass(), TEXT("bSpawnReplacementsOnDeath"))->SetPropertyValue_InContainer(NPC, false);
	NPC->GetCharacterMovement()->DisableMovement();
	USkeletalMeshComponent* Mesh = NPC->GetMesh();
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->bEnableUpdateRateOptimizations = false;
	UClass* LocomotionClass = Mesh->GetAnimClass();
	const TArray<UMaterialInterface*> OriginalMaterials = Mesh->GetMaterials();
	Advance(0.1f);
	TestFalse(TEXT("Ghoul can chase before contact"), NPC->ShouldPauseChase());
	Player->SetActorLocation(FVector(120, 0, 100));
	Advance(0.1f);
	TestTrue(TEXT("Contact pauses Ghoul's chase"), NPC->ShouldPauseChase());
	TestTrue(TEXT("Ghoul contact still drains energy"), Player->GetCurrentEnergy() < Player->GetMaxEnergy());
	UAnimSingleNodeInstance* Animation = Mesh->GetSingleNodeInstance();
	if (!TestNotNull(TEXT("Contact starts a sequence"), Animation)) { return false; }
	UAnimSequence* Attack = Cast<UAnimSequence>(Animation->GetCurrentAsset());
	if (!TestNotNull(TEXT("Ghoul claw animation is loaded"), Attack)) { return false; }
	TestEqual(TEXT("Ghoul uses the adapted open claw swipe"), Attack->GetFName(), FName(TEXT("Anim_Ghoul_Claw")));
	const FVector FirstHand = Mesh->GetSocketLocation(TEXT("hand_r"));
	const FVector Position = NPC->GetActorLocation();
	Advance(0.35f);
	TestTrue(TEXT("Ghoul swipes its hand"), FVector::Dist(FirstHand, Mesh->GetSocketLocation(TEXT("hand_r"))) > 10);
	const double Height = Mesh->GetSocketLocation(TEXT("head")).Z - Mesh->GetSocketLocation(TEXT("foot_l")).Z;
	TestTrue(TEXT("Ghoul's animation stays upright and correctly scaled"), Height > 80 && Height < 240);
	TestTrue(TEXT("Ghoul stays planted during the swipe"), NPC->GetActorLocation().Equals(Position, 0.1f));
	Advance(Attack->GetPlayLength() + 0.2f);
	TestEqual(TEXT("Ghoul keeps swiping during contact"), Animation->GetCurrentAsset(), static_cast<UAnimationAsset*>(Attack));
	TestFalse(TEXT("Contact reuses the timer without enabling Ghoul's actor tick"), NPC->IsActorTickEnabled());
	Player->SetActorLocation(FVector(0, 120, 100));
	Advance(0.2f);
	TestTrue(TEXT("Ghoul turns toward a player moving around it"),
		FVector::DotProduct(NPC->GetActorForwardVector(), FVector(0, 1, 0)) > 0.95f);
	Player->SetActorLocation(FVector(600, 0, 100));
	const float EnergyAfterContact = Player->GetCurrentEnergy();
	Advance(0.3f);
	TestEqual(TEXT("Leaving Ghoul stops energy drain"), Player->GetCurrentEnergy(), EnergyAfterContact);
	TestFalse(TEXT("Leaving Ghoul releases its chase pause"), NPC->ShouldPauseChase());
	TestEqual(TEXT("Leaving contact restores animation blueprint mode"), Mesh->GetAnimationMode(), EAnimationMode::AnimationBlueprint);
	if (TestNotNull(TEXT("Ghoul locomotion instance is restored"), Mesh->GetAnimInstance()))
	{
		TestEqual(TEXT("Ghoul restores its original locomotion blueprint"), Mesh->GetAnimInstance()->GetClass(), LocomotionClass);
	}
	for (int32 Index = 0; Index < OriginalMaterials.Num(); ++Index)
	{
		TestEqual(TEXT("Ghoul's original materials return"), Mesh->GetMaterial(Index), OriginalMaterials[Index]);
	}
	NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Advance(0.4f);
	TestTrue(TEXT("Ghoul resumes chasing after contact"), NPC->GetVelocity().Size2D() > 10);
	NPC->GetCharacterMovement()->DisableMovement();
	Player->SetActorLocation(NPC->GetActorLocation() + FVector(120, 0, 0));
	Advance(0.1f);
	TestTrue(TEXT("Ghoul restarts swiping on new contact"), NPC->ShouldPauseChase());
	UGameplayStatics::ApplyDamage(NPC, NPC->GetCurrentHealth(), nullptr, nullptr, nullptr);
	const float EnergyAfterDeath = Player->GetCurrentEnergy();
	Advance(0.2f);
	TestTrue(TEXT("Ghoul can die during a swipe"), NPC->IsDead());
	TestEqual(TEXT("Ghoul death replaces its swipe"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(), FName(TEXT("MM_Death_Front_01")));
	TestEqual(TEXT("Dead Ghoul stops draining energy"), Player->GetCurrentEnergy(), EnergyAfterDeath);
	return true;
}
#endif
