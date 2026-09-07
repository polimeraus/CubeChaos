// Copyright Epic Games, Inc. All Rights Reserved.
#include "MusicReactionWorldSubsystem.h"
#include "Algo/BinarySearch.h"
#include "MusicReactionListener.h"
#include "GameFramework/Actor.h"
#include "Components/AudioComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"

UMusicReactionWorldSubsystem::UMusicReactionWorldSubsystem()
{
	SetTickableTickType(ETickableTickType::Never);
}

bool UMusicReactionWorldSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMusicReactionWorldSubsystem::RegisterReactionActor(AActor* Actor)
{
	const auto* Listener = Cast<IMusicReactionListener>(Actor);
	if (!Listener || Actor->GetWorld() != GetWorld() || !Listener->IsMusicReactionEnabled()) { return; }
	ReactionActors.AddUnique(Actor);
	if (!MusicComponent && !LoadHandle) { StartMusic(); }
}

void UMusicReactionWorldSubsystem::UnregisterReactionActor(AActor* Actor)
{
	ReactionActors.RemoveAll([Actor](const auto& Entry) { return !Entry.IsValid() || Entry.Get() == Actor; });
	if (ReactionActors.IsEmpty()) { StopMusic(); }
}

void UMusicReactionWorldSubsystem::Deinitialize()
{
	StopMusic();
	ReactionActors.Reset();
	Super::Deinitialize();
}

TStatId UMusicReactionWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMusicReactionWorldSubsystem, STATGROUP_Tickables);
}

ETickableTickType UMusicReactionWorldSubsystem::GetTickableTickType() const
{
	return MusicComponent && !ReactionActors.IsEmpty() ? Super::GetTickableTickType() : ETickableTickType::Never;
}

void UMusicReactionWorldSubsystem::SetMusicTrack(TSoftObjectPtr<UMusicReactionTrack> Track)
{
	StopMusic();
	TrackOverride = Track;
	StartMusic();
}

void UMusicReactionWorldSubsystem::StartMusic()
{
	if (!GetWorld() || IsRunningDedicatedServer() || ReactionActors.IsEmpty() || MusicComponent || LoadHandle ||
		!GEngine || !GEngine->UseSound()) { return; }
	const auto Track = TrackOverride.IsNull() ? GetDefault<UMusicReactionSettings>()->DefaultTrack : TrackOverride;
	if (Track.IsNull()) { return; }
	LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Track.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &UMusicReactionWorldSubsystem::FinishLoadingTrack));
}

void UMusicReactionWorldSubsystem::FinishLoadingTrack()
{
	if (ReactionActors.IsEmpty()) { StopMusic(); return; }
	const auto Track = TrackOverride.IsNull() ? GetDefault<UMusicReactionSettings>()->DefaultTrack : TrackOverride;
	ActiveTrack = Track.Get();
	LoadHandle.Reset();
	if (!ActiveTrack || !ActiveTrack->IsBakedDataValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Music reaction track is missing or needs Bake Analysis: %s"), *Track.ToString());
		StopMusic();
		return;
	}
	ActiveTrack->GetSortedKeyPoints(KeyPointTimeline);
	ResetTimeline();
	if (!CreatePlaybackComponent(0.0f)) { StopMusic(); }
}

bool UMusicReactionWorldSubsystem::CreatePlaybackComponent(float StartSeconds)
{
	DestroyPlaybackComponent();
	if (!ActiveTrack) { return false; }
	MusicComponent = UGameplayStatics::CreateSound2D(GetWorld(), ActiveTrack->Sound,
		FMath::Clamp(ActiveTrack->Volume, 0.0f, 1.0f), 1.0f, StartSeconds, nullptr, false, false);
	if (!MusicComponent) { return false; }
	// Bind before Play so the initial position and short sounds cannot be missed.
	MusicComponent->OnAudioFinished.AddDynamic(this, &UMusicReactionWorldSubsystem::HandleMusicFinished);
	MusicComponent->OnAudioPlaybackPercent.AddDynamic(this, &UMusicReactionWorldSubsystem::HandlePlaybackPercent);
	PlaybackClock.Reset(StartSeconds, FPlatformTime::Seconds());
	MusicComponent->Play(StartSeconds);
	RefreshPauseState();
	SetTickableTickType(ETickableTickType::Conditional);
	return true;
}

