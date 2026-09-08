// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedDestructibleFloor.h"

#include "Materials/MaterialInstanceDynamic.h"

void AChunkedDestructibleFloor::RefreshSideNeonMaterial()
{
	if (SideNeon.Mode == EFloorSideNeonMode::Off || !UsesPlanarAnimation())
	{
		SideNeonMaterialInstance = nullptr;
		return;
	}
	UMaterialInterface* Parent = SideNeonMaterial.LoadSynchronous();
	if (!Parent)
	{
		SideNeonMaterialInstance = nullptr;
		return;
	}
	if (!SideNeonMaterialInstance || SideNeonMaterialInstance->Parent != Parent)
	{
		SideNeonMaterialInstance = UMaterialInstanceDynamic::Create(Parent, this);
	}
	if (!SideNeonMaterialInstance) { return; }

	const float Thickness = FMath::Max(static_cast<float>(EffectiveCellDimensions.Z), 1.0f);
	SideNeonMaterialInstance->SetVectorParameterValue(TEXT("GlowColor"),
		SideNeon.Color.GetClamped(0.0f, 1.0f) * FMath::Clamp(SideNeon.Intensity, 0.0f, 100.0f));
	SideNeonMaterialInstance->SetVectorParameterValue(TEXT("BodyColor"), SideNeon.BodyColor.GetClamped());
	SideNeonMaterialInstance->SetVectorParameterValue(TEXT("Stripe"), FLinearColor(
		FMath::Clamp(SideNeon.StripePosition, 0.0f, 1.0f),
		FMath::Clamp(SideNeon.StripeWidth / Thickness, 0.0f, 1.0f),
		SideNeon.Mode == EFloorSideNeonMode::FullSide ? 1.0f : 0.0f, 0.0f));
}

void AChunkedDestructibleFloor::SetSideNeonSettings(const FFloorSideNeonSettings& NewSettings)
{
	const bool bWasActive = SideNeonMaterialInstance != nullptr;
	const EFloorSideNeonScope PreviousScope = SideNeon.Scope;
	SideNeon = NewSettings;
	SideNeon.Intensity = FMath::Clamp(SideNeon.Intensity, 0.0f, 100.0f);
	SideNeon.StripeWidth = FMath::Max(SideNeon.StripeWidth, 0.0f);
	SideNeon.StripePosition = FMath::Clamp(SideNeon.StripePosition, 0.0f, 1.0f);
	RefreshSideNeonMaterial();
	const bool bIsActive = SideNeonMaterialInstance != nullptr;
	if (bWasActive != bIsActive || (bIsActive && PreviousScope != SideNeon.Scope))
	{
		// Preserve cell damage, animation state and chunk ownership. Only material IDs / side UVs change.
		RebuildAllChunks();
	}
}

void AChunkedDestructibleFloor::SetSideNeonColor(FLinearColor NewColor, float NewIntensity)
{
	FFloorSideNeonSettings Settings = SideNeon;
	Settings.Color = NewColor;
	Settings.Intensity = NewIntensity;
	SetSideNeonSettings(Settings);
}

void AChunkedDestructibleFloor::AppendFloorSideQuad(
	UE::Geometry::FDynamicMesh3& Mesh,
	const FVector3d& A, const FVector3d& B, const FVector3d& C, const FVector3d& D,
	const FVector3f& Normal, int32 MaterialIndex,
	float ULength, float VLength, bool bOuterBoundary) const
{
	if (SideNeonMaterialInstance &&
		(bOuterBoundary || SideNeon.Scope == EFloorSideNeonScope::AllExposedSides))
	{
		// Reuse the same four vertices / two triangles. UV0.x becomes height, 0 bottom -> 1 top.
		// This is independent of the shape, chunk, original texture scale and world transform.
		AppendMeshQuad(Mesh, A, B, C, D, Normal, FMath::Max(FloorMaterials.Num(), 1), 1.0f, 1.0f, 1.0f);
	}
	else
	{
		AppendMeshQuad(Mesh, A, B, C, D, Normal, MaterialIndex, ULength, VLength, UVWorldUnits);
	}
}
