#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricLobbyPlatform.h"
#include "BombProjectile.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

using namespace UE::Geometry;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLobbyPlatformGeometryTest, "MyProject6.LobbyPlatform.GeometryAndLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLobbyPlatformGeometryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* A = World->SpawnActor<AParametricLobbyPlatform>();
	if (!TestNotNull(TEXT("Platform spawns"), A)) { return false; }
	TestNotNull(TEXT("Analytic surface material exists"), A->SurfaceMaterial.Get());
	TestEqual(TEXT("Reference has three circular spawn pads and two edge arms"), A->GetPadIds().Num(), 3);
	TestFalse(TEXT("A circle-free direction anchor is not a spawn pad"), A->IsPadSurfaceIntact(TEXT("Entrance")));
	const auto* Surface = Cast<UMaterialInstanceDynamic>(A->GetDynamicMeshComponent()->GetMaterial(0));
	float Inset = -1;
	TestTrue(TEXT("Circle-free arm clips to a parallel inset of the outline"), Surface->GetScalarParameterValue(FMaterialParameterInfo(TEXT("PathInset2")), Inset) && Inset == 65);
	FLinearColor Segment;
	TestTrue(TEXT("Edge arm continues past the slab for contour clipping"), Surface->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Segment2")), Segment)
		&& FVector2D(Segment.B, Segment.A).Size() > 3600);
	for (int32 Slot : {1, 2})
	{
		float Emission = -1;
		TestTrue(TEXT("Body and upper rim are not additional luminous layers"), A->GetDynamicMeshComponent()->GetMaterial(Slot)->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Emission")), Emission) && Emission == 0);
	}
	for (FLobbyPlatformPad& Pad : A->Pads) { Pad.bShowCircle = true; }
	TestTrue(TEXT("Reference has no invalid settings"), A->LayoutWarning.IsEmpty());
	TestFalse(TEXT("No per-frame tick"), A->PrimaryActorTick.bCanEverTick);
	for (ELobbyPlatformShape Shape : {ELobbyPlatformShape::RoundedRectangle, ELobbyPlatformShape::Rectangle,
		ELobbyPlatformShape::Ellipse, ELobbyPlatformShape::Triangle, ELobbyPlatformShape::Hexagon, ELobbyPlatformShape::RegularPolygon})
	{
		A->Shape = Shape;
		for (float Corner : {0.f, 1.f, 300.f, 100000.f})
		{
			A->CornerRadius = Corner;
			A->RebuildPlatform();
			const FDynamicMesh3* Mesh = A->GetDynamicMeshComponent()->GetMesh();
			TestTrue(TEXT("Closed solid slab for every shape and corner size"), Mesh->IsClosed());
			TestTrue(TEXT("Topology and overlays valid"), Mesh->CheckValidity());
			TestTrue(TEXT("Origin stays on the top"), FMath::IsNearlyZero(Mesh->GetBounds().Max.Z));
			TestTrue(TEXT("Thickness goes down"), FMath::IsNearlyEqual(Mesh->GetBounds().Min.Z, -80.0));
			bool bFacesValid = true;
			for (int32 T : Mesh->TriangleIndicesItr())
			{
				bFacesValid &= Mesh->GetTriArea(T) > 1.e-8;
				if (Mesh->Attributes()->GetMaterialID()->GetValue(T) == 0) { bFacesValid &= Mesh->GetTriNormal(T).Z > .99; }
			}
			TestTrue(TEXT("No degenerate faces; top normals point up"), bFacesValid);
			TestEqual(TEXT("All named pads survive changing shape"), A->GetPadIds().Num(), 5);
			for (FName Id : A->GetPadIds())
			{
				FVector P; TestTrue(TEXT("Effective pad centre exists"), A->GetPadWorldCenter(Id, P));
				FHitResult Hit;
				TestTrue(TEXT("Every fitted centre rests on solid collision"), World->LineTraceSingleByChannel(
					Hit, P + FVector(0, 0, 100), P - FVector(0, 0, 200), ECC_Pawn) && Hit.GetActor() == A && FMath::Abs(Hit.ImpactPoint.Z) < .01);
			}
		}
	}
	A->Shape = ELobbyPlatformShape::RoundedRectangle; A->CornerRadius = 300;
	A->RebuildPlatform();
	const FTransform ActorTransform(FRotator(15, 67, -11), FVector(2600, -700, 180), FVector(1.7, .8, 1.2));
	FVector Local; A->GetPadWorldCenter(TEXT("Right"), Local);
	A->SetActorTransform(ActorTransform);
	FVector WorldCenter; A->GetPadWorldCenter(TEXT("Right"), WorldCenter);
	TestTrue(TEXT("Centre respects translation, rotation and nonuniform scale"), WorldCenter.Equals(ActorTransform.TransformPosition(Local), .001));
	A->Pads[1].SpawnOffset = FVector(5, 10, 96);
	A->Pads[1].SpawnRotation = FRotator(0, 30, 0);
	FTransform Spawn; A->GetPadSpawnTransform(TEXT("Right"), Spawn);
	TestTrue(TEXT("Spawn offset uses platform local axes"), Spawn.GetLocation().Equals(ActorTransform.TransformPosition(Local + A->Pads[1].SpawnOffset), .001));
	TestTrue(TEXT("Per-pad spawn rotation composes with platform"), Spawn.GetRotation().Equals(ActorTransform.GetRotation() * A->Pads[1].SpawnRotation.Quaternion(), .001));
	A->SetActorTransform(FTransform::Identity);
	A->Pads[1].Position = FVector2D(.92, .92); A->Width = 1800; A->Depth = 1200; A->RebuildPlatform();
	FVector Fitted; A->GetPadWorldCenter(TEXT("Right"), Fitted);
	FLinearColor Data; auto* M = Cast<UMaterialInstanceDynamic>(A->GetDynamicMeshComponent()->GetMaterial(0));
	TestTrue(TEXT("GPU pad centre exactly matches API centre after fit and resize"), M && M->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Pad1")), Data)
		&& FMath::IsNearlyEqual(double(Data.R), Fitted.X, .001) && FMath::IsNearlyEqual(double(Data.G), Fitted.Y, .001));
	A->Pads[1].Id = TEXT("Center"); A->RebuildPlatform();
	TestFalse(TEXT("Duplicate names are ambiguous and rejected"), A->GetPadWorldCenter(TEXT("Center"), Fitted));
	TestFalse(TEXT("Invalid layout gives feedback"), A->LayoutWarning.IsEmpty());
	A->ResetReferenceLayout(); A->Pads[1].bEnabled = false; A->RebuildPlatform();
	TestFalse(TEXT("Disabled pad cannot be used for spawning"), A->GetPadWorldCenter(TEXT("Right"), Fitted));
	TestFalse(TEXT("Unknown ID returns failure"), A->GetPadSpawnTransform(TEXT("Unknown"), Spawn));
	A->bEnableCollision = false; A->RebuildPlatform();
	FHitResult Hit;
	TestFalse(TEXT("Collision can be disabled"), World->LineTraceSingleByChannel(Hit, FVector(0,0,100), FVector(0,0,-200), ECC_Pawn));
	A->RimWidth = 0; A->BevelSize = 0; A->Thickness = 2; A->RebuildPlatform();
	TestTrue(TEXT("Zero rim/bevel and thin slab remain closed"), A->GetDynamicMeshComponent()->GetMesh()->IsClosed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLobbyPlatformDestructionTest, "MyProject6.LobbyPlatform.DamageAndProtection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLobbyPlatformDestructionTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* A = World->SpawnActor<AParametricLobbyPlatform>();
	TestTrue(TEXT("Destruction defaults on"), A->bDestructible);
	TestTrue(TEXT("Centre starts intact"), A->IsPadSurfaceIntact(TEXT("Center")));
	FHitResult Hit(A, A->GetDynamicMeshComponent(), FVector(0,0,0), FVector::UpVector);
	TestTrue(TEXT("Standard point damage creates a real cavity"), UGameplayStatics::ApplyPointDamage(A, 100,
		FVector(0,0,-1), Hit, nullptr, nullptr, nullptr) > 0);
	TestEqual(TEXT("One point impact recorded"), A->DamageCutCount, 1);
	TestTrue(TEXT("Damaged slab remains watertight"), A->GetDynamicMeshComponent()->GetMesh()->IsClosed());
	TestFalse(TEXT("Hole removes original spawn support"), A->IsPadSurfaceIntact(TEXT("Center")));
	TestNull(TEXT("Cannot spawn onto a destroyed pad"), A->SpawnActorAtPad(TEXT("Center"), ACharacter::StaticClass()));
	FHitResult Trace;
	TestFalse(TEXT("Through-hole removes collision too"), World->LineTraceSingleByChannel(Trace, FVector(0,0,200), FVector(0,0,-200), ECC_Pawn));
	bool bFoundCut = false, bCorrectUV = true;
	const FDynamicMesh3* Mesh = A->GetDynamicMeshComponent()->GetMesh();
	for (int32 T : Mesh->TriangleIndicesItr())
	{
		if (Mesh->Attributes()->GetMaterialID()->GetValue(T) != 4) { continue; }
		bFoundCut = true;
		const FIndex3i V = Mesh->GetTriangle(T), UV = Mesh->Attributes()->PrimaryUV()->GetTriangle(T);
		for (int32 K=0; K<3; ++K) { bCorrectUV &= FMath::IsNearlyEqual(double(Mesh->Attributes()->PrimaryUV()->GetElement(UV[K]).Y), -Mesh->GetVertex(V[K]).Z/A->Thickness, .0001); }
	}
	TestTrue(TEXT("Cut walls show one aligned interior layer"), bFoundCut && bCorrectUV);
	A->RebuildPlatform();
	TestFalse(TEXT("Rebuilding does not heal damage"), A->IsPadSurfaceIntact(TEXT("Center")));
	A->RepairPlatform();
	TestTrue(TEXT("Explicit repair restores support"), A->IsPadSurfaceIntact(TEXT("Center")));
	A->bDestructible = false;
	TestFalse(TEXT("Flag blocks direct carving"), A->RemovePlatformSphere(FVector::ZeroVector, 300));
	TestEqual(TEXT("Flag blocks point damage"), UGameplayStatics::ApplyPointDamage(A,100,FVector(0,0,-1),Hit,nullptr,nullptr,nullptr), 0.f);
	TestFalse(TEXT("Flag blocks complete destruction"), A->DestroyPlatformCompletely());
	TestTrue(TEXT("Protected slab keeps collision"), World->LineTraceSingleByChannel(Trace,FVector(0,0,200),FVector(0,0,-200),ECC_Pawn));
	A->bDestructible = true;
	FRadialDamageEvent Blast; Blast.Origin = FVector(700,0,20); Blast.Params = FRadialDamageParams(100,300);
	TestTrue(TEXT("Radial damage uses explosion origin/radius"), A->TakeDamage(100,Blast,nullptr,nullptr) > 0);
	TestFalse(TEXT("Explosion opens its local region"), World->LineTraceSingleByChannel(Trace,FVector(700,0,200),FVector(700,0,-200),ECC_Pawn));
	TestTrue(TEXT("Distant centre stays intact"), A->IsPadSurfaceIntact(TEXT("Center")));
	A->RepairPlatform();
	auto* Bomb = World->SpawnActor<ABombProjectile>(FVector(0,0,30), FRotator::ZeroRotator);
	Bomb->BlastRadius = 300; Bomb->BlastDamage = 100; Bomb->Explode();
	TestTrue(TEXT("Existing bomb damage path reaches the lobby platform"), A->DamageCutCount > 0);
	TestFalse(TEXT("Bomb removes real collision"), A->IsPadSurfaceIntact(TEXT("Center")));
	A->RepairPlatform();
	A->SetActorTransform(FTransform(FRotator(20,40,0),FVector(7000,-2000,1000),FVector(2,1,.5)));
	FVector Centre; A->GetPadWorldCenter(TEXT("Center"), Centre);
	TestTrue(TEXT("Damage supports transformed and nonuniformly scaled slabs"), A->RemovePlatformSphere(Centre, 200));
	TestFalse(TEXT("Transformed centre is removed"), A->IsPadSurfaceIntact(TEXT("Center")));
	TestTrue(TEXT("Enabled complete destruction succeeds"), A->DestroyPlatformCompletely());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLobbyPlatformSpawnTest, "MyProject6.LobbyPlatform.SpawnLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLobbyPlatformSpawnTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* A = World->SpawnActor<AParametricLobbyPlatform>();
	A->Pads[0].ActorClass = ACharacter::StaticClass();
	A->Pads[0].SpawnOffset.Z = 110;
	AActor* First = A->SpawnActorAtPad(TEXT("Center"), nullptr);
	if (!TestNotNull(TEXT("Configured class spawns on named pad"), First)) { return false; }
	FTransform Expected; A->GetPadSpawnTransform(TEXT("Center"), Expected);
	TestTrue(TEXT("Spawn is at the exact requested transform"), First->GetActorTransform().Equals(Expected, .001));
	TestEqual(TEXT("Owner identifies the platform"), First->GetOwner(), static_cast<AActor*>(A));
	TestEqual(TEXT("Spawn attaches when requested"), First->GetAttachParentActor(), static_cast<AActor*>(A));
	TestEqual(TEXT("Repeated calls do not duplicate actors"), A->SpawnActorAtPad(TEXT("Center"), nullptr), First);
	TestEqual(TEXT("Batch spawning is idempotent"), A->SpawnConfiguredActors().Num(), 1);
	A->RebuildPlatform();
	TestEqual(TEXT("Rebuild preserves existing actor"), A->GetSpawnedActorAtPad(TEXT("Center")), First);
	A->ClearSpawnedActorAtPad(TEXT("Center"));
	TestNull(TEXT("Clear frees the slot"), A->GetSpawnedActorAtPad(TEXT("Center")));
	AActor* Second = A->SpawnActorAtPad(TEXT("Center"), nullptr);
	TestNotNull(TEXT("Cleared pad respawns"), Second);
	if (Second) { Second->Destroy(); }
	TestNotNull(TEXT("Externally destroyed actor can be replaced"), A->SpawnActorAtPad(TEXT("Center"), nullptr));
	TestNull(TEXT("Recursive lobby spawning rejected"), A->SpawnActorAtPad(TEXT("Right"), AParametricLobbyPlatform::StaticClass()));
	TestNull(TEXT("Invalid pad rejected"), A->SpawnActorAtPad(TEXT("Missing"), ACharacter::StaticClass()));
	A->ClearSpawnedActors();
	TestNull(TEXT("Unconfigured class rejected"), A->SpawnActorAtPad(TEXT("Right"), nullptr));
	return true;
}
#endif
