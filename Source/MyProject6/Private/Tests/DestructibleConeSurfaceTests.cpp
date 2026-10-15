#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedDestructibleConeStructure.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleConeSurfaceTest,
	"MyProject6.Destructible.ConeSurfaceIntegrity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleConeSurfaceTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Spawn;
	Spawn.bDeferConstruction = true;
	auto* Cone = World->SpawnActor<AChunkedDestructibleConeStructure>(
		AChunkedDestructibleConeStructure::StaticClass(), FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Cone spawns"), Cone)) { return false; }
	Cone->ConeAnimationType = EConeStructureAnimationType::None;
	Cone->bGenerateEntrance = false;
	Cone->bScatterDestroyedPieces = false;
	Cone->bPlayDestroyTickSound = false;
	Cone->FlashDuration = 0;
	Cone->CircularSegments = 32;
	Cone->SpiralSegmentsPerTurn = 32;
	Cone->LowestPlatformHeightBlocks = 4;
	Cone->PlatformThicknessBlocks = 2;
	Cone->bTaperPlatformsUpward = false;
	auto MakeMesh = []()
	{
		FDynamicMesh3 Mesh;
		Mesh.EnableVertexNormals(FVector3f::UnitZ());
		Mesh.EnableAttributes();
		Mesh.Attributes()->SetNumUVLayers(1);
		Mesh.Attributes()->EnableMaterialID();
		return Mesh;
	};
	auto Area = [](const FDynamicMesh3& Mesh)
	{
		double Result = 0;
		for (int32 T : Mesh.TriangleIndicesItr()) { Result += Mesh.GetTriArea(T); }
		return Result;
	};
	auto CheckCut = [&](FVector3d Center, int32 CellMaterial, double ExpectedArea)
	{
		FDynamicMesh3 Mesh = MakeMesh();
		Cone->AppendClippedDestructionCutFace(Mesh, Center + FVector3d(-1,-1,0),
			Center + FVector3d(1,-1,0), Center + FVector3d(1,1,0), Center + FVector3d(-1,1,0),
			FVector3f::UnitZ(), CellMaterial);
		TestTrue(FString::Printf(TEXT("Cut at Z=%.0f material=%d covers solid exactly once (area %.4f)"),
			Center.Z, CellMaterial, Area(Mesh)), FMath::IsNearlyEqual(Area(Mesh), ExpectedArea, .001));
	};
	for (EConeStructurePlatformStyle Style : {EConeStructurePlatformStyle::CircularRings,
		EConeStructurePlatformStyle::RisingSpiral})
	{
		Cone->PlatformStyle = Style;
		Cone->ConfigureConeStructure(12, 5, 0, .8f, FVector(50), FVector::ZeroVector);
		for (double Height : {150.0, 450.0})
		{
			const double Radius = Cone->GetConeOuterRadiusAtHeight(Height) - 15;
			const FVector3d Center(Radius * .8, Radius * .6, Height);
			// Cell material does not describe all solids intersecting a wall/platform cell.
			CheckCut(Center, 0, 4);
			CheckCut(Center, 1, 4);
		}
	}
	Cone->PlatformStyle = EConeStructurePlatformStyle::CircularRings;
	Cone->ConfigureConeStructure(12, 5, 5, 1, FVector(50), FVector::ZeroVector);
	CheckCut(FVector3d(220, 5, 250), 0, 4); // Overlapping ring and wall: one surface.
	CheckCut(FVector3d(220, 5, 250), 1, 4);
	CheckCut(FVector3d(310, 5, 250), 0, 4); // Platform must survive a wall-labelled cut.
	CheckCut(FVector3d(310, 5, 250), 1, 4);

	// A triangle lying exactly on a shared voxel plane belongs to its solid side only.
	FDynamicMesh3 Boundary = MakeMesh();
	const FVector3d A(219, 4, 300), B(221, 4, 300), C(219, 6, 300);
	Cone->AppendClippedSurfaceTriangleAcrossLayers(Boundary, INDEX_NONE, A, B, C, FVector3f::UnitZ(), 1);
	TestTrue(TEXT("Grid-aligned surface is emitted once, without z fighting"), FMath::IsNearlyEqual(Area(Boundary), 2.0, .001));
	FIntPoint SolidCell;
	if (TestTrue(TEXT("Boundary has a solid-side cell"), Cone->WorldLocationToCell(FVector(220, 5, 299), SolidCell)))
	{
		TestTrue(TEXT("Solid-side cell is destroyed"), Cone->DestroyCell(SolidCell));
		Boundary = MakeMesh();
		Cone->AppendClippedSurfaceTriangleAcrossLayers(Boundary, INDEX_NONE, A, B, C, FVector3f::UnitZ(), 1);
		TestEqual(TEXT("A surviving neighbor cannot retain a deleted surface as a 2D sheet"), Boundary.TriangleCount(), 0);
	}

	// Above the last ring there is only cone wall. Door reveals must be clipped to that wall.
	Cone->bGeneratePlatforms = false;
	Cone->bGenerateEntrance = true;
	Cone->EntranceWidthBlocks = 3;
	Cone->EntranceHeightBlocks = 6;
	Cone->FirstDoorBottomHeightBlocks = 4;
	Cone->ConfigureConeStructure(12, 5, 0, .8f, FVector(50), FVector::ZeroVector);
	// A cut near the tip can contain the entire annulus. Triangulation must preserve
	// its hollow center instead of filling it with a disk or overlapping sector fans.
	FDynamicMesh3 Annulus = MakeMesh();
	const double Extent=Cone->GetConeOuterRadiusAtHeight(450)+10;
	Cone->AppendClippedDestructionCutFace(Annulus, FVector3d(-Extent,-Extent,450), FVector3d(Extent,-Extent,450),
		FVector3d(Extent,Extent,450), FVector3d(-Extent,Extent,450), FVector3f::UnitZ(), 0);
	const int32 Segments = Cone->GetEffectiveCircularSegments();
	const double ExpectedAnnulusArea = .5 * Segments * FMath::Sin(UE_TWO_PI / Segments) *
		(FMath::Square(Cone->GetConeOuterRadiusAtHeight(450)) - FMath::Square(Cone->GetConeInnerRadiusAtHeight(450)));
	TestTrue(TEXT("Upper cross section preserves the hollow cone interior"),
		FMath::IsNearlyEqual(Area(Annulus), ExpectedAnnulusArea, .01));
	int32 OutsideRevealTriangles = 0;
	for (int32 Chunk = 0; Chunk < Cone->ChunkCount.X * Cone->ChunkCount.Y; ++Chunk)
	{
		FDynamicMesh3 Mesh;
		Cone->BuildChunkMesh(Chunk, Mesh);
		for (int32 T : Mesh.TriangleIndicesItr())
		{
			const FVector3d Center = Mesh.GetTriCentroid(T);
			const FVector3d Normal = Mesh.GetTriNormal(T);
			const bool bPlanar = FMath::Abs(Normal.X) > .99999 || FMath::Abs(Normal.Y) > .99999 || FMath::Abs(Normal.Z) > .99999;
			if (bPlanar && Center.Z > 50 && Center.Z < 590 && !Cone->IsPointInsideRenderedStructure(Center))
			{
				++OutsideRevealTriangles;
			}
		}
	}
	TestEqual(TEXT("Door reveals never protrude as floating rectangular sheets"), OutsideRevealTriangles, 0);
	return true;
}

#endif
