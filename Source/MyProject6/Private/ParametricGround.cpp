#include "ParametricGround.h"
#include "ParametricGroundGeometry.h"

#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace UE::Geometry;

namespace ParametricGround
{
	double SafePositive(float Value, double Minimum, double Fallback)
	{
		return FMath::IsFinite(Value) ? FMath::Max(double(Value), Minimum) : Fallback;
	}
}

AParametricGround::AParametricGround(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bEnableComputeMeshPool = false;
	GetDynamicMeshComponent()->SetCanEverAffectNavigation(true);
	GetDynamicMeshComponent()->bUseAsyncCooking = false;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(
		TEXT("/Game/GameplayActors/Ground/Materials/M_ParametricGround.M_ParametricGround"));
	if (MaterialFinder.Succeeded())
	{
		GroundMaterial = MaterialFinder.Object;
		BoundaryMaterial = MaterialFinder.Object;
	}
}

void AParametricGround::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildGround();
}

void AParametricGround::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	if (!IsTemplate())
	{
		// Restores transient material instances after loading a saved level, including cooked maps.
		// Regenerate from the saved parameters if the dynamic mesh was not serialized.
		if (GetDynamicMeshComponent()->GetMesh()->TriangleCount() == 0)
		{
			RebuildGround();
		}
		else
		{
			UpdateMaterials();
		}
	}
}

void AParametricGround::UpdateMaterials()
{
	UMaterialInterface* Fallback = GetDefault<AParametricGround>()->GroundMaterial;
	if (!Fallback)
	{
		Fallback = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	auto Apply = [this](int32 Slot, UMaterialInterface* Parent, const FLinearColor& Color,
		TObjectPtr<UMaterialInstanceDynamic>& MID)
	{
		if (!MID || MID->Parent != Parent)
		{
			MID = UMaterialInstanceDynamic::Create(Parent, this);
			MID->SetFlags(RF_Transient);
		}
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		GetDynamicMeshComponent()->SetMaterial(Slot, MID);
	};
	Apply(0, GroundMaterial ? GroundMaterial.Get() : Fallback, GroundColor, GroundMID);
	Apply(1, BoundaryMaterial ? BoundaryMaterial.Get() : Fallback, BoundaryColor, BoundaryMID);
}

void AParametricGround::RebuildGround()
{
	if (IsTemplate())
	{
		return;
	}
	UDynamicMeshComponent* Component = GetDynamicMeshComponent();
	const double SafeSize = ParametricGround::SafePositive(Size, 10.0, 20000.0);
	const double SafeThickness = ParametricGround::SafePositive(Thickness, 1.0, 50.0);
	const double SafeUV = ParametricGround::SafePositive(UVTileSize, 1.0, 100.0);
	FDynamicMesh3 Mesh;
	ParametricGroundGeometry::BuildSlab(Mesh, Shape, SafeSize, SafeThickness, SafeUV, CircleSegments,
		bShowBoundary, BoundaryInset, BoundaryWidth);

	// The line is a coplanar material region of a closed slab: no overlapping surfaces or raised seams.
	Component->SetTransientDeferCollisionUpdates(true);
	Component->SetMesh(MoveTemp(Mesh));
	UpdateMaterials();
	Component->SetCollisionEnabled(bEnableCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Component->SetTransientDeferCollisionUpdates(false);
	Component->SetComplexAsSimpleCollisionEnabled(bEnableCollision, true);
}