void UMusicReactionWorldSubsystem::DestroyPlaybackComponent()
{
	if (MusicComponent)
	{
		MusicComponent->OnAudioFinished.RemoveDynamic(this, &UMusicReactionWorldSubsystem::HandleMusicFinished);
		MusicComponent->OnAudioPlaybackPercent.RemoveDynamic(this, &UMusicReactionWorldSubsystem::HandlePlaybackPercent);
		MusicComponent->Stop();
		MusicComponent->DestroyComponent();
		MusicComponent = nullptr;
	}
}

void UMusicReactionWorldSubsystem::StopMusic()
{
	SetTickableTickType(ETickableTickType::Never);
	if (LoadHandle) { LoadHandle->CancelHandle(); LoadHandle.Reset(); }
	DestroyPlaybackComponent();
	ResetTimeline();
	ActiveTrack = nullptr;
	KeyPointTimeline.Empty();
	DeferredKeyPoints.Empty();
	bUserPaused = false;
	PlaybackClock.bPaused = false;
}

double UMusicReactionWorldSubsystem::GetPlaybackSeconds() const
{
	return ActiveTrack ? FMath::Clamp(PlaybackClock.GetPosition(FPlatformTime::Seconds()), 0.0,
		static_cast<double>(ActiveTrack->BakedDuration)) : 0.0;
}

double UMusicReactionWorldSubsystem::GetReactionSeconds() const
{
	return ActiveTrack ? FMath::Clamp(GetPlaybackSeconds() + GetDefault<UMusicReactionSettings>()->VisualOffsetSeconds,
		0.0, static_cast<double>(ActiveTrack->BakedDuration)) : 0.0;
}

void UMusicReactionWorldSubsystem::SetMusicPaused(bool bPaused)
{
	bUserPaused = bPaused;
	RefreshPauseState();
}

void UMusicReactionWorldSubsystem::RefreshPauseState()
{
	const bool bPaused = bUserPaused || (GetWorld() && GetWorld()->IsPaused());
	if (PlaybackClock.bPaused != bPaused) { PlaybackClock.SetPaused(bPaused, FPlatformTime::Seconds()); }
	if (MusicComponent) { MusicComponent->SetPaused(bPaused); }
}

void UMusicReactionWorldSubsystem::SeekMusic(float TimeSeconds)
{
	if (!ActiveTrack || !MusicComponent || !FMath::IsFinite(TimeSeconds)) { return; }
	const float Start = FMath::Clamp(TimeSeconds, 0.0f, FMath::Max(0.0f, ActiveTrack->BakedDuration - 0.001f));
	ResetTimeline(Start);
	// A new component isolates callbacks queued by the previous playback instance.
	if (!CreatePlaybackComponent(Start)) { StopMusic(); }
}

void UMusicReactionWorldSubsystem::ResetTimeline(float StartSeconds)
{
	++PlaybackGeneration;
	PlaybackClock.Reset(StartSeconds, FPlatformTime::Seconds());
	LastAudioSeconds = StartSeconds;
	LastAudioLoop = 0;
	DeferredKeyPoints.Reset();
	NextDeferredKeyPoint = 0;
	bReceivedPlaybackPercent = false;
	bRestartQueued = false;
	NextOnsetIndex = ActiveTrack ? Algo::LowerBound(ActiveTrack->OnsetTimes, StartSeconds) : 0;
	NextBeatIndex = ActiveTrack ? Algo::LowerBound(ActiveTrack->BeatTimes, StartSeconds) : 0;
	NextKeyPointIndex = Algo::LowerBoundBy(KeyPointTimeline, StartSeconds, &FMusicReactionEvent::TimeSeconds);
	for (const auto& Entry : ReactionActors)
	{
		if (auto* Listener = Cast<IMusicReactionListener>(Entry.Get())) { Listener->ResetMusicPlayback(); }
	}
}

