#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricGround.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricGroundTest, "MyProject6.Ground.GeometryBoundaryAndCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricGroundTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Ground = World->SpawnActor<AParametricGround>();
	if (!TestNotNull(TEXT("Native ground spawns"), Ground)) { return false; }
	TestEqual(TEXT("Default shape is circle"), Ground->Shape, EParametricGroundShape::Circle);
	TestEqual(TEXT("Default slab thickness is half a metre"), Ground->Thickness, 50.f);
	TestEqual(TEXT("Default ground spans 200 metres"), Ground->Size, 20000.f);
	TestNotNull(TEXT("Default tintable material exists"), Ground->GroundMaterial.Get());
	TestFalse(TEXT("Ground needs no per-frame work"), Ground->PrimaryActorTick.bCanEverTick);
	Ground->Size = 2000;
	Ground->BoundaryInset = 100;
	Ground->BoundaryWidth = 40;
	Ground->BoundaryColor = FLinearColor::Red;
	auto* Component = Ground->GetDynamicMeshComponent();

	for (EParametricGroundShape Shape : { EParametricGroundShape::Circle, EParametricGroundShape::Square, EParametricGroundShape::Hexagon })
	{
		Ground->Shape = Shape;
		Ground->RebuildGround();
		const FDynamicMesh3& Mesh = *Component->GetMesh();
		TestTrue(TEXT("Slab is watertight"), Mesh.IsClosed());
		TestTrue(TEXT("Topology and attribute overlays are valid"), Mesh.CheckValidity());
		TestTrue(TEXT("Top stays at actor origin"), FMath::IsNearlyZero(Mesh.GetBounds().Max.Z));
		TestTrue(TEXT("Thickness extends below the surface"), FMath::IsNearlyEqual(Mesh.GetBounds().Min.Z, -50.0));
		const int32 Sides = Shape == EParametricGroundShape::Circle ? Ground->CircleSegments :
			Shape == EParametricGroundShape::Square ? 4 : 6;
		const double Step = UE_DOUBLE_TWO_PI / Sides;
		const double Start = Shape == EParametricGroundShape::Square ? UE_DOUBLE_PI * .25 : 0.0;
		const double Apothem = 1000.0 * (Shape == EParametricGroundShape::Circle ? FMath::Cos(Step * .5) : 1.0);
		double TopArea = 0, BoundaryArea = 0;
		bool bValidBoundary = true, bPositiveArea = true, bUpwardTopNormals = true;
		for (int32 Triangle : Mesh.TriangleIndicesItr())
		{
			const double Area = Mesh.GetTriArea(Triangle);
			bPositiveArea &= Area > 0;
			const FIndex3i V = Mesh.GetTriangle(Triangle);
			if (Mesh.GetVertex(V.A).Z == 0 && Mesh.GetVertex(V.B).Z == 0 && Mesh.GetVertex(V.C).Z == 0)
			{
				TopArea += Area;
				bUpwardTopNormals &= Mesh.GetTriNormal(Triangle).Z > .99 &&
					Mesh.Attributes()->PrimaryNormals()->GetElement(Mesh.Attributes()->PrimaryNormals()->GetTriangle(Triangle).A).Z > .99;
			}
			if (Mesh.Attributes()->GetMaterialID()->GetValue(Triangle) != 1) { continue; }
			BoundaryArea += Area;
			for (int32 Vertex : { Mesh.GetTriangle(Triangle).A, Mesh.GetTriangle(Triangle).B, Mesh.GetTriangle(Triangle).C })
			{
				const FVector3d P = Mesh.GetVertex(Vertex);
				double EdgeDistance = TNumericLimits<double>::Max();
				for (int32 Side = 0; Side < Sides; ++Side)
				{
					const double NormalAngle = Start + Step * (Side + .5);
					EdgeDistance = FMath::Min(EdgeDistance, Apothem - P.X * FMath::Cos(NormalAngle) - P.Y * FMath::Sin(NormalAngle));
				}
				bValidBoundary &= FMath::IsNearlyZero(P.Z) &&
					(FMath::IsNearlyEqual(EdgeDistance, 100.0, 1.e-5) || FMath::IsNearlyEqual(EdgeDistance, 140.0, 1.e-5));
			}
		}
		const double ExpectedTop = Sides * Apothem * Apothem * FMath::Tan(Step * .5);
		const double ExpectedBoundary = Sides * (FMath::Square(Apothem - 100) - FMath::Square(Apothem - 140)) * FMath::Tan(Step * .5);
		TestTrue(TEXT("All faces have positive area"), bPositiveArea);
		TestTrue(TEXT("Top normals point upward with Unreal front-face winding"), bUpwardTopNormals);
		TestTrue(TEXT("Boundary gap and width are exact perpendicular offsets on the top"), bValidBoundary);
		TestTrue(TEXT("No holes or overlapping top faces"), FMath::IsNearlyEqual(TopArea, ExpectedTop, .01));
		TestTrue(TEXT("Boundary covers precisely its closed ring"), FMath::IsNearlyEqual(BoundaryArea, ExpectedBoundary, .01));

		FHitResult Hit;
		TestTrue(TEXT("Simple pawn queries hit the top"), World->LineTraceSingleByChannel(
			Hit, FVector(25, 35, 200), FVector(25, 35, -200), ECC_Pawn) && Hit.GetActor() == Ground && FMath::Abs(Hit.ImpactPoint.Z) < .01);
		TestTrue(TEXT("Character capsule is supported"), World->SweepSingleByChannel(Hit,
			FVector(25, 35, 250), FVector(25, 35, -250), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(34, 88)) && Hit.GetActor() == Ground && Hit.ImpactNormal.Z > .99);
		TestFalse(TEXT("Outside the footprint remains empty"), World->LineTraceSingleByChannel(
			Hit, FVector(2000, 2000, 200), FVector(2000, 2000, -200), ECC_Visibility));
		const double A = Start + Step * .5;
		const FVector OnLine(FMath::Cos(A) * (Apothem - 120), FMath::Sin(A) * (Apothem - 120), 0);
		TestTrue(TEXT("Painted boundary has no collision step"), World->LineTraceSingleByChannel(
			Hit, OnLine + FVector(0, 0, 200), OnLine - FVector(0, 0, 200), ECC_Pawn) && FMath::Abs(Hit.ImpactPoint.Z) < .01);
		TestTrue(TEXT("Underside collision matches thickness"), World->LineTraceSingleByChannel(
			Hit, FVector(25, 35, -200), FVector(25, 35, 200), ECC_Visibility) && FMath::Abs(Hit.ImpactPoint.Z + 50) < .01);
	}
	FLinearColor Tint;
	TestTrue(TEXT("Boundary color reaches the rendered material"), Component->GetMaterial(1)->GetVectorParameterValue(
		FMaterialParameterInfo(TEXT("Color")), Tint) && Tint.Equals(FLinearColor::Red));
	Ground->Thickness = 125;
	Ground->BoundaryInset = 0;
	Ground->RebuildGround();
	TestTrue(TEXT("Zero inset remains watertight"), Component->GetMesh()->IsClosed());
	TestEqual(TEXT("Changing thickness keeps top fixed"), Component->GetMesh()->GetBounds().Max.Z, 0.0);
	TestEqual(TEXT("Changed thickness is applied"), Component->GetMesh()->GetBounds().Min.Z, -125.0);
	Ground->Size = 10;
	Ground->BoundaryInset = 1.e9f;
	Ground->BoundaryWidth = 1.e9f;
	Ground->RebuildGround();
	TestTrue(TEXT("Oversized boundary is limited to the footprint"), Component->GetMesh()->CheckValidity() && Component->GetMesh()->IsClosed());
	Ground->Size = 2000;
	Ground->bShowBoundary = false;
	Ground->bEnableCollision = false;
	Ground->RebuildGround();
	bool bNoBoundary = true;
	for (int32 T : Component->GetMesh()->TriangleIndicesItr())
	{
		bNoBoundary &= Component->GetMesh()->Attributes()->GetMaterialID()->GetValue(T) == 0;
	}
	TestTrue(TEXT("Boundary can be hidden"), bNoBoundary);
	FHitResult Hit;
	TestFalse(TEXT("Collision can be disabled"), World->LineTraceSingleByChannel(
		Hit, FVector(25, 35, 200), FVector(25, 35, -200), ECC_Pawn));
	Ground->bEnableCollision = true;
	Ground->bShowBoundary = true;
	Ground->BoundaryWidth = 0;
	Ground->RebuildGround();
	TestTrue(TEXT("Collision can be re-enabled"), World->LineTraceSingleByChannel(
		Hit, FVector(25, 35, 200), FVector(25, 35, -200), ECC_Pawn));
	TestTrue(TEXT("Zero-width boundary remains valid"), Component->GetMesh()->CheckValidity() && Component->GetMesh()->IsClosed());
	return true;
}
#endif
