#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "BallProjectile.h"
#include "CannonballProjectile.h"
#include "ChasingNPC.h"
#include "RadicalMikeNPC.h"
#include "WhisperFollower.h"
#include "NPCHealthBarComponent.h"
#include "NPCHealthBarWidget.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCProjectileHealthTest,
	"MyProject6.NPC.ProjectileHealthAndBars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCProjectileHealthTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->BeginPlay();
	APlayerController* Viewer = World->SpawnActor<APlayerController>();
	Viewer->SetAsLocalPlayerController();
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(-600, 0, 600), FRotator::ZeroRotator);
	Viewer->SetViewTarget(Camera);
	Viewer->PlayerCameraManager->UpdateCamera(0.0f);
	auto Advance = [World](float Duration)
	{
		const int32 Frames = FMath::CeilToInt(Duration * 60.0f);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Duration / Frames);
		}
	};
	auto Health = [](ACharacter* NPC)
	{
		return Cast<AChasingNPC>(NPC) ? CastChecked<AChasingNPC>(NPC)->GetCurrentHealth()
			: CastChecked<AWhisperFollower>(NPC)->GetCurrentHealth();
	};
	auto Dead = [](ACharacter* NPC)
	{
		return Cast<AChasingNPC>(NPC) ? CastChecked<AChasingNPC>(NPC)->IsDead()
			: CastChecked<AWhisperFollower>(NPC)->IsDead();
	};
	UClass* MikeClass = LoadClass<ARadicalMikeNPC>(nullptr,
		TEXT("/Game/Characters/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C"));
	UClass* WhisperClass = LoadClass<AWhisperFollower>(nullptr,
		TEXT("/Game/Characters/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"));
	if (!TestNotNull(TEXT("Saved Mike blueprint loads"), MikeClass)
		|| !TestNotNull(TEXT("Saved Whisper blueprint loads"), WhisperClass)) { return false; }
	const TArray<UClass*> Classes = { AChasingNPC::StaticClass(), WhisperClass, MikeClass };
	const float MaxHealths[] = { 100.0f, 150.0f, 200.0f };
	for (int32 Type = 0; Type < Classes.Num(); ++Type)
	{
		// Exercise yellow, red, and alternating shots through actual swept collisions.
		for (int32 Pattern = 0; Pattern < 3; ++Pattern)
		{
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ACharacter* NPC = World->SpawnActor<ACharacter>(Classes[Type], FVector(0, 0, 500), FRotator::ZeroRotator, Spawn);
			if (!TestNotNull(TEXT("NPC spawns"), NPC)) { return false; }
			NPC->GetCharacterMovement()->DisableMovement();
			if (AChasingNPC* Chaser = Cast<AChasingNPC>(NPC))
			{
				FindFProperty<FBoolProperty>(Chaser->GetClass(), TEXT("bSpawnReplacementsOnDeath"))->SetPropertyValue_InContainer(Chaser, false);
			}
			if (ARadicalMikeNPC* Mike = Cast<ARadicalMikeNPC>(NPC)) { Mike->bEnableShooting = false; }
			const FString Case = FString::Printf(TEXT("%s pattern %d"), *Classes[Type]->GetName(), Pattern);
			TestEqual(Case + TEXT(" starts with its type's full health"), Health(NPC), MaxHealths[Type]);
			TestTrue(Case + TEXT(" accepts damage"), NPC->CanBeDamaged());
			UNPCHealthBarComponent* Bar = NPC->FindComponentByClass<UNPCHealthBarComponent>();
			if (!TestNotNull(Case + TEXT(" has a health bar"), Bar)) { return false; }
			UNPCHealthBarWidget* Gauge = Cast<UNPCHealthBarWidget>(Bar->GetUserWidgetObject());
			if (!TestNotNull(Case + TEXT(" creates the gauge"), Gauge)) { return false; }
			TestTrue(Case + TEXT(" always faces the camera"), Bar->GetWidgetSpace() == EWidgetSpace::Screen);
			const FName HeadBone = Type == 2 ? TEXT("head_01") : TEXT("head");
			TestTrue(Case + TEXT(" sits just above the head"), FMath::IsNearlyEqual(
				Bar->GetComponentLocation().Z - NPC->GetMesh()->GetSocketLocation(HeadBone).Z, Type == 2 ? 60.0 : 26.0, 0.1));
			TestEqual(Case + TEXT(" begins with a full gauge"), Gauge->GetHealthPercent(), 1.0f);
			TestEqual(Case + TEXT(" ignores negative damage"), UGameplayStatics::ApplyDamage(NPC, -25, nullptr, nullptr, nullptr), 0.0f);
			float Expected = MaxHealths[Type];
			int32 Hits = 0;
			while (Expected > 0.0f)
			{
				const bool bRed = Pattern == 1 || (Pattern == 2 && Hits % 2 == 1);
				ABallProjectile* Shot = World->SpawnActor<ABallProjectile>(
					bRed ? ACannonballProjectile::StaticClass() : ABallProjectile::StaticClass(),
					FVector(-250, 0, 500), FRotator::ZeroRotator, Spawn);
				if (!TestNotNull(Case + TEXT(" projectile spawns"), Shot)) { return false; }
				Advance(0.2f);
				Expected = FMath::Max(0.0f, Expected - (bRed ? 50.0f : 25.0f));
				++Hits;
				TestTrue(Case + TEXT(" consumes each projectile on impact"), Shot->IsActorBeingDestroyed());
				TestEqual(Case + TEXT(" applies exactly one dose of impact damage"), Health(NPC), Expected);
				TestEqual(Case + TEXT(" gauge tracks health"), Gauge->GetHealthPercent(), Expected / MaxHealths[Type]);
				TestEqual(Case + TEXT(" dies only at zero"), Dead(NPC), Expected == 0.0f);
				TestEqual(Case + TEXT(" gauge is visible until death"), Bar->IsVisible(), Expected > 0.0f);
				if (Hits == 1) { TestFalse(Case + TEXT(" survives the first hit"), Dead(NPC)); }
			}
			TestFalse(Case + TEXT(" stops blocking at death"), NPC->GetActorEnableCollision());
			TestNull(Case + TEXT(" stops following at death"), NPC->GetController());
			TestEqual(Case + TEXT(" ignores further hits"), UGameplayStatics::ApplyDamage(NPC, 50, nullptr, nullptr, nullptr), 0.0f);
			TestFalse(Case + TEXT(" remains for its death animation"), NPC->IsActorBeingDestroyed());
			if (Type == 1)
			{
				UAnimSingleNodeInstance* Animation = NPC->GetMesh()->GetSingleNodeInstance();
				if (TestNotNull(TEXT("Whisper plays a death sequence"), Animation))
				{
					TestEqual(TEXT("Whisper uses its own death animation"), Animation->GetCurrentAsset()->GetFName(), FName(TEXT("Anim_Whisper_Death")));
				}
				Advance(NPC->GetLifeSpan() + 0.1f);
				TestTrue(TEXT("Whisper is removed after the death hold"), NPC->IsActorBeingDestroyed());
			}
			else { NPC->Destroy(); }
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCHealthBarVisibilityTest,
	"MyProject6.NPC.HealthBarVisibilityAndColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FNPCHealthBarVisibilityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->BeginPlay();
	APlayerController* Viewer = World->SpawnActor<APlayerController>();
	Viewer->SetAsLocalPlayerController();
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(-600, 0, 160), FRotator::ZeroRotator);
	Viewer->SetViewTarget(Camera);
	Viewer->PlayerCameraManager->UpdateCamera(0.0f);
	AChasingNPC* NPC = World->SpawnActor<AChasingNPC>(FVector(0, 0, 100), FRotator::ZeroRotator);
	NPC->GetCharacterMovement()->DisableMovement();
	UNPCHealthBarComponent* Bar = NPC->FindComponentByClass<UNPCHealthBarComponent>();
	UNPCHealthBarWidget* Gauge = Cast<UNPCHealthBarWidget>(Bar->GetUserWidgetObject());
	if (!TestNotNull(TEXT("Health bar widget exists"), Gauge)) { return false; }
	auto Advance = [World]()
	{
		for (int32 Frame = 0; Frame < 3; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.0f / 60.0f); }
	};
	Advance();
	TestEqual(TEXT("Bar is thinner"), Bar->GetDrawSize(), FVector2D(64.0f, 7.0f));
	TestEqual(TEXT("Original green remains the default"), Gauge->GetBarColor(), FLinearColor(0.04f, 0.9f, 0.08f, 1.0f));
	TestTrue(TEXT("Visible character has a visible bar"), Bar->IsVisible());
	Gauge->TakeWidget();
	const FLinearColor Purple(0.55f, 0.08f, 1.0f, 1.0f);
	Bar->SetBarColor(Purple);
	TestEqual(TEXT("Runtime color change reaches the live widget"), Gauge->GetBarColor(), Purple);
	Bar->BarColor = FLinearColor::Yellow;
	Advance();
	TestEqual(TEXT("Details/property edits reach the widget"), Gauge->GetBarColor(), FLinearColor::Yellow);

	AActor* Wall = World->SpawnActor<AActor>();
	UBoxComponent* Shape = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Shape);
	Shape->SetBoxExtent(FVector(20, 150, 300));
	Shape->SetCollisionProfileName(TEXT("BlockAll"));
	Shape->RegisterComponent();
	Wall->SetActorLocation(FVector(-300, 0, 100));
	Advance();
	TestFalse(TEXT("Wall hides the bar when it hides the character"), Bar->IsVisible());
	TestTrue(TEXT("Occluded bars keep checking visibility"), Bar->IsComponentTickEnabled());
	UGameplayStatics::ApplyDamage(NPC, 25, nullptr, nullptr, nullptr);
	TestFalse(TEXT("Taking damage behind cover does not reveal the bar"), Bar->IsVisible());
	TestEqual(TEXT("Health still updates while hidden"), Gauge->GetHealthPercent(), 0.75f);

	Wall->SetActorLocation(FVector(-300, 400, 100));
	Advance();
	TestTrue(TEXT("Bar reappears when the wall no longer blocks the character"), Bar->IsVisible());
	// Cover the body but leave only the floating bar above cover: it must stay hidden.
	const FVector Head = NPC->GetMesh()->GetSocketLocation(TEXT("head"));
	FVector View;
	FRotator ViewRotation;
	Viewer->GetPlayerViewPoint(View, ViewRotation);
	const float WallTop = FMath::Lerp((View.Z + Head.Z) * 0.5, (View.Z + Bar->GetComponentLocation().Z) * 0.5, 0.5f);
	Shape->SetBoxExtent(FVector(20, 150, 150));
	Wall->SetActorLocation(FVector(-300, 0, WallTop - 150));
	Advance();
	TestFalse(TEXT("A bar peeking above cover cannot reveal an invisible NPC"), Bar->IsVisible());
	Wall->AddActorWorldOffset(FVector(0, 0, -80));
	Advance();
	TestTrue(TEXT("A visible head above partial cover keeps the bar visible"), Bar->IsVisible());
	Wall->Destroy();
	Advance();
	TestTrue(TEXT("Removing cover restores visibility"), Bar->IsVisible());
	NPC->GetMesh()->SetVisibility(false);
	Advance();
	TestFalse(TEXT("Hidden character mesh also hides its bar"), Bar->IsVisible());
	NPC->GetMesh()->SetVisibility(true);
	Advance();
	TestTrue(TEXT("Showing the character restores its bar"), Bar->IsVisible());
	Camera->SetActorRotation(FRotator(0, 180, 0));
	Advance();
	TestFalse(TEXT("Character behind the camera has no visible bar"), Bar->IsVisible());
	return true;
}
#endif