void UMusicReactionWorldSubsystem::HandleMusicFinished()
{
	// Drain end-of-track cues on Tick before resetting a looping timeline.
	if (ActiveTrack)
	{
		// A culled/inaudible voice can finish immediately. Never spin a restart loop for it.
		if (GetPlaybackSeconds() + 0.25 < ActiveTrack->BakedDuration)
		{
			StopMusic();
			return;
		}
		PlaybackClock.Reset(ActiveTrack->BakedDuration, FPlatformTime::Seconds());
		bRestartQueued = true;
	}
}

void UMusicReactionWorldSubsystem::HandlePlaybackPercent(const USoundWave* PlayingSoundWave, float PlaybackPercent)
{
	if (!ActiveTrack || PlayingSoundWave != ActiveTrack->Sound || !FMath::IsFinite(PlaybackPercent) || PlaybackClock.bPaused) { return; }
	// The mixer reports cumulative percentages for looping waves (1.25 means loop 2, 25%).
	const bool bNativeLoop = ActiveTrack->Sound->bLooping;
	const int32 AudioLoop = bNativeLoop ? FMath::Max(0, FMath::FloorToInt(PlaybackPercent)) : 0;
	const float Position = (bNativeLoop ? FMath::Frac(FMath::Max(0.0f, PlaybackPercent)) :
		FMath::Clamp(PlaybackPercent, 0.0f, 1.0f)) * ActiveTrack->BakedDuration;
	if (AudioLoop > LastAudioLoop)
	{
		if (!ActiveTrack->bLoop)
		{
			MusicComponent->Stop();
			return;
		}
		// Carry old-loop cues into the next frame budget instead of discarding its tail.
		TArray<FMusicReactionEvent> Remaining;
		for (int32 Index = NextDeferredKeyPoint; Index < DeferredKeyPoints.Num(); ++Index) { Remaining.Add(DeferredKeyPoints[Index]); }
		for (int32 Index = NextKeyPointIndex; Index < KeyPointTimeline.Num(); ++Index) { Remaining.Add(KeyPointTimeline[Index]); }
		for (int32 Loop = LastAudioLoop + 1; Loop < AudioLoop; ++Loop) { Remaining.Append(KeyPointTimeline); }
		ResetTimeline();
		DeferredKeyPoints = MoveTemp(Remaining);
	}
	LastAudioLoop = AudioLoop;
	LastAudioSeconds = Position;
	bReceivedPlaybackPercent = true;
	PlaybackClock.Synchronize(Position, FPlatformTime::Seconds());
}

void UMusicReactionWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ReactionActors.RemoveAll([](const auto& Entry)
	{
		const auto* Listener = Cast<IMusicReactionListener>(Entry.Get());
		return !Listener || !Listener->IsMusicReactionEnabled();
	});
	if (ReactionActors.IsEmpty()) { StopMusic(); return; }
	if (!MusicComponent || !ActiveTrack) { return; }
	RefreshPauseState();
	if (PlaybackClock.bPaused) { return; }
	if (bRestartQueued)
	{
		const uint32 Generation = PlaybackGeneration;
		ProcessTimeline(ActiveTrack->BakedDuration);
		if (Generation != PlaybackGeneration || NextKeyPointIndex < KeyPointTimeline.Num()) { return; }
		if (!ActiveTrack->bLoop) { StopMusic(); return; }
		ResetTimeline();
		if (!CreatePlaybackComponent(0.0f)) { StopMusic(); }
		return;
	}
	if (MusicComponent->IsPlaying()) { ProcessTimeline(GetReactionSeconds()); }
}

