// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BoxElem.h"

namespace DestructibleCollision
{
	/** Greedily merge a small 3D chunk. Never bridge a hole, gap, or clipped roof cell. */
	inline void AppendMergedBoxes(FKAggregateGeom& OutCollision, FIntVector Minimum, FIntVector Maximum,
		FVector BlockSize, FVector Spacing, FVector GridMinimum, TBitArray<>& Occupied)
	{
		const FIntVector Count = Maximum-Minimum;
		check(Occupied.Num() == Count.X*Count.Y*Count.Z);
		auto Index = [&](int32 X,int32 Y,int32 Z) { return X+Count.X*(Y+Count.Y*Z); };
		for(int32 Z=0;Z<Count.Z;++Z) for(int32 Y=0;Y<Count.Y;++Y) for(int32 X=0;X<Count.X;++X)
		{
			if(!Occupied[Index(X,Y,Z)]) continue;
			int32 EndX=X+1,EndY=Y+1,EndZ=Z+1;
			if(Spacing.X==0.) while(EndX<Count.X && Occupied[Index(EndX,Y,Z)]) ++EndX;
			auto RowFull = [&](int32 Row,int32 Layer)
			{
				for(int32 I=X;I<EndX;++I) if(!Occupied[Index(I,Row,Layer)]) return false;
				return true;
			};
			if(Spacing.Y==0.) while(EndY<Count.Y && RowFull(EndY,Z)) ++EndY;
			if(Spacing.Z==0.) while(EndZ<Count.Z)
			{
				bool Full=true;
				for(int32 J=Y;J<EndY && Full;++J) Full=RowFull(J,EndZ);
				if(!Full) break;
				++EndZ;
			}
			for(int32 K=Z;K<EndZ;++K) for(int32 J=Y;J<EndY;++J) for(int32 I=X;I<EndX;++I)
				Occupied[Index(I,J,K)]=false;
			const FVector Size=BlockSize*FVector(EndX-X,EndY-Y,EndZ-Z);
			FKBoxElem& Box=OutCollision.BoxElems.Emplace_GetRef(Size.X,Size.Y,Size.Z);
			Box.Center=GridMinimum+FVector(Minimum+FIntVector(X,Y,Z))*(BlockSize+Spacing)+Size*.5;
		}
	}

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
