// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleDecorativeFloor.h"
#include "ChunkedFloor.generated.h"

/**
 * Unified chunked, destructible floor actor.
 *
 * Choose the outer silhouette from OuterShape and the grid/motif treatment from FloorPattern.
 * All variants use the same lightweight cell data, chunk meshes, collision, damage, explosions,
 * ripple, flash, sound and material palette inherited from AChunkedDestructibleFloor.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Chunked Floor"))
class MYPROJECT6_API AChunkedFloor : public AChunkedDestructibleDecorativeFloor
{
	GENERATED_BODY()

public:
	AChunkedFloor();

	/** Short unified-name wrapper for changing the silhouette at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	void SetFloorShape(
		EChunkedDecorativeFloorShape NewShape,
		FVector2D NewShapeSize,
		bool bPreserveCellState = true);

	/** Configures the shape, cell dimensions and cell gaps in one rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Chunked Floor|Generation")
	void ConfigureChunkedFloor(
		EChunkedDecorativeFloorShape NewShape,
		FVector2D NewShapeSize,
		FVector NewCellDimensions,
		FVector2D NewCellSpacing,
		bool bPreserveCellState = true);

	/** Assigns randomized generation, shape, pattern and destruction settings before spawning. */
	void RandomizeFloorParameters(
		float MinFloorSize,
		float MaxFloorSize,
		FRandomStream& RandomStream);

	UFUNCTION(BlueprintPure, Category = "Chunked Floor|Shape")
	EChunkedDecorativeFloorShape GetFloorShape() const { return OuterShape; }

	UFUNCTION(BlueprintPure, Category = "Chunked Floor|Shape")
	FVector2D GetFloorShapeSize() const { return ShapeSize; }
};