void UMusicReactionWorldSubsystem::ProcessTimeline(double Position)
{
	if (!ActiveTrack) { return; }
	const uint32 Generation = PlaybackGeneration;
	// Critical cues retain their cursor when the frame budget is exhausted; none are dropped.
	int32 CueBudget = 8;
	while (DeferredKeyPoints.IsValidIndex(NextDeferredKeyPoint) && CueBudget > 0)
	{
		const FMusicReactionEvent Event = DeferredKeyPoints[NextDeferredKeyPoint++];
		// Its original timestamp belongs to the completed loop; restart only the catch-up visual.
		FMusicReactionEvent VisualEvent = Event;
		VisualEvent.TimeSeconds = static_cast<float>(Position);
		for (const auto& Entry : ReactionActors)
		{
			if (auto* Listener = Cast<IMusicReactionListener>(Entry.Get())) { Listener->ApplyMusicEvent(VisualEvent, Position); }
		}
		OnKeyPoint.Broadcast(Event);
		--CueBudget;
		if (Generation != PlaybackGeneration || PlaybackClock.bPaused) { return; }
	}
	if (NextDeferredKeyPoint == DeferredKeyPoints.Num()) { DeferredKeyPoints.Reset(); NextDeferredKeyPoint = 0; }
	while (KeyPointTimeline.IsValidIndex(NextKeyPointIndex) && KeyPointTimeline[NextKeyPointIndex].TimeSeconds <= Position && CueBudget-- > 0)
	{
		const FMusicReactionEvent Event = KeyPointTimeline[NextKeyPointIndex++];
		DispatchReaction(Event, Position);
		if (Generation != PlaybackGeneration || PlaybackClock.bPaused) { return; }
	}
	// Coalesce ambient onsets after a hitch; choose the strongest instead of the first weak one.
	const int32 End = Algo::UpperBound(ActiveTrack->OnsetTimes, Position);
	const int32 First = FMath::Max(NextOnsetIndex, Algo::LowerBound(ActiveTrack->OnsetTimes, Position - 0.12));
	int32 Best = INDEX_NONE;
	for (int32 Index = First; Index < End; ++Index)
	{
		if (Best == INDEX_NONE || ActiveTrack->OnsetStrengths[Index] > ActiveTrack->OnsetStrengths[Best]) { Best = Index; }
	}
	NextOnsetIndex = FMath::Max(NextOnsetIndex, End);
	if (Best != INDEX_NONE)
	{
		FMusicReactionEvent Event;
		Event.Type = EMusicReactionEventType::Onset;
		Event.TimeSeconds = ActiveTrack->OnsetTimes[Best];
		Event.Strength = ActiveTrack->OnsetStrengths[Best];
		Event.Duration = 0.0f;
		Event.Priority = 10;
		DispatchReaction(Event, Position);
	}
	const int32 BeatEnd = Algo::UpperBound(ActiveTrack->BeatTimes, Position);
	if (BeatEnd > NextBeatIndex && Position - ActiveTrack->BeatTimes[BeatEnd - 1] <= 0.10)
	{
		FMusicReactionEvent Event;
		Event.Type = EMusicReactionEventType::Beat;
		Event.TimeSeconds = ActiveTrack->BeatTimes[BeatEnd - 1];
		Event.Strength = 0.32f;
		Event.Duration = 0.0f;
		Event.Priority = 1;
		DispatchReaction(Event, Position);
	}
	NextBeatIndex = FMath::Max(NextBeatIndex, BeatEnd);
}

void UMusicReactionWorldSubsystem::DispatchReaction(const FMusicReactionEvent& Event, double Position)
{
	for (const auto& Entry : ReactionActors)
	{
		if (auto* Listener = Cast<IMusicReactionListener>(Entry.Get())) { Listener->ApplyMusicEvent(Event, Position); }
	}
	if (Event.IsKeyPoint()) { OnKeyPoint.Broadcast(Event); }
}
