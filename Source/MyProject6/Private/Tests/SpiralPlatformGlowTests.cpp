#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "SpiralPlatformGlow.h"
#include "QuadPanelConeStructure.h"
#include "ChunkedDestructibleConeStructure.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

using namespace UE::Geometry;
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSpiralPlatformGlowTest, "MyProject6.Destructible.SpiralPlatformGlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSpiralPlatformGlowTest::RunTest(const FString& Parameters)
{
	FSpiralPlatformGlowSettings Settings;
	Settings.SliceDuration = .125f;
	Settings.LoopDelay = .5f;
	for (bool bAscending : { true, false })
	{
		for (auto Direction : { ESpiralPlatformGlowDirection::BottomToTop, ESpiralPlatformGlowDirection::TopToBottom })
		{
			Settings.Direction = Direction;
			const bool bForward = (Direction == ESpiralPlatformGlowDirection::BottomToTop) == bAscending;
			for (int32 Step = 0; Step < 8; ++Step)
			{
				TestEqual(TEXT("Every slice follows the requested height direction"),
					USpiralPlatformGlow::SliceAtTime(Step * .125 + .01, 8, Settings, bAscending), bForward ? Step : 7 - Step);
			}
			TestEqual(TEXT("No glow during loop delay"), USpiralPlatformGlow::SliceAtTime(1.1, 8, Settings, bAscending), INDEX_NONE);
			TestEqual(TEXT("Loop restarts at the requested end"), USpiralPlatformGlow::SliceAtTime(1.51, 8, Settings, bAscending), bForward ? 0 : 7);
		}
	}
	Settings.bLoop = false;
	TestEqual(TEXT("Non-looping animation finishes"), USpiralPlatformGlow::SliceAtTime(2, 8, Settings, true), INDEX_NONE);
	Settings.bEnabled = false;
	TestEqual(TEXT("Disabled animation has no active slice"), USpiralPlatformGlow::SliceAtTime(0, 8, Settings, true), INDEX_NONE);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Spawn; Spawn.bDeferConstruction = true;
	auto CheckCone = [&](auto* Cone)
	{
		Cone->bGeneratePlatforms = true;
		Cone->bGenerateEntrance = false;
		Cone->bGenerateConeBase = false;
		Cone->bScatterDestroyedPieces = false;
		Cone->bPlayDestroyTickSound = false;
		Cone->FlashDuration = 0;
		Cone->ConeAnimationType = EConeStructureAnimationType::None;
		Cone->SetReactToMusic(false);
		Cone->PlatformStyle = EConeStructurePlatformStyle::RisingSpiral;
		Cone->PlatformOverhangBlocks = 3;
		Cone->LowestPlatformHeightBlocks = 1;
		Cone->SpiralEndHeightBlocks = 9;
		Cone->PlatformSpiralTurns = 1;
		Cone->SpiralSegmentsPerTurn = 16;
		Cone->CircularSegments = 32;
		Cone->MaxRuntimeCircularSegments = 32;
		Cone->SpiralDestructionMode = EConeStructureSpiralDestructionMode::OneTrianglePerHit;
		Cone->ConfigureConeStructure(12, 5, 2, .8f, FVector(50), FVector::ZeroVector);
		auto MakeMesh = []()
		{
			FDynamicMesh3 Mesh; Mesh.EnableVertexNormals(FVector3f::UnitZ()); Mesh.EnableAttributes();
			Mesh.Attributes()->SetNumUVLayers(1); Mesh.Attributes()->EnableMaterialID(); return Mesh;
		};
		for (bool bClockwise : { false, true })
		{
			Cone->bPlatformSpiralClockwise = bClockwise;
			Cone->RebuildConeStructure(false);
			FDynamicMesh3 Whole = MakeMesh(); Cone->AppendSpiralSurfaces(Whole, INDEX_NONE);
			int32 SliceTriangleTotal = 0;
			for (int32 Slice = 0; Slice < Cone->GetSpiralSegmentCount() * 2; ++Slice)
			{
				FDynamicMesh3 Mesh = MakeMesh(); Cone->AppendSpiralSurfaces(Mesh, INDEX_NONE, Slice);
				TestTrue(TEXT("Every intact triangle slice produces a highlight"), Mesh.TriangleCount() > 0);
				SliceTriangleTotal += Mesh.TriangleCount();
			}
			TestEqual(TEXT("Slice selection partitions the original platform exactly"), SliceTriangleTotal, Whole.TriangleCount());
		}
		const int32 SolidComponents = Cone->ChunkComponents.Num();
		int32 SolidTriangles = 0;
		for (UDynamicMeshComponent* Component : Cone->ChunkComponents) { SolidTriangles += Component->GetMesh()->TriangleCount(); }
		Cone->PlaySpiralPlatformGlow();
		auto* Glow = Cone->SpiralPlatformGlowRuntime.Get();
		if (!TestNotNull(TEXT("Both cone classes start the effect"), Glow)) { return; }
		const auto& PrimitiveData = Glow->Surface->GetCustomPrimitiveData().Data;
		if (TestTrue(TEXT("Built-in glow supplies its custom primitive data inputs"), PrimitiveData.Num() >= 5))
		{
			TestEqual(TEXT("Built-in glow selects the custom primitive branch"), PrimitiveData[0], 1.f);
			for (int32 Channel = 1; Channel <= 3; ++Channel)
			{ TestEqual(TEXT("Built-in glow has visible emissive RGB"), PrimitiveData[Channel], Cone->SpiralPlatformGlow.GlowIntensity); }
			TestEqual(TEXT("Built-in glow opacity is one"), PrimitiveData[4], 1.f);
		}
		Glow->RenderSlice(6, true);
		TestTrue(TEXT("Selected slice is visible"), Glow->Surface && Glow->Surface->IsVisible());
		TestEqual(TEXT("Highlight never changes collision"), Glow->Surface->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestEqual(TEXT("Solid component count is unchanged"), Cone->ChunkComponents.Num(), SolidComponents);
		int32 AfterTriangles = 0;
		for (UDynamicMeshComponent* Component : Cone->ChunkComponents) { AfterTriangles += Component->GetMesh()->TriangleCount(); }
		TestEqual(TEXT("Solid geometry is unchanged"), AfterTriangles, SolidTriangles);
		const double Started = FPlatformTime::Seconds();
		int32 MaxGlowTriangles = 0;
		for (int32 Step = 0; Step < 64; ++Step)
		{
			Glow->RenderSlice(Step % (Cone->GetSpiralSegmentCount() * 2), true);
			MaxGlowTriangles = FMath::Max(MaxGlowTriangles, Glow->Surface->GetMesh()->TriangleCount());
		}
		AddInfo(FString::Printf(TEXT("%s: 64 slice updates averaged %.3f ms CPU; maximum %d glow triangles (NullRHI excludes GPU cost)."),
			*Cone->GetClass()->GetName(), (FPlatformTime::Seconds() - Started) * 1000 / 64, MaxGlowTriangles));
		Glow->RenderSlice(6, true);
		// Strike the middle of an actual clipped top triangle in the highlighted slice.
		FDynamicMesh3 Slice = MakeMesh(); Cone->AppendSpiralSurfaces(Slice, INDEX_NONE, 6);
		FVector Impact = FVector::ZeroVector;
		double BestArea = 0;
		for (int32 Triangle : Slice.TriangleIndicesItr())
		{
			if (Slice.GetVertexNormal(Slice.GetTriangle(Triangle).A).Z > .5f)
			{
				FHitResult Candidate;
				Candidate.ImpactPoint = Cone->GetActorTransform().TransformPosition(FVector(Slice.GetTriCentroid(Triangle)));
				Candidate.Location = Candidate.ImpactPoint; Candidate.ImpactNormal = FVector::UpVector;
				int32 Segment = INDEX_NONE, Half = INDEX_NONE;
				if (Cone->ResolveSpiralSegmentFromHit(Candidate, Segment, &Half) && Segment * 2 + Half == 6 && Slice.GetTriArea(Triangle) > BestArea)
				{ Impact = FVector(Slice.GetTriCentroid(Triangle)); BestArea = Slice.GetTriArea(Triangle); }
			}
		}
		TestTrue(TEXT("Hit sample is inside the highlighted slice rather than a shared cut edge"), BestArea > 0);
		FHitResult Hit; Hit.bBlockingHit = true;
		Hit.ImpactPoint = Cone->GetActorTransform().TransformPosition(Impact);
		Hit.Location = Hit.ImpactPoint; Hit.ImpactNormal = FVector::UpVector;
		TestTrue(TEXT("The actual projectile destruction path removes the glowing slice"), Cone->DestroyConeCellFromHit(Hit));
		TestTrue(TEXT("Highlight is hidden synchronously when its slice is removed"), !Glow->Surface->IsVisible());
		TestEqual(TEXT("Destroyed slice leaves no glow triangles"), Glow->Surface->GetMesh()->TriangleCount(), 0);
		Glow->RenderSlice(7, true);
		TestTrue(TEXT("Adjacent intact slice still glows"), Glow->Surface->IsVisible());
		Cone->SpiralDestructionMode = EConeStructureSpiralDestructionMode::OneSegmentPerHit;
		TestTrue(TEXT("Segment destruction also removes its remaining triangle"), Cone->DestroyConeCellFromHit(Hit));
		TestTrue(TEXT("Segment destruction removes the glow immediately"), !Glow->Surface->IsVisible());
		// Also exercise backing-cell destruction, which uses the batch hook instead of segment removal.
		Cone->RebuildConeStructure(false);
		Cone->SpiralDestructionMode = EConeStructureSpiralDestructionMode::ExistingCellDestruction;
		Cone->PlaySpiralPlatformGlow(); Glow = Cone->SpiralPlatformGlowRuntime.Get(); Glow->RenderSlice(6, true);
		const double AreaBefore = [&]() { double Area = 0; for (int32 T : Glow->Surface->GetMesh()->TriangleIndicesItr()) { Area += Glow->Surface->GetMesh()->GetTriArea(T); } return Area; }();
		FIntPoint Cell;
		if (TestTrue(TEXT("Highlight has a backing destruction cell"), Cone->WorldLocationToCell(Impact - FVector(0, 0, 1), Cell)))
		{
			TestTrue(TEXT("Backing cell destruction succeeds"), Cone->DestroyCell(Cell));
			double AreaAfter = 0; for (int32 T : Glow->Surface->GetMesh()->TriangleIndicesItr()) { AreaAfter += Glow->Surface->GetMesh()->GetTriArea(T); }
			TestTrue(TEXT("Partial cell damage trims the highlight immediately"), AreaAfter < AreaBefore);
		}
		FSpiralPlatformGlowSettings Custom;
		Custom.Material = UMaterial::GetDefaultMaterial(MD_Surface);
		Custom.Direction = ESpiralPlatformGlowDirection::TopToBottom;
		Cone->SetSpiralPlatformGlowSettings(Custom);
		Glow = Cone->SpiralPlatformGlowRuntime.Get();
		TestEqual(TEXT("Custom material is used directly"), Glow->Surface->GetMaterial(0), Custom.Material.Get());
		TestEqual(TEXT("Runtime direction changes restart from the top"), Glow->CurrentSlice, Cone->GetSpiralSegmentCount() * 2 - 1);
		// The user's selected AliasReference material has Glow Intensity=0 by default.
		// A private instance must enable emission while keeping the source/color unchanged.
		UMaterialInterface* Alias = LoadObject<UMaterialInterface>(nullptr, TEXT("/DatasmithContent/Materials/AliasReference.AliasReference"));
		if (TestNotNull(TEXT("Selected AliasReference material exists"), Alias))
		{
			float SourceIntensity = 0;
			FLinearColor SourceColor;
			Alias->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Glow Intensity")), SourceIntensity);
			Alias->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), SourceColor);
			Custom.Material = Alias; Custom.GlowIntensity = 12.f;
			Cone->SetSpiralPlatformGlowSettings(Custom); Glow = Cone->SpiralPlatformGlowRuntime.Get();
			auto* Instance = Cast<UMaterialInstanceDynamic>(Glow->Surface->GetMaterial(0));
			if (TestNotNull(TEXT("Custom emission uses an isolated material instance"), Instance))
			{
				float Value = 0; FLinearColor Color;
				Instance->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Glow Intensity")), Value);
				TestEqual(TEXT("AliasReference emission is enabled at the requested brightness"), Value, 12.f);
				Instance->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")), Color);
				TestEqual(TEXT("Selected material color is retained"), Color, SourceColor);
				Alias->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Glow Intensity")), Value);
				TestEqual(TEXT("Source material is never modified"), Value, SourceIntensity);
			}
		}
		Cone->StopSpiralPlatformGlow();
		TestFalse(TEXT("Stop releases the timer"), World->GetTimerManager().TimerExists(Glow->Timer));
		TestNull(TEXT("Stop removes the effect component"), Glow->Surface.Get());
		Custom.bEnabled = false; Cone->SetSpiralPlatformGlowSettings(Custom);
		TestNull(TEXT("Disabled effect allocates no runtime resource"), Cone->SpiralPlatformGlowRuntime.Get());
	};
	CheckCone(World->SpawnActor<AQuadPanelConeStructure>(AQuadPanelConeStructure::StaticClass(), FTransform::Identity, Spawn));
	CheckCone(World->SpawnActor<AChunkedDestructibleConeStructure>(AChunkedDestructibleConeStructure::StaticClass(), FTransform::Identity, Spawn));
	return true;
}
#endif
