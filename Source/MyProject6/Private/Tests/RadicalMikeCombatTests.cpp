#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "RadicalMikeNPC.h"
#include "RadicalBusterProjectile.h"
#include "MyProject6Character.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRadicalMikeCombatTest, "MyProject6.NPC.RadicalMikeCombat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRadicalMikeCombatTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->BeginPlay();
	auto Advance = [World](float Duration)
	{
		const int32 Frames = FMath::CeilToInt(Duration * 60.0f);
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Duration / Frames);
		}
	};
	auto Box = [World](FVector Location, FVector Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Shape = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Shape);
		Shape->SetBoxExtent(Extent);
		Shape->SetCollisionProfileName(TEXT("BlockAll"));
		Shape->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	};
	Box(FVector(0, 0, -50), FVector(5000, 5000, 50));
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* PlayerClass = LoadClass<AMyProject6Character>(nullptr,
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("Player blueprint loads"), PlayerClass)) { return false; }
	AMyProject6Character* Player = World->SpawnActor<AMyProject6Character>(PlayerClass, FVector(800, 0, 100), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("Player spawns"), Player)) { return false; }
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	Controller->Possess(Player);
	Player->GetCharacterMovement()->MaxWalkSpeed = 0;
	TestEqual(TEXT("Target lookup finds the actual player pawn"), UGameplayStatics::GetPlayerCharacter(World, 0), static_cast<ACharacter*>(Player));

	UClass* NPCClass = LoadClass<ARadicalMikeNPC>(nullptr,
		TEXT("/Game/Characters/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C"));
	if (!TestNotNull(TEXT("Saved RadicalMike blueprint inherits the armed native class"), NPCClass)) { return false; }
	ARadicalMikeNPC* NPC = World->SpawnActor<ARadicalMikeNPC>(NPCClass, FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("RadicalMike spawns"), NPC)) { return false; }
	FindFProperty<FBoolProperty>(NPC->GetClass(), TEXT("bSpawnReplacementsOnDeath"))->SetPropertyValue_InContainer(NPC, false);
	TestEqual(TEXT("Weapon uses the supplied mesh"), NPC->BusterWeapon->GetStaticMesh()->GetFName(), FName(TEXT("SM_RadicalBuster")));
	TestEqual(TEXT("Weapon follows the supplied right-arm socket"), NPC->BusterWeapon->GetAttachSocketName(), FName(TEXT("Buster_RSocket")));
	TestTrue(TEXT("Attachment socket exists on RadicalMike"), NPC->GetMesh()->DoesSocketExist(TEXT("Buster_RSocket")));
	TestEqual(TEXT("Weapon never blocks movement or its own shots"), NPC->BusterWeapon->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestTrue(TEXT("RunChase bone scaling cannot shrink the equipped weapon"), NPC->BusterWeapon->IsUsingAbsoluteScale());
	Advance(2.4f);
	TestEqual(TEXT("No damage before the first firing interval"), Player->GetCurrentEnergy(), 100.0f);
	Advance(0.2f);
	TestTrue(TEXT("Attack pauses the chase during the fire animation"), NPC->ShouldPauseChase());
	Advance(0.8f);
	TestEqual(TEXT("First projectile drains exactly ten energy on impact"), Player->GetCurrentEnergy(), 90.0f);
	TestFalse(TEXT("Chasing resumes after firing"), NPC->ShouldPauseChase());
	Advance(2.5f);
	TestEqual(TEXT("Second timed shot drains another ten energy"), Player->GetCurrentEnergy(), 80.0f);

	AActor* Wall = Box(FVector(400, 0, 150), FVector(25, 400, 150));
	Advance(3.0f);
	TestEqual(TEXT("Cover prevents ranged damage"), Player->GetCurrentEnergy(), 80.0f);
	Wall->Destroy();
	NPC->FireRange = 300;
	Advance(2.5f);
	TestEqual(TEXT("Out-of-range player is not fired on"), Player->GetCurrentEnergy(), 80.0f);
	NPC->FireRange = 2500;
	NPC->bEnableShooting = false;
	Advance(2.5f);
	TestEqual(TEXT("Shooting can be disabled"), Player->GetCurrentEnergy(), 80.0f);
	NPC->bEnableShooting = true;

	// Collision is checked by the moving projectile as well as the NPC's sight query.
	Wall = Box(FVector(400, 0, 150), FVector(25, 400, 150));
	const FTransform ShotTransform(FRotator::ZeroRotator, FVector(100, 0, 100));
	auto* BlockedShot = World->SpawnActorDeferred<ARadicalBusterProjectile>(ARadicalBusterProjectile::StaticClass(),
		ShotTransform, NPC, NPC, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	BlockedShot->ConfigureShot(10, 2200, 2500);
	BlockedShot->FinishSpawning(ShotTransform);
	Advance(0.6f);
	TestTrue(TEXT("A projectile colliding with cover is destroyed"), BlockedShot->IsActorBeingDestroyed());
	TestEqual(TEXT("A blocked projectile cannot drain energy through cover"), Player->GetCurrentEnergy(), 80.0f);
	Wall->Destroy();

	// Kill during the windup: both the pending shot and the animation recovery must be cancelled.
	for (int32 Frame = 0; Frame < 180 && !NPC->ShouldPauseChase(); ++Frame) { Advance(1.0f / 60.0f); }
	TestTrue(TEXT("Next attack begins after cover is removed"), NPC->ShouldPauseChase());
	UGameplayStatics::ApplyDamage(NPC, 100, nullptr, nullptr, nullptr);
	TestTrue(TEXT("Lethal hit kills the shooter"), NPC->IsDead());
	TestFalse(TEXT("Death disables the living actor tick until fade starts"), NPC->IsActorTickEnabled());
	Advance(3.0f);
	TestEqual(TEXT("Death during windup cancels all subsequent shots"), Player->GetCurrentEnergy(), 80.0f);
	TestFalse(TEXT("The weapon disappears with death"), NPC->BusterWeapon->IsVisible());

	const FTransform ShortTransform(FRotator::ZeroRotator, FVector(3000, 2000, 100));
	auto* ShortShot = World->SpawnActorDeferred<ARadicalBusterProjectile>(ARadicalBusterProjectile::StaticClass(),
		ShortTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	ShortShot->ConfigureShot(10, 1000, 100);
	ShortShot->FinishSpawning(ShortTransform);
	Advance(0.2f);
	TestTrue(TEXT("Missed projectiles expire at their configured range"), ShortShot->IsActorBeingDestroyed());
	return true;
}
#endif
