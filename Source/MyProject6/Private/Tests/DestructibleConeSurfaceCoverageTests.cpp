#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedDestructibleConeStructure.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDestructibleConeSurfaceCoverageTest,
	"MyProject6.Destructible.ConeSurfaceCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDestructibleConeSurfaceCoverageTest::RunTest(const FString& Parameters)
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
	Cone->bGenerateCollision = false;
	Cone->bScatterDestroyedPieces = false;
	Cone->bPlayDestroyTickSound = false;
	Cone->FlashDuration = 0;
	Cone->PlatformStyle = EConeStructurePlatformStyle::RisingSpiral;
	Cone->PlatformOverhangBlocks = 10;
	Cone->LowestPlatformHeightBlocks = 0;
	Cone->SpiralEndHeightBlocks = 85;
	Cone->PlatformThicknessBlocks = 1;
	Cone->PlatformSpiralTurns = 4;
	Cone->SpiralStartAngleDegrees = 45;
	Cone->SpiralSegmentsPerTurn = 64;
	Cone->CircularSegments = 120;
	Cone->MaxRuntimeCircularSegments = 128;
	Cone->SetReactToMusic(false);

	// The placed level cone: four turns, tapering from 20 blocks to one over 86 layers.
	// Check every source triangle, including its tiny cell-clipped corners. An area
	// deficit means real missing geometry even if a center-point ray still hits it.
	for (bool bClockwise : {true, false})
	{
		Cone->bPlatformSpiralClockwise = bClockwise;
		Cone->ConfigureConeStructure(86, 20, 1, 1, FVector(100), FVector::ZeroVector);
		int32 Missing[2] = {}, Overlapping[2] = {};
		double MissingArea[2] = {}, ExtraArea[2] = {};
		auto CheckTriangle = [&](FVector3d A, FVector3d B, FVector3d C, FVector3d Normal, int32 Material, int32 Section)
		{
			const double Expected = (B - A).Cross(C - A).Length() * .5;
			if (Expected < 1.e-10) { return; }
			FDynamicMesh3 Mesh;
			Mesh.EnableVertexNormals(FVector3f::UnitZ());
			Mesh.EnableAttributes();
			Mesh.Attributes()->SetNumUVLayers(1);
			Mesh.Attributes()->EnableMaterialID();
			Cone->AppendClippedSurfaceTriangleAcrossLayers(Mesh, INDEX_NONE, A, B, C, FVector3f(Normal), Material);
			double Actual = 0;
			for (int32 T : Mesh.TriangleIndicesItr()) { Actual += Mesh.GetTriArea(T); }
			const double Error = Expected - Actual;
			const double Tolerance = FMath::Max(1.e-6, Expected * 1.e-9);
			if (Error > Tolerance)
			{
				if (Missing[Material] < 8)
				{
					AddInfo(FString::Printf(TEXT("CW=%d material=%d section=%d lost=%.9f center=%s"),
						bClockwise, Material, Section, Error, *((A+B+C)/3).ToString()));
				}
				++Missing[Material];
				MissingArea[Material] += Error;
			}
			if (Error < -Tolerance) { ++Overlapping[Material]; ExtraArea[Material] -= Error; }
		};
		const int32 Segments = Cone->GetEffectiveCircularSegments();
		for (int32 Z = 1; Z < Cone->GridHeightBlocks; ++Z)
		{
			for (int32 S = 0; S < Segments; ++S)
			{
				const double A = UE_TWO_PI * S / Segments, B = UE_TWO_PI * (S+1) / Segments;
				for (bool bInner : {false, true})
				{
					const float Bottom = Z * 100.0f, Top = Bottom + 100;
					const float R0 = bInner ? Cone->GetConeInnerRadiusAtHeight(Bottom) : Cone->GetConeOuterRadiusAtHeight(Bottom);
					const float R1 = bInner ? Cone->GetConeInnerRadiusAtHeight(Top) : Cone->GetConeOuterRadiusAtHeight(Top);
					const FVector3d P(FMath::Cos(A)*R0, FMath::Sin(A)*R0, Bottom);
					const FVector3d Q(FMath::Cos(B)*R0, FMath::Sin(B)*R0, Bottom);
					const FVector3d R(FMath::Cos(B)*R1, FMath::Sin(B)*R1, Top);
					const FVector3d T(FMath::Cos(A)*R1, FMath::Sin(A)*R1, Top);
					const FVector3d N = FVector3d(FMath::Cos((A+B)*.5), FMath::Sin((A+B)*.5), 1900.0/8600).GetSafeNormal() * (bInner ? -1 : 1);
					CheckTriangle(P,Q,R,N,0,Z*Segments+S);
					CheckTriangle(P,R,T,N,0,Z*Segments+S);
				}
			}
		}
		const int32 Count = Cone->GetSpiralSegmentCount();
		auto Section = [&](int32 S, FVector3d& Inner, FVector3d& Outer)
		{
			const float Alpha = static_cast<float>(S) / Count;
			const float TotalAngle = UE_TWO_PI * Cone->PlatformSpiralTurns;
			const double Angle = FMath::DegreesToRadians(Cone->SpiralStartAngleDegrees) + (bClockwise ? -1.0f : 1.0f) * TotalAngle * Alpha;
			const float Z = FMath::Lerp(Cone->GetSpiralBottomHeight(false), Cone->GetSpiralBottomHeight(true), Alpha) + Cone->GetSpiralThickness();
			const FVector3d Radial(FMath::Cos(Angle), FMath::Sin(Angle), 0);
			Inner = Radial * FMath::Max(Cone->GetConeOuterRadiusAtHeight(Z) - Cone->SpiralWallOverlapBlocks * Cone->CachedRadialBlockUnit * Cone->GetPlatformWidthScaleAtHeight(Z), 0.0f) + FVector3d(0,0,Z);
			Outer = Radial * Cone->GetPlatformOuterRadiusAtHeight(Z) + FVector3d(0,0,Z);
		};
		for (int32 S = 0; S < Count; ++S)
		{
			FVector3d I0,O0,I1,O1;
			Section(S,I0,O0); Section(S+1,I1,O1);
			auto CheckFace = [&](FVector3d A,FVector3d B,FVector3d C,FVector3d Direction)
			{
				FVector3d N = (B-A).Cross(C-A).GetSafeNormal();
				if (N.Dot(Direction) < 0) { N = -N; }
				CheckTriangle(A,B,C,N,1,S);
			};
			const FVector3d Down(0,0,-Cone->GetSpiralThickness());
			for (bool bBottom : {false,true})
			{
				const FVector3d Offset = bBottom ? Down : FVector3d::Zero();
				const FVector3d N = bBottom ? -FVector3d::UnitZ() : FVector3d::UnitZ();
				CheckFace(I0+Offset,O0+Offset,O1+Offset,N);
				CheckFace(I0+Offset,O1+Offset,I1+Offset,N);
			}
			CheckFace(O0+Down,O1,O0,O0+O1);
			CheckFace(O0+Down,O1+Down,O1,O0+O1);
			CheckFace(I0+Down,I0,I1,-I0-I1);
			CheckFace(I0+Down,I1,I1+Down,-I0-I1);
		}
		for (int32 Material=0; Material<2; ++Material)
		{
			TestEqual(FString::Printf(TEXT("CW=%d material=%d: no holes (lost area %.6f)"), bClockwise,Material,MissingArea[Material]), Missing[Material], 0);
			TestEqual(FString::Printf(TEXT("CW=%d material=%d: no duplicate coverage (extra area %.6f)"), bClockwise,Material,ExtraArea[Material]), Overlapping[Material], 0);
		}
	}
	return true;
}

#endif
