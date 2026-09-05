// Copyright Epic Games, Inc. All Rights Reserved.
#include "MusicReactionTrack.h"

#include "Algo/Sort.h"
#include "Sound/SoundWave.h"
#if WITH_EDITOR
#include "OnsetNRT.h"
#endif

namespace
{
	bool ValidTimes(const TArray<float>& Times, float Duration)
	{
		float Previous = -1.0f;
		for (float Time : Times)
		{
			if (!FMath::IsFinite(Time) || Time < 0.0f || Time < Previous || Time > Duration)
			{
				return false;
			}
			Previous = Time;
		}
		return true;
	}
}

bool UMusicReactionTrack::IsBakedDataValid() const
{
	if (!Sound || !FMath::IsFinite(BakedDuration) || BakedDuration <= 0.0f ||
		BakedSoundPath != FSoftObjectPath(Sound) ||
		!FMath::IsNearlyEqual(BakedDuration, Sound->GetDuration(), 0.02f) ||
		OnsetTimes.Num() != OnsetStrengths.Num() ||
		!ValidTimes(OnsetTimes, BakedDuration) || !ValidTimes(BeatTimes, BakedDuration))
	{
		return false;
	}
	for (float Strength : OnsetStrengths)
	{
		if (!FMath::IsFinite(Strength) || Strength < 0.0f || Strength > 1.0f)
		{
			return false;
		}
	}
	return true;
}

void UMusicReactionTrack::GetSortedKeyPoints(TArray<FMusicReactionEvent>& OutEvents) const
{
	OutEvents = KeyPoints;
	if (bUseSuggestedAccents)
	{
		OutEvents.Append(SuggestedAccents);
	}
	OutEvents.RemoveAll([this](const FMusicReactionEvent& Event)
	{
		return !Event.IsKeyPoint() || !FMath::IsFinite(Event.TimeSeconds) ||
			Event.TimeSeconds < 0.0f || Event.TimeSeconds >= BakedDuration ||
			!FMath::IsFinite(Event.Strength) || !FMath::IsFinite(Event.Duration);
	});
	OutEvents.StableSort([](const FMusicReactionEvent& A, const FMusicReactionEvent& B)
	{
		return A.TimeSeconds == B.TimeSeconds ? A.Priority > B.Priority : A.TimeSeconds < B.TimeSeconds;
	});
}

