#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "QuadPanelConeStructure.h"
#include "BallProjectile.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Components/DynamicMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

using namespace UE::Geometry;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FQuadPanelConeTest, "MyProject6.Destructible.QuadPanelCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FQuadPanelConeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Spawn; Spawn.bDeferConstruction = true;
	auto* Cone = World->SpawnActor<AQuadPanelConeStructure>(AQuadPanelConeStructure::StaticClass(), FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Independent variant spawns"), Cone)) { return false; }
	Cone->bGeneratePlatforms = false;
	Cone->bGenerateEntrance = false;
	Cone->bGenerateConeBase = false;
	Cone->bScatterDestroyedPieces = false;
	Cone->bPlayDestroyTickSound = false;
	Cone->FlashDuration = 0;
	Cone->ConeAnimationType = EConeStructureAnimationType::None;
	Cone->SetReactToMusic(false);
	Cone->WallPanelSegments = 24;
	Cone->PanelJointWidth = 0;
	Cone->ConfigureConeStructure(12, 5, 2, .5, FVector(100), FVector::ZeroVector);
	TestEqual(TEXT("Panel surface lighting is smooth by default"), Cone->PanelShadingSmoothness, 1.f);
	TestEqual(TEXT("Only whole wall panels own live cells"), Cone->GetConeStructureMetrics().GeneratedBlockCount, 12 * 24);
	for (int32 Row = 0; Row < 12; ++Row)
	{
		for (int32 Column = 0; Column < 24; ++Column)
		{
			const FIntPoint Cell = Cone->PanelCell(Column, Row);
			FVector3d V[8]; Cone->GetPanelCorners(Cell, V);
			TestTrue(TEXT("Trapezoid lower and upper edges remain horizontal"), V[0].Z == V[1].Z && V[2].Z == V[3].Z);
			FIntPoint Resolved;
			TestTrue(TEXT("Panel center resolves to its own independent cell"),
				Cone->WorldLocationToCell(Cone->GetCellLocalCenter(Cell), Resolved) && Resolved == Cell);
			FDynamicMesh3 Mesh;
			Mesh.EnableVertexNormals(FVector3f::UnitZ()); Mesh.EnableAttributes();
			Mesh.Attributes()->SetNumUVLayers(1); Mesh.Attributes()->EnableMaterialID();
			Cone->AppendPanel(Mesh, Cell);
			TestEqual(TEXT("A whole piece has exactly six quad faces"), Mesh.TriangleCount(), 12);
			FDynamicMesh3 FlatMesh;
			FlatMesh.EnableVertexNormals(FVector3f::UnitZ()); FlatMesh.EnableAttributes();
			FlatMesh.Attributes()->SetNumUVLayers(1); FlatMesh.Attributes()->EnableMaterialID();
			{
				TGuardValue<float> FlatShading(Cone->PanelShadingSmoothness, 0.f);
				Cone->AppendPanel(FlatMesh, Cell);
			}
			TestEqual(TEXT("Smoothing adds no vertices"), Mesh.VertexCount(), FlatMesh.VertexCount());
			TestEqual(TEXT("Smoothing adds no normal elements"), Mesh.Attributes()->PrimaryNormals()->ElementCount(), FlatMesh.Attributes()->PrimaryNormals()->ElementCount());
			for (int32 Vertex : Mesh.VertexIndicesItr())
			{
				TestTrue(TEXT("Smoothing leaves render and collision positions identical"), Mesh.GetVertex(Vertex) == FlatMesh.GetVertex(Vertex));
				TestTrue(TEXT("Smoothed vertex normals stay finite and unit length"), FMath::IsNearlyEqual(Mesh.GetVertexNormal(Vertex).SizeSquared(), 1.f, 1.e-5f));
			}
			for (int32 Triangle : Mesh.TriangleIndicesItr())
			{
				const FIndex3i Vertices = Mesh.GetTriangle(Triangle);
				const auto* Normals = Mesh.Attributes()->PrimaryNormals();
				const FIndex3i NormalElements = Normals->GetTriangle(Triangle);
				TestTrue(TEXT("Smoothing preserves triangle topology and winding"), Vertices == FlatMesh.GetTriangle(Triangle));
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					TestTrue(TEXT("The render overlay uses the vertex's smooth normal"), Normals->GetElement(NormalElements[Corner]) == Mesh.GetVertexNormal(Vertices[Corner]));
					if (Triangle >= 4)
					{
						TestTrue(TEXT("End caps and exposed damage cuts retain flat shading"),
							Mesh.GetVertexNormal(Vertices[Corner]) == FlatMesh.GetVertexNormal(Vertices[Corner]));
					}
				}
			}
			for (int32 Triangle : Mesh.TriangleIndicesItr()) { TestTrue(TEXT("No degenerate triangles"), Mesh.GetTriArea(Triangle) > 0.01); }
		}
	}
	// Verify shading continuity at both horizontal/vertical chunk boundaries and
	// the angular wrap. Shared shell positions must have identical lighting normals.
	for (const FIntPoint SeamCell : { Cone->PanelCell(7, 3), Cone->PanelCell(23, 3) })
	{
		for (const FIntPoint Offset : { FIntPoint(1, 0), FIntPoint(0, 1) })
		{
			FDynamicMesh3 Left, Right;
			for (FDynamicMesh3* M : { &Left, &Right }) { M->EnableVertexNormals(FVector3f::UnitZ()); }
			Cone->AppendPanel(Left, SeamCell);
			Cone->AppendPanel(Right, Cone->PanelCell(SeamCell.X - Cone->PanelCellOffset + Offset.X, SeamCell.Y + Offset.Y));
			int32 SharedCorners = 0;
			for (int32 A = 0; A < 8; ++A)
			{
				for (int32 B = 0; B < 8; ++B)
				{
					if (!Left.GetVertex(A).Equals(Right.GetVertex(B), 1.e-6)) { continue; }
					++SharedCorners;
					TestTrue(TEXT("Joined shell panels have continuous lighting across chunk/wrap seams"),
						Left.GetVertexNormal(A).Equals(Right.GetVertexNormal(B), 1.e-6f));
				}
			}
			TestEqual(TEXT("Both outer and inner shell edges meet"), SharedCorners, 4);
		}
	}
	{
		TGuardValue<float> PointedTop(Cone->TopRadiusBlocks, 0.f);
		FDynamicMesh3 Tip;
		Tip.EnableVertexNormals(FVector3f::UnitZ()); Tip.EnableAttributes();
		Cone->AppendPanel(Tip, Cone->PanelCell(0, 11));
		for (int32 Vertex : Tip.VertexIndicesItr())
		{
			TestTrue(TEXT("The pointed apex has finite unit normals"), FMath::IsNearlyEqual(Tip.GetVertexNormal(Vertex).SizeSquared(), 1.f, 1.e-5f));
		}
	}
	const FIntPoint Cell = Cone->PanelCell(2, 5), Neighbor = Cone->PanelCell(3, 5);
	FVector3d V[8]; Cone->GetPanelCorners(Cell, V);
	const FVector Impact = FVector((V[0] + V[1] + V[2] + V[3]) * .25);
	const FVector Outward = FVector(Impact.X, Impact.Y, 0).GetSafeNormal();
	FHitResult Trace;
	TestTrue(TEXT("The rendered panel blocks a real projectile trace"), World->LineTraceSingleByChannel(
		Trace, Impact + Outward * 150.f, Impact - Outward * 150.f, ECC_Visibility) && Trace.GetActor() == Cone);
	FHitResult Hit(Cone, Cone->ChunkComponents[Cone->PanelChunk(Cell)], Impact, Outward);
	TestTrue(TEXT("Shot destroys exactly the struck trapezoid"), Cone->DestroyConeCellFromHit(Hit));
	TestFalse(TEXT("Struck panel is absent"), Cone->Cells[Cone->ToCellIndex(Cell)].bExists);
	TestTrue(TEXT("Adjacent panel stays intact"), Cone->Cells[Cone->ToCellIndex(Neighbor)].bExists);
	TestFalse(TEXT("Repeating the same hit does not carve an unrelated cell"), Cone->DestroyConeCellFromHit(Hit));
	FDynamicMesh3 Damaged; Cone->BuildChunkMesh(Cone->PanelChunk(Cell), Damaged);
	TestEqual(TEXT("Chunk loses one complete twelve-triangle piece"), Damaged.TriangleCount(), 31 * 12);
	const int32 Blasted = Cone->DestroyConeCellsFromHit(FHitResult(Cone,
		Cone->ChunkComponents[Cone->PanelChunk(Neighbor)], Cone->GetCellLocalCenter(Neighbor), Outward), 160.f);
	TestTrue(TEXT("Radial hits remove nearby complete panels"), Blasted > 1);
	Cone->RebuildConeStructure(true);
	TestFalse(TEXT("Preserve rebuild retains the hole"), Cone->Cells[Cone->ToCellIndex(Cell)].bExists);
	Cone->RebuildConeStructure(false);
	TestTrue(TEXT("Fresh rebuild restores the panel"), Cone->Cells[Cone->ToCellIndex(Cell)].bExists);
	ABallProjectile* Projectile = World->SpawnActor<ABallProjectile>(FVector(3000, 0, 1500), FRotator::ZeroRotator);
	static_cast<AActor*>(Projectile)->NotifyHit(nullptr, Cone, Cone->ChunkComponents[Cone->PanelChunk(Cell)], false, Impact, Outward, FVector::ZeroVector, Hit);
	TestFalse(TEXT("Existing ball projectile routes to whole-panel destruction"), Cone->Cells[Cone->ToCellIndex(Cell)].bExists);
	TestTrue(TEXT("Projectile does not alter neighboring panel"), Cone->Cells[Cone->ToCellIndex(Neighbor)].bExists);
	FDynamicMesh3 Glow; Glow.EnableVertexNormals(FVector3f::UnitZ()); Glow.EnableAttributes();
	Glow.Attributes()->SetNumUVLayers(1); Glow.Attributes()->EnableMaterialID();
	Cone->AppendConeGlowBandSurfaces(Glow, 500, 600);
	TestEqual(TEXT("Fixed row glow follows intact panels and respects the hole"), Glow.TriangleCount(), 23 * 12);
	Cone->SetActorTransform(FTransform(FRotator(0, 43, 0), FVector(170, -290, 50), FVector(1.5, .8, 2.)));
	FIntPoint Transformed;
	TestTrue(TEXT("Translated rotated scaled actor resolves panel hits"), Cone->WorldLocationToCell(
		Cone->GetActorTransform().TransformPosition(Cone->GetCellLocalCenter(Cell)), Transformed) && Transformed == Cell);
	// The existing actor is still an independent class; copying cannot alter its generated state.
	auto* Original = World->SpawnActor<AChunkedDestructibleConeStructure>(AChunkedDestructibleConeStructure::StaticClass(), FTransform::Identity, Spawn);
	Original->ConfigureConeStructure(8, 4, 1, 1, FVector(100), FVector::ZeroVector);
	const auto Before = Original->GetConeStructureMetrics();
	Cone->CopySettingsFromCone(Original);
	TestEqual(TEXT("Clone takes the original cone height"), Cone->ConeHeightBlocks, 8);
	TestEqual(TEXT("Copy leaves source cell count unchanged"), Original->GetConeStructureMetrics().GeneratedBlockCount, Before.GeneratedBlockCount);
	TestTrue(TEXT("Source is not converted to the new class"), Original->GetClass() == AChunkedDestructibleConeStructure::StaticClass());

	// Fire from an existing hole into all four exposed panel edges. Exact shared
	// boundaries must resolve to the solid side of the contact, not the empty hole.
	Cone->bGeneratePlatforms = false;
	Cone->bGenerateEntrance = false;
	Cone->bGenerateConeBase = false;
	Cone->bScatterDestroyedPieces = false;
	Cone->bPlayDestroyTickSound = false;
	Cone->FlashDuration = 0;
	Cone->ConeAnimationType = EConeStructureAnimationType::None;
	Cone->SetReactToMusic(false);
	Cone->PanelJointWidth = 0;
	const FTransform HitTransforms[] = {
		FTransform(FRotator::ZeroRotator, FVector(5000, 0, 0)),
		FTransform(FRotator(12, 43, 7), FVector(6000, -5000, 120), FVector(1.5, .8, 2.))
	};
	auto CountIntactPanels = [Cone]()
	{
		int32 Count = 0;
		for (const auto& Data : Cone->Cells) { Count += Data.bExists ? 1 : 0; }
		return Count;
	};
	for (const FTransform& Transform : HitTransforms)
	{
		Cone->SetActorTransform(Transform);
		for (int32 HoleColumn : { 0, 7, 23 })
		{
			Cone->ConfigureConeStructure(12, 5, 2, .5, FVector(100), FVector::ZeroVector);
			const FIntPoint Hole = Cone->PanelCell(HoleColumn, 4);
			TestTrue(TEXT("Create a hole across row/column chunk boundaries"), Cone->DestroyCell(Hole));
			const FIntPoint Targets[] = { Cone->PanelCell(HoleColumn, 3), Cone->PanelCell(HoleColumn, 5),
				Cone->PanelCell(HoleColumn - 1, 4), Cone->PanelCell(HoleColumn + 1, 4) };
			const int32 Faces[4][4] = { {3, 2, 6, 7}, {0, 4, 5, 1}, {1, 5, 6, 2}, {0, 3, 7, 4} };
			for (int32 Direction = 0; Direction < 4; ++Direction)
			{
				const FIntPoint Target = Targets[Direction];
				FVector3d Corners[8]; Cone->GetPanelCorners(Target, Corners);
				const int32* Face = Faces[Direction];
				const FVector3d FaceCenter = (Corners[Face[0]] + Corners[Face[1]] + Corners[Face[2]] + Corners[Face[3]]) * .25;
				FVector3d FaceNormal = (Corners[Face[1]] - Corners[Face[0]]).Cross(Corners[Face[2]] - Corners[Face[0]]).GetSafeNormal();
				if (FaceNormal.Dot(FaceCenter - FVector3d(Cone->GetCellLocalCenter(Target))) < 0) { FaceNormal = -FaceNormal; }
				const FVector Start = Transform.TransformPosition(FVector(FaceCenter + FaceNormal * 20.));
				const FVector End = Transform.TransformPosition(FVector(FaceCenter - FaceNormal * 20.));
				FHitResult LineHit, SphereHit;
				const FString Context = FString::Printf(TEXT("hole=%d direction=%d scale=%s"), HoleColumn, Direction, *Transform.GetScale3D().ToString());
				if (!TestTrue(Context + TEXT(" line hits the exposed panel"), World->LineTraceSingleByChannel(
					LineHit, Start, End, ECC_GameTraceChannel1) && LineHit.GetActor() == Cone)) { continue; }
				if (!TestTrue(Context + TEXT(" projectile sphere hits the exposed panel"), World->SweepSingleByChannel(
					SphereHit, Start, End, FQuat::Identity, ECC_GameTraceChannel1, FCollisionShape::MakeSphere(2.f)) && SphereHit.GetActor() == Cone)) { continue; }
				FIntPoint Resolved;
				TestTrue(Context + TEXT(" line resolves to the solid neighbor"), Cone->ResolveExistingCellFromHit(LineHit, Resolved) && Resolved == Target);
				TestTrue(Context + TEXT(" sphere resolves to the solid neighbor"), Cone->ResolveExistingCellFromHit(SphereHit, Resolved) && Resolved == Target);
				const FVector WorldNormal = Transform.GetRotation().RotateVector(FVector(FaceNormal) / Transform.GetScale3D()).GetSafeNormal();
				FHitResult ExactHit(Cone, Cone->ChunkComponents[Cone->PanelChunk(Target)], Transform.TransformPosition(FVector(FaceCenter)), WorldNormal);
				TestTrue(Context + TEXT(" exact shared edge resolves into the struck face"), Cone->ResolveExistingCellFromHit(ExactHit, Resolved) && Resolved == Target);
				const int32 IntactBefore = CountIntactPanels();
				if (Direction % 2 == 0)
				{
					TestTrue(Context + TEXT(" single shot destroys the struck edge"), Cone->DestroyConeCellFromHit(SphereHit));
				}
				else
				{
					TestEqual(Context + TEXT(" small blast still includes the directly hit edge"), Cone->DestroyConeCellsFromHit(LineHit, 1.f), 1);
				}
				TestFalse(Context + TEXT(" target is absent"), Cone->Cells[Cone->ToCellIndex(Target)].bExists);
				TestEqual(Context + TEXT(" exactly one solid neighbor is removed"), CountIntactPanels(), IntactBefore - 1);
				TestFalse(Context + TEXT(" repeated contact cannot drill into another panel"), Cone->DestroyConeCellFromHit(SphereHit));
			}
		}
	}

	// Regression: destruction must remove an already-rendered traveling highlight
	// immediately, even while the music update is throttled or no new beat arrives.
	Cone->SetActorTransform(FTransform::Identity);
	Cone->bGeneratePlatforms = false;
	Cone->bGenerateEntrance = false;
	Cone->bGenerateConeBase = false;
	Cone->bScatterDestroyedPieces = false;
	Cone->bPlayDestroyTickSound = false;
	Cone->bUseDestructionRipple = false;
	Cone->FlashDuration = 0;
	Cone->ConeAnimationType = EConeStructureAnimationType::None;
	Cone->ConfigureConeStructure(12, 5, 2, .5, FVector(100), FVector::ZeroVector);
	Cone->SetMusicAnimationType(EMusicAnimationType::TravelingColumns);
	Cone->SetReactToMusic(true);
	TArray<int32> PanelCells;
	for (int32 Row = 0; Row < 12; ++Row)
	{
		for (int32 Column = 0; Column < Cone->WallPanelSegments; ++Column)
		{
			PanelCells.Add(Cone->ToCellIndex(Cone->PanelCell(Column, Row)));
		}
	}
	Cone->RenderMusicEqualizerCells(PanelCells);
	if (!TestNotNull(TEXT("Traveling glow has a surface mesh"), Cone->MusicSquareComponent.Get())) { return false; }
	const int32 GlowTrianglesBefore = Cone->MusicSquareComponent->GetMesh()->TriangleCount();
	if (!TestTrue(TEXT("Regression starts with visible white glow"), GlowTrianglesBefore > 0)) { return false; }
	FIntPoint LitCell;
	const auto* GlowMesh = Cone->MusicSquareComponent->GetMesh();
	const FVector LitPoint(GlowMesh->GetTriCentroid(*GlowMesh->TriangleIndicesItr().begin()));
	if (!TestTrue(TEXT("Rendered white triangle resolves to its backing panel"),
		Cone->FindPanelAtLocalPoint(LitPoint, LitCell))) { return false; }
	const FIntPoint FarCell = Cone->PanelCell((LitCell.X - Cone->PanelCellOffset + Cone->WallPanelSegments / 2) % Cone->WallPanelSegments, LitCell.Y);
	const auto* UnaffectedChunkMesh = Cone->ChunkComponents[Cone->PanelChunk(FarCell)]->GetDynamicMesh();
	UDynamicMeshComponent* GlowComponent = Cone->MusicSquareComponent;
	Cone->NextMusicEqualizerUpdateTime = 1000.;
	const FVector LitOutward = FVector(LitPoint.X, LitPoint.Y, 0).GetSafeNormal();
	TestTrue(TEXT("A shot destroys a currently white panel"), Cone->DestroyConeCellFromHit(
		FHitResult(Cone, Cone->ChunkComponents[Cone->PanelChunk(LitCell)], LitPoint, LitOutward)));
	auto CheckGlowHasBackingPanels = [this, Cone](const TCHAR* Message)
	{
		const auto* Surface = Cone->MusicSquareComponent->GetMesh();
		int32 OrphanTriangles = 0;
		for (int32 Triangle : Surface->TriangleIndicesItr())
		{
			FIntPoint BackingCell;
			if (!Cone->FindPanelAtLocalPoint(FVector(Surface->GetTriCentroid(Triangle)), BackingCell) ||
				!Cone->Cells[Cone->ToCellIndex(BackingCell)].bExists) { ++OrphanTriangles; }
		}
		TestEqual(Message, OrphanTriangles, 0);
	};
	CheckGlowHasBackingPanels(TEXT("No white geometry remains over the shot hole before the next music update"));
	TestTrue(TEXT("Only the destroyed panel's glow is removed"),
		Cone->MusicSquareComponent->GetMesh()->TriangleCount() > 0 &&
		Cone->MusicSquareComponent->GetMesh()->TriangleCount() < GlowTrianglesBefore);
	TestTrue(TEXT("A shot reuses the existing glow component"), Cone->MusicSquareComponent == GlowComponent);
	TestTrue(TEXT("A shot leaves unrelated collision/render chunks untouched"),
		Cone->ChunkComponents[Cone->PanelChunk(FarCell)]->GetDynamicMesh() == UnaffectedChunkMesh);
	Cone->RenderMusicEqualizerCells(PanelCells);
	CheckGlowHasBackingPanels(TEXT("Later music updates cannot restore glow over the destroyed panel"));
	TestTrue(TEXT("A blast removes panels across chunk boundaries"), Cone->DestroyConeCellsFromHit(
		FHitResult(Cone, Cone->ChunkComponents[Cone->PanelChunk(FarCell)], Cone->GetCellLocalCenter(FarCell), -LitOutward), 350.f) > 1);
	CheckGlowHasBackingPanels(TEXT("Batched radial destruction immediately clears white geometry from every hole"));
	Cone->DestroyCellsInRadius(FVector(0, 0, 600), 2000.f, EChunkedFloorBlastShape::Circle);
	TestEqual(TEXT("Removing the final lit panels leaves no white triangles"), Cone->MusicSquareComponent->GetMesh()->TriangleCount(), 0);
	TestFalse(TEXT("An empty glow mesh is hidden immediately"), Cone->MusicSquareComponent->IsVisible());
	Cone->SetReactToMusic(false);
	TestNull(TEXT("Stopping music still releases the glow component"), Cone->MusicSquareComponent.Get());
	TestFalse(TEXT("Destruction cleanup adds no idle tick"), Cone->IsActorTickEnabled());
	return true;
}
#endif
