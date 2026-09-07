// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MusicReactionTrack.h"
#include "MusicReactionClock.h"
#include "MusicReactionWorldSubsystem.generated.h"

class UAudioComponent;
class AActor;
struct FStreamableHandle;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMusicKeyPointDelegate, const FMusicReactionEvent&, Event);

/** Loads baked data on demand. No listeners: no tick or loaded assets. */
UCLASS()
class MYPROJECT6_API UMusicReactionWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
	friend class FDestructibleMusicAnimationTest;
	friend class FMusicReactionTimelineTest;
	friend class FMusicReactiveColumnGridTest;
public:
	UMusicReactionWorldSubsystem();
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	void RegisterReactionActor(AActor* Actor);
	void UnregisterReactionActor(AActor* Actor);
	UFUNCTION(BlueprintPure, Category = "Animation|Music")
	float GetDetectedBPM() const { return ActiveTrack ? ActiveTrack->EstimatedBPM : 0.0f; }
	UFUNCTION(BlueprintPure, Category = "Animation|Music")
	double GetPlaybackSeconds() const;
	double GetReactionSeconds() const;
	UFUNCTION(BlueprintPure, Category = "Animation|Music")
	bool HasPlayback() const { return MusicComponent != nullptr; }
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetMusicTrack(TSoftObjectPtr<UMusicReactionTrack> Track);
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SetMusicPaused(bool bPaused);
	/** Seeking skips earlier key points and resets active visuals. */
	UFUNCTION(BlueprintCallable, Category = "Animation|Music")
	void SeekMusic(float TimeSeconds);
	/** Key points are delivered once, including after a frame hitch. */
	UPROPERTY(BlueprintAssignable, Category = "Animation|Music")
	FMusicKeyPointDelegate OnKeyPoint;
protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
private:
	UFUNCTION()
	void HandleMusicFinished();
	UFUNCTION()
	void HandlePlaybackPercent(const USoundWave* PlayingSoundWave, float PlaybackPercent);
	void StartMusic();
	void FinishLoadingTrack();
	void StopMusic();
	bool CreatePlaybackComponent(float StartSeconds);
	void DestroyPlaybackComponent();
	void ResetTimeline(float StartSeconds = 0.0f);
	void RefreshPauseState();
	void ProcessTimeline(double Position);
	void DispatchReaction(const FMusicReactionEvent& Event, double Position);
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> MusicComponent;
	UPROPERTY(Transient)
	TObjectPtr<UMusicReactionTrack> ActiveTrack;
	TSoftObjectPtr<UMusicReactionTrack> TrackOverride;
	TSharedPtr<FStreamableHandle> LoadHandle;
	TArray<FMusicReactionEvent> KeyPointTimeline;
	TArray<FMusicReactionEvent> DeferredKeyPoints;
	int32 NextDeferredKeyPoint = 0;
	int32 LastAudioLoop = 0;
	TArray<TWeakObjectPtr<AActor>> ReactionActors;
	FMusicReactionClock PlaybackClock;
	int32 NextOnsetIndex = 0;
	int32 NextBeatIndex = 0;
	int32 NextKeyPointIndex = 0;
	uint32 PlaybackGeneration = 0;
	float LastAudioSeconds = 0.0f;
	bool bReceivedPlaybackPercent = false;
	bool bRestartQueued = false;
	bool bUserPaused = false;
};
