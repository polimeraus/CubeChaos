// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedDestructiblePyramid.h"
#include "ChunkedDestructibleConeStructure.h"
#include "MusicReactionWorldSubsystem.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/EngineBaseTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleMusicAnimationTest,
	"MyProject6.Animation.MusicGeometryAndIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleMusicAnimationTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	AChunkedDestructiblePyramid* Pyramid = World->SpawnActor<AChunkedDestructiblePyramid>();
	if (!TestNotNull(TEXT("Pyramid spawns"), Pyramid))
	{
		return false;
	}
	Pyramid->ConeAnimationType = EConeStructureAnimationType::None;
	Pyramid->Shape.BaseWidth = 640.0f;
	Pyramid->Shape.Height = 560.0f;
	Pyramid->Shape.PieceSize = 80.0f;
	Pyramid->bTopSlice = true;
	Pyramid->TopSliceMaterial = Pyramid->FloorMaterials.Last();
	Pyramid->GenerateFloor(false);
	Pyramid->DispatchBeginPlay();
	UMusicReactionWorldSubsystem* Music = World->GetSubsystem<UMusicReactionWorldSubsystem>();
	if (!TestNotNull(TEXT("Music subsystem exists"), Music))
	{
		return false;
	}
	auto CheckIdle = [this, Pyramid, Music, World]()
	{
		TestFalse(TEXT("Idle actor does not tick"), Pyramid->IsActorTickEnabled());
		TestFalse(TEXT("No fixed animation timer"), World->GetTimerManager().TimerExists(Pyramid->FixedAnimationTimerHandle));
		TestFalse(TEXT("No fixed animation meshes"), Pyramid->HasFixedAnimationResources());
		TestNull(TEXT("No equalizer component"), Pyramid->MusicEqualizerInstances.Get());
		TestNull(TEXT("No music material"), Pyramid->MusicReactionMaterialInstance.Get());
		TestTrue(TEXT("No equalizer layout"), Pyramid->MusicEqualizerSlots.IsEmpty());
		TestTrue(TEXT("No piece triangle cache"), Pyramid->MusicPieceTriangles.IsEmpty());
		TestTrue(TEXT("Subsystem is excluded from ticking"), Music->GetTickableTickType() == ETickableTickType::Never);
		TestTrue(TEXT("No registered music actors"), Music->ReactionActors.IsEmpty());
		TestNull(TEXT("No audio component"), Music->MusicComponent.Get());
		TestNull(TEXT("No music asset reference"), Music->MusicSound.Get());
		TestNull(TEXT("No onset asset reference"), Music->OnsetAnalysis.Get());
		TestTrue(TEXT("No onset timeline"), Music->OnsetTimes.IsEmpty());
	};
	CheckIdle();
	Pyramid->TriggerMusicReaction(1.0f, true);
	CheckIdle();

	TArray<FDynamicMesh3> OriginalMeshes;
	for (UDynamicMeshComponent* Component : Pyramid->ChunkComponents)
	{
		OriginalMeshes.Add(*Component->GetMesh());
	}
	auto CheckGeometry = [this, Pyramid, &OriginalMeshes](bool bCheckMaterials)
	{
		TestEqual(TEXT("Chunk count is unchanged"), Pyramid->ChunkComponents.Num(), OriginalMeshes.Num());
		for (int32 ChunkIndex = 0; ChunkIndex < OriginalMeshes.Num(); ++ChunkIndex)
		{
			const FDynamicMesh3& Before = OriginalMeshes[ChunkIndex];
			const FDynamicMesh3& After = *Pyramid->ChunkComponents[ChunkIndex]->GetMesh();
			TestEqual(TEXT("No extra vertices"), After.VertexCount(), Before.VertexCount());
			TestEqual(TEXT("No extra triangles"), After.TriangleCount(), Before.TriangleCount());
			for (const int32 VertexID : Before.VertexIndicesItr())
			{
				if (!After.IsVertex(VertexID) || Before.GetVertex(VertexID) != After.GetVertex(VertexID))
				{
					AddError(TEXT("Music moved or scaled a piece vertex"));
					break;
				}
			}
			if (bCheckMaterials)
			{
				for (const int32 TriangleID : Before.TriangleIndicesItr())
				{
					TestEqual(TEXT("Original material restored, including cut faces"),
						After.Attributes()->GetMaterialID()->GetValue(TriangleID),
						Before.Attributes()->GetMaterialID()->GetValue(TriangleID));
				}
			}
		}
	};

	Pyramid->SetReactToMusic(true);
	TestEqual(TEXT("Enabling registers the actor"), Music->ReactionActors.Num(), 1);
	TestNull(TEXT("Pyramid never allocates cube instances"), Pyramid->MusicEqualizerInstances.Get());
	Pyramid->TriggerMusicReaction(1.0f, true);
	TestTrue(TEXT("Beat creates active columns"), Pyramid->HasActiveMusicEqualizer());
	Pyramid->UpdateMusicEqualizer(World->GetTimeSeconds() + 0.2);
	TestTrue(TEXT("Beat highlights real pieces"), !Pyramid->MusicHighlightedCells.IsEmpty());
	CheckGeometry(false);

	// Select every piece to cover triangular corners, apex, steps, interior and sliced surfaces.
	TArray<int32> AllCells;
	for (int32 CellIndex = 0; CellIndex < Pyramid->Cells.Num(); ++CellIndex)
	{
		AllCells.Add(CellIndex);
	}
	Pyramid->RenderMusicEqualizerCells(AllCells);
	CheckGeometry(false);
	for (UDynamicMeshComponent* Component : Pyramid->ChunkComponents)
	{
		const FDynamicMesh3* Mesh = Component->GetMesh();
		for (const int32 TriangleID : Mesh->TriangleIndicesItr())
		{
			TestEqual(TEXT("Entire selected piece uses its music material"),
				Mesh->Attributes()->GetMaterialID()->GetValue(TriangleID), Pyramid->GetMusicMaterialSlot());
		}
	}
	Pyramid->RebuildChunk(0);
	CheckGeometry(false);
	Pyramid->UpdateMusicEqualizer(World->GetTimeSeconds() + 5.0);
	TestFalse(TEXT("Expired columns stop ticking"), Pyramid->IsActorTickEnabled());
	CheckGeometry(true);

	Pyramid->RenderMusicEqualizerCells(AllCells);
	Pyramid->SetReactToMusic(false);
	CheckGeometry(true);
	CheckIdle();

	Pyramid->SetReactToMusic(true);
	Pyramid->RenderMusicEqualizerCells(AllCells);
	Pyramid->DestroyCell(FIntPoint(0, 0));
	Pyramid->SetReactToMusic(false);
	TestFalse(TEXT("Stopping music does not restore a destroyed cell"), Pyramid->Cells[0].bExists);
	Pyramid->RestoreCell(FIntPoint(0, 0));
	CheckGeometry(true);
	CheckIdle();

	AChunkedDestructibleConeStructure* Cone = World->SpawnActor<AChunkedDestructibleConeStructure>();
	if (TestNotNull(TEXT("Cone spawns"), Cone))
	{
		Cone->ConeAnimationType = EConeStructureAnimationType::None;
		Cone->DispatchBeginPlay();
		TestNull(TEXT("Inactive cone has no music renderer"), Cone->MusicEqualizerInstances.Get());
		Cone->SetReactToMusic(true);
		Cone->TriggerMusicReaction(1.0f, true);
		TestNotNull(TEXT("Cone lazily creates its existing renderer"), Cone->MusicEqualizerInstances.Get());
		Cone->SetReactToMusic(false);
		TestNull(TEXT("Disabling releases the cone renderer"), Cone->MusicEqualizerInstances.Get());
		TestFalse(TEXT("Disabled cone does not tick"), Cone->IsActorTickEnabled());
		Cone->Destroy();
	}
	CheckIdle();
	Pyramid->SetReactToMusic(true);
	Pyramid->Destroy();
	TestTrue(TEXT("Destroy unregisters the last actor"), Music->ReactionActors.IsEmpty());
	TestTrue(TEXT("Destroy disables subsystem ticking"), Music->GetTickableTickType() == ETickableTickType::Never);
	return true;
}

#endif
