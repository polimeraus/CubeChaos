#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "GameOverMenuTestController.h"
#include "MyProject6Character.h"
#include "EnergyHUDWidget.h"
#include "SPlayerDamageFeedback.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/Layout/SBorder.h"
#include <limits>

namespace PlayerFeedbackTest
{
struct FWorld
{
    UWorld* World;
    AGameOverMenuTestController* Controller;
    AMyProject6Character* Player;
    FWorld()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
        World->InitializeActorsForPlay(FURL()); World->BeginPlay();
        Controller=World->SpawnActor<AGameOverMenuTestController>();
        Controller->InitializeTestMenu();
        auto* Class=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
        Player=World->SpawnActor<AMyProject6Character>(Class);
        Controller->Possess(Player);
        Player->SetActorTickEnabled(false);
    }
    ~FWorld()
    {
        World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    void Advance(float Seconds)
    {
        for (int32 I=0,N=FMath::CeilToInt(Seconds*60);I<N;++I) { ++GFrameCounter; World->Tick(LEVELTICK_All,Seconds/N); }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerDamageFeedbackTest,"MyProject6.Gameplay.PlayerDamageFeedback.Behavior",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPlayerDamageFeedbackTest::RunTest(const FString&)
{
    PlayerFeedbackTest::FWorld T;
    auto* HUD=T.Controller->GetTestMenu();
    const TSharedRef<SWidget> Root=HUD->TakeWidget();
    if (!TestTrue(TEXT("Damage overlay is constructed"),HUD->DamageFeedback.IsValid())) return false;
    auto& Feedback=*HUD->DamageFeedback;
    TestEqual(TEXT("HUD starts without a damage flash"),Feedback.Peak,0.f);
    TestFalse(TEXT("Idle HUD has no feedback timer"),Feedback.bTimerActive);
    TestEqual(TEXT("Three saved human hurt clips are available"),T.Controller->PainSounds.Num(),3);
    auto* SavedControllerClass=LoadClass<AMyProject6PlayerController>(nullptr,
        TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonPlayerController.BP_FirstPersonPlayerController_C"));
    if (TestNotNull(TEXT("Saved player controller loads in a fresh process"),SavedControllerClass))
        TestEqual(TEXT("Saved Blueprint inherits the native hurt audio defaults"),SavedControllerClass->GetDefaultObject<AMyProject6PlayerController>()->PainSounds.Num(),3);
    for (USoundBase* Sound : T.Controller->PainSounds)
    {
        if (TestNotNull(TEXT("Saved hurt audio loads"),Sound))
            TestTrue(TEXT("Hurt audio stays shorter than 350 ms"),Sound->GetDuration()>.2f && Sound->GetDuration()<.35f);
    }
    T.Player->DrainEnergy(0); T.Player->DrainEnergy(-5);
    T.Player->DrainEnergy(std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("No damage does not trigger a grunt"),T.Controller->NextPainVariation,0);
    TestEqual(TEXT("Invalid input cannot corrupt energy"),T.Player->GetCurrentEnergy(),100.f);
    T.Player->DrainEnergy(10);
    TestEqual(TEXT("Actual energy drain triggers one sound"),T.Controller->NextPainVariation,1);
    TestTrue(TEXT("Damage triggers a red pulse"),Feedback.Peak>0.f && Feedback.Peak<=.90f);
    const double Cooldown=T.Controller->NextPainSoundTime;
    for (int32 I=0;I<60;++I) T.Player->DrainEnergy(.1f);
    TestEqual(TEXT("Many contact ticks cannot overlap or queue sounds"),T.Controller->NextPainVariation,1);
    TestEqual(TEXT("Contact does not postpone sound indefinitely"),T.Controller->NextPainSoundTime,Cooldown);
    TestTrue(TEXT("Repeated damage never makes the screen opaque"),Feedback.Peak<=.90f);
    TestTrue(TEXT("Pulse fades smoothly"),Feedback.OpacityAt(Feedback.StartedAt+.48)<Feedback.Peak*.3f);
    TestEqual(TEXT("Pulse fully disappears after its short fade"),Feedback.OpacityAt(Feedback.StartedAt+1.0),0.f);
    TestEqual(TEXT("Animation stops its active timer when finished"),Feedback.Animate(Feedback.StartedAt+1.0,0.f),EActiveTimerReturnType::Stop);
    TestFalse(TEXT("Finished feedback leaves no timer running"),Feedback.bTimerActive);
    T.Advance(.9f); T.Player->DrainEnergy(1);
    TestEqual(TEXT("A later hit selects the next voice variation"),T.Controller->NextPainVariation,2);
    TestTrue(TEXT("Only one reusable audio component is used"),T.Controller->PainAudioComponent!=nullptr);
    TestFalse(TEXT("Hurt audio is not positioned in the world"),T.Controller->PainAudioComponent->bAllowSpatialization);
    T.Controller->UpdateEnergyHUD(100,100);
    TestEqual(TEXT("HUD refresh alone does not play pain audio"),T.Controller->NextPainVariation,2);
    T.Player->DrainEnergy(1000);
    TestTrue(TEXT("Lethal damage retains the game-over flow"),T.Player->IsEnergyDepleted());
    TestEqual(TEXT("Game over clears the red flash"),Feedback.Peak,0.f);
    TestFalse(TEXT("Game over stops the short hurt voice"),T.Controller->PainAudioComponent->IsPlaying());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerDamageFeedbackRenderTest,"MyProject6.Gameplay.PlayerDamageFeedback.Rendering",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPlayerDamageFeedbackRenderTest::RunTest(const FString&)
{
    if (GUsingNullRHI) { AddInfo(TEXT("Visual capture needs an RHI.")); return true; }
    PlayerFeedbackTest::FWorld T;
    auto* HUD=T.Controller->GetTestMenu();
    auto Root=SNew(SBorder).Padding(0).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(.035f,.055f,.085f,1.f))[HUD->TakeWidget()];
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("PlayerFeedback/Preview");
    IFileManager::Get().MakeDirectory(*Directory,true);
    FWidgetRenderer Renderer(true);
    auto Capture=[&](const TCHAR* Name,FVector2D Size,float Age,float Damage)
    {
        HUD->DamageFeedback->Clear();
        HUD->NotifyEnergyDamage(Damage);
        HUD->DamageFeedback->StartedAt=FSlateApplication::Get().GetCurrentTime()-Age;
        UTextureRenderTarget2D* Target=Renderer.DrawWidget(Root,Size);
        if (!TestNotNull(TEXT("HUD renders"),Target)) return TArray<FColor>();
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(false);
        TestTrue(TEXT("HUD pixels are readable"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags));
        TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Target->SizeX,Target->SizeY,Pixels,PNG);
        TestTrue(TEXT("HUD preview is saved"),FFileHelper::SaveArrayToFile(PNG,*(Directory/Name)));
        return Pixels;
    };
    const auto Idle=Capture(TEXT("Idle.png"),FVector2D(1280,720),0,0);
    const auto Hit=Capture(TEXT("Hit.png"),FVector2D(1280,720),.02f,.15f);
    const auto Fading=Capture(TEXT("Fading.png"),FVector2D(1280,720),.36f,.15f);
    const auto Gone=Capture(TEXT("Gone.png"),FVector2D(1280,720),1.f,.15f);
    if (Idle.Num()==1280*720 && Hit.Num()==Idle.Num() && Fading.Num()==Idle.Num() && Gone.Num()==Idle.Num())
    {
        int32 Changes[4]={0,0,0,0};
        int32 CenterTinted=0;
        for (int32 Y=0;Y<720;++Y) for (int32 X=0;X<1280;++X)
        {
            const int32 I=Y*1280+X;
            if (Hit[I].R>Idle[I].R+3) ++Changes[(X>=640?1:0)+(Y>=360?2:0)];
            if (X>=448 && X<832 && Y>=252 && Y<468)
                {
                    if (Hit[I].R>Idle[I].R+3) ++CenterTinted;
                    if (Hit[I].G<Idle[I].G*.7f) { AddError(TEXT("Screen tint must preserve gameplay visibility")); return false; }
                }
        }
        TestTrue(TEXT("Damage is visible across the centre too"),CenterTinted>80000);
        for (int32 Count : Changes) TestTrue(TEXT("Every corner has visible red feedback"),Count>100);
        TestTrue(TEXT("Expired effect restores the original screen"),Idle==Gone);
    }
    Capture(TEXT("Small.png"),FVector2D(640,360),.02f,.15f);
    Capture(TEXT("Ultrawide.png"),FVector2D(2560,1080),.02f,.15f);
    return true;
}
#endif
