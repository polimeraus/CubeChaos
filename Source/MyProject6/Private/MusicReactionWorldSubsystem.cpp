// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicReactionWorldSubsystem.h"

#include "ChunkedDestructibleFloor.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "OnsetNRT.h"
#include "Sound/SoundWave.h"

namespace
{
	constexpr float PilotFallbackBPM = 120.0f;
	constexpr float PilotMinimumBPM = 80.0f;
	constexpr float PilotMaximumBPM = 180.0f;
	constexpr float TempoHistogramBinSize = 0.5f;
	constexpr int32 TempoLookAheadCount = 12;
	constexpr float OnsetCatchupWindowSeconds = 0.12f;
	constexpr float BeatCatchupWindowSeconds = 0.10f;
	constexpr int32 MaxEventsPerFrame = 4;
	constexpr float PilotMusicVolume = 0.45f;
	const TCHAR* PilotMusicPath = TEXT("/Game/Musics/nuriOp.nuriOp");
	const TCHAR* PilotOnsetPath = TEXT("/Game/Musics/nuriOp_OnsetNRT.nuriOp_OnsetNRT");
}

UMusicReactionWorldSubsystem::UMusicReactionWorldSubsystem()
{
	DetectedBPM = PilotFallbackBPM;
	SetTickableTickType(ETickableTickType::Never);
}

bool UMusicReactionWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UMusicReactionWorldSubsystem::RegisterReactionActor(AChunkedDestructibleFloor* Actor)
{
	if (!Actor || Actor->GetWorld() != GetWorld() || !Actor->IsMusicReactionEnabled())
	{
		return;
	}
	ReactionActors.AddUnique(Actor);
	if (!MusicComponent)
	{
		StartMusic();
	}
}

void UMusicReactionWorldSubsystem::UnregisterReactionActor(AChunkedDestructibleFloor* Actor)
{
	ReactionActors.RemoveAll([Actor](const TWeakObjectPtr<AChunkedDestructibleFloor>& Entry)
	{
		return !Entry.IsValid() || Entry.Get() == Actor;
	});
	if (ReactionActors.IsEmpty())
	{
		StopMusic();
	}
}

void UMusicReactionWorldSubsystem::Deinitialize()
{
	StopMusic();
	ReactionActors.Reset();
	OnsetTimes.Reset();
	OnsetStrengths.Reset();
	OnsetAnalysis = nullptr;
	MusicSound = nullptr;
	Super::Deinitialize();
}

TStatId UMusicReactionWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMusicReactionWorldSubsystem, STATGROUP_Tickables);
}

ETickableTickType UMusicReactionWorldSubsystem::GetTickableTickType() const
{
	return MusicComponent && !ReactionActors.IsEmpty()
		? Super::GetTickableTickType() : ETickableTickType::Never;
}

void UMusicReactionWorldSubsystem::StartMusic()
{
	if (!GetWorld() || IsRunningDedicatedServer() || ReactionActors.IsEmpty() || MusicComponent)
	{
		return;
	}

	MusicSound = LoadObject<USoundWave>(nullptr, PilotMusicPath);
	OnsetAnalysis = LoadObject<UOnsetNRT>(nullptr, PilotOnsetPath);
	if (!MusicSound)
	{
		UE_LOG(LogTemp, Warning, TEXT("Music reaction pilot could not load %s"), PilotMusicPath);
		return;
	}

	CacheAnalysis();
	ResetTimeline();
	MusicComponent = UGameplayStatics::SpawnSound2D(
		GetWorld(),
		MusicSound,
		PilotMusicVolume,
		1.0f,
		0.0f,
		nullptr,
		false,
		false);
	if (MusicComponent)
	{
		SetTickableTickType(ETickableTickType::Conditional);
		MusicComponent->OnAudioFinished.AddDynamic(this, &UMusicReactionWorldSubsystem::HandleMusicFinished);
		MusicComponent->OnAudioPlaybackPercent.AddDynamic(this, &UMusicReactionWorldSubsystem::HandlePlaybackPercent);
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Music reaction pilot started: %.2f BPM, %d cached onsets (%s)."),
		DetectedBPM,
		OnsetTimes.Num(),
		OnsetAnalysis ? TEXT("Audio Synesthesia") : TEXT("BPM fallback only"));
}

