// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedFloor.h"

AChunkedFloor::AChunkedFloor()
{
	// A neutral default makes the two main drop-downs (shape and pattern) immediately useful.
	OuterShape = EChunkedDecorativeFloorShape::Grid;
	ShapeSize = FVector2D(5000.0, 5000.0);
	bUseBrightBorder = false;
	FloorPattern = EChunkedFloorPattern::Checkerboard;
}

void AChunkedFloor::SetFloorShape(
	EChunkedDecorativeFloorShape NewShape,
	FVector2D NewShapeSize,
	bool bPreserveCellState)
{
	SetDecorativeFloorShape(NewShape, NewShapeSize, bPreserveCellState);
}

void AChunkedFloor::ConfigureChunkedFloor(
	EChunkedDecorativeFloorShape NewShape,
	FVector2D NewShapeSize,
	FVector NewCellDimensions,
	FVector2D NewCellSpacing,
	bool bPreserveCellState)
{
	ConfigureDecorativeFloor(
		NewShape,
		NewShapeSize,
		NewCellDimensions,
		NewCellSpacing,
		bPreserveCellState);
}
