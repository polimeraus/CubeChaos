#include "MusicReactiveColumnGrid.h"
#include "MusicReactionTrack.h"
#include "MusicReactionWorldSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	float SafeColumnValue(float Value, float Minimum, float Maximum, float Fallback)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Fallback;
	}
}

AMusicReactiveColumnGrid::AMusicReactiveColumnGrid()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("GridRoot")));
	Columns = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Columns"));
	Columns->SetupAttachment(RootComponent);
	Columns->SetMobility(EComponentMobility::Movable);
	Columns->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Columns->SetGenerateOverlapEvents(false);
	Columns->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Cube.Succeeded()) { Columns->SetStaticMesh(Cube.Object); }
	if (Material.Succeeded()) { ColumnMaterial = Material.Object; }
}

void AMusicReactiveColumnGrid::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildGrid();
	if (HasActorBegunPlay()) { RefreshMusicRegistration(); }
}

void AMusicReactiveColumnGrid::BeginPlay()
{
	Super::BeginPlay();
	RebuildGrid();
	RefreshMusicRegistration();
}

void AMusicReactiveColumnGrid::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		if (auto* Music = GetWorld()->GetSubsystem<UMusicReactionWorldSubsystem>()) { Music->UnregisterReactionActor(this); }
	}
	ResetMusicPlayback();
	Super::EndPlay(EndPlayReason);
}

void AMusicReactiveColumnGrid::ConfigureGrid(float NewSquareSize, float NewWidth, float NewLength, float NewGap)
{
	SquareSize = NewSquareSize;
	SurfaceWidth = NewWidth;
	SurfaceLength = NewLength;
	Gap = NewGap;
	RebuildGrid();
}

void AMusicReactiveColumnGrid::ConfigureHeights(float NewMinimum, float NewRest, float NewMaximum)
{
	MinimumHeight = NewMinimum;
	RestHeight = NewRest;
	MaximumHeight = NewMaximum;
	RebuildGrid();
}

void AMusicReactiveColumnGrid::RebuildGrid()
{
	SquareSize = SafeColumnValue(SquareSize, 1, 1000, 50);
	SurfaceWidth = SafeColumnValue(SurfaceWidth, SquareSize, 128000, SquareSize);
	SurfaceLength = SafeColumnValue(SurfaceLength, SquareSize, 128000, SquareSize);
	Gap = SafeColumnValue(Gap, 0, 1000, 2);
	MinimumHeight = SafeColumnValue(MinimumHeight, 1, 10000, 20);
	MaximumHeight = SafeColumnValue(MaximumHeight, MinimumHeight, 10000, MinimumHeight);
	RestHeight = SafeColumnValue(RestHeight, MinimumHeight, MaximumHeight, MinimumHeight);
	ResetMusicPlayback();
	const double Pitch = SquareSize + Gap;
	ColumnCount = FIntPoint(FMath::Clamp(FMath::FloorToInt((SurfaceWidth + Gap) / Pitch), 1, 128),
		FMath::Clamp(FMath::FloorToInt((SurfaceLength + Gap) / Pitch), 1, 128));
	GeneratedSize = FVector2D(ColumnCount.X * Pitch - Gap, ColumnCount.Y * Pitch - Gap);
	const int32 Count = ColumnCount.X * ColumnCount.Y;
	ColumnCenters.SetNum(Count);
	ColumnTransforms.SetNum(Count);
	ColumnHeights.Init(RestHeight, Count);
	for (int32 Y = 0; Y < ColumnCount.Y; ++Y)
	{
		for (int32 X = 0; X < ColumnCount.X; ++X)
		{
			const int32 Index = Y * ColumnCount.X + X;
			const FVector2D Center((X - (ColumnCount.X - 1) * .5) * Pitch, (Y - (ColumnCount.Y - 1) * .5) * Pitch);
			ColumnCenters[Index] = Center;
			ColumnTransforms[Index] = FTransform(FQuat::Identity, FVector(Center.X, Center.Y, RestHeight * .5),
				FVector(SquareSize / 100.0, SquareSize / 100.0, RestHeight / 100.0));
		}
	}
	BuildDistances();
	Columns->ClearInstances();
	Columns->SetMaterial(0, ColumnMaterial);
	Columns->AddInstances(ColumnTransforms, false, false, false);
}