void UMusicReactionWorldSubsystem::StopMusic()
{
	SetTickableTickType(ETickableTickType::Never);
	if (MusicComponent)
	{
		MusicComponent->OnAudioFinished.RemoveDynamic(this, &UMusicReactionWorldSubsystem::HandleMusicFinished);
		MusicComponent->OnAudioPlaybackPercent.RemoveDynamic(this, &UMusicReactionWorldSubsystem::HandlePlaybackPercent);
		MusicComponent->Stop();
		MusicComponent->DestroyComponent();
		MusicComponent = nullptr;
	}
	OnsetTimes.Empty();
	OnsetStrengths.Empty();
	OnsetAnalysis = nullptr;
	MusicSound = nullptr;
	ResetTimeline();
}

void UMusicReactionWorldSubsystem::CacheAnalysis()
{
	OnsetTimes.Reset();
	OnsetStrengths.Reset();
	DetectedBPM = PilotFallbackBPM;
	BeatPhaseSeconds = 0.0f;

	if (!OnsetAnalysis || OnsetAnalysis->DurationInSeconds <= 0.0f)
	{
		return;
	}

	OnsetAnalysis->GetNormalizedChannelOnsetsBetweenTimes(
		0.0f,
		OnsetAnalysis->DurationInSeconds,
		0,
		OnsetTimes,
		OnsetStrengths);
	if (OnsetTimes.Num() != OnsetStrengths.Num())
	{
		OnsetTimes.Reset();
		OnsetStrengths.Reset();
		return;
	}

	EstimateTempoAndPhase();
}

void UMusicReactionWorldSubsystem::EstimateTempoAndPhase()
{
	if (OnsetTimes.Num() < 3)
	{
		return;
	}

	const int32 HistogramBinCount =
		FMath::FloorToInt((PilotMaximumBPM - PilotMinimumBPM) / TempoHistogramBinSize) + 1;
	TArray<double> Histogram;
	Histogram.Init(0.0, HistogramBinCount);

	for (int32 FirstIndex = 0; FirstIndex < OnsetTimes.Num(); ++FirstIndex)
	{
		const int32 LastIndex = FMath::Min(FirstIndex + TempoLookAheadCount, OnsetTimes.Num() - 1);
		for (int32 SecondIndex = FirstIndex + 1; SecondIndex <= LastIndex; ++SecondIndex)
		{
			const float Interval = OnsetTimes[SecondIndex] - OnsetTimes[FirstIndex];
			if (Interval <= 0.08f || Interval > 4.0f)
			{
				continue;
			}

			float CandidateBPM = 60.0f / Interval;
			while (CandidateBPM < PilotMinimumBPM)
			{
				CandidateBPM *= 2.0f;
			}
			while (CandidateBPM > PilotMaximumBPM)
			{
				CandidateBPM *= 0.5f;
			}
			if (CandidateBPM < PilotMinimumBPM || CandidateBPM > PilotMaximumBPM)
			{
				continue;
			}

			const int32 BinIndex = FMath::Clamp(
				FMath::RoundToInt((CandidateBPM - PilotMinimumBPM) / TempoHistogramBinSize),
				0,
				HistogramBinCount - 1);
			const double PairStrength =
				FMath::Max(static_cast<double>(OnsetStrengths[FirstIndex]), 0.05) *
				FMath::Max(static_cast<double>(OnsetStrengths[SecondIndex]), 0.05);
			Histogram[BinIndex] += PairStrength / FMath::Sqrt(static_cast<double>(SecondIndex - FirstIndex));
		}
	}

	int32 BestBin = 0;
	double BestScore = -1.0;
	for (int32 BinIndex = 0; BinIndex < Histogram.Num(); ++BinIndex)
	{
		const double SmoothedScore =
			Histogram[BinIndex] +
			(BinIndex > 0 ? Histogram[BinIndex - 1] * 0.5 : 0.0) +
			(BinIndex + 1 < Histogram.Num() ? Histogram[BinIndex + 1] * 0.5 : 0.0);
		if (SmoothedScore > BestScore)
		{
			BestScore = SmoothedScore;
			BestBin = BinIndex;
		}
	}
	DetectedBPM = PilotMinimumBPM + BestBin * TempoHistogramBinSize;

	const float BeatInterval = 60.0f / DetectedBPM;
	const int32 CandidateCount = FMath::Min(OnsetTimes.Num(), 256);
	float BestPhase = 0.0f;
	BestScore = -1.0;
	for (int32 CandidateIndex = 0; CandidateIndex < CandidateCount; ++CandidateIndex)
	{
		const float CandidatePhase = FMath::Fmod(OnsetTimes[CandidateIndex], BeatInterval);
		double PhaseScore = 0.0;
		for (int32 OnsetIndex = 0; OnsetIndex < OnsetTimes.Num(); ++OnsetIndex)
		{
			const float RawDistance = FMath::Abs(
				FMath::Fmod(OnsetTimes[OnsetIndex] - CandidatePhase + BeatInterval, BeatInterval));
			const float BeatDistance = FMath::Min(RawDistance, BeatInterval - RawDistance);
			const double Alignment = FMath::Exp(-FMath::Square(static_cast<double>(BeatDistance) / 0.045));
			PhaseScore += FMath::Max(static_cast<double>(OnsetStrengths[OnsetIndex]), 0.02) * Alignment;
		}
		if (PhaseScore > BestScore)
		{
			BestScore = PhaseScore;
			BestPhase = CandidatePhase;
		}
	}
	BeatPhaseSeconds = BestPhase;
}

