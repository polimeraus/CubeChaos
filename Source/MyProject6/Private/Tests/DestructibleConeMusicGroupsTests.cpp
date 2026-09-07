#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedDestructibleConeStructure.h"
#include "CannonballProjectile.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleConeMusicGroupsTest,
	"MyProject6.Animation.ConeMusicGroupedSquares",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleConeMusicGroupsTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Spawn;
	Spawn.bDeferConstruction = true;
	auto* Cone = World->SpawnActor<AChunkedDestructibleConeStructure>(AChunkedDestructibleConeStructure::StaticClass(), FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Grouped cone spawns"), Cone)) { return false; }
	TestEqual(TEXT("Default glow size matches the right-click destruction diameter"), Cone->GetMusicGlowGroupSize(),
		GetDefault<ACannonballProjectile>()->GetDestructionDiameter());
	Cone->ConeAnimationType = EConeStructureAnimationType::None;
	Cone->bGeneratePlatforms = false;
	Cone->bGenerateEntrance = false;
	Cone->bScatterDestroyedPieces = false;
	Cone->bPlayDestroyTickSound = false;
	Cone->FlashDuration = 0;
	Cone->ConfigureConeStructure(12, 5, 2, .6f, FVector(50), FVector::ZeroVector);
	Cone->DispatchBeginPlay();
	TestTrue(TEXT("Disabled music allocates no grouped layout"), Cone->MusicGlowGroups.IsEmpty());
	Cone->SetReactToMusic(true);
	Cone->EnsureMusicGlowGroups();
	const int32 InitialGroupCount = Cone->MusicGlowGroups.Num();
	int32 GroupIndex = INDEX_NONE;
	for (int32 Index = 0; Index < InitialGroupCount; ++Index)
	{
		const auto& Group = Cone->MusicGlowGroups[Index];
		if (Group.Num() > 2 && Cone->GetMusicGlowGroupKey(Group[0], 200).Z == INDEX_NONE)
		{
			GroupIndex = Index;
			break;
		}
	}
	if (!TestTrue(TEXT("A 200 cm square combines multiple small shell pieces"), GroupIndex != INDEX_NONE)) { return false; }
	const TArray<int32> Members = Cone->MusicGlowGroups[GroupIndex];
	const int32 Seed = Members[0];
	Cone->RenderMusicEqualizerCells(MakeArrayView(&Seed, 1));
	TestNotNull(TEXT("Traveling uses a surface renderer"), Cone->MusicSquareComponent.Get());
	TestTrue(TEXT("The surface renderer has no collision"), Cone->MusicSquareComponent &&
		Cone->MusicSquareComponent->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
	if (Cone->MusicSquareComponent)
	{
		const auto* Surface = Cone->MusicSquareComponent->GetMesh();
		TestTrue(TEXT("Selected square contains visible surface triangles"), Surface->TriangleCount() > 0);
		for (int32 Triangle : Surface->TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Surface->GetTriVertices(Triangle, A, B, C);
			const FVector3d Radial(A.X + B.X + C.X, A.Y + B.Y + C.Y, 0);
			TestTrue(TEXT("Square winding faces outward in Unreal"), (B - A).Cross(C - A).Dot(Radial) < 0);
		}
	}
	// Measure the actual clipped surface, including rows near the tip, rather than
	// only checking which backing cells were assigned the same logical group.
	const double Height = Cone->BlockDimensions.Z * Cone->GridHeightBlocks;
	const double Slope = (Cone->TopRadiusBlocks - Cone->BottomRadiusBlocks) * Cone->CachedRadialBlockUnit / Height;
	const double SurfaceScale = FMath::Sqrt(1.0 + Slope * Slope);
	int32 MeasuredUpperSquares = 0;
	for (const auto& Row : Cone->MusicSquareRows)
	{
		if (Row.MinZ < Height * .6) { continue; }
		for (int32 Column = 0; Column < Row.Columns; ++Column)
		{
			const int32 Group = Row.FirstGroup + Column;
			const double Angle = -UE_PI + (Column + .5) * UE_TWO_PI / Row.Columns;
			const FVector3d Tangent(-FMath::Sin(Angle), FMath::Cos(Angle), 0);
			double MinU = UE_BIG_NUMBER, MaxU = -UE_BIG_NUMBER, MinZ = UE_BIG_NUMBER, MaxZ = -UE_BIG_NUMBER;
			for (const auto& Chunk : Cone->MusicSquareChunkTriangles)
			{
				const auto* Triangles = Chunk.Value.Find(Group);
				if (!Triangles) { continue; }
				for (const auto& Triangle : *Triangles)
				{
					for (const FVector3d& Point : { Triangle.A, Triangle.B, Triangle.C })
					{
						MinU = FMath::Min(MinU, Point.Dot(Tangent)); MaxU = FMath::Max(MaxU, Point.Dot(Tangent));
						MinZ = FMath::Min(MinZ, Point.Z); MaxZ = FMath::Max(MaxZ, Point.Z);
					}
				}
			}
			if (MaxZ < MinZ) { continue; }
			++MeasuredUpperSquares;
			TestTrue(TEXT("Upper highlights have equal tangent width and slant height"),
				FMath::IsNearlyEqual(MaxU - MinU, (MaxZ - MinZ) * SurfaceScale, .05));
		}
	}
	TestTrue(TEXT("Upper cone square geometry was measured"), MeasuredUpperSquares > 0);
	TestEqual(TEXT("One animation cell lights an entire fixed square"), Cone->MusicHighlightedCells.Num(), Members.Num());
	for (int32 Cell : Members) { TestTrue(TEXT("All square members light together"), Cone->MusicHighlightedCells.Contains(Cell)); }
	const int32 SecondSeed = Members[1];
	Cone->RenderMusicEqualizerCells(MakeArrayView(&SecondSeed, 1));
	TestEqual(TEXT("Selecting another small piece in the square preserves its extent"), Cone->MusicHighlightedCells.Num(), Members.Num());
	for (int32 Cell : Members) { TestTrue(TEXT("The grouped square does not slide with the selected small cell"), Cone->MusicHighlightedCells.Contains(Cell)); }

	const TArray<int32> OriginalGroups = Cone->MusicCellGroupIndices;
	if (!TestTrue(TEXT("A member can be destroyed during grouped animation"), Cone->DestroyCell(Cone->FromCellIndex(SecondSeed)))) { return false; }
	Cone->RenderMusicEqualizerCells(MakeArrayView(&Seed, 1));
	TestFalse(TEXT("Destroyed member stays dark"), Cone->MusicHighlightedCells.Contains(SecondSeed));
	TestEqual(TEXT("Only the destroyed member leaves the lit square"), Cone->MusicHighlightedCells.Num(), Members.Num() - 1);
	TestTrue(TEXT("Destruction does not regroup neighboring pieces"), Cone->MusicCellGroupIndices == OriginalGroups);

	Cone->SetMusicGlowGroupSize(400);
	TestTrue(TEXT("Changing size clears the old square immediately"), Cone->MusicHighlightedCells.IsEmpty());
	Cone->EnsureMusicGlowGroups();
	TestTrue(TEXT("Larger requested size uses no more squares, limited by the cone curvature"), Cone->MusicGlowGroups.Num() <= InitialGroupCount);
	Cone->SetMusicGlowGroupSize(10000);
	Cone->EnsureMusicGlowGroups();
	TestTrue(TEXT("An oversized request still produces squares that fit the cone"), !Cone->MusicSquareRows.IsEmpty());
	Cone->SetMusicGlowGroupSize(1);
	Cone->EnsureMusicGlowGroups();
	TestTrue(TEXT("Small size returns to finer pieces"), Cone->MusicGlowGroups.Num() > InitialGroupCount);
	Cone->SetMusicGlowGroupSize(200);
	Cone->EnsureMusicGlowGroups();
	TestTrue(TEXT("Returning to the same size restores exactly the same fixed partition"), Cone->MusicCellGroupIndices == OriginalGroups);

	TestEqual(TEXT("Cone traveling defaults to a slower speed"), Cone->GetTravelingColumnsSpeed(), .65f);
	Cone->TriggerMusicReaction(1, true);
	if (TestTrue(TEXT("Speed check has an active column"), !Cone->ActiveMusicEqualizerColumns.IsEmpty()))
	{
		const float SlowDuration = Cone->ActiveMusicEqualizerColumns[0].Duration;
		const double Now = Cone->GetMusicAnimationTime();
		const double Progress = (Now - Cone->ActiveMusicEqualizerColumns[0].StartTime) / SlowDuration;
		Cone->SetTravelingColumnsSpeed(1);
		const auto& Column = Cone->ActiveMusicEqualizerColumns[0];
		TestTrue(TEXT("Speed multiplier scales travel duration"), FMath::IsNearlyEqual(Column.Duration, SlowDuration * .65f));
		TestTrue(TEXT("Changing speed preserves current travel position"),
			FMath::IsNearlyEqual((Now - Column.StartTime) / Column.Duration, Progress, .0001));
	}
	Cone->ResetMusicPlayback();
	Cone->SetTravelingColumnsSpeed(.65f);

	for (EMusicAnimationType Type : { EMusicAnimationType::TravelingColumns, EMusicAnimationType::Equalizer })
	{
		Cone->SetMusicAnimationType(Type);
		Cone->TriggerMusicReaction(1, true);
		Cone->UpdateMusicEqualizer(Cone->GetMusicAnimationTime() + .2);
		TestTrue(TEXT("Both music modes light grouped squares"), !Cone->MusicHighlightedCells.IsEmpty());
		for (const auto& Group : Cone->MusicGlowGroups)
		{
			const bool bLit = Group.ContainsByPredicate([Cone](int32 Cell) { return Cone->MusicHighlightedCells.Contains(Cell); });
			if (!bLit) { continue; }
			for (int32 Cell : Group)
			{
				if (Cone->Cells[Cell].bExists && !Cone->PendingCellIndices.Contains(Cell))
				{
					TestTrue(TEXT("Animation never lights only a fraction of an intact square"), Cone->MusicHighlightedCells.Contains(Cell));
				}
			}
		}
		Cone->ResetMusicPlayback();
	}
	Cone->SetReactToMusic(false);
	TestNull(TEXT("Disabling releases the surface renderer"), Cone->MusicSquareComponent.Get());
	TestTrue(TEXT("Disabling releases group membership and square caches"), Cone->MusicGlowGroups.IsEmpty() && Cone->MusicCellGroupIndices.IsEmpty());
	TestFalse(TEXT("Disabling preserves the destroyed member"), Cone->Cells[SecondSeed].bExists);
	Cone->SetMusicGlowGroupSize(-50);
	TestEqual(TEXT("Size has a positive lower bound"), Cone->GetMusicGlowGroupSize(), 1.0f);
	Cone->SetMusicGlowGroupSize(200);
	Cone->SetMusicAnimationType(EMusicAnimationType::TravelingColumns);
	Cone->bGeneratePlatforms = true;
	Cone->PlatformStyle = EConeStructurePlatformStyle::RisingSpiral;
	Cone->ConfigureConeStructure(8, 4, 1, .6f, FVector(80), FVector::ZeroVector);
	Cone->SetReactToMusic(true);
	Cone->EnsureMusicGlowGroups();
	TestEqual(TEXT("Regeneration rebuilds membership for the new geometry"), Cone->MusicCellGroupIndices.Num(), Cone->Cells.Num());
	TArray<int32> AllCells;
	for (int32 Cell = 0; Cell < Cone->Cells.Num(); ++Cell) { if (Cone->Cells[Cell].bExists) { AllCells.Add(Cell); } }
	Cone->RenderMusicEqualizerCells(AllCells);
	int32 PlatformTriangles = 0;
	for (const auto& Entry : Cone->MusicChunkOriginalMaterials)
	{
		const auto* Shell = Cone->MusicChunkShellTriangles.Find(Entry.Key);
		const auto* Mesh = Cone->ChunkComponents[Entry.Key]->GetMesh();
		for (int32 Triangle : Mesh->TriangleIndicesItr())
		{
			if (Entry.Value[Triangle] != 1) { continue; }
			++PlatformTriangles;
			TestFalse(TEXT("Spiral triangles never enter the square surface cache"), Shell && Shell->Contains(Triangle));
			TestEqual(TEXT("Spiral keeps its original material during traveling"), Mesh->Attributes()->GetMaterialID()->GetValue(Triangle), 1);
		}
	}
	TestTrue(TEXT("Spiral exclusion was tested on generated platform geometry"), PlatformTriangles > 0);
	Cone->SetReactToMusic(false);
	Cone->Destroy();
	return true;
}
#endif
