// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedDestructibleConeStructure.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleConeSpiralCutFacesTest,
	"MyProject6.Destructible.ConeSpiralCutFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleConeSpiralCutFacesTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Spawn;
	Spawn.bDeferConstruction = true;
	auto* Cone = World->SpawnActor<AChunkedDestructibleConeStructure>(
		AChunkedDestructibleConeStructure::StaticClass(), FTransform::Identity, Spawn);
	if (!TestNotNull(TEXT("Test cone spawns"), Cone)) { return false; }
	Cone->ConeAnimationType = EConeStructureAnimationType::None;
	Cone->bGenerateEntrance = false;
	Cone->bScatterDestroyedPieces = false;
	Cone->bPlayDestroyTickSound = false;
	Cone->FlashDuration = 0;
	Cone->PlatformStyle = EConeStructurePlatformStyle::RisingSpiral;
	Cone->PlatformOverhangBlocks = 3;
	Cone->LowestPlatformHeightBlocks = 2;
	Cone->SpiralEndHeightBlocks = 9;
	Cone->PlatformThicknessBlocks = 2;
	Cone->PlatformSpiralTurns = 1.25f;
	Cone->SpiralSegmentsPerTurn = 32;
	Cone->CircularSegments = 32;
	Cone->MaxRuntimeCircularSegments = 32;

	auto MakeMesh = []()
	{
		FDynamicMesh3 Mesh;
		Mesh.EnableVertexNormals(FVector3f::UnitZ());
		Mesh.EnableAttributes();
		Mesh.Attributes()->SetNumUVLayers(1);
		Mesh.Attributes()->EnableMaterialID();
		return Mesh;
	};

	for (bool bClockwise : {false, true})
	{
		for (bool bTaper : {false, true})
		{
			Cone->bPlatformSpiralClockwise = bClockwise;
			Cone->bTaperPlatformsUpward = bTaper;
			Cone->ConfigureConeStructure(12, 5, 2, .8f, FVector(50), FVector::ZeroVector);
			const int32 SegmentCount = Cone->GetSpiralSegmentCount();
			const int32 Segment = SegmentCount / 3;
			FVector3d InnerTop[2], OuterTop[2];
			for (int32 End = 0; End < 2; ++End)
			{
				const float Alpha = static_cast<float>(Segment + End) / SegmentCount;
				const float Z = FMath::Lerp(Cone->GetSpiralBottomHeight(false),
					Cone->GetSpiralBottomHeight(true), Alpha) + Cone->GetSpiralThickness();
				const double Angle = (bClockwise ? -1.0 : 1.0) * UE_TWO_PI * Cone->PlatformSpiralTurns * Alpha;
				const FVector3d Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0);
				const float InnerRadius = FMath::Max(Cone->GetConeOuterRadiusAtHeight(Z) -
					Cone->SpiralWallOverlapBlocks * Cone->CachedRadialBlockUnit *
					Cone->GetPlatformWidthScaleAtHeight(Z), 0.0f);
				InnerTop[End] = Radial * InnerRadius + FVector3d(0, 0, Z);
				OuterTop[End] = Radial * Cone->GetPlatformOuterRadiusAtHeight(Z) + FVector3d(0, 0, Z);
			}
			const FVector3d SliceCenters[] = {
				(InnerTop[0] + OuterTop[0] + OuterTop[1]) / 3.0 - FVector3d(0, 0, Cone->GetSpiralThickness() * .5),
				(InnerTop[0] + OuterTop[1] + InnerTop[1]) / 3.0 - FVector3d(0, 0, Cone->GetSpiralThickness() * .5)};

			// Each small square lies strictly inside one solid slice. Every cell-cut direction
			// must cover it completely and face the viewer standing in the removed cell.
			auto CheckSlice = [&](int32 Slice, bool bPresent)
			{
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					for (double Sign : {-1.0, 1.0})
					{
						FVector3d U = FVector3d::Zero(), V = FVector3d::Zero(), Normal = FVector3d::Zero();
						U[(Axis + 1) % 3] = 1;
						V[(Axis + 2) % 3] = 1;
						Normal[Axis] = Sign;
						const FVector3d Center = SliceCenters[Slice];
						FDynamicMesh3 Mesh = MakeMesh();
						Cone->AppendClippedDestructionCutFace(Mesh, Center - U - V,
							Center + U - V, Center + U + V, Center - U + V, FVector3f(Normal), 1);
						double Area = 0;
						for (int32 Triangle : Mesh.TriangleIndicesItr())
						{
							FVector3d A, B, C;
							Mesh.GetTriVertices(Triangle, A, B, C);
							const FVector3d Cross = (B - A).Cross(C - A);
							Area += Cross.Length() * .5;
							TestTrue(TEXT("Cut winding faces the hole in Unreal"), Cross.Dot(Normal) < 0);
							TestEqual(TEXT("Cut retains platform material"), Mesh.Attributes()->GetMaterialID()->GetValue(Triangle), 1);
						}
						TestTrue(FString::Printf(TEXT("CW=%d taper=%d slice=%d axis=%d present=%d: cut area %.4f"),
							bClockwise, bTaper, Slice, Axis, bPresent, Area),
							FMath::IsNearlyEqual(Area, bPresent ? 4.0 : 0.0, .001));
					}
				}
			};
			CheckSlice(0, true);
			CheckSlice(1, true);
			Cone->DestroyedSpiralTriangleSliceIndices.Add(Segment * 2);
			CheckSlice(0, false);
			CheckSlice(1, true);
			Cone->DestroyedSpiralTriangleSliceIndices.Reset();
			Cone->DestroyedSpiralSegmentIndices.Add(Segment);
			CheckSlice(0, false);
			CheckSlice(1, false);
			Cone->DestroyedSpiralSegmentIndices.Reset();

			// Exercise the actual wall damage path at its overlap with the inner spiral edge.
			const FVector3d WallSample = (InnerTop[0] + InnerTop[1]) * .5 -
				FVector3d(0, 0, Cone->GetSpiralThickness() * .5);
			FIntPoint WallCell;
			if (TestTrue(TEXT("Inner spiral edge resolves to a backing cell"),
				Cone->WorldLocationToCell(FVector(WallSample), WallCell)) &&
				TestTrue(TEXT("Wall cell can be destroyed"), Cone->DestroyCell(WallCell)))
			{
				FDynamicMesh3 Cuts = MakeMesh();
				Cone->AppendDestroyedCellCutFaces(Cuts, WallCell);
				TestTrue(TEXT("Wall destruction closes the exposed spiral volume"), Cuts.TriangleCount() > 0);
			}
		}
	}
	return true;
}

#endif