void AMusicReactiveColumnGrid::BuildDistances()
{
	ColumnDistances.SetNum(ColumnCenters.Num());
	MaximumDistance = 0;
	const double HalfX = FMath::Max((GeneratedSize.X - SquareSize) * .5, SquareSize * .5);
	const double HalfY = FMath::Max((GeneratedSize.Y - SquareSize) * .5, SquareSize * .5);
	for (int32 Index = 0; Index < ColumnCenters.Num(); ++Index)
	{
		const FVector2D Center = ColumnCenters[Index];
		// Chebyshev distance in the rectangle's proportions makes rectangular rings.
		// Euclidean distance gives true circles even on a wide or narrow rectangle.
		const double Distance = ReactionType == EColumnGridReactionType::RectangularWave
			? FMath::Max(FMath::Abs(Center.X) / HalfX, FMath::Abs(Center.Y) / HalfY) * FMath::Min(HalfX, HalfY)
			: Center.Size();
		ColumnDistances[Index] = Distance;
		MaximumDistance = FMath::Max(MaximumDistance, Distance);
	}
}

void AMusicReactiveColumnGrid::SetReactionType(EColumnGridReactionType NewType)
{
	if (ReactionType == NewType) { return; }
	ResetMusicPlayback();
	ReactionType = NewType;
	BuildDistances();
}

void AMusicReactiveColumnGrid::SetWaveDirection(EColumnGridWaveDirection NewDirection)
{
	if (WaveDirection == NewDirection) { return; }
	ResetMusicPlayback();
	WaveDirection = NewDirection;
}

void AMusicReactiveColumnGrid::SetWaveSpeed(float NewSpeed)
{
	WaveSpeed = SafeColumnValue(NewSpeed, 1, 50000, 600);
	if (ReactionType == EColumnGridReactionType::Equalizer) { return; }
	const double Now = GetAnimationTime();
	for (FMotionPulse& Pulse : ActivePulses)
	{
		const double Ratio = Pulse.Speed / WaveSpeed;
		Pulse.StartTime = Now - (Now - Pulse.StartTime) * Ratio;
		Pulse.Duration *= Ratio;
		Pulse.TravelTime *= Ratio;
		Pulse.Speed = WaveSpeed;
	}
}

bool AMusicReactiveColumnGrid::IsMusicReactionEnabled() const
{
	return bReactToMusic && (bReactToOnsets || bReactToBeats || bReactToKeyPoints);
}

void AMusicReactiveColumnGrid::RefreshMusicRegistration()
{
	if (!HasActorBegunPlay() || !GetWorld()) { return; }
	if (auto* Music = GetWorld()->GetSubsystem<UMusicReactionWorldSubsystem>())
	{
		if (IsMusicReactionEnabled()) { Music->RegisterReactionActor(this); }
		else { Music->UnregisterReactionActor(this); }
	}
}

void AMusicReactiveColumnGrid::SetReactToMusic(bool bEnabled)
{
	bReactToMusic = bEnabled;
	if (!IsMusicReactionEnabled()) { ResetMusicPlayback(); }
	RefreshMusicRegistration();
}

void AMusicReactiveColumnGrid::SetMusicSources(bool bOnsets, bool bBeats, bool bKeyPoints)
{
	bReactToOnsets = bOnsets;
	bReactToBeats = bBeats;
	bReactToKeyPoints = bKeyPoints;
	SetReactToMusic(bReactToMusic);
}

double AMusicReactiveColumnGrid::GetAnimationTime() const
{
	if (bPreviewActive) { return FPlatformTime::Seconds(); }
	if (GetWorld())
	{
		if (const auto* Music = GetWorld()->GetSubsystem<UMusicReactionWorldSubsystem>(); Music && Music->HasPlayback())
		{
			return Music->GetReactionSeconds();
		}
		return GetWorld()->GetRealTimeSeconds();
	}
	return 0;
}

