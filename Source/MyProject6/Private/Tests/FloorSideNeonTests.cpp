// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ChunkedFloor.h"
#include "ChunkedDestructibleTriangle.h"
#include "ChunkedDestructibleRoundedBlob.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFloorSideNeonTest,
	"MyProject6.Destructible.SideNeon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFloorSideNeonTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	struct FCounts { int32 Triangles = 0; int32 Sides = 0; int32 Neon = 0; double NeonArea = 0; };
	auto Inspect = [this](AChunkedDestructibleFloor* Floor)
	{
		FCounts Result;
		for (const UDynamicMeshComponent* Component : Floor->ChunkComponents)
		{
			const FDynamicMesh3* Mesh = Component->GetMesh();
			Result.Triangles += Mesh->TriangleCount();
			for (int32 TID : Mesh->TriangleIndicesItr())
			{
				const bool bSide = FMath::Abs(Mesh->GetTriNormal(TID).Z) < 0.01;
				Result.Sides += bSide ? 1 : 0;
				if (Mesh->Attributes()->GetMaterialID()->GetValue(TID) != FMath::Max(Floor->FloorMaterials.Num(), 1)) { continue; }
				TestTrue(TEXT("Neon material is only on vertical sides"), bSide);
				++Result.Neon;
				Result.NeonArea += Mesh->GetTriArea(TID);
				TestEqual(TEXT("Every chunk shares the actor's single neon material"),
					Component->GetMaterial(FMath::Max(Floor->FloorMaterials.Num(), 1)),
					static_cast<UMaterialInterface*>(Floor->SideNeonMaterialInstance));
				const FIndex3i Vertices = Mesh->GetTriangle(TID);
				const FDynamicMeshUVOverlay* UV = Mesh->Attributes()->PrimaryUV();
				const FIndex3i Elements = UV->GetTriangle(TID);
				for (int32 Corner = 0; Corner < 3; ++Corner)
				{
					const double ExpectedHeight = 1.0 + Mesh->GetVertex(Vertices[Corner]).Z / Floor->EffectiveCellDimensions.Z;
					TestTrue(TEXT("Stripe height stays continuous across corners and chunks"),
						FMath::IsNearlyEqual(static_cast<double>(UV->GetElement(Elements[Corner]).X), ExpectedHeight, 1.e-5));
				}
			}
		}
		return Result;
	};

	const EChunkedDecorativeFloorShape Shapes[] = {
		EChunkedDecorativeFloorShape::Grid, EChunkedDecorativeFloorShape::Triangle,
		EChunkedDecorativeFloorShape::Star, EChunkedDecorativeFloorShape::Circle,
		EChunkedDecorativeFloorShape::Square, EChunkedDecorativeFloorShape::RoundedBlob
	};
	for (double Gap : { 0.0, 5.0 })
	{
		// All unified shapes plus the three legacy floor actor implementations.
		for (int32 Case = 0; Case < 9; ++Case)
		{
			UClass* Class = Case < 6 ? AChunkedFloor::StaticClass() :
				Case == 6 ? AChunkedDestructibleFloor::StaticClass() :
				Case == 7 ? AChunkedDestructibleTriangle::StaticClass() : AChunkedDestructibleRoundedBlob::StaticClass();
			AChunkedDestructibleFloor* Floor = World->SpawnActorDeferred<AChunkedDestructibleFloor>(Class, FTransform::Identity);
			Floor->ChunkSideLength = 4;
			Floor->CellDimensions = FVector(100, 100, 50);
			Floor->AreaSize = FVector2D(1200);
			Floor->FlashDuration = 0;
			Floor->bPlayDestroyTickSound = false;
			Floor->UVWorldUnits = 37; // Neon must not depend on the original texture tiling.
			Floor->FinishSpawning(FTransform::Identity);
			if (auto* Unified = Cast<AChunkedFloor>(Floor))
			{
				Unified->ConfigureChunkedFloor(Shapes[Case], FVector2D(1200), FVector(100, 100, 50), FVector2D(Gap), false);
			}
			else if (auto* Triangle = Cast<AChunkedDestructibleTriangle>(Floor))
			{
				Triangle->ConfigureTriangle(1200, 1200, 0.23f, FVector(100, 100, 50), FVector2D(Gap), false);
			}
			else if (auto* Blob = Cast<AChunkedDestructibleRoundedBlob>(Floor))
			{
				Blob->ConfigureRoundedBlob(FVector2D(1200), 5, 0.2f, 0.8f, 42, 16,
					FVector(100, 100, 50), FVector2D(Gap), false);
			}
			else { Floor->ConfigureFloor(FVector2D(1200), FVector(100, 100, 50), FVector2D(Gap), false); }
			AddInfo(FString::Printf(TEXT("Case %d, gap %.0f: %s"), Case, Gap, *Class->GetName()));
			TestNull(TEXT("Disabled neon allocates no dynamic material"), Floor->SideNeonMaterialInstance.Get());
			const FCounts Baseline = Inspect(Floor);
			const int32 ComponentCount = Floor->GetComponents().Num();
			FFloorSideNeonSettings Settings;
			Settings.Mode = EFloorSideNeonMode::Stripe;
			Floor->SetSideNeonSettings(Settings);
			if (!TestNotNull(TEXT("Neon material asset is packaged and usable"), Floor->SideNeonMaterialInstance.Get())) { Floor->Destroy(); continue; }
			const FCounts Outer = Inspect(Floor);
			TestEqual(TEXT("Neon adds no triangles"), Outer.Triangles, Baseline.Triangles);
			TestEqual(TEXT("Neon adds no components or lights"), Floor->GetComponents().Num(), ComponentCount);
			TestFalse(TEXT("Static neon does not enable actor tick"), Floor->IsActorTickEnabled());
			TestTrue(TEXT("Every shape has a lit perimeter"), Outer.Neon > 0);
			if (Gap == 0) { TestEqual(TEXT("All intact touching side faces are on the original perimeter"), Outer.Neon, Outer.Sides); }
			else { TestTrue(TEXT("Outer-only excludes internal cell gaps"), Outer.Neon < Outer.Sides); }

			TArray<UDynamicMesh*> MeshObjects;
			for (UDynamicMeshComponent* Component : Floor->ChunkComponents) { MeshObjects.Add(Component->GetDynamicMesh()); }
			UMaterialInstanceDynamic* OriginalMID = Floor->SideNeonMaterialInstance;
			Settings.Mode = EFloorSideNeonMode::FullSide;
			Settings.StripeWidth = 7;
			Settings.StripePosition = 0.25f;
			Floor->SetSideNeonSettings(Settings);
			Floor->SetSideNeonColor(FLinearColor(1, 0, 0.5), 12);
			TestEqual(TEXT("Appearance reuses the same material"), Floor->SideNeonMaterialInstance.Get(), OriginalMID);
			for (int32 Index = 0; Index < MeshObjects.Num(); ++Index)
			{
				TestEqual(TEXT("Color / width / position / full-side mode do not rebuild chunks"), Floor->ChunkComponents[Index]->GetDynamicMesh(), MeshObjects[Index]);
			}
			TestTrue(TEXT("Color and intensity reach the shader"),
				OriginalMID->K2_GetVectorParameterValue(TEXT("GlowColor")).Equals(FLinearColor(12, 0, 6, 12)));
			TestTrue(TEXT("Width is normalized to the floor thickness"),
				OriginalMID->K2_GetVectorParameterValue(TEXT("Stripe")).Equals(FLinearColor(0.25f, 0.14f, 1, 0)));

			FIntPoint Interior = FIntPoint::NoneValue;
			for (int32 Y = 2; Y < Floor->CellCount.Y - 2 && Interior == FIntPoint::NoneValue; ++Y)
			{
				for (int32 X = 2; X < Floor->CellCount.X - 2; ++X)
				{
					bool bInterior = true;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX) { bInterior &= Floor->Cells[Floor->ToCellIndex(FIntPoint(X + DX, Y + DY))].bExists; }
					}
					if (bInterior) { Interior = FIntPoint(X, Y); break; }
				}
			}
			if (TestTrue(TEXT("Found an interior cell for damage"), Interior != FIntPoint::NoneValue))
			{
				Floor->DestroyCell(Interior);
				const FCounts DamagedOuter = Inspect(Floor);
				TestTrue(TEXT("Interior damage does not create neon in outer-only mode"),
					FMath::IsNearlyEqual(DamagedOuter.NeonArea, Outer.NeonArea, 0.1));
				Settings.Scope = EFloorSideNeonScope::AllExposedSides;
				Floor->SetSideNeonSettings(Settings);
				const FCounts DamagedAll = Inspect(Floor);
				TestEqual(TEXT("Scope switch adds no triangles"), DamagedAll.Triangles, DamagedOuter.Triangles);
				TestEqual(TEXT("All-exposed lights every side including the damage hole"), DamagedAll.Neon, DamagedAll.Sides);
				TestTrue(TEXT("All-exposed lights more than just the perimeter"), DamagedAll.NeonArea > DamagedOuter.NeonArea);
				TestFalse(TEXT("Changing neon preserves destroyed cells"), Floor->Cells[Floor->ToCellIndex(Interior)].bExists);
				Floor->RestoreCell(Interior);
				TestEqual(TEXT("Restoring a cell removes the hole geometry"), Inspect(Floor).Triangles, Baseline.Triangles);
			}

			Settings.Mode = EFloorSideNeonMode::Off;
			Floor->SetSideNeonSettings(Settings);
			TestEqual(TEXT("Off removes every neon material assignment"), Inspect(Floor).Neon, 0);
			TestNull(TEXT("Off releases the neon instance"), Floor->SideNeonMaterialInstance.Get());
			for (const UDynamicMeshComponent* Component : Floor->ChunkComponents)
			{
				TestEqual(TEXT("Off restores the original material slots"), Component->GetNumMaterials(), FMath::Max(Floor->FloorMaterials.Num(), 1));
			}
			Floor->Destroy();
		}
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

#endif
