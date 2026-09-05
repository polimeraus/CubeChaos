// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Audio-position anchor plus monotonic real time; never adds a whole frame to a fresh callback. */
struct FMusicReactionClock
{
	double Position = 0.0;
	double AnchorTime = 0.0;
	bool bPaused = false;

	double GetPosition(double Now) const
	{
		return Position + (bPaused ? 0.0 : FMath::Max(0.0, Now - AnchorTime));
	}
	void Reset(double Seconds, double Now)
	{
		Position = Seconds;
		AnchorTime = Now;
	}
	void SetPaused(bool bNewPaused, double Now)
	{
		Reset(GetPosition(Now), Now);
		bPaused = bNewPaused;
	}
	void Synchronize(double AudioSeconds, double Now)
	{
		if (!bPaused)
		{
			Reset(AudioSeconds, Now);
		}
	}
};
