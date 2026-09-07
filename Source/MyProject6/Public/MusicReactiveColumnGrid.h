#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MusicReactionListener.h"
#include "MusicReactiveColumnGrid.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EColumnGridReactionType : uint8
{
	Equalizer,
	CircularWave UMETA(DisplayName = "Circular Wave"),
	RectangularWave UMETA(DisplayName = "Rectangular Wave")
};

UENUM(BlueprintType)
enum class EColumnGridWaveDirection : uint8
{
	Outward UMETA(DisplayName = "Center To Edges"),
	Inward UMETA(DisplayName = "Edges To Center"),
	Alternating UMETA(DisplayName = "Alternate Both Directions")
};

/** Square columns anchored to local XY, extending along local Z in response to music. */
UCLASS(Blueprintable, meta = (DisplayName = "Music Reactive Column Grid"))
class MYPROJECT6_API AMusicReactiveColumnGrid : public AActor, public IMusicReactionListener
{
	GENERATED_BODY()
	friend class FMusicReactiveColumnGridTest;
public:
	AMusicReactiveColumnGrid();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return bPreviewActive; }
	virtual bool IsMusicReactionEnabled() const override;
	virtual void ApplyMusicEvent(const FMusicReactionEvent& Event, double CurrentTime) override;
	virtual void ResetMusicPlayback() override;

	UFUNCTION(BlueprintCallable, Category = "Column Grid|Grid")
	void ConfigureGrid(float NewSquareSize, float NewWidth, float NewLength, float NewGap = 2.0f);
	UFUNCTION(BlueprintCallable, Category = "Column Grid|Motion")
	void ConfigureHeights(float NewMinimum, float NewRest, float NewMaximum);
	UFUNCTION(BlueprintCallable, Category = "Column Grid|Motion")
	void SetReactionType(EColumnGridReactionType NewType);
	UFUNCTION(BlueprintCallable, Category = "Column Grid|Motion")
	void SetWaveDirection(EColumnGridWaveDirection NewDirection);
	UFUNCTION(BlueprintCallable, Category = "Column Grid|Motion")
	void SetWaveSpeed(float NewSpeed);
	UFUNCTION(BlueprintCallable, Category = "Column Grid|Music")
	void SetReactToMusic(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Column Grid|Music")
	void SetMusicSources(bool bOnsets, bool bBeats, bool bKeyPoints);
	UFUNCTION(BlueprintCallable, Category = "Column Grid|Music")
	void TriggerMusicReaction(float Strength = 1.0f);
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Column Grid|Preview")
	void PreviewReaction();
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Column Grid|Preview")
	void StopPreview();
	UFUNCTION(BlueprintPure, Category = "Column Grid|Grid")
	FIntPoint GetColumnCount() const { return ColumnCount; }
	UFUNCTION(BlueprintPure, Category = "Column Grid|Grid")
	FVector2D GetGeneratedSize() const { return GeneratedSize; }
	UFUNCTION(BlueprintPure, Category = "Column Grid|Motion")
	float GetColumnHeight(int32 X, int32 Y) const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Column Grid|Components")
	TObjectPtr<UInstancedStaticMeshComponent> Columns;
	/** Each top face remains this size, regardless of column height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Grid", meta = (ClampMin = "1", ClampMax = "1000", Units = "cm"))
	float SquareSize = 50.0f;
	/** Fit whole squares inside this width; see Generated Size for the resulting dimensions. Up to 128 columns per axis. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Grid", meta = (ClampMin = "1", ClampMax = "128000", Units = "cm"))
	float SurfaceWidth = 1600.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Grid", meta = (ClampMin = "1", ClampMax = "128000", Units = "cm"))
	float SurfaceLength = 1000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Grid", meta = (ClampMin = "0", ClampMax = "1000", Units = "cm"))
	float Gap = 2.0f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Column Grid|Grid")
	FIntPoint ColumnCount = FIntPoint::ZeroValue;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Column Grid|Grid", meta = (Units = "cm"))
	FVector2D GeneratedSize = FVector2D::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Appearance")
	TObjectPtr<UMaterialInterface> ColumnMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetReactionType, Category = "Column Grid|Motion")
	EColumnGridReactionType ReactionType = EColumnGridReactionType::CircularWave;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetWaveDirection, Category = "Column Grid|Motion", meta = (EditCondition = "ReactionType != EColumnGridReactionType::Equalizer", EditConditionHides))
	EColumnGridWaveDirection WaveDirection = EColumnGridWaveDirection::Alternating;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Motion", meta = (ClampMin = "1", ClampMax = "10000", Units = "cm"))
	float MinimumHeight = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Motion", meta = (ClampMin = "1", ClampMax = "10000", Units = "cm"))
	float RestHeight = 150.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Motion", meta = (ClampMin = "1", ClampMax = "10000", Units = "cm"))
	float MaximumHeight = 450.0f;
	/** Propagation speed along the surface, in cm per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetWaveSpeed, Category = "Column Grid|Motion", meta = (ClampMin = "1", ClampMax = "50000", Units = "cm/s", EditCondition = "ReactionType != EColumnGridReactionType::Equalizer", EditConditionHides))
	float WaveSpeed = 600.0f;
	/** Width of one crest-and-trough packet, measured along the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Column Grid|Motion", meta = (ClampMin = "1", ClampMax = "10000", Units = "cm", EditCondition = "ReactionType != EColumnGridReactionType::Equalizer", EditConditionHides))
	float WaveWidth = 350.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Column Grid|Motion", meta = (ClampMin = "0.1", ClampMax = "10", Units = "s", EditCondition = "ReactionType == EColumnGridReactionType::Equalizer", EditConditionHides))
	float EqualizerResponseDuration = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetReactToMusic, Category = "Column Grid|Music")
	bool bReactToMusic = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Music", meta = (EditCondition = "bReactToMusic"))
	bool bReactToOnsets = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Music", meta = (EditCondition = "bReactToMusic"))
	bool bReactToBeats = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Column Grid|Music", meta = (EditCondition = "bReactToMusic"))
	bool bReactToKeyPoints = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Column Grid|Music")
	FName MusicReactionGroup;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Column Grid|Music", meta = (ClampMin = "0", ClampMax = "4"))
	float Sensitivity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Column Grid|Music", meta = (ClampMin = "0", ClampMax = "1"))
	float MinimumOnsetStrength = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "Column Grid|Music")
	int32 RandomSeed = 7123;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Column Grid|Motion", meta = (ClampMin = "10", ClampMax = "60", Units = "Hz"))
	float UpdateRate = 30.0f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FMotionPulse
	{
		double StartTime = 0, Duration = 1, TravelTime = 0;
		float Strength = 1, Speed = 600;
		int32 Priority = 0;
		uint32 Seed = 0;
		bool bInward = false;
		TArray<float> EqualizerWeights;
	};
	TArray<FMotionPulse> ActivePulses;
	TArray<FVector2D> ColumnCenters;
	TArray<FTransform> ColumnTransforms;
	TArray<float> ColumnHeights;
	TArray<double> ColumnDistances;
	double MaximumDistance = 0, NextUpdateTime = 0, LastEventTime = -1.e30;
	int32 LastEventPriority = -1;
	uint32 PulseSequence = 0;
	bool bPreviewActive = false;
	void RebuildGrid();
	void BuildDistances();
	void RefreshMusicRegistration();
	void StartPulse(float Strength, double StartTime, float Duration, int32 Priority);
	void UpdateColumns(double CurrentTime);
	double GetAnimationTime() const;
	float EvaluateHeight(int32 Index, double CurrentTime) const;
};
