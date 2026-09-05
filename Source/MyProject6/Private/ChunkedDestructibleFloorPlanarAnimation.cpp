#include "ChunkedDestructibleFloor.h"

EConeStructureAnimationType AChunkedDestructibleFloor::GetFixedAnimationType() const
{
	if (!UsesPlanarAnimation()) { return ConeAnimationType; }
	switch (PlanarAnimationType)
	{
	case EPlanarAnimationType::FlowingWave: return EConeStructureAnimationType::BottomToTopGlowWave;
	case EPlanarAnimationType::SineBands: return EConeStructureAnimationType::BottomToTopSineRows;
	case EPlanarAnimationType::Spiral: return EConeStructureAnimationType::RisingSpiralGlow;
	default: return EConeStructureAnimationType::None;
	}
}

void AChunkedDestructibleFloor::SetPlanarAnimationType(EPlanarAnimationType Type)
{
	PlanarAnimationType = Type;
	if (UsesPlanarAnimation() && HasActorBegunPlay()) { PlayFixedAnimation(); }
}

void AChunkedDestructibleFloor::SetPlanarFlow(FDestructiblePlanarFlow Flow)
{
	Flow.AngleDegrees = FMath::IsFinite(Flow.AngleDegrees) ? FMath::UnwindDegrees(Flow.AngleDegrees) : 0.0f;
	Flow.Origin.X = FMath::IsFinite(Flow.Origin.X) ? FMath::Clamp(Flow.Origin.X, 0.0, 1.0) : 0.5;
	Flow.Origin.Y = FMath::IsFinite(Flow.Origin.Y) ? FMath::Clamp(Flow.Origin.Y, 0.0, 1.0) : 0.5;
	PlanarFlow = Flow;
	if (!UsesPlanarAnimation()) { return; }
	ResetMusicPlayback();
	if (IsMusicReactionEnabled() && HasActorBegunPlay()) { BuildMusicEqualizerLayout(); }
	if (PlanarAnimation) { PlanarAnimation->Initialize(this); }
}

FVector2D AChunkedDestructibleFloor::GetPlanarAnimationCoordinates(FVector Position) const
{
	return PlanarFlow.Coordinates((FVector2D(Position.X, Position.Y) - GridLocalMin) /
		FVector2D(FMath::Max(EffectiveGridSize.X, 1.0), FMath::Max(EffectiveGridSize.Y, 1.0)), EffectiveGridSize);
}

FVector2D AChunkedDestructibleFloor::GetMusicEqualizerCoordinates(FVector Position) const
{
	if (MusicAnimationType != EMusicAnimationType::Equalizer) { return GetPlanarAnimationCoordinates(Position); }
	const FVector2D UV = (FVector2D(Position.X, Position.Y) - GridLocalMin) /
		FVector2D(FMath::Max(EffectiveGridSize.X, 1.0), FMath::Max(EffectiveGridSize.Y, 1.0));
	switch (MusicEqualizerBaseEdge)
	{
	case EMusicEqualizerBaseEdge::MaxX: return FVector2D(1.0 - UV.X, UV.Y);
	case EMusicEqualizerBaseEdge::MinY: return FVector2D(UV.Y, UV.X);
	case EMusicEqualizerBaseEdge::MaxY: return FVector2D(1.0 - UV.Y, UV.X);
	default: return UV;
	}
}

bool AChunkedDestructibleFloor::EnsurePlanarAnimationResources()
{
	if (!UsesPlanarAnimation()) { return false; }
	if (PlanarAnimation) { return true; }
	PlanarAnimation = NewObject<UDestructiblePlanarAnimation>(this);
	if (!PlanarAnimation->Initialize(this))
	{
		PlanarAnimation->Release();
		PlanarAnimation = nullptr;
		return false;
	}
	return true;
}

bool AChunkedDestructibleFloor::CreateFixedAnimationResources()
{
	return EnsurePlanarAnimationResources();
}

