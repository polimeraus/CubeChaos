#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "WhisperFollower.h"
#include "MyProject6Character.h"
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
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWhisperContactTest, "MyProject6.NPC.WhisperContactGlowAndClaws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWhisperContactTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->BeginPlay();
	auto Advance = [World](float Seconds, float FramesPerSecond = 60.0f)
	{
		const int32 Frames = FMath::CeilToInt(Seconds * FramesPerSecond);
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
	UClass* WhisperClass = LoadClass<AWhisperFollower>(nullptr,
		TEXT("/Game/Characters/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMyProject6Character* Player = World->SpawnActor<AMyProject6Character>(PlayerClass, FVector(600, 0, 100), FRotator::ZeroRotator, Spawn);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Player);
	Player->GetCharacterMovement()->DisableMovement();
	AWhisperFollower* NPC = World->SpawnActor<AWhisperFollower>(WhisperClass, FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Saved Whisper blueprint spawns"), NPC)) { return false; }
	NPC->GetCharacterMovement()->DisableMovement();
	USkeletalMeshComponent* Mesh = NPC->GetMesh();
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	TArray<UMaterialInterface*> Original = Mesh->GetMaterials();
	UMaterialInterface* Glow = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/NPC/Materials/M_NPCContactGlow.M_NPCContactGlow"));
	Advance(0.1f);
	TestFalse(TEXT("No claw attack before contact"), NPC->ShouldPauseFollow());
	TestEqual(TEXT("Whisper does not drain energy at a distance"), Player->GetCurrentEnergy(), Player->GetMaxEnergy());
	Player->SetActorLocation(FVector(120, 0, 100));
	Advance(0.1f);
	TestTrue(TEXT("Contact immediately drains 20 energy per second"),
		FMath::IsNearlyEqual(Player->GetCurrentEnergy(), Player->GetMaxEnergy() - 2.0f, 0.01f));
	TestTrue(TEXT("Touching the player starts the claw attack"), NPC->ShouldPauseFollow());
	for (int32 Index = 0; Index < Original.Num(); ++Index)
	{
		TestEqual(TEXT("Contact uses the same glow material as other NPCs"), Mesh->GetMaterial(Index), Glow);
	}
	UAnimSingleNodeInstance* Animation = Mesh->GetSingleNodeInstance();
	if (!TestNotNull(TEXT("Whisper has an animation instance"), Animation)) { return false; }
	UAnimSequence* Attack = Cast<UAnimSequence>(Animation->GetCurrentAsset());
	if (!TestNotNull(TEXT("A real claw sequence is playing"), Attack)) { return false; }
	TestEqual(TEXT("Uses Whisper's own claw attack"), Attack->GetFName(), FName(TEXT("Anim_Whisper_Attack2")));
	const FVector FirstLeft = Mesh->GetSocketLocation(TEXT("hand_l"));
	const FVector FirstRight = Mesh->GetSocketLocation(TEXT("hand_r"));
	const FVector Position = NPC->GetActorLocation();
	const float EnergyBeforeSwipe = Player->GetCurrentEnergy();
	Advance(0.35f);
	TestTrue(TEXT("Energy keeps draining during the swipe"),
		FMath::IsNearlyEqual(Player->GetCurrentEnergy(), EnergyBeforeSwipe - 7.0f, 0.01f));
	TestTrue(TEXT("Claw animation moves the hands"), FVector::Dist(FirstLeft, Mesh->GetSocketLocation(TEXT("hand_l"))) > 10
		|| FVector::Dist(FirstRight, Mesh->GetSocketLocation(TEXT("hand_r"))) > 10);
	const double Height = Mesh->GetSocketLocation(TEXT("head")).Z - Mesh->GetSocketLocation(TEXT("foot_l")).Z;
	TestTrue(TEXT("Imported attack keeps a correctly scaled upright pose"), Height > 80 && Height < 240);
	TestTrue(TEXT("Whisper stays planted during the swipe"), NPC->GetActorLocation().Equals(Position, 0.1f));
	Advance(Attack->GetPlayLength() + 0.2f);
	TestEqual(TEXT("Claw attack repeats while contact continues"), Animation->GetCurrentAsset(), static_cast<UAnimationAsset*>(Attack));
	Player->SetActorLocation(FVector(600, 0, 100));
	const float EnergyAfterContact = Player->GetCurrentEnergy();
	Advance(0.1f);
	TestEqual(TEXT("Leaving contact stops energy drain"), Player->GetCurrentEnergy(), EnergyAfterContact);
	TestFalse(TEXT("Leaving contact releases the follow pause"), NPC->ShouldPauseFollow());
	TestTrue(TEXT("Leaving contact restores locomotion"), Animation->GetCurrentAsset()->IsA<UBlendSpace>());
	for (int32 Index = 0; Index < Original.Num(); ++Index)
	{
		TestEqual(TEXT("Leaving contact restores the original material"), Mesh->GetMaterial(Index), Original[Index]);
	}
	NPC->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Advance(0.4f);
	TestTrue(TEXT("Whisper resumes following the player"), NPC->GetVelocity().Size2D() > 10);
	NPC->GetCharacterMovement()->DisableMovement();
	Player->SetActorLocation(NPC->GetActorLocation() + FVector(120, 0, 0));
	FObjectProperty* AttackProperty = FindFProperty<FObjectProperty>(NPC->GetClass(), TEXT("ContactAttackAnimation"));
	FFloatProperty* DrainProperty = FindFProperty<FFloatProperty>(NPC->GetClass(), TEXT("ContactEnergyDrainPerSecond"));
	AttackProperty->SetObjectPropertyValue_InContainer(NPC, nullptr);
	DrainProperty->SetPropertyValue_InContainer(NPC, 30.0f);
	const float EnergyWithoutAnimation = Player->GetCurrentEnergy();
	Advance(0.2f, 30.0f);
	TestTrue(TEXT("Configurable contact damage works without an attack animation at 30 FPS"),
		FMath::IsNearlyEqual(Player->GetCurrentEnergy(), EnergyWithoutAnimation - 6.0f, 0.01f));
	Advance(0.2f, 120.0f);
	TestTrue(TEXT("The same contact damage rate is maintained at 120 FPS"),
		FMath::IsNearlyEqual(Player->GetCurrentEnergy(), EnergyWithoutAnimation - 12.0f, 0.01f));
	AttackProperty->SetObjectPropertyValue_InContainer(NPC, Attack);
	DrainProperty->SetPropertyValue_InContainer(NPC, 20.0f);
	Advance(0.1f);
	TestEqual(TEXT("New contact restarts the claw animation"), Animation->GetCurrentAsset(), static_cast<UAnimationAsset*>(Attack));
	UGameplayStatics::ApplyDamage(NPC, NPC->GetCurrentHealth(), nullptr, nullptr, nullptr);
	const float EnergyAfterDeath = Player->GetCurrentEnergy();
	Advance(0.1f);
	TestEqual(TEXT("Dead Whisper cannot drain energy"), Player->GetCurrentEnergy(), EnergyAfterDeath);
	TestTrue(TEXT("Death during a swipe still kills Whisper"), NPC->IsDead());
	TestEqual(TEXT("Death replaces the attack sequence"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(), FName(TEXT("Anim_Whisper_Death")));
	for (int32 Index = 0; Index < Original.Num(); ++Index)
	{
		TestEqual(TEXT("Death removes contact glow"), Mesh->GetMaterial(Index), Original[Index]);
	}
	AWhisperFollower* Finisher = World->SpawnActor<AWhisperFollower>(WhisperClass, FVector(0, 600, 100), FRotator::ZeroRotator, Spawn);
	Finisher->GetCharacterMovement()->DisableMovement();
	Player->SetActorLocation(FVector(120, 600, 100));
	Player->DrainEnergy(Player->GetCurrentEnergy() - 0.1f);
	Advance(0.1f);
	TestTrue(TEXT("Whisper contact can deplete the player's energy"), Player->IsEnergyDepleted());
	TestEqual(TEXT("Lethal contact clamps player energy to zero"), Player->GetCurrentEnergy(), 0.0f);
	TestFalse(TEXT("Claw interaction stops when player energy is depleted"), Finisher->ShouldPauseFollow());
	return true;
}
#endif
