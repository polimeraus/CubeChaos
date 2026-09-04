// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"

namespace DestructibleCollision
{
	/** Merge touching blocks within each X row. Holes, spacing and chunk boundaries stay exact. */
	inline void AppendBoxRuns(
		FKAggregateGeom& OutCollision,
		FIntVector Minimum,
		FIntVector Maximum,
		FVector BlockSize,
		FVector Spacing,
		FVector GridMinimum,
		TFunctionRef<bool(int32, int32, int32)> CellExists)
	{
		const FVector Pitch = BlockSize + Spacing;
		for (int32 Z = Minimum.Z; Z < Maximum.Z; ++Z)
		{
			for (int32 Y = Minimum.Y; Y < Maximum.Y; ++Y)
			{
				for (int32 X = Minimum.X; X < Maximum.X;)
				{
					if (!CellExists(X, Y, Z))
					{
						++X;
						continue;
					}
					const int32 FirstX = X++;
					if (Spacing.X == 0.0)
					{
						while (X < Maximum.X && CellExists(X, Y, Z))
						{
							++X;
						}
					}
					const FVector Size(BlockSize.X * (X - FirstX), BlockSize.Y, BlockSize.Z);
					FKBoxElem& Box = OutCollision.BoxElems.Emplace_GetRef(Size.X, Size.Y, Size.Z);
					Box.Center = GridMinimum + FVector(FirstX * Pitch.X, Y * Pitch.Y, Z * Pitch.Z) + Size * 0.5;
				}
			}
		}
	}
}