void AMusicReactiveColumnGrid::ApplyMusicEvent(const FMusicReactionEvent& Event, double CurrentTime)
{
	if (!IsMusicReactionEnabled() || !FMath::IsFinite(CurrentTime) || !FMath::IsFinite(Event.TimeSeconds) ||
		!FMath::IsFinite(Event.Strength) || !FMath::IsFinite(Event.Duration) ||
		(!Event.TargetGroup.IsNone() && Event.TargetGroup != MusicReactionGroup)) { return; }
	if ((Event.Type == EMusicReactionEventType::Beat && !bReactToBeats) ||
		(Event.Type == EMusicReactionEventType::Onset && (!bReactToOnsets || Event.Strength < MinimumOnsetStrength)) ||
		(Event.IsKeyPoint() && !bReactToKeyPoints)) { return; }
	const float Strength = FMath::Clamp(Event.Strength * SafeColumnValue(Sensitivity, 0, 4, 1), 0.0f, 1.0f);
	if (Strength <= 0) { return; }
	if (FMath::Abs(Event.TimeSeconds - LastEventTime) < .075 && Event.Priority <= LastEventPriority) { return; }
	ActivePulses.RemoveAll([&Event](const FMotionPulse& Pulse)
	{
		return Pulse.Priority < Event.Priority && FMath::Abs(Pulse.StartTime - Event.TimeSeconds) < .075;
	});
	const double StartTime = Event.IsKeyPoint() && CurrentTime - Event.TimeSeconds > .08 ? CurrentTime : Event.TimeSeconds;
	StartPulse(Strength, StartTime, Event.Duration, Event.Priority);
	LastEventTime = Event.TimeSeconds;
	LastEventPriority = Event.Priority;
	NextUpdateTime = FMath::Min(NextUpdateTime, CurrentTime);
}

void AMusicReactiveColumnGrid::StartPulse(float Strength, double StartTime, float Duration, int32 Priority)
{
	if (ColumnCenters.IsEmpty()) { RebuildGrid(); }
	FMotionPulse Pulse;
	Pulse.StartTime = StartTime;
	Pulse.Strength = Strength;
	Pulse.Priority = Priority;
	Pulse.Seed = HashCombineFast(uint32(RandomSeed), ++PulseSequence);
	if (ReactionType == EColumnGridReactionType::Equalizer)
	{
		// Generate an independent, stable level per square once per cue. Nearby
		// cell indices must not collapse to almost identical equalizer heights.
		FRandomStream Random(int32(Pulse.Seed));
		Pulse.EqualizerWeights.Reserve(ColumnCenters.Num());
		for (int32 Index = 0; Index < ColumnCenters.Num(); ++Index) { Pulse.EqualizerWeights.Add(Random.FRandRange(.25f, 1.0f)); }
	}
	Pulse.bInward = WaveDirection == EColumnGridWaveDirection::Inward ||
		(WaveDirection == EColumnGridWaveDirection::Alternating && PulseSequence % 2 == 0);
	Pulse.Speed = SafeColumnValue(WaveSpeed, 1, 50000, 600);
	const double DefaultDuration = ReactionType == EColumnGridReactionType::Equalizer
		? SafeColumnValue(EqualizerResponseDuration, .1f, 10, .9f)
		: SafeColumnValue(WaveWidth, 1, 10000, 350) / Pulse.Speed;
	Pulse.Duration = Duration > 0 ? FMath::Clamp(double(Duration), .1, 10.0) : DefaultDuration;
	Pulse.TravelTime = ReactionType == EColumnGridReactionType::Equalizer ? 0 : MaximumDistance / Pulse.Speed;
	// A dense onset sequence has bounded work and preserves more important cues.
	if (ActivePulses.Num() >= 8)
	{
		int32 Weakest = 0;
		for (int32 Index = 1; Index < ActivePulses.Num(); ++Index)
		{
			if (ActivePulses[Index].Priority < ActivePulses[Weakest].Priority) { Weakest = Index; }
		}
		if (Priority < ActivePulses[Weakest].Priority) { return; }
		ActivePulses.RemoveAt(Weakest);
	}
	ActivePulses.Add(MoveTemp(Pulse));
	SetActorTickEnabled(true);
}

