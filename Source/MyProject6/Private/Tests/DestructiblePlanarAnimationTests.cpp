#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedFloor.h"
#include "ChunkedDestructibleTriangle.h"
#include "ChunkedDestructibleRoundedBlob.h"
#include "ChunkedDestructiblePyramid.h"
#include "ChunkedDestructibleConeStructure.h"
#include "ChunkedDestructibleHouse.h"
#include "ChunkedDestructibleHalloweenPumpkin.h"
#include "ChunkedDestructibleHalloweenPumpkin2.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructiblePlanarAnimationTest,
	"MyProject6.Animation.PlanarGeometryFlowAndIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructiblePlanarAnimationTest::RunTest(const FString& Parameters)
{
	FDestructiblePlanarFlow Flow;
	const FVector2D Size(800, 400);
	TestTrue(TEXT("+X travels from left to right"), Flow.Coordinates(FVector2D(0, .5), Size).Equals(FVector2D(0, .5)) &&
		Flow.Coordinates(FVector2D(1, .5), Size).Equals(FVector2D(1, .5)));
	Flow.AngleDegrees = 90;
	TestTrue(TEXT("+Y travels from bottom to top in local XY"), FMath::IsNearlyEqual(Flow.Coordinates(FVector2D(.5, 1), Size).X, 1.0));
	Flow.AngleDegrees = 45;
	TestTrue(TEXT("Diagonal spans opposite corners of a non-square footprint"),
		FMath::IsNearlyZero(Flow.Coordinates(FVector2D(0, 0), Size).X) &&
		FMath::IsNearlyEqual(Flow.Coordinates(FVector2D(1, 1), Size).X, 1.0));
	Flow.Mode = EPlanarFlowMode::Radial;
	Flow.Origin = FVector2D(0, 0);
	TestTrue(TEXT("Corner radial flow reaches opposite corner"), FMath::IsNearlyZero(Flow.Coordinates(FVector2D(0, 0), Size).X) &&
		FMath::IsNearlyEqual(Flow.Coordinates(FVector2D(1, 1), Size).X, 1.0));
	Flow.bReverse = true;
	TestTrue(TEXT("Reverse swaps origin and destination"), FMath::IsNearlyEqual(Flow.Coordinates(FVector2D(0, 0), Size).X, 1.0));
	for (UClass* VolumeClass : {AChunkedDestructiblePyramid::StaticClass(), AChunkedDestructibleConeStructure::StaticClass(),
		AChunkedDestructibleHouse::StaticClass(), AChunkedDestructibleHalloweenPumpkin::StaticClass(), AChunkedDestructibleHalloweenPumpkin2::StaticClass()})
	{
		const auto* Defaults = CastChecked<AChunkedDestructibleFloor>(VolumeClass->GetDefaultObject());
		TestFalse(TEXT("Volume families retain their original renderer"), Defaults->UsesPlanarAnimation());
		TestTrue(TEXT("Volume families retain the serialized animation selection"), Defaults->GetFixedAnimationType() == Defaults->ConeAnimationType);
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	const TArray<UClass*> Classes = {AChunkedDestructibleFloor::StaticClass(), AChunkedDestructibleTriangle::StaticClass(),
		AChunkedDestructibleRoundedBlob::StaticClass(), AChunkedDestructibleDecorativeFloor::StaticClass(), AChunkedFloor::StaticClass()};
	for (UClass* Class : Classes)
	{
		FActorSpawnParameters Spawn;
		Spawn.bDeferConstruction = true;
		auto* Floor = World->SpawnActor<AChunkedDestructibleFloor>(Class, FTransform::Identity, Spawn);
		if (!TestNotNull(TEXT("Floor spawns"), Floor)) { return false; }
		Floor->ChunkSideLength = 4;
		Floor->FlashDuration = 0;
		Floor->bPlayDestroyTickSound = false;
		const FVector Piece(80, 80, 40);
		if (auto* Triangle = Cast<AChunkedDestructibleTriangle>(Floor)) { Triangle->ConfigureTriangle(800, 700, .25f, Piece, FVector2D(2), false); }
		else if (auto* Blob = Cast<AChunkedDestructibleRoundedBlob>(Floor)) { Blob->ConfigureRoundedBlob(FVector2D(800, 700), 5, .2f, .6f, 42, 12, Piece, FVector2D(2), false); }
		else if (auto* Decorative = Cast<AChunkedDestructibleDecorativeFloor>(Floor))
		{
			Decorative->ConfigureDecorativeFloor(EChunkedDecorativeFloorShape::Star, FVector2D(800, 700), Piece, FVector2D(2), false);
			Decorative->ConfigureHeartMotif(true, FVector2D(350, 300), .35f, FVector2D::ZeroVector, 20);
		}
		else { Floor->ConfigureFloor(FVector2D(800, 700), Piece, FVector2D::ZeroVector, false); }
		Floor->DispatchBeginPlay();
		auto CheckIdle = [this, World, Floor]()
		{
			TestFalse(TEXT("Disabled planar actor does not tick"), Floor->IsActorTickEnabled());
			TestFalse(TEXT("Disabled planar actor has no fixed timer"), World->GetTimerManager().TimerExists(Floor->FixedAnimationTimerHandle));
			TestNull(TEXT("Disabled planar actor holds no animation resources"), Floor->PlanarAnimation.Get());
			TestTrue(TEXT("Disabled planar actor has no music layout"), Floor->MusicEqualizerSlots.IsEmpty());
			TestTrue(TEXT("Disabled planar actor has no music update buffer"), Floor->PlanarMusicMask.IsEmpty());
		};
		CheckIdle();
		Floor->TriggerMusicReaction(1, true);
		CheckIdle();
		Floor->SetPlanarFlow(Flow);
		Floor->SetPlanarAnimationType(EPlanarAnimationType::FlowingWave);
		if (!TestNotNull(TEXT("Shared planar renderer activates"), Floor->PlanarAnimation.Get())) { return false; }
		auto CheckSurface = [this, Floor]()
		{
			TestEqual(TEXT("Only one animation surface per chunk"), Floor->PlanarAnimation->Surfaces.Num(), Floor->ChunkComponents.Num());
			for (int32 Index = 0; Index < Floor->ChunkComponents.Num(); ++Index)
			{
				const auto* Surface = Floor->PlanarAnimation->Surfaces[Index].Get();
				const FDynamicMesh3& Source = *Floor->ChunkComponents[Index]->GetMesh();
				const FDynamicMesh3& Effect = *Surface->GetMesh();
				TestTrue(TEXT("No animation collision"), Surface->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
				TestTrue(TEXT("No surface offset or scaling"), Surface->GetRelativeTransform().Equals(FTransform::Identity));
				TestEqual(TEXT("Clipped geometry retains every vertex"), Effect.VertexCount(), Source.VertexCount());
				TestEqual(TEXT("Clipped geometry retains every triangle"), Effect.TriangleCount(), Source.TriangleCount());
				for (const int32 Vertex : Source.VertexIndicesItr())
				{
					if (!Effect.IsVertex(Vertex) || Effect.GetVertex(Vertex) != Source.GetVertex(Vertex))
					{ AddError(TEXT("Animation changed a clipped boundary or displaced a vertex")); break; }
				}
				for (const int32 Triangle : Source.TriangleIndicesItr())
				{
					if (Effect.GetTriangle(Triangle) != Source.GetTriangle(Triangle))
					{ AddError(TEXT("Animation changed the original topology")); break; }
					TestEqual(TEXT("Effect uses one material batch per chunk"), Effect.Attributes()->GetMaterialID()->GetValue(Triangle), 0);
				}
			}
		};
		CheckSurface();
		TArray<FDynamicMesh3> OriginalMeshes;
		for (const UDynamicMeshComponent* Chunk : Floor->ChunkComponents) { OriginalMeshes.Add(*Chunk->GetMesh()); }
		UDynamicMesh* InitialSurfaceMesh = Floor->PlanarAnimation->Surfaces[0]->GetDynamicMesh();
		const float Alphas[] = {.15f, .45f, .75f};
		for (int32 Frame = 0; Frame < 60; ++Frame) { Floor->RenderFixedAnimation(Alphas); }
		TestTrue(TEXT("Animation frames never rebuild geometry"), Floor->PlanarAnimation->Surfaces[0]->GetDynamicMesh() == InitialSurfaceMesh);
		for (int32 Index = 0; Index < OriginalMeshes.Num(); ++Index)
		{
			const FDynamicMesh3& Before = OriginalMeshes[Index];
			const FDynamicMesh3& After = *Floor->ChunkComponents[Index]->GetMesh();
			for (int32 Triangle : Before.TriangleIndicesItr())
			{
				TestEqual(TEXT("Source palette and motifs are untouched"), After.Attributes()->GetMaterialID()->GetValue(Triangle),
					Before.Attributes()->GetMaterialID()->GetValue(Triangle));
			}
		}
		Floor->SetReactToMusic(true);
		Floor->TriggerMusicReaction(1, true);
		Floor->UpdateMusicEqualizer(Floor->GetMusicAnimationTime() + .05);
		TestNull(TEXT("Planar music has no small cube instances"), Floor->MusicEqualizerInstances.Get());
		TestTrue(TEXT("Music writes a bounded surface mask"), Floor->PlanarMusicMask.Num() <= 32 * 48 &&
			Floor->PlanarMusicMask.ContainsByPredicate([](uint8 Value) { return Value > 0; }));
		int32 Destroyed = INDEX_NONE;
		for (int32 Index = 0; Index < Floor->Cells.Num(); ++Index)
		{
			if (Floor->Cells[Index].bExists && Floor->DestroyCell(Floor->FromCellIndex(Index))) { Destroyed = Index; break; }
		}
		TestTrue(TEXT("Destruction succeeds during both animations"), Destroyed != INDEX_NONE);
		CheckSurface();
		Floor->GenerateFloor(true);
		TestFalse(TEXT("Regeneration preserves the destroyed cell"), Destroyed != INDEX_NONE && Floor->Cells[Destroyed].bExists);
		CheckSurface();
		for (EPlanarAnimationType Type : {EPlanarAnimationType::SineBands, EPlanarAnimationType::Spiral})
		{
			Floor->SetPlanarAnimationType(Type);
			TestTrue(TEXT("Each planar fixed mode starts"), Floor->bFixedAnimationPlaying);
			CheckSurface();
		}
		Floor->SetPlanarAnimationType(EPlanarAnimationType::None);
		Floor->UpdateMusicEqualizer(Floor->GetMusicAnimationTime() + 10.0);
		TestFalse(TEXT("Expired music stops actor ticking"), Floor->IsActorTickEnabled());
		TestFalse(TEXT("Expired music removes all animation draws"), Floor->PlanarAnimation->bSurfacesVisible);
		Floor->SetReactToMusic(false);
		CheckIdle();
		TestFalse(TEXT("Stopping animations does not restore destroyed geometry"), Destroyed != INDEX_NONE && Floor->Cells[Destroyed].bExists);
		Floor->Destroy();
	}
	return true;
}
#endif
