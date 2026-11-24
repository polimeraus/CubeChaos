// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleDecorativeFloor.h"
#include "SculptureMeshComponent.h"
#include "ChunkedDestructibleHouse.h"
#include "SimpleHouseActorDestructable.h"
#include "ChunkedDestructibleHalloweenPumpkin.h"
#include "ChunkedDestructibleHalloweenPumpkin2.h"
#include "ChunkedDestructibleHalloweenPumpkin3.h"
#include "ChunkedDestructibleHalloweenPumpkin4.h"
#include "ChunkedDestructibleHalloweenPumpkin5.h"
#include "DestructibleBoxCollision.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"

namespace
{
	struct FTestWorld
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FTestWorld()
		{
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}
		~FTestWorld()
		{
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleQueueTest,
	"MyProject6.Destructible.CancelledDestruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleQueueTest::RunTest(const FString& Parameters)
{
	FTestWorld Fixture;
	AChunkedDestructibleFloor* Floor = Fixture.World->SpawnActor<AChunkedDestructibleFloor>();
	Floor->ConfigureFloor(FVector2D(400.0), FVector(100.0, 100.0, 50.0), FVector2D::ZeroVector, false);
	Floor->FlashDuration = 0.0f;
	Floor->bPlayDestroyTickSound = false;
	const FIntPoint Cell(1, 1);
	const int32 Index = Floor->ToCellIndex(Cell);
	Floor->QueueCellDestruction(Index, 5.0f, true);
	Floor->RestoreCell(Cell);
	TestTrue(TEXT("Restoring removes the old deadline"), Floor->QueuedDestructions.IsEmpty());
	TestFalse(TEXT("Cancelling the last request stops actor tick immediately"), Floor->IsActorTickEnabled());
	Floor->QueueCellDestruction(Index, 10.0f, true);
	Floor->ProcessQueuedDestructions(5.0);
	TestTrue(TEXT("Old deadline cannot destroy a newly scheduled cell"), Floor->Cells[Index].bExists);
	Floor->ProcessQueuedDestructions(10.0);
	TestFalse(TEXT("New deadline still executes"), Floor->Cells[Index].bExists);
	Floor->RestoreCell(Cell);
	Floor->QueueCellDestruction(Index, 5.0f, true);
	Floor->SetCellDestructible(Cell, false);
	TestTrue(TEXT("Invulnerability cancels queued work"), Floor->QueuedDestructions.IsEmpty());
	TestFalse(TEXT("Invulnerability leaves no idle tick"), Floor->IsActorTickEnabled());
	Floor->SetCellDestructible(Cell, true);
	Floor->QueueCellDestruction(Index, 20.0f, true);
	Floor->QueueCellDestruction(0, 8.0f, true);
	Floor->QueueCellDestruction(1, 12.0f, true);
	Floor->RestoreCell(Cell);
	Floor->ProcessQueuedDestructions(9.0);
	TestFalse(TEXT("Remaining heap executes the earlier deadline"), Floor->Cells[0].bExists);
	TestTrue(TEXT("Remaining heap preserves the later deadline"), Floor->Cells[1].bExists);
	Floor->RestoreAllCells();
	TestFalse(TEXT("RestoreAll stops an empty destruction queue"), Floor->IsActorTickEnabled());
	Floor->QueueCellDestruction(Index, 1.0f, true);
	Floor->SetAllCellsDestructible(false);
	TestFalse(TEXT("SetAllCellsDestructible stops an empty queue"), Floor->IsActorTickEnabled());
	// Flash cleanup must not affect music on the actual planar surface.
	Floor->FlashInstances->SetStaticMesh(nullptr);
	Floor->SetReactToMusic(true);
	Floor->TriggerMusicReaction(1.0f, true);
	Floor->UpdateMusicEqualizer(Floor->GetMusicAnimationTime() + 0.05);
	TestNull(TEXT("Planar music never uses flash cubes"), Floor->MusicEqualizerInstances.Get());
	TestNotNull(TEXT("Planar music still renders after flash expiry"), Floor->PlanarAnimation.Get());
	Floor->SetReactToMusic(false);

	// Overlapping ripples can expire nonadjacent ISM slots as durations change.
	const double Now = Fixture.World->GetTimeSeconds();
	Floor->FlashDuration = 1.f;
	for (int32 X = 0; X < 4; ++X)
	{
		Floor->SpawnCellFlash(FIntPoint(X, 0), X % 2 == 0 ? Now - 2. : Now);
	}
	Floor->UpdateFlashes();
	TestEqual(TEXT("Only the two live flash instances remain"), Floor->FlashInstances->GetInstanceCount(), 2);
	for (int32 X : {1, 3})
	{
		const FTransform Expected = Floor->GetCellFlashTransform(FIntPoint(X, 0));
		const int32 Slot = Floor->ActiveFlashes.IndexOfByPredicate([&](const auto& Flash) { return Flash.Transform.Equals(Expected); });
		FTransform Actual;
		TestTrue(TEXT("Surviving ISM slots match their expiry records"), Slot != INDEX_NONE &&
			Floor->FlashInstances->GetInstanceTransform(Slot, Actual) && Actual.Equals(Expected));
	}
	for (auto& Flash : Floor->ActiveFlashes) Flash.ExpireTime = Now;
	Floor->UpdateFlashes();
	TestNull(TEXT("The last flash releases its render source"), Floor->FlashInstances->GetStaticMesh());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDecorativeFloorImpactCollisionTest,
	"MyProject6.Destructible.DecorativeFloorImpactCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDecorativeFloorImpactCollisionTest::RunTest(const FString&)
{
	FTestWorld Fixture;
	auto* Floor = Fixture.World->SpawnActor<AChunkedDestructibleDecorativeFloor>();
	Floor->ConfigureDecorativeFloor(EChunkedDecorativeFloorShape::Circle, FVector2D(1200),
		FVector(100, 100, 50), FVector2D::ZeroVector, false);
	auto HitsFloor = [&](FVector Position)
	{
		FHitResult Hit;
		return Fixture.World->LineTraceSingleByChannel(Hit, Position + FVector(0, 0, 150),
			Position - FVector(0, 0, 150), ECC_GameTraceChannel1) && Hit.GetActor() == Floor;
	};
	TInlineComponentArray<USculptureMeshComponent*> Chunks(Floor);
	auto CachedUpdates = [&]()
	{
		int32 Total = 0;
		for (const auto* Chunk : Chunks) { Total += Chunk->GetCachedCollisionUpdates(); }
		return Total;
	};
	const FVector Center(-150, -150, 0), Edge(-535, -240, 0), Outside(-570, -280, 0);
	FIntPoint Cell = FIntPoint::NoneValue;
	if (!TestTrue(TEXT("Center maps to an intact floor cell"), HitsFloor(Center) && Floor->WorldLocationToCell(Center, Cell))) return false;
	const int32 Before = CachedUpdates();
	TestTrue(TEXT("Interior cell breaks"), Floor->DestroyCell(Cell));
	TestTrue(TEXT("Unchanged edge hulls were reused"), CachedUpdates() > Before);
	TestFalse(TEXT("The hole loses collision immediately"), HitsFloor(Center));
	TestTrue(TEXT("Adjacent box still supports the player"), HitsFloor(Center + FVector(100, 0, 0)));
	TestTrue(TEXT("The clipped edge remains solid"), HitsFloor(Edge));
	TestFalse(TEXT("The clipped corner stays outside"), HitsFloor(Outside));

	FIntPoint EdgeCell = FIntPoint::NoneValue;
	if (!TestTrue(TEXT("Edge maps to a clipped cell"), Floor->WorldLocationToCell(Edge, EdgeCell))) return false;
	TestTrue(TEXT("Clipped cell breaks"), Floor->DestroyCell(EdgeCell));
	TestFalse(TEXT("Removed convex hulls lose collision immediately"), HitsFloor(Edge));
	TestTrue(TEXT("Restoring both cells restores their original collision"),
		Floor->RestoreCell(EdgeCell) && Floor->RestoreCell(Cell) && HitsFloor(Edge) && HitsFloor(Center));
	TestFalse(TEXT("Restoring the edge preserves its outline"), HitsFloor(Outside));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleCollisionTest,
	"MyProject6.Destructible.MergedCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleCollisionTest::RunTest(const FString& Parameters)
{
	// Three-axis merging must preserve holes and tiny gaps on every axis.
	for (const FVector Spacing : {FVector::ZeroVector, FVector(.001, 0, 0), FVector(0, .001, 0), FVector(0, 0, .001), FVector(.001)})
	{
		const FIntVector Minimum(2, 3, 4), Count(4, 3, 2);
		const FVector BlockSize(10, 20, 30), GridMinimum(-100, -200, -300);
		TBitArray<> Occupied(true, 24);
		Occupied[5] = false;
		FKAggregateGeom Collision;
		DestructibleCollision::AppendMergedBoxes(Collision, Minimum, Minimum + Count, BlockSize, Spacing, GridMinimum, Occupied);
		double Volume = 0;
		for (const FKBoxElem& Box : Collision.BoxElems) Volume += double(Box.X) * Box.Y * Box.Z;
		TestEqual(TEXT("3D merge preserves exact occupied volume"), Volume, 23. * 10 * 20 * 30);
		if (Spacing.IsZero()) TestTrue(TEXT("Solid slabs merge across all three axes"), Collision.BoxElems.Num() < 10);
		for (int32 Z = 0; Z < Count.Z; ++Z) for (int32 Y = 0; Y < Count.Y; ++Y) for (int32 X = 0; X < Count.X; ++X)
		{
			const FVector Corner = GridMinimum + FVector(Minimum + FIntVector(X, Y, Z)) * (BlockSize + Spacing);
			auto Coverage = [&](FVector Point)
			{
				int32 Hits = 0;
				for (const FKBoxElem& Box : Collision.BoxElems)
				{
					const FVector Delta = (Point - Box.Center).GetAbs();
					Hits += Delta.X < Box.X * .5 && Delta.Y < Box.Y * .5 && Delta.Z < Box.Z * .5 ? 1 : 0;
				}
				return Hits;
			};
			TestEqual(TEXT("No filled holes or overlapping collision"), Coverage(Corner + BlockSize * .5), X + Count.X * (Y + Count.Y * Z) == 5 ? 0 : 1);
			for (int32 Axis = 0; Axis < 3; ++Axis) if (Spacing[Axis] > 0)
			{
				FVector Gap = Corner + BlockSize * .5;
				Gap[Axis] = Corner[Axis] + BlockSize[Axis] + Spacing[Axis] * .5;
				TestEqual(TEXT("Even sub-millimetre gaps remain open"), Coverage(Gap), 0);
			}
		}
	}
	// A hole splits a run, and even a sub-centimetre gap must remain a gap.
	for (double Spacing : {0.0, 0.001, 5.0})
	{
		FKAggregateGeom Collision;
		DestructibleCollision::AppendBoxRuns(Collision, FIntVector(1, 2, 3), FIntVector(7, 3, 4),
			FVector(10.0, 20.0, 30.0), FVector(Spacing, 2.0, 4.0), FVector(-100.0, -200.0, -300.0),
			[](int32 X, int32, int32) { return X != 4; });
		TestEqual(TEXT("Only touching cells merge"), Collision.BoxElems.Num(), Spacing == 0.0 ? 2 : 5);
		double Volume = 0.0;
		for (const FKBoxElem& Box : Collision.BoxElems)
		{
			Volume += Box.X * Box.Y * Box.Z;
		}
		TestEqual(TEXT("Merging preserves solid volume"), Volume, 5.0 * 10.0 * 20.0 * 30.0);
	}

	FTestWorld Fixture;
	const TArray<UClass*> Classes = {
		AChunkedDestructibleHalloweenPumpkin::StaticClass(),
		AChunkedDestructibleHalloweenPumpkin2::StaticClass(),
		AChunkedDestructibleHalloweenPumpkin3::StaticClass(),
		AChunkedDestructibleHalloweenPumpkin4::StaticClass(),
		AChunkedDestructibleHalloweenPumpkin5::StaticClass()
	};
	for (UClass* Class : Classes)
	{
		AChunkedDestructibleFloor* Actor = Fixture.World->SpawnActor<AChunkedDestructibleFloor>(Class);
		Actor->FlashDuration = 0.0f;
		Actor->bPlayDestroyTickSound = false;
		auto Verify = [this, Actor, Class](const TCHAR* Phase)
		{
			TArray<FKBoxElem> Boxes;
			for (int32 ChunkIndex = 0; ChunkIndex < Actor->ChunkComponents.Num(); ++ChunkIndex)
			{
				FKAggregateGeom Collision;
				Actor->BuildChunkSimpleCollision(ChunkIndex, Collision);
				Boxes.Append(Collision.BoxElems);
			}
			int32 LiveCells = 0;
			for (const auto& Cell : Actor->Cells) { LiveCells += Cell.bExists ? 1 : 0; }
			double Volume = 0.0;
			for (const FKBoxElem& Box : Boxes) { Volume += static_cast<double>(Box.X) * Box.Y * Box.Z; }
			const FVector Size = Actor->EffectiveCellDimensions;
			const double ExpectedVolume = LiveCells * Size.X * Size.Y * Size.Z;
			TestTrue(TEXT("Real actor collision preserves volume"), FMath::IsNearlyEqual(Volume, ExpectedVolume, ExpectedVolume * 1.e-6));
			TestTrue(TEXT("Collision has no more boxes than the original"), Boxes.Num() <= LiveCells);
			// Sample both intact cells and carved openings throughout the generated volume.
			const int32 Stride = FMath::Max(Actor->Cells.Num() / 128, 1);
			for (int32 CellIndex = 0; CellIndex < Actor->Cells.Num(); CellIndex += Stride)
			{
				const FVector Center = Actor->GetCellLocalCenter(Actor->FromCellIndex(CellIndex));
				int32 CoverCount = 0;
				for (const FKBoxElem& Box : Boxes)
				{
					const FVector Delta = (Center - Box.Center).GetAbs();
					CoverCount += Delta.X < Box.X * 0.5 && Delta.Y < Box.Y * 0.5 && Delta.Z < Box.Z * 0.5 ? 1 : 0;
				}
				TestEqual(TEXT("Solid cells remain solid; holes remain empty; boxes do not overlap"),
					CoverCount, Actor->Cells[CellIndex].bExists ? 1 : 0);
			}
			AddInfo(FString::Printf(TEXT("%s %s: %d original boxes -> %d merged boxes"),
				*Class->GetName(), Phase, LiveCells, Boxes.Num()));
		};
		Verify(TEXT("intact"));
		for (int32 CellIndex = 0; CellIndex < Actor->Cells.Num(); ++CellIndex)
		{
			if (Actor->Cells[CellIndex].bExists)
			{
				Actor->DestroyCell(Actor->FromCellIndex(CellIndex));
				break;
			}
		}
		Verify(TEXT("destroyed"));
		Actor->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleStructureTest,
	"MyProject6.Destructible.HouseSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleStructureTest::RunTest(const FString& Parameters)
{
	FTestWorld Fixture;
	AChunkedDestructibleHouse* House = Fixture.World->SpawnActor<AChunkedDestructibleHouse>();
	auto VerifyProfiles = [this](AChunkedDestructibleHouse* Building)
	{
		int32 Occupied = 0;
		const FIntPoint Count = Building->GetCellCount();
		for (int32 Z = 0; Z < Count.Y; ++Z)
		{
			for (int32 XY = 0; XY < Count.X; ++XY)
			{
				const FIntPoint Cell(XY, Z);
				TArray<FVector2D> Expected;
				const bool bExists = Building->GetCellPolygonXZ(Cell, Expected);
				AChunkedDestructibleHouse::FCellProfile Fallback;
				const auto* Profile = Building->FindCellProfile(Cell, Fallback);
				if (bExists != (Profile != nullptr) || (Profile &&
					(Profile->Polygon != Expected || Profile->Bounds != FBox2D(Expected))))
				{
					AddError(FString::Printf(TEXT("House profile changed at (%d, %d)"), XY, Z));
					return;
				}
				Occupied += bExists;
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d occupied cells, %d stored profiles"),
			*Building->GetClass()->GetName(), Occupied, Building->CellProfiles.Num()));
		TestTrue(TEXT("Matching profiles share storage across the depth of the house"),
			Building->CellProfiles.Num() > 0 && Building->CellProfiles.Num() < Occupied);
	};
	VerifyProfiles(House);
	auto* Facade = Fixture.World->SpawnActor<ASimpleHouseActorDestructable>(FVector(10000, 0, 0), FRotator::ZeroRotator);
	VerifyProfiles(Facade);
	House->bCollapseUnsupportedSections = true;
	House->CollapseDelayRange = FVector2D(10.0);
	House->bScatterDestroyedPieces = false;
	for (auto& Cell : House->Cells) { Cell.bExists = false; }
	TArray<int32> Supported;
	for (const FIntVector Position : {FIntVector(1, 1, 0), FIntVector(1, 1, 1), FIntVector(2, 1, 1), FIntVector(3, 1, 1)})
	{
		const int32 Index = House->ToCellIndex(House->EncodeCell(Position.X, Position.Y, Position.Z));
		House->Cells[Index].bExists = true;
		Supported.Add(Index);
	}
	const int32 Floating = House->ToCellIndex(House->EncodeCell(5, 1, 2));
	House->Cells[Floating].bExists = true;
	House->EvaluateStructuralIntegrity();
	for (const int32 Index : Supported)
	{
		TestFalse(TEXT("Connected cantilever remains supported"), House->PendingCellIndices.Contains(Index));
	}
	TestTrue(TEXT("Disconnected section collapses"), House->PendingCellIndices.Contains(Floating));
	House->ScheduleStructuralIntegrityCheck();
	House->GenerateFloor(false);
	TestFalse(TEXT("Regeneration cancels pending support check"), House->bIntegrityCheckQueued);
	TestFalse(TEXT("Regeneration clears support timer"), Fixture.World->GetTimerManager().TimerExists(House->StructuralIntegrityTimerHandle));
	return true;
}

#endif
