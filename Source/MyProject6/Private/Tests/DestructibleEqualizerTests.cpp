#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructiblePyramid.h"
#include "ChunkedDestructibleConeStructure.h"
#include "MusicReactionTrack.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleEqualizerTest,
	"MyProject6.Animation.EqualizerFilledColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleEqualizerTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

	for (UClass* Class : { AChunkedDestructibleFloor::StaticClass(), AChunkedDestructiblePyramid::StaticClass(),
		AChunkedDestructibleConeStructure::StaticClass() })
	{
		FActorSpawnParameters Spawn;
		Spawn.bDeferConstruction = true;
		auto* Actor = World->SpawnActor<AChunkedDestructibleFloor>(Class, FTransform::Identity, Spawn);
		if (!TestNotNull(TEXT("Equalizer actor spawns"), Actor)) { return false; }
		Actor->ConeAnimationType = EConeStructureAnimationType::None;
		Actor->FlashDuration = 0;
		Actor->bPlayDestroyTickSound = false;
		if (auto* Pyramid = Cast<AChunkedDestructiblePyramid>(Actor))
		{
			FChunkedPyramidShapeSettings Shape;
			Shape.BaseWidth = 640;
			Shape.Height = 560;
			Shape.PieceSize = 80;
			Pyramid->ConfigurePyramid(Shape, FChunkedPyramidStepSettings(), FChunkedPyramidEntranceSettings(),
				FChunkedPyramidInteriorFloorSettings());
		}
		else if (Actor->UsesPlanarAnimation())
		{
			Actor->ConfigureFloor(FVector2D(800, 700), FVector(80, 80, 40), FVector2D(2), false);
		}
		Actor->DispatchBeginPlay();
		Actor->SetMusicAnimationType(EMusicAnimationType::Equalizer);
		Actor->SetReactToMusic(true);
		Actor->MusicReactionRandomStream.Initialize(12345);
		FMusicReactionEvent Event;
		Event.Type = EMusicReactionEventType::Onset;
		Event.Priority = 10;
		Event.TimeSeconds = 10;
		Event.Duration = 1;
		Event.Strength = 1;
		Actor->ApplyMusicEvent(Event, 10);
		TestEqual(TEXT("Every stationary column responds"), Actor->ActiveMusicEqualizerColumns.Num(), Actor->MusicEqualizerColumnCount);
		TSet<int32> PeakRows;
		for (const auto& Column : Actor->ActiveMusicEqualizerColumns)
		{
			PeakRows.Add(FMath::RoundToInt(Column.PeakHeight * Actor->MusicEqualizerRowCount));
		}
		TestTrue(TEXT("Columns have distinct music-driven levels"), PeakRows.Num() > 1);

		TArray<int32> LitFrames;
		LitFrames.Init(0, Actor->MusicEqualizerRowCount);
		for (int32 Frame = 1; Frame <= 24; ++Frame)
		{
			const double Now = 10.0 + Frame * 0.04;
			Actor->UpdateMusicEqualizer(Now);
			if (Actor->UsesPlanarAnimation())
			{
				TestEqual(TEXT("Mask has the configured resolution"), Actor->PlanarMusicMask.Num(),
					Actor->MusicEqualizerColumnCount * Actor->MusicEqualizerRowCount);
				for (int32 Column = 0; Column < Actor->MusicEqualizerColumnCount; ++Column)
				{
					bool bUnlitBelow = false;
					for (int32 Row = 0; Row < Actor->MusicEqualizerRowCount; ++Row)
					{
						const bool bLit = Actor->PlanarMusicMask[Row * Actor->MusicEqualizerColumnCount + Column] != 0;
						TestFalse(TEXT("No lit segment floats above a dark lower row"), bUnlitBelow && bLit);
						bUnlitBelow |= !bLit;
						LitFrames[Row] += bLit ? 1 : 0;
					}
				}
			}
			else
			{
				for (const auto& Column : Actor->ActiveMusicEqualizerColumns)
				{
					const int32 FilledRows = FMath::RoundToInt(Column.GetEqualizerHeight(Now) * Actor->MusicEqualizerRowCount);
					for (int32 Row = 0; Row < FilledRows; ++Row)
					{
						const int32 SlotIndex = Column.ColumnIndex * Actor->MusicEqualizerRowCount + Row;
						if (Actor->UsesMusicPieceMaterials())
						{
							for (int32 Cell : Actor->MusicEqualizerSlotCells[SlotIndex])
							{
								TestTrue(TEXT("All lower real pieces glow, including pieces sharing a bin"), Actor->MusicEqualizerUsedCellBuffer.Contains(Cell));
							}
						}
						else
						{
							const auto& Slot = Actor->MusicEqualizerSlots[SlotIndex];
							if (Slot.CellIndex == INDEX_NONE) { continue; }
							FTransform Transform;
							Actor->MusicEqualizerInstances->GetInstanceTransform(SlotIndex, Transform, false);
							FTransform Expected = Slot.Transform;
							Expected.SetScale3D(Expected.GetScale3D() * Actor->MusicReactionSurfaceScale);
							TestTrue(TEXT("A lit box stays at its fixed position and size"), Transform.Equals(Expected));
						}
					}
				}
			}
		}
		if (Actor->UsesPlanarAnimation())
		{
			TestTrue(TEXT("The base is lit for longer than the upper rows"), LitFrames[0] > LitFrames.Last());
			for (int32 Row = 1; Row < LitFrames.Num(); ++Row)
			{
				TestTrue(TEXT("Lit probability never increases with height"), LitFrames[Row - 1] >= LitFrames[Row]);
			}
		}
		else if (Actor->MusicEqualizerInstances)
		{
			TestEqual(TEXT("Pool fits every column at full height"), Actor->MusicEqualizerInstances->GetInstanceCount(),
				Actor->MusicEqualizerColumnCount * Actor->MusicEqualizerRowCount);
		}

		Actor->ResetMusicPlayback();
		Actor->ApplyMusicEvent(Event, 10);
		TArray<float> PreviousHeights;
		for (const auto& Column : Actor->ActiveMusicEqualizerColumns) { PreviousHeights.Add(Column.GetEqualizerHeight(10.25)); }
		Event.TimeSeconds = 10.25;
		Actor->ApplyMusicEvent(Event, 10.25);
		for (const auto& Column : Actor->ActiveMusicEqualizerColumns)
		{
			TestEqual(TEXT("Re-trigger retains the current height without a base reset"), Column.GetEqualizerHeight(10.25), PreviousHeights[Column.ColumnIndex]);
		}

		// Force a full-height bar to exercise the entire pool and destruction during a filled frame.
		for (auto& Column : Actor->ActiveMusicEqualizerColumns) { Column.PeakHeight = 1; }
		Actor->UpdateMusicEqualizer(10.4);
		int32 Destroyed = INDEX_NONE;
		for (int32 Index = 0; Index < Actor->Cells.Num(); ++Index)
		{
			if (Actor->Cells[Index].bExists && Actor->DestroyCell(Actor->FromCellIndex(Index))) { Destroyed = Index; break; }
		}
		Actor->UpdateMusicEqualizer(10.45);
		if (!TestTrue(TEXT("Destruction remains available during equalizer playback"), Destroyed != INDEX_NONE)) { return false; }
		TestFalse(TEXT("Destroyed pieces cannot be highlighted"), Actor->MusicEqualizerUsedCellBuffer.Contains(Destroyed));
		Actor->UpdateMusicEqualizer(12);
		TestFalse(TEXT("Quiet equalizer stops ticking"), Actor->IsActorTickEnabled());
		TestTrue(TEXT("Quiet equalizer releases active levels"), Actor->ActiveMusicEqualizerColumns.IsEmpty());

		if (Actor->UsesPlanarAnimation())
		{
			FDestructiblePlanarFlow Flow;
			Flow.Mode = EPlanarFlowMode::Radial;
			Flow.bReverse = true;
			Actor->SetPlanarFlow(Flow);
			for (EMusicEqualizerBaseEdge Edge : { EMusicEqualizerBaseEdge::MinX, EMusicEqualizerBaseEdge::MaxX,
				EMusicEqualizerBaseEdge::MinY, EMusicEqualizerBaseEdge::MaxY })
			{
				Actor->SetMusicEqualizerBaseEdge(Edge);
				const bool bY = Edge == EMusicEqualizerBaseEdge::MinY || Edge == EMusicEqualizerBaseEdge::MaxY;
				const bool bReverse = Edge == EMusicEqualizerBaseEdge::MaxX || Edge == EMusicEqualizerBaseEdge::MaxY;
				FVector2D BaseUV(.5, .5), TopUV(.5, .5);
				BaseUV[bY ? 1 : 0] = bReverse ? 1 : 0;
				TopUV[bY ? 1 : 0] = bReverse ? 0 : 1;
				auto Coordinates = [Actor](FVector2D UV)
				{
					const FVector2D Point = Actor->GridLocalMin + UV * Actor->EffectiveGridSize;
					return Actor->GetMusicEqualizerCoordinates(FVector(Point.X, Point.Y, 0));
				};
				TestTrue(TEXT("Selected base edge stays at zero despite radial/reverse fixed flow"), FMath::IsNearlyZero(Coordinates(BaseUV).X));
				TestTrue(TEXT("Opposite edge is the top of the bar"), FMath::IsNearlyEqual(Coordinates(TopUV).X, 1.0));
			}
		}
		Actor->SetMusicAnimationType(EMusicAnimationType::TravelingColumns);
		TestTrue(TEXT("Changing type clears the old bars"), Actor->ActiveMusicEqualizerColumns.IsEmpty());
		Actor->SetReactToMusic(false);
		TestTrue(TEXT("Disabling releases the complete-piece layout"), Actor->MusicEqualizerSlotCells.IsEmpty());
		TestFalse(TEXT("Disabling does not restore destroyed geometry"), Actor->Cells[Destroyed].bExists);
		Actor->Destroy();
	}
	return true;
}
#endif
