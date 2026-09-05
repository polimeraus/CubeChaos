// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "MusicReactionTrack.generated.h"

class USoundWave;
class UOnsetNRT;

UENUM(BlueprintType)
enum class EMusicReactionEventType : uint8
{
	Beat UMETA(Hidden),
	Onset UMETA(Hidden),
	Accent,
	Sweep,
	Burst
};

/** An authored key point, or a timestamped event delivered to a destructible actor. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FMusicReactionEvent
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", Units = "s"))
	float TimeSeconds = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Name;
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EMusicReactionEventType Type = EMusicReactionEventType::Accent;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "1"))
	float Strength = 1.0f;
	/** Zero uses the actor's normal travel duration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "3", Units = "s"))
	float Duration = 0.45f;
	/** None broadcasts to all groups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName TargetGroup;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	int32 Priority = 100;
	bool IsKeyPoint() const { return Type != EMusicReactionEventType::Beat && Type != EMusicReactionEventType::Onset; }
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FMusicTempoSection
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", Units = "s"))
	float StartTimeSeconds = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "20", ClampMax = "400"))
	float BPM = 120.0f;
	/** First beat relative to the section start. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", Units = "s"))
	float BeatOffsetSeconds = 0.0f;
};

/** Editor-baked music data. Runtime playback never runs the audio analyzer or tempo estimator. */
UCLASS(BlueprintType)
class MYPROJECT6_API UMusicReactionTrack : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	TObjectPtr<USoundWave> Sound;
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "Analysis")
	TObjectPtr<UOnsetNRT> SourceAnalysis;
#endif
	/** Empty: estimate a constant tempo. Add sections for known tempos or tempo changes, then bake. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Analysis")
	TArray<FMusicTempoSection> TempoSections;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Key Points", meta = (TitleProperty = "Name"))
	TArray<FMusicReactionEvent> KeyPoints;
	/** Suggested accents are acoustic peaks, not automatic chorus/drop classification. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Key Points")
	bool bUseSuggestedAccents = true;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Key Points")
	TArray<FMusicReactionEvent> SuggestedAccents;
	UPROPERTY(EditAnywhere, Category = "Analysis", meta = (ClampMin = "0", ClampMax = "1"))
	float SuggestedAccentThreshold = 0.8f;
	UPROPERTY(EditAnywhere, Category = "Analysis", meta = (ClampMin = "1", Units = "s"))
	float SuggestedAccentSpacing = 6.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Baked Analysis")
	float EstimatedBPM = 120.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Baked Analysis")
	float BeatPhaseSeconds = 0.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Baked Analysis")
	float BakedDuration = 0.0f;
	UPROPERTY(VisibleAnywhere, Category = "Baked Analysis")
	FSoftObjectPath BakedSoundPath;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Baked Analysis")
	TArray<float> OnsetTimes;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Baked Analysis")
	TArray<float> OnsetStrengths;
	/** Baked beat times may also be corrected individually. Baking replaces these values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Baked Analysis")
	TArray<float> BeatTimes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music", meta = (ClampMin = "0", ClampMax = "1"))
	float Volume = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music")
	bool bLoop = true;

#if WITH_EDITOR
	/** Call after SourceAnalysis.OnAnalysisComplete, or from the asset editor after analysis finishes. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Analysis")
	bool BakeAnalysis();
	void EstimateTempoAndPhase();
#endif
	bool IsBakedDataValid() const;
	void GetSortedKeyPoints(TArray<FMusicReactionEvent>& OutEvents) const;
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Music Reactions"))
class MYPROJECT6_API UMusicReactionSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category = "Music")
	TSoftObjectPtr<UMusicReactionTrack> DefaultTrack;
	/** Positive values make visuals lead the audio clock. Tune against the target audio device. */
	UPROPERTY(Config, EditAnywhere, Category = "Music", meta = (ClampMin = "-0.25", ClampMax = "0.25", Units = "s"))
	float VisualOffsetSeconds = 0.0f;
};