bool AChunkedDestructibleFloor::HasFixedAnimationResources() const
{
	return PlanarAnimation != nullptr;
}

void AChunkedDestructibleFloor::ReleasePlanarAnimationIfUnused()
{
	if (PlanarAnimation && !bFixedAnimationPlaying && !IsMusicReactionEnabled())
	{
		PlanarAnimation->Release();
		PlanarAnimation = nullptr;
	}
}

void AChunkedDestructibleFloor::DestroyFixedAnimationResources()
{
	if (PlanarAnimation) { PlanarAnimation->SetFixedAnimation(TConstArrayView<float>()); }
	ReleasePlanarAnimationIfUnused();
}

void AChunkedDestructibleFloor::RenderFixedAnimation(TConstArrayView<float> Alphas)
{
	if (PlanarAnimation) { PlanarAnimation->SetFixedAnimation(Alphas); }
}

void AChunkedDestructibleFloor::RenderMusicEqualizerCells(TConstArrayView<int32> CellsToHighlight)
{
	// Shared stop/seek/expiry path. Volume shapes retain their own renderer overrides.
	if (PlanarAnimation && CellsToHighlight.IsEmpty()) { PlanarAnimation->ClearMusic(); }
}

void AChunkedDestructibleFloor::ClearMusicPieceMaterials()
{
	if (PlanarAnimation) { PlanarAnimation->ClearMusic(); }
	PlanarMusicMask.Empty();
	ReleasePlanarAnimationIfUnused();
}

void AChunkedDestructibleFloor::UpdatePlanarMusic(double CurrentTime)
{
	if (!EnsurePlanarAnimationResources()) { return; }
	const int32 Columns = CachedMusicEqualizerColumnCount;
	const int32 Rows = CachedMusicEqualizerRowCount;
	PlanarMusicMask.Init(0, Columns * Rows);
	const int32 TailLength = FMath::Clamp(MusicEqualizerTailLength, 1, 8);
	const float PeakIntensity = FMath::Max(StrongMusicGlowIntensity, WeakMusicGlowIntensity);
	const bool bFilled = MusicAnimationType == EMusicAnimationType::Equalizer;
	for (const FActiveMusicEqualizerColumn& Column : ActiveMusicEqualizerColumns)
	{
		const float Progress = (CurrentTime - Column.StartTime) / FMath::Max(Column.Duration, 0.01f);
		if (Progress < 0.0f || Progress >= 1.0f || Column.ColumnIndex < 0 || Column.ColumnIndex >= Columns) { continue; }
		const int32 PeakRows = FMath::Clamp(FMath::RoundToInt(Rows * Column.PeakHeight), 1, Rows);
		const int32 Head = FMath::FloorToInt(Progress * (PeakRows + TailLength));
		const float Intensity = FMath::Lerp(FMath::Max(WeakMusicGlowIntensity, 0.0f), PeakIntensity,
			FMath::Pow(FMath::Clamp(Column.Strength, 0.0f, 1.0f), 0.75f)) * (bFilled ? 1.0f : FMath::Square(1.0f - Progress));
		const int32 FilledRows = FMath::Clamp(FMath::RoundToInt(Column.GetEqualizerHeight(CurrentTime) * Rows), 0, Rows);
		for (int32 Tail = 0; Tail < (bFilled ? FilledRows : TailLength); ++Tail)
		{
			const int32 Row = bFilled ? Tail : Head - Tail;
			if (Row < 0 || Row >= PeakRows) { continue; }
			const float Fade = bFilled ? 1.0f : 1.0f - static_cast<float>(Tail) / TailLength;
			uint8& Value = PlanarMusicMask[Row * Columns + Column.ColumnIndex];
			Value = FMath::Max(Value, static_cast<uint8>(FMath::Clamp(
				FMath::RoundToInt(255.0f * Intensity * Fade / FMath::Max(PeakIntensity, 0.001f)), 0, 255)));
		}
	}
	PlanarAnimation->SetMusicMask(PlanarMusicMask, Columns, Rows);
}
