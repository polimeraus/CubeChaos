// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleHouse.h"
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
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleCollisionTest,
	"MyProject6.Destructible.MergedCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleCollisionTest::RunTest(const FString& Parameters)
{
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