void UMusicReactionWorldSubsystem::ResetTimeline()
{
	PlaybackSeconds = 0.0f;
	LastPlaybackPercent = 0.0f;
	NextOnsetIndex = 0;
	bReceivedPlaybackPercent = false;
	bRestartQueued = false;

	const float BeatInterval = 60.0f / FMath::Max(DetectedBPM, 1.0f);
	NextBeatIndex = FMath::Max(0, FMath::CeilToInt(-BeatPhaseSeconds / BeatInterval));
}

void UMusicReactionWorldSubsystem::HandleMusicFinished()
{
	bRestartQueued = true;
}

void UMusicReactionWorldSubsystem::HandlePlaybackPercent(
	const USoundWave* PlayingSoundWave,
	float PlaybackPercent)
{
	if (!MusicSound || PlayingSoundWave != MusicSound)
	{
		return;
	}

	// The audio renderer is the authority. Tick remains the fallback on platforms where this
	// callback is not available for a particular compression format.
	const float Duration = MusicSound->GetDuration();
	if (Duration > 0.0f)
	{
		if (bReceivedPlaybackPercent && PlaybackPercent + 0.5f < LastPlaybackPercent)
		{
			ResetTimeline();
		}
		PlaybackSeconds = FMath::Clamp(PlaybackPercent, 0.0f, 1.0f) * Duration;
		LastPlaybackPercent = PlaybackPercent;
		bReceivedPlaybackPercent = true;
	}
}

void UMusicReactionWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ReactionActors.RemoveAll([](const TWeakObjectPtr<AChunkedDestructibleFloor>& Entry)
	{
		return !Entry.IsValid() || !Entry->IsMusicReactionEnabled();
	});
	if (ReactionActors.IsEmpty())
	{
		StopMusic();
		return;
	}
	if (!MusicComponent)
	{
		return;
	}

	if (bRestartQueued)
	{
		ResetTimeline();
		MusicComponent->Play(0.0f);
	}
	if (!MusicComponent->IsPlaying())
	{
		return;
	}

	PlaybackSeconds += FMath::Max(DeltaTime, 0.0f);

	int32 DispatchedOnsets = 0;
	while (NextOnsetIndex < OnsetTimes.Num() && OnsetTimes[NextOnsetIndex] <= PlaybackSeconds)
	{
		const float EventTime = OnsetTimes[NextOnsetIndex];
		if (PlaybackSeconds - EventTime <= OnsetCatchupWindowSeconds && DispatchedOnsets < MaxEventsPerFrame)
		{
			DispatchReaction(OnsetStrengths[NextOnsetIndex], true);
			++DispatchedOnsets;
		}
		++NextOnsetIndex;
	}

	const float BeatInterval = 60.0f / FMath::Max(DetectedBPM, 1.0f);
	int32 DispatchedBeats = 0;
	for (;;)
	{
		const float BeatTime = BeatPhaseSeconds + NextBeatIndex * BeatInterval;
		if (BeatTime > PlaybackSeconds)
		{
			break;
		}
		if (PlaybackSeconds - BeatTime <= BeatCatchupWindowSeconds && DispatchedBeats < 2)
		{
			DispatchReaction(0.32f, false);
			++DispatchedBeats;
		}
		++NextBeatIndex;
	}
}

void UMusicReactionWorldSubsystem::DispatchReaction(float Strength, bool bIsOnset)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (int32 ActorIndex = ReactionActors.Num() - 1; ActorIndex >= 0; --ActorIndex)
	{
		if (AChunkedDestructibleFloor* Actor = ReactionActors[ActorIndex].Get())
		{
			Actor->TriggerMusicReaction(FMath::Clamp(Strength, 0.0f, 1.0f), bIsOnset);
		}
		else
		{
			ReactionActors.RemoveAtSwap(ActorIndex, 1, EAllowShrinking::No);
		}
	}
}
