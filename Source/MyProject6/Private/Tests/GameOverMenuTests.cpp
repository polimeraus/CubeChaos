#include "GameOverMenuTestController.h"
#include "EnergyHUDWidget.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"

void AGameOverMenuTestController::BeginPlay()
{
	APlayerController::BeginPlay(); // A synthetic world has no viewport to mount the HUD into.
}

void AGameOverMenuTestController::InitializeTestMenu()
{
	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine);
	Player = LocalPlayer;
	LocalPlayer->PlayerController = this;
	GetGameInstance()->AddLocalPlayer(LocalPlayer, FPlatformUserId::CreateFromInternalId(0));
	SetAsLocalPlayerController();
	EnergyHUDWidget = CreateWidget<UEnergyHUDWidget>(this, UEnergyHUDWidget::StaticClass());
}

FString AGameOverMenuTestController::ConsoleCommand(const FString& Command, bool bWriteToLog)
{
	LastConsoleCommand = Command;
	++ConsoleCommandCount;
	return FString();
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "MyProject6Character.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	struct FGameOverTestWorld
	{
		UWorld* World;
		AGameOverMenuTestController* Controller;
		AMyProject6Character* Character;
		FGameOverTestWorld()
		{
			// Keep the map's PIE name, but isolate packages between tests in one process.
			static int32 WorldSerial = 0;
			UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/GameOverMenu/Run%d/UEDPIE_2_Entry"), ++WorldSerial));
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("UEDPIE_2_Entry"), Package);
			World->StreamingLevelsPrefix = TEXT("UEDPIE_2_");
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->SetGameInstance(NewObject<UGameInstance>(GEngine));
			World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			Controller = World->SpawnActor<AGameOverMenuTestController>();
			Controller->InitializeTestMenu();
			UClass* CharacterClass = LoadClass<AMyProject6Character>(nullptr,
				TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
			Character = CharacterClass ? World->SpawnActor<AMyProject6Character>(CharacterClass) : nullptr;
			if (Character) { Controller->Possess(Character); }
		}
		~FGameOverTestWorld()
		{
			GEngine->GetWorldContextFromWorldChecked(World).TravelURL.Reset();
			World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};

	void FindButtons(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SButton>>& Buttons)
	{
		if (Widget->GetType() == TEXT("SButton")) { Buttons.Add(StaticCastSharedRef<SButton>(Widget)); }
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index) { FindButtons(Children->GetChildAt(Index), Buttons); }
	}

	bool HasVisibleText(const TSharedRef<SWidget>& Widget, const FString& Text)
	{
		if (!Widget->GetVisibility().IsVisible()) { return false; }
		if (Widget->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(Text)) { return true; }
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (HasVisibleText(Children->GetChildAt(Index), Text)) { return true; }
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameOverMenuActionsTest, "MyProject6.Gameplay.GameOverMenu.PausedActionsAndRestart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameOverMenuActionsTest::RunTest(const FString& Parameters)
{
	FGameOverTestWorld TestWorld;
	if (!TestNotNull(TEXT("Actual player Blueprint loads"), TestWorld.Character)) { return false; }
	UEnergyHUDWidget* Menu = TestWorld.Controller->GetTestMenu();
	TSharedRef<SWidget> Root = Menu->TakeWidget();
	TestFalse(TEXT("Menu starts hidden during gameplay"), HasVisibleText(Root, TEXT("Yeniden Başla")));
	TestWorld.Controller->RestartAfterGameOver();
	TestWorld.Controller->QuitAfterGameOver();
	TestTrue(TEXT("Restart cannot trigger before game over"), GEngine->GetWorldContextFromWorldChecked(TestWorld.World).TravelURL.IsEmpty());
	TestEqual(TEXT("Quit cannot trigger before game over"), TestWorld.Controller->ConsoleCommandCount, 0);
	TestWorld.Character->DrainEnergy(100);
	TestTrue(TEXT("Energy depletion reveals the actual menu"), HasVisibleText(Root, TEXT("Yeniden Başla")));
	TestTrue(TEXT("Quit is visible"), HasVisibleText(Root, TEXT("Çık")));
	TestTrue(TEXT("Cursor is shown"), TestWorld.Controller->bShowMouseCursor);
	TestTrue(TEXT("World is paused at game over"), UGameplayStatics::IsGamePaused(TestWorld.World));
	TArray<TSharedRef<SButton>> Buttons;
	FindButtons(Root, Buttons);
	if (!TestEqual(TEXT("Restart, controls and quit are available"), Buttons.Num(), 3)) { return false; }
	TestTrue(TEXT("Initial keyboard focus targets restart"), Menu->GetGameOverFocusTarget() == Buttons[0]);
	Buttons[1]->SimulateClick();
	TestTrue(TEXT("Controls can be opened while paused"), HasVisibleText(Root, TEXT("Havadayken platform oluştur")));
	TestTrue(TEXT("Help never unpauses the dead character"), UGameplayStatics::IsGamePaused(TestWorld.World));
	Buttons[1]->SimulateClick();
	TestFalse(TEXT("Controls can be collapsed"), HasVisibleText(Root, TEXT("Havadayken platform oluştur")));
	Buttons[0]->SimulateClick();
	const FWorldContext& Context = GEngine->GetWorldContextFromWorldChecked(TestWorld.World);
	TestEqual(TEXT("Restart reloads the same map without its PIE prefix"), Context.TravelURL, FString(TEXT("Entry")));
	TestFalse(TEXT("Restart releases pause so level travel can complete"), UGameplayStatics::IsGamePaused(TestWorld.World));
	TestFalse(TEXT("Restart hides the pointer"), TestWorld.Controller->bShowMouseCursor);
	TestFalse(TEXT("Restart clears blocked movement input"), TestWorld.Controller->IsMoveInputIgnored());
	TestFalse(TEXT("Restart clears blocked look input"), TestWorld.Controller->IsLookInputIgnored());
	Buttons[2]->SimulateClick();
	TestEqual(TEXT("Another action cannot race a pending restart"), TestWorld.Controller->ConsoleCommandCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameOverMenuQuitTest, "MyProject6.Gameplay.GameOverMenu.QuitAndRepeatedClicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameOverMenuQuitTest::RunTest(const FString& Parameters)
{
	FGameOverTestWorld TestWorld;
	if (!TestNotNull(TEXT("Player loads"), TestWorld.Character)) { return false; }
	TestWorld.Character->DrainEnergy(100);
	TArray<TSharedRef<SButton>> Buttons;
	FindButtons(TestWorld.Controller->GetTestMenu()->TakeWidget(), Buttons);
	if (!TestEqual(TEXT("Three buttons are built"), Buttons.Num(), 3)) { return false; }
	Buttons[2]->SimulateClick();
	Buttons[2]->SimulateClick();
	TestWorld.Controller->ShowEnergyGameOver();
	Buttons[2]->SimulateClick();
	TestEqual(TEXT("Exit routes through Unreal's standard game/PIE quit path"), TestWorld.Controller->LastConsoleCommand, FString(TEXT("quit")));
	TestEqual(TEXT("Repeated clicks dispatch quit only once"), TestWorld.Controller->ConsoleCommandCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameOverMenuRenderingTest, "MyProject6.Gameplay.GameOverMenu.RenderingAndSmallScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGameOverMenuRenderingTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI) { AddInfo(TEXT("Menu image capture requires an RHI; run without -nullrhi.")); return true; }
	FGameOverTestWorld TestWorld;
	if (!TestNotNull(TEXT("Player loads"), TestWorld.Character)) { return false; }
	TestWorld.Character->DrainEnergy(100);
	UEnergyHUDWidget* Menu = TestWorld.Controller->GetTestMenu();
	Menu->ShowGameOver(134.0f);
	TSharedRef<SWidget> Root = Menu->TakeWidget();
	TestTrue(TEXT("Survival time is displayed as minutes and seconds"), HasVisibleText(Root, TEXT("02:14")));
	TArray<TSharedRef<SButton>> Buttons;
	FindButtons(Root, Buttons);
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("GameOverMenu/Preview");
	IFileManager::Get().MakeDirectory(*Directory, true);
	FWidgetRenderer Renderer(true);
	auto Capture = [&](const TCHAR* Name, FVector2D Size)
	{
		UTextureRenderTarget2D* Target = Renderer.DrawWidget(Root, Size);
		if (!TestNotNull(TEXT("Menu renders to a texture"), Target)) { return; }
		// SScaleBox invalidates its normalization prepass after seeing a new viewport
		// size. Capture settled layout, as displayed on the following gameplay frames.
		Renderer.DrawWidget(Target, Root, Size, 0.f);
		Renderer.DrawWidget(Target, Root, Size, 0.f);
		FlushRenderingCommands();
		TArray<FColor> Pixels;
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(false);
		TestTrue(TEXT("Rendered menu pixels are readable"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, ReadFlags));
		TArray64<uint8> PNG;
		FImageUtils::PNGCompressImageArray(Target->SizeX, Target->SizeY, Pixels, PNG);
		TestTrue(TEXT("Preview image is saved"), FFileHelper::SaveArrayToFile(PNG, *(Directory / Name)));
		for (const TSharedRef<SButton>& Button : Buttons)
		{
			const FGeometry& Geometry = Button->GetCachedGeometry();
			const FVector2D Position = Geometry.GetAbsolutePosition();
			const FVector2D Extent = Geometry.GetAbsoluteSize();
			TestTrue(TEXT("Every menu button fits the viewport"), Extent.X > 0 && Extent.Y > 0
				&& Position.X >= 0 && Position.Y >= 0 && Position.X + Extent.X <= Size.X + 1 && Position.Y + Extent.Y <= Size.Y + 1);
		}
	};
	Capture(TEXT("game_over.png"), FVector2D(1280, 720));
	Buttons[1]->SimulateClick();
	Capture(TEXT("game_over_controls.png"), FVector2D(1280, 720));
	Capture(TEXT("game_over_small.png"), FVector2D(640, 360));
	FlushRenderingCommands();
	return true;
}

#endif