#if WITH_EDITOR
bool UMusicReactionTrack::BakeAnalysis()
{
	if (!Sound || !SourceAnalysis || SourceAnalysis->Sound != Sound ||
		!SourceAnalysis->Settings || !SourceAnalysis->Settings->bDownmixToMono ||
		!FMath::IsNearlyEqual(SourceAnalysis->DurationInSeconds, Sound->GetDuration(), 0.02f) ||
		SourceAnalysis->DurationInSeconds <= 0.0f)
	{
		UE_LOG(LogTemp, Error, TEXT("Music bake requires a completed mono onset analysis of the selected sound."));
		return false;
	}
	// Validate all editor inputs before replacing the previously baked result.
	TArray<FMusicTempoSection> Sections = TempoSections;
	Sections.Sort([](const auto& A, const auto& B) { return A.StartTimeSeconds < B.StartTimeSeconds; });
	float PreviousStart = -1.0f;
	for (const auto& Section : Sections)
	{
		if (!FMath::IsFinite(Section.StartTimeSeconds) || Section.StartTimeSeconds <= PreviousStart ||
			Section.StartTimeSeconds < 0.0f || Section.StartTimeSeconds >= Sound->GetDuration() ||
			!FMath::IsFinite(Section.BPM) || Section.BPM < 20.0f || Section.BPM > 400.0f ||
			!FMath::IsFinite(Section.BeatOffsetSeconds) || Section.BeatOffsetSeconds < 0.0f)
		{
			UE_LOG(LogTemp, Error, TEXT("Music bake has an invalid or duplicate tempo section."));
			return false;
		}
		PreviousStart = Section.StartTimeSeconds;
	}
	TArray<float> Times, Strengths;
	SourceAnalysis->GetNormalizedChannelOnsetsBetweenTimes(0.0f, SourceAnalysis->DurationInSeconds, 0, Times, Strengths);
	if (Times.Num() != Strengths.Num() || !ValidTimes(Times, Sound->GetDuration()))
	{
		return false;
	}
	for (float Strength : Strengths)
	{
		if (!FMath::IsFinite(Strength) || Strength < 0.0f || Strength > 1.0f)
		{
			return false;
		}
	}
	Modify();
	OnsetTimes = MoveTemp(Times);
	OnsetStrengths = MoveTemp(Strengths);
	BakedDuration = Sound->GetDuration();
	BakedSoundPath = FSoftObjectPath(Sound);
	EstimatedBPM = 120.0f;
	BeatPhaseSeconds = 0.0f;
	if (Sections.IsEmpty())
	{
		EstimateTempoAndPhase();
		FMusicTempoSection Section;
		Section.BPM = EstimatedBPM;
		Section.BeatOffsetSeconds = BeatPhaseSeconds;
		Sections.Add(Section);
	}
	else
	{
		EstimatedBPM = Sections[0].BPM;
		BeatPhaseSeconds = Sections[0].BeatOffsetSeconds;
	}
	BeatTimes.Reset();
	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		const auto& Section = Sections[Index];
		const double End = Sections.IsValidIndex(Index + 1) ? Sections[Index + 1].StartTimeSeconds : BakedDuration;
		const double First = Section.StartTimeSeconds + Section.BeatOffsetSeconds;
		for (int32 Beat = 0; First + Beat * (60.0 / Section.BPM) < End; ++Beat)
		{
			BeatTimes.Add(static_cast<float>(First + Beat * (60.0 / Section.BPM)));
		}
	}
	// Sparse, strongest-first acoustic accents. Authored key points are never overwritten.
	SuggestedAccents.Reset();
	TArray<int32> Candidates;
	const float Threshold = FMath::IsFinite(SuggestedAccentThreshold) ? FMath::Clamp(SuggestedAccentThreshold, 0.0f, 1.0f) : 0.8f;
	const float Spacing = FMath::IsFinite(SuggestedAccentSpacing) ? FMath::Max(SuggestedAccentSpacing, 1.0f) : 6.0f;
	for (int32 Index = 0; Index < OnsetTimes.Num(); ++Index)
	{
		if (OnsetStrengths[Index] >= Threshold) { Candidates.Add(Index); }
	}
	Candidates.Sort([this](int32 A, int32 B) { return OnsetStrengths[A] > OnsetStrengths[B]; });
	for (int32 Index : Candidates)
	{
		if (!SuggestedAccents.ContainsByPredicate([&](const auto& Event) { return FMath::Abs(Event.TimeSeconds - OnsetTimes[Index]) < Spacing; }))
		{
			FMusicReactionEvent Event;
			Event.TimeSeconds = OnsetTimes[Index];
			Event.Strength = OnsetStrengths[Index];
			Event.Name = TEXT("SuggestedAccent");
			Event.Priority = 20;
			SuggestedAccents.Add(Event);
		}
	}
	SuggestedAccents.Sort([](const auto& A, const auto& B) { return A.TimeSeconds < B.TimeSeconds; });
	MarkPackageDirty();
	return IsBakedDataValid();
}
#endif

#if WITH_EDITOR
namespace
{
	constexpr float PilotMinimumBPM = 80.0f;
	constexpr float PilotMaximumBPM = 180.0f;
	constexpr float TempoHistogramBinSize = 0.1f;
	constexpr int32 TempoLookAheadCount = 12;
}
void UMusicReactionTrack::EstimateTempoAndPhase()
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
	if (BestScore <= 0.0) { return; }
	EstimatedBPM = PilotMinimumBPM + BestBin * TempoHistogramBinSize;

	const float BeatInterval = 60.0f / EstimatedBPM;
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

#endif
