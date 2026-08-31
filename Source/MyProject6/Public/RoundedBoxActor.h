// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "RoundedBoxActor.generated.h"

class UMaterialInterface;
class UDynamicMesh;

/**
 * Parametric box whose mesh is generated with Geometry Script.
 * The actor origin is at the centre of the bottom face so it sits on the surface
 * onto which it is dropped in the editor.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API ARoundedBoxActor : public ADynamicMeshActor
{
	GENERATED_BODY()

public:
	ARoundedBoxActor(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;

	/** Size along the local X axis, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Box|Dimensions",
		meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float Width = 200.0f;

	/** Size along the local Y axis, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Box|Dimensions",
		meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float Depth = 200.0f;

	/** Size along the local Z axis, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Box|Dimensions",
		meta = (ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float Height = 100.0f;

	/** Radius of every rounded edge. It is clamped to fit the smallest dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Box|Corners",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float CornerRadius = 15.0f;

	/** Number of loops used by the rounded profile. Higher values look smoother. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Box|Corners",
		meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "8"))
	int32 CornerSegments = 4;

	/** Shape of the bevel profile: 0 is flat/chamfered, 1 is fully round. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Box|Corners",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Roundness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Box|Rendering")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rounded Box|Collision")
	bool bEnableCollision = true;

	/** Rebuilds the dynamic mesh immediately from the current parameters. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Rounded Box")
	void RebuildMesh();

	/** Removes a spherical volume from the mesh at a world-space impact location. */
	UFUNCTION(BlueprintCallable, Category = "Rounded Box|Destruction")
	void CarveSphereAtWorldLocation(FVector WorldLocation, float Radius, float DepthFraction = 0.65f);

private:
	UDynamicMesh* GetOrCreateDamageCutter(float Radius);

	/** Reused because projectile cavity radii are normally constant between impacts. */
	UPROPERTY(Transient)
	TObjectPtr<UDynamicMesh> CachedDamageCutterMesh;

	float CachedDamageCutterRadius = -1.0f;
};
