// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MusicReactionWorldSubsystem.generated.h"

class UAudioComponent;
class UOnsetNRT;
class USoundWave;
class AChunkedDestructibleFloor;

/**
 * Starts the track and its precomputed onset/beat timeline on the first actor registration.
 * The last unregistration releases audio/analysis resources and removes subsystem ticking.
 */
UCLASS()
class MYPROJECT6_API UMusicReactionWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
	friend class FDestructibleMusicAnimationTest;

public:
	UMusicReactionWorldSubsystem();

	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	void RegisterReactionActor(AChunkedDestructibleFloor* Actor);
	void UnregisterReactionActor(AChunkedDestructibleFloor* Actor);

	UFUNCTION(BlueprintPure, Category = "Animation|Music")
	float GetDetectedBPM() const { return DetectedBPM; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	UFUNCTION()
	void HandleMusicFinished();

	UFUNCTION()
	void HandlePlaybackPercent(const USoundWave* PlayingSoundWave, float PlaybackPercent);

	void StartMusic();
	void StopMusic();
	void CacheAnalysis();
	void EstimateTempoAndPhase();
	void ResetTimeline();
	void DispatchReaction(float Strength, bool bIsOnset);

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;

	UPROPERTY(Transient)
	TObjectPtr<USoundWave> MusicSound;

	UPROPERTY(Transient)
	TObjectPtr<UOnsetNRT> OnsetAnalysis;

	TArray<float> OnsetTimes;
	TArray<float> OnsetStrengths;
	TArray<TWeakObjectPtr<AChunkedDestructibleFloor>> ReactionActors;

	float DetectedBPM = 120.0f;
	float BeatPhaseSeconds = 0.0f;
	float PlaybackSeconds = 0.0f;
	float LastPlaybackPercent = 0.0f;
	int32 NextOnsetIndex = 0;
	int32 NextBeatIndex = 0;
	bool bReceivedPlaybackPercent = false;
	bool bRestartQueued = false;
};