float AMusicReactiveColumnGrid::EvaluateHeight(int32 Index, double CurrentTime) const
{
	double Displacement = 0;
	for (const FMotionPulse& Pulse : ActivePulses)
	{
		const bool bEqualizer = ReactionType == EColumnGridReactionType::Equalizer;
		const double Distance = Pulse.bInward ? MaximumDistance - ColumnDistances[Index] : ColumnDistances[Index];
		const double Age = CurrentTime - Pulse.StartTime - (bEqualizer ? 0 : Distance / Pulse.Speed);
		const double Phase = Age / Pulse.Duration;
		if (Phase <= 0 || Phase >= 1) { continue; }
		double Value;
		if (bEqualizer)
		{
			const double Envelope = Phase < .12 ? FMath::SmoothStep(0.0, .12, Phase) : 1 - FMath::SmoothStep(.12, 1.0, Phase);
			const double Weight = Pulse.EqualizerWeights[Index];
			Value = Envelope * Weight;
		}
		else
		{
			// A smooth raised crest followed by a recessed trough, with zero velocity
			// at both ends of the packet, as in the reference's traveling ripples.
			Value = FMath::Sin(UE_TWO_PI * Phase) * FMath::Square(FMath::Sin(UE_PI * Phase));
		}
		Displacement += Value * Pulse.Strength * (Value >= 0 ? MaximumHeight - RestHeight : RestHeight - MinimumHeight);
	}
	return FMath::Clamp(float(RestHeight + Displacement), MinimumHeight, MaximumHeight);
}

void AMusicReactiveColumnGrid::UpdateColumns(double CurrentTime)
{
	if (!FMath::IsFinite(CurrentTime) || CurrentTime < NextUpdateTime) { return; }
	NextUpdateTime = CurrentTime + 1.0 / SafeColumnValue(UpdateRate, 10, 60, 30);
	ActivePulses.RemoveAll([CurrentTime](const FMotionPulse& Pulse)
	{
		return CurrentTime >= Pulse.StartTime + Pulse.TravelTime + Pulse.Duration;
	});
	for (int32 Index = 0; Index < ColumnCenters.Num(); ++Index)
	{
		const float Height = EvaluateHeight(Index, CurrentTime);
		ColumnHeights[Index] = Height;
		ColumnTransforms[Index].SetLocation(FVector(ColumnCenters[Index].X, ColumnCenters[Index].Y, Height * .5));
		ColumnTransforms[Index].SetScale3D(FVector(SquareSize / 100.0, SquareSize / 100.0, Height / 100.0));
	}
	if (!ColumnTransforms.IsEmpty()) { Columns->BatchUpdateInstancesTransforms(0, ColumnTransforms, false, true, true); }
	if (ActivePulses.IsEmpty()) { bPreviewActive = false; SetActorTickEnabled(false); }
}

void AMusicReactiveColumnGrid::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateColumns(GetAnimationTime());
}

void AMusicReactiveColumnGrid::ResetMusicPlayback()
{
	ActivePulses.Reset();
	bPreviewActive = false;
	NextUpdateTime = 0;
	LastEventTime = -1.e30;
	LastEventPriority = -1;
	PulseSequence = 0;
	for (int32 Index = 0; Index < ColumnTransforms.Num(); ++Index)
	{
		ColumnHeights[Index] = RestHeight;
		ColumnTransforms[Index].SetLocation(FVector(ColumnCenters[Index].X, ColumnCenters[Index].Y, RestHeight * .5));
		ColumnTransforms[Index].SetScale3D(FVector(SquareSize / 100.0, SquareSize / 100.0, RestHeight / 100.0));
	}
	if (Columns && Columns->GetInstanceCount() == ColumnTransforms.Num() && !ColumnTransforms.IsEmpty())
	{
		Columns->BatchUpdateInstancesTransforms(0, ColumnTransforms, false, true, true);
	}
	SetActorTickEnabled(false);
}

void AMusicReactiveColumnGrid::TriggerMusicReaction(float Strength)
{
	FMusicReactionEvent Event;
	Event.TimeSeconds = GetAnimationTime();
	Event.Strength = Strength;
	Event.Duration = 0;
	ApplyMusicEvent(Event, GetAnimationTime());
}

void AMusicReactiveColumnGrid::PreviewReaction()
{
	ResetMusicPlayback();
	bPreviewActive = GetWorld() && !GetWorld()->IsGameWorld();
	StartPulse(1, GetAnimationTime(), 0, 100);
}

void AMusicReactiveColumnGrid::StopPreview()
{
	ResetMusicPlayback();
}

float AMusicReactiveColumnGrid::GetColumnHeight(int32 X, int32 Y) const
{
	return X >= 0 && Y >= 0 && X < ColumnCount.X && Y < ColumnCount.Y && ColumnHeights.IsValidIndex(Y * ColumnCount.X + X)
		? ColumnHeights[Y * ColumnCount.X + X] : 0;
}
