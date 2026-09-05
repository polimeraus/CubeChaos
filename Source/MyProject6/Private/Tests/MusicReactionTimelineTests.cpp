// Copyright Epic Games, Inc. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "MusicReactionClock.h"
#include "MusicReactionTrack.h"
#include "MusicReactionWorldSubsystem.h"
#include "ChunkedDestructiblePyramid.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "OnsetNRT.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMusicReactionClockTest,
	"MyProject6.Animation.MusicClock", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMusicReactionClockTest::RunTest(const FString& Parameters)
{
	FMusicReactionClock Clock;
	Clock.Reset(10.0, 100.0);
	Clock.Synchronize(10.1, 100.1);
	TestEqual(TEXT("A fresh audio callback does not gain an extra game frame"), Clock.GetPosition(100.1), 10.1, 0.00001);
	TestEqual(TEXT("Interpolate only time since the callback"), Clock.GetPosition(100.15), 10.15, 0.00001);
	Clock.SetPaused(true, 100.2);
	Clock.Synchronize(12.0, 102.0);
	TestEqual(TEXT("Pause freezes playback and ignores in-flight callbacks"), Clock.GetPosition(105.0), 10.2, 0.00001);
	Clock.SetPaused(false, 105.0);
	TestEqual(TEXT("Resume excludes the paused interval"), Clock.GetPosition(105.3), 10.5, 0.00001);
	Clock.Reset(3.0, 110.0);
	TestEqual(TEXT("Seek replaces the old clock anchor"), Clock.GetPosition(110.2), 3.2, 0.00001);
	Clock.Reset(0.0, 115.0);
	TestEqual(TEXT("Loop starts at zero without old elapsed time"), Clock.GetPosition(115.0), 0.0);
	// There is deliberately no game DeltaTime input: changing world time dilation cannot alter this clock.
	TestEqual(TEXT("A long frame advances real time correctly"), Clock.GetPosition(117.0), 2.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMusicReactionTimelineTest,
	"MyProject6.Animation.MusicTimeline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMusicReactionTimelineTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Actor = World->SpawnActor<AChunkedDestructiblePyramid>();
	auto* Music = World->GetSubsystem<UMusicReactionWorldSubsystem>();
	if (!TestNotNull(TEXT("Actor"), Actor) || !TestNotNull(TEXT("Music subsystem"), Music)) { return false; }
	Actor->Shape.BaseWidth = 640.0f;
	Actor->Shape.Height = 560.0f;
	Actor->Shape.PieceSize = 80.0f;
	Actor->GenerateFloor(false);
	Actor->bReactToMusic = true;
	Actor->MusicReactionGroup = TEXT("Pyramid");
	Music->ReactionActors.Add(Actor);
	auto* Track = NewObject<UMusicReactionTrack>();
	Track->Sound = NewObject<USoundWave>();
	Track->Sound->Duration = 30.0f;
	Track->BakedDuration = 30.0f;
	Track->BakedSoundPath = FSoftObjectPath(Track->Sound);
	Track->OnsetTimes = { 1.01f, 1.06f, 1.09f };
	Track->OnsetStrengths = { 0.2f, 0.9f, 0.4f };
	Track->BeatTimes = { 1.1f };
	Music->ActiveTrack = Track;
	TestTrue(TEXT("Valid baked timeline"), Track->IsBakedDataValid());
	Music->ProcessTimeline(1.11);
	TestEqual(TEXT("All due ambient events consumed"), Music->NextOnsetIndex, 3);
	TestTrue(TEXT("Strongest onset produces columns"), !Actor->ActiveMusicEqualizerColumns.IsEmpty());
	for (const auto& Column : Actor->ActiveMusicEqualizerColumns)
	{
		TestEqual(TEXT("Overlapping beat is merged with the stronger onset"), Column.Priority, 10);
		TestEqual(TEXT("Columns retain the real event timestamp, with no jitter"), Column.StartTime, 1.06, 0.00001);
	}
	const int32 Count = Actor->ActiveMusicEqualizerColumns.Num();
	Music->ProcessTimeline(1.12);
	TestEqual(TEXT("The same onset is never delivered twice"), Actor->ActiveMusicEqualizerColumns.Num(), Count);

	Actor->ResetMusicPlayback();
	for (int32 Index = 0; Index < 12; ++Index)
	{
		FMusicReactionEvent Event;
		Event.TimeSeconds = 2.0f + Index * 0.1f;
		Event.TargetGroup = TEXT("Pyramid");
		Event.Type = EMusicReactionEventType::Burst;
		Track->KeyPoints.Add(Event);
	}
	Track->GetSortedKeyPoints(Music->KeyPointTimeline);
	Music->ProcessTimeline(5.0);
	TestEqual(TEXT("Key point dispatch respects its per-frame budget"), Music->NextKeyPointIndex, 8);
	TestTrue(TEXT("Late critical cues remain visible"), !Actor->ActiveMusicEqualizerColumns.IsEmpty());
	for (const auto& Column : Actor->ActiveMusicEqualizerColumns)
	{
		TestEqual(TEXT("An expired key point receives a fresh catch-up pulse"), Column.StartTime, 5.0);
		TestEqual(TEXT("Key point priority survives the visual budget"), Column.Priority, 100);
	}
	Music->ProcessTimeline(5.01);
	TestEqual(TEXT("All remaining critical cues survive a hitch"), Music->NextKeyPointIndex, 12);
	Music->ProcessTimeline(5.02);
	TestEqual(TEXT("Completed critical cues do not repeat"), Music->NextKeyPointIndex, 12);

	Music->ResetTimeline(2.5f);
	TestEqual(TEXT("Seek skips earlier cues and includes the exact target time"), Music->NextKeyPointIndex, 5);
	TestTrue(TEXT("Seek clears visuals from the old position"), Actor->ActiveMusicEqualizerColumns.IsEmpty());
	Music->ResetTimeline();
	TestEqual(TEXT("Loop rewinds the key point cursor"), Music->NextKeyPointIndex, 0);
	Track->Sound->bLooping = true;
	Music->HandlePlaybackPercent(Track->Sound, 1.05f);
	TestEqual(TEXT("Native looping playback uses the fractional percentage"), Music->LastAudioSeconds, 1.5f, 0.0001f);
	TestEqual(TEXT("Native loop preserves all previous-loop cues"), Music->DeferredKeyPoints.Num(), 12);
	Music->ProcessTimeline(1.5);
	TestEqual(TEXT("Old-loop cues respect the same frame budget"), Music->NextDeferredKeyPoint, 8);
	Music->ProcessTimeline(1.51);
	TestTrue(TEXT("Old-loop backlog drains without losing its tail"), Music->DeferredKeyPoints.IsEmpty());
	Music->ResetTimeline();
	FMusicReactionEvent Cue;
	Cue.TargetGroup = TEXT("Other");
	Actor->ApplyMusicEvent(Cue, 0.0);
	TestTrue(TEXT("Targeted cue cannot affect a different actor group"), Actor->ActiveMusicEqualizerColumns.IsEmpty());
	Cue.TargetGroup = TEXT("Pyramid");
	Actor->bReactToOnsets = false;
	Actor->bReactToBPM = false;
	Actor->ApplyMusicEvent(Cue, 0.0);
	TestTrue(TEXT("Key points work without ambient onsets or beats"), !Actor->ActiveMusicEqualizerColumns.IsEmpty());
	Actor->SetReactToKeyPoints(false);
	TestFalse(TEXT("Disabling every source makes the actor idle"), Actor->IsActorTickEnabled());
	TestFalse(TEXT("All sources disabled is effectively music disabled"), Actor->IsMusicReactionEnabled());
	Music->HandleMusicFinished();
	TestFalse(TEXT("An early voice completion does not endlessly restart the track"), Music->bRestartQueued);
	TestNull(TEXT("Stop releases baked track"), Music->ActiveTrack.Get());
	TestTrue(TEXT("Stop excludes subsystem from ticking"), Music->GetTickableTickType() == ETickableTickType::Never);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMusicReactionBakeTest,
	"MyProject6.Animation.MusicBake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMusicReactionBakeTest::RunTest(const FString& Parameters)
{
	auto* Track = NewObject<UMusicReactionTrack>();
	Track->Sound = LoadObject<USoundWave>(nullptr, TEXT("/Game/Musics/nuriOp.nuriOp"));
	Track->SourceAnalysis = LoadObject<UOnsetNRT>(nullptr, TEXT("/Game/Musics/nuriOp_OnsetNRT.nuriOp_OnsetNRT"));
	if (!TestNotNull(TEXT("Sound available"), Track->Sound.Get()) || !TestNotNull(TEXT("NRT analysis available"), Track->SourceAnalysis.Get())) { return false; }
	FMusicReactionEvent Authored;
	Authored.Name = TEXT("AuthoredChorus");
	Authored.TimeSeconds = 12.0f;
	Track->KeyPoints.Add(Authored);
	FMusicTempoSection First;
	First.BPM = 120.0f;
	First.BeatOffsetSeconds = 0.25f;
	FMusicTempoSection Second;
	Second.StartTimeSeconds = 2.0f;
	Second.BPM = 60.0f;
	Track->TempoSections = { First, Second };
	TestTrue(TEXT("Bake completed data"), Track->BakeAnalysis());
	TestEqual(TEXT("Rebaking preserves authored key points"), Track->KeyPoints[0].Name, Authored.Name);
	TestEqual(TEXT("First beat has the configured offset"), Track->BeatTimes[0], 0.25f);
	TestEqual(TEXT("Tempo change starts a new section"), Track->BeatTimes[4], 2.0f);
	TestEqual(TEXT("New section uses its own BPM"), Track->BeatTimes[5], 3.0f);
	for (int32 Index = 1; Index < Track->SuggestedAccents.Num(); ++Index)
	{
		TestTrue(TEXT("Suggested accents remain sparse"), Track->SuggestedAccents[Index].TimeSeconds - Track->SuggestedAccents[Index - 1].TimeSeconds >= Track->SuggestedAccentSpacing);
	}
	Track->BeatTimes.Swap(0, 1);
	TestFalse(TEXT("Unsorted beat edits are rejected instead of silently losing events"), Track->IsBakedDataValid());
	Track->BeatTimes.Swap(0, 1);
	Track->BakedSoundPath.Reset();
	TestFalse(TEXT("A timeline baked for another sound is rejected"), Track->IsBakedDataValid());
	// No useful intervals must keep the fallback, rather than choosing the first histogram bin (80 BPM).
	Track->EstimatedBPM = 120.0f;
	Track->OnsetTimes = { 0.0f, 10.0f, 20.0f };
	Track->OnsetStrengths = { 1.0f, 1.0f, 1.0f };
	Track->EstimateTempoAndPhase();
	TestEqual(TEXT("Insufficient tempo evidence retains fallback"), Track->EstimatedBPM, 120.0f);
	return true;
}
#endif
