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
		TestNull(TEXT("No music track reference"), Music->ActiveTrack.Get());
		TestFalse(TEXT("No pending asset load"), Music->LoadHandle.IsValid());
		TestTrue(TEXT("No key point timeline"), Music->KeyPointTimeline.IsEmpty());
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
		Cone->bScatterDestroyedPieces = false;
		Cone->FlashDuration = 0;
		Cone->bPlayDestroyTickSound = false;
		Cone->ConfigureConeStructure(8, 4, 1, .6f, FVector(80, 100, 70), FVector::ZeroVector);
		Cone->DispatchBeginPlay();
		TestNull(TEXT("Inactive cone has no music renderer"), Cone->MusicEqualizerInstances.Get());
		TArray<FDynamicMesh3> ConeOriginals;
		for (UDynamicMeshComponent* Component : Cone->ChunkComponents) { ConeOriginals.Add(*Component->GetMesh()); }
		auto CheckConeGeometry = [this, Cone, &ConeOriginals](bool bCheckMaterials)
		{
			for (int32 Chunk = 0; Chunk < ConeOriginals.Num(); ++Chunk)
			{
				const FDynamicMesh3& Before = ConeOriginals[Chunk];
				const FDynamicMesh3& After = *Cone->ChunkComponents[Chunk]->GetMesh();
				TestEqual(TEXT("Cone glow adds no geometry"), After.TriangleCount(), Before.TriangleCount());
				TestEqual(TEXT("Cone glow adds no vertices"), After.VertexCount(), Before.VertexCount());
				for (int32 Vertex : Before.VertexIndicesItr())
				{
					TestTrue(TEXT("Cone pieces keep their full size and position"), Before.GetVertex(Vertex) == After.GetVertex(Vertex));
				}
				for (int32 Triangle : Before.TriangleIndicesItr())
				{
					TestTrue(TEXT("Cone glow preserves the exact curved and clipped triangles"), Before.GetTriangle(Triangle) == After.GetTriangle(Triangle));
					if (bCheckMaterials)
					{
						TestEqual(TEXT("Cone and platform materials restore after music"), After.Attributes()->GetMaterialID()->GetValue(Triangle), Before.Attributes()->GetMaterialID()->GetValue(Triangle));
					}
				}
			}
		};
		Cone->SetReactToMusic(true);
		Cone->TriggerMusicReaction(1.0f, true);
		Cone->UpdateMusicEqualizer(Cone->GetMusicAnimationTime() + .2);
		TestNull(TEXT("Cone music never creates small overlay cubes"), Cone->MusicEqualizerInstances.Get());
		TestTrue(TEXT("Traveling columns highlight real cone cells"), !Cone->MusicHighlightedCells.IsEmpty());
		CheckConeGeometry(false);
		TArray<int32> ConeCells;
		for (int32 Cell = 0; Cell < Cone->Cells.Num(); ++Cell) { if (Cone->Cells[Cell].bExists) { ConeCells.Add(Cell); } }
		Cone->RenderMusicEqualizerCells(ConeCells);
		CheckConeGeometry(false);
		TArray<UDynamicMesh*> CachedMeshes;
		for (UDynamicMeshComponent* Component : Cone->ChunkComponents)
		{
			CachedMeshes.Add(Component->GetDynamicMesh());
			const FDynamicMesh3& Mesh = *Component->GetMesh();
			for (int32 Triangle : Mesh.TriangleIndicesItr())
			{
				TestTrue(TEXT("Every curved cone/platform triangle belongs to a real cell"), Mesh.GetTriangleGroup(Triangle) != INDEX_NONE);
				TestTrue(TEXT("Traveling squares preserve original cone and platform materials"),
					Mesh.Attributes()->GetMaterialID()->GetValue(Triangle) < Cone->MusicMaterialSlot);
				const FVector Center = Cone->GetCellLocalCenter(Cone->FromCellIndex(Mesh.GetTriangleGroup(Triangle)));
				const FIndex3i Vertices = Mesh.GetTriangle(Triangle);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const FVector Delta = (FVector(Mesh.GetVertex(Vertices[Corner])) - Center).GetAbs();
					TestTrue(TEXT("A glow triangle stays inside the actual piece dimensions"),
						Delta.X <= Cone->BlockDimensions.X * .5 + .01 && Delta.Y <= Cone->BlockDimensions.Y * .5 + .01 &&
						Delta.Z <= Cone->BlockDimensions.Z * .5 + .01);
				}
			}
		}
		for (int32 Frame = 0; Frame < 8; ++Frame)
		{
			const int32 Cell = ConeCells[Frame % ConeCells.Num()];
			Cone->RenderMusicEqualizerCells(MakeArrayView(&Cell, 1));
			for (int32 Chunk = 0; Chunk < CachedMeshes.Num(); ++Chunk)
			{
				TestTrue(TEXT("Music frames reuse cone geometry and collision"), CachedMeshes[Chunk] == Cone->ChunkComponents[Chunk]->GetDynamicMesh());
			}
		}
		Cone->ResetMusicPlayback();
		CheckConeGeometry(true);
		Cone->RenderMusicEqualizerCells(ConeCells);
		const int32 DestroyedCell = ConeCells[0];
		Cone->DestroyCell(Cone->FromCellIndex(DestroyedCell));
		Cone->RenderMusicEqualizerCells(ConeCells);
		for (UDynamicMeshComponent* Component : Cone->ChunkComponents)
		{
			const FDynamicMesh3& Mesh = *Component->GetMesh();
			for (int32 Triangle : Mesh.TriangleIndicesItr())
			{
				TestTrue(TEXT("Exposed cut faces belong to intact neighboring pieces"), Mesh.GetTriangleGroup(Triangle) != INDEX_NONE && Mesh.GetTriangleGroup(Triangle) != DestroyedCell);
			}
		}
		Cone->SetReactToMusic(false);
		TestNull(TEXT("Disabling releases the cone renderer"), Cone->MusicEqualizerInstances.Get());
		TestTrue(TEXT("Disabling releases cone music material cache"), Cone->MusicChunkOriginalMaterials.IsEmpty());
		TestFalse(TEXT("Stopping music keeps the cone destruction hole"), Cone->Cells[DestroyedCell].bExists);
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
