#if WITH_DEV_AUTOMATION_TESTS

#include "MusicReactiveColumnGrid.h"
#include "MusicReactionWorldSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMusicReactiveColumnGridTest,
	"MyProject6.Animation.MusicReactiveColumnGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMusicReactiveColumnGridTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Grid = World->SpawnActor<AMusicReactiveColumnGrid>();
	if (!TestNotNull(TEXT("Column grid actor spawns"), Grid)) { return false; }
	Grid->ConfigureGrid(50, 1050, 650, 0);
	Grid->ConfigureHeights(20, 150, 450);
	Grid->WaveWidth = 360;
	Grid->SetWaveSpeed(600);
	Grid->DispatchBeginPlay();
	auto* Music = World->GetSubsystem<UMusicReactionWorldSubsystem>();
	TestTrue(TEXT("Standalone grid registers with the shared music system"), Music->ReactionActors.Contains(Grid));
	TestFalse(TEXT("Idle grid has no actor tick"), Grid->IsActorTickEnabled());
	TestEqual(TEXT("Width determines the number of squares"), Grid->GetColumnCount().X, 21);
	TestEqual(TEXT("Length determines the number of squares"), Grid->GetColumnCount().Y, 13);
	TestEqual(TEXT("One mesh instance per square column"), Grid->Columns->GetInstanceCount(), 273);
	TestEqual(TEXT("Generated width is correct"), Grid->GetGeneratedSize().X, 1050.0);
	TestEqual(TEXT("Generated length is correct"), Grid->GetGeneratedSize().Y, 650.0);
	const auto Index = [Grid](int32 X, int32 Y) { return Y * Grid->ColumnCount.X + X; };
	const int32 Center = Index(10, 6), AlongX = Index(16, 6), AlongY = Index(10, 12), Corner = Index(20, 12);
	FMusicReactionEvent Event;
	Event.TimeSeconds = 5;
	Event.Strength = 1;
	Event.Duration = 0;
	Grid->SetWaveDirection(EColumnGridWaveDirection::Outward);
	Music->DispatchReaction(Event, 5);
	TestEqual(TEXT("A routed music cue creates one outward wave"), Grid->ActivePulses.Num(), 1);
	TestTrue(TEXT("Circular center rises before the edge"), Grid->EvaluateHeight(Center, 5.2) > Grid->RestHeight &&
		Grid->EvaluateHeight(Corner, 5.2) == Grid->RestHeight);
	TestEqual(TEXT("Equal circular radii move together on a non-square rectangle"),
		Grid->EvaluateHeight(AlongX, 5.7), Grid->EvaluateHeight(AlongY, 5.7), .001f);
	TestTrue(TEXT("The circular crest propagates away from the center"), Grid->EvaluateHeight(AlongX, 5.7) > Grid->RestHeight &&
		Grid->EvaluateHeight(Center, 5.7) == Grid->RestHeight);
	TestTrue(TEXT("The reference-style wave includes a recessed trough"), Grid->EvaluateHeight(Center, 5.45) < Grid->RestHeight);
	Grid->UpdateColumns(5.7);
	for (int32 I = 0; I < Grid->Columns->GetInstanceCount(); ++I)
	{
		FTransform Transform;
		Grid->Columns->GetInstanceTransform(I, Transform, false);
		TestEqual(TEXT("Animated column width stays constant"), Transform.GetScale3D().X * 100, 50.0);
		TestEqual(TEXT("Animated column top remains square"), Transform.GetScale3D().Y, Transform.GetScale3D().X);
		TestTrue(TEXT("The column base remains anchored at local Z zero"),
			FMath::IsNearlyZero(Transform.GetLocation().Z - Transform.GetScale3D().Z * 50, .001));
		TestTrue(TEXT("Columns never slide sideways"), FVector2D(Transform.GetLocation()) == Grid->ColumnCenters[I]);
	}
	Grid->SetWaveDirection(EColumnGridWaveDirection::Inward);
	Music->DispatchReaction(Event, 5);
	TestTrue(TEXT("Inward wave reaches the outer edge before the center"), Grid->EvaluateHeight(Corner, 5.2) > Grid->RestHeight &&
		Grid->EvaluateHeight(Center, 5.2) == Grid->RestHeight);
	const double Arrival = 5 + Grid->MaximumDistance / 600 + .2;
	TestTrue(TEXT("Inward wave reaches the center later"), Grid->EvaluateHeight(Center, Arrival) > Grid->RestHeight &&
		Grid->EvaluateHeight(Corner, Arrival) == Grid->RestHeight);
	Grid->SetReactionType(EColumnGridReactionType::RectangularWave);
	Grid->SetWaveDirection(EColumnGridWaveDirection::Outward);
	Music->DispatchReaction(Event, 5);
	const float EdgeHeight = Grid->EvaluateHeight(Index(20, 6), 5.7);
	TestTrue(TEXT("Rectangular wave reaches its outer frame"), EdgeHeight > Grid->RestHeight);
	TestEqual(TEXT("Long and short rectangle edges rise together"), EdgeHeight, Grid->EvaluateHeight(AlongY, 5.7), .001f);
	TestEqual(TEXT("Rectangle corners belong to the same frame"), EdgeHeight, Grid->EvaluateHeight(Corner, 5.7), .001f);
	Grid->SetWaveDirection(EColumnGridWaveDirection::Alternating);
	Music->DispatchReaction(Event, 5);
	Event.TimeSeconds = 5.2f;
	Music->DispatchReaction(Event, 5.2);
	if (TestEqual(TEXT("Separated music cues produce separate waves"), Grid->ActivePulses.Num(), 2))
	{
		TestFalse(TEXT("Alternating starts outward"), Grid->ActivePulses[0].bInward);
		TestTrue(TEXT("Next cue travels inward"), Grid->ActivePulses[1].bInward);
	}
	Grid->ResetMusicPlayback();
	Grid->SetWaveDirection(EColumnGridWaveDirection::Outward);
	Grid->StartPulse(1, Grid->GetAnimationTime() - .2, 0, 100);
	const float HeightBeforeSpeedChange = Grid->EvaluateHeight(Center, Grid->GetAnimationTime());
	const double PreviousTravelTime = Grid->ActivePulses[0].TravelTime;
	Grid->SetWaveSpeed(1200);
	TestEqual(TEXT("Speed change preserves current surface height"), Grid->EvaluateHeight(Center, Grid->GetAnimationTime()), HeightBeforeSpeedChange, .001f);
	TestEqual(TEXT("Twice the speed halves travel time"), Grid->ActivePulses[0].TravelTime, PreviousTravelTime * .5, .00001);

	Grid->SetReactionType(EColumnGridReactionType::Equalizer);
	Event.TimeSeconds = 10;
	Music->DispatchReaction(Event, 10);
	Grid->UpdateColumns(10.15);
	float Min = Grid->MaximumHeight, Max = Grid->MinimumHeight;
	for (float Height : Grid->ColumnHeights) { Min = FMath::Min(Min, Height); Max = FMath::Max(Max, Height); }
	TestTrue(TEXT("Equalizer raises columns to different music-driven heights"), Min > Grid->RestHeight && Max > Min + 10);
	TestTrue(TEXT("Equalizer respects height limits"), Min >= Grid->MinimumHeight && Max <= Grid->MaximumHeight);
	Grid->UpdateColumns(12);
	TestFalse(TEXT("Completed response stops actor ticking"), Grid->IsActorTickEnabled());
	for (float Height : Grid->ColumnHeights) { TestEqual(TEXT("Completed response returns to rest"), Height, Grid->RestHeight); }

	Grid->MusicReactionGroup = TEXT("Columns");
	Event.TargetGroup = TEXT("Other");
	Event.TimeSeconds = 15;
	Music->DispatchReaction(Event, 15);
	TestTrue(TEXT("Unrelated target groups leave the grid idle"), Grid->ActivePulses.IsEmpty());
	Event.TargetGroup = TEXT("Columns");
	Music->DispatchReaction(Event, 15);
	TestFalse(TEXT("Matching target group reaches the grid"), Grid->ActivePulses.IsEmpty());
	Music->ResetTimeline();
	TestTrue(TEXT("Seeking or looping resets standalone actor motion"), Grid->ActivePulses.IsEmpty());
	Grid->SetMusicSources(true, true, true);
	Event.Type = EMusicReactionEventType::Onset;
	Event.Priority = 10;
	Music->DispatchReaction(Event, 15);
	Event.Type = EMusicReactionEventType::Beat;
	Event.Priority = 1;
	Music->DispatchReaction(Event, 15);
	TestEqual(TEXT("Same-beat onset and beat do not double the motion"), Grid->ActivePulses.Num(), 1);
	Grid->SetMusicSources(false, false, false);
	TestTrue(TEXT("Disabling all sources unregisters the actor"), Music->ReactionActors.IsEmpty());
	TestFalse(TEXT("Disabling all sources stops motion"), Grid->IsActorTickEnabled());
	Grid->SetMusicSources(true, true, true);
	TestTrue(TEXT("Re-enabling sources registers again"), Music->ReactionActors.Contains(Grid));
	Grid->SetReactToMusic(false);
	TestTrue(TEXT("Turning music off releases the listener"), Music->ReactionActors.IsEmpty());

	Grid->ConfigureGrid(60, 1000, 730, 4);
	TestTrue(TEXT("Non-divisible dimensions fit whole square columns inside the requested area"),
		Grid->GeneratedSize.X <= 1000 && Grid->GeneratedSize.Y <= 730 && Grid->SquareSize == 60);
	Grid->ConfigureGrid(-10, std::numeric_limits<float>::quiet_NaN(), 1000000, -1);
	Grid->ConfigureHeights(-10, std::numeric_limits<float>::quiet_NaN(), -1);
	TestTrue(TEXT("Invalid dimensions and very large grids have bounded allocation"), Grid->SquareSize >= 1 &&
		Grid->Gap >= 0 && Grid->Columns->GetInstanceCount() <= 128 * 128);
	for (const FTransform& Transform : Grid->ColumnTransforms) { TestFalse(TEXT("Sanitized transforms stay finite"), Transform.ContainsNaN()); }
	Grid->Destroy();
	return true;
}
#endif
