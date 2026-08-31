// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "SimpleHouseActor.generated.h"

class UMaterialInterface;
class UDynamicMesh;

/**
 * A stylised, hollow house generated entirely with Geometry Script.
 * Doors and windows are real boolean-cut openings, not textured details.
 */
UCLASS(BlueprintType, Blueprintable)
class MYPROJECT6_API ASimpleHouseActor : public ADynamicMeshActor
{
	GENERATED_BODY()

public:
	ASimpleHouseActor(const FObjectInitializer& ObjectInitializer);

	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Dimensions", meta = (ClampMin = "10.0", Units = "cm"))
	float Width = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Dimensions", meta = (ClampMin = "10.0", Units = "cm"))
	float Depth = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Dimensions", meta = (ClampMin = "10.0", Units = "cm"))
	float WallHeight = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Dimensions", meta = (ClampMin = "10.0", Units = "cm"))
	float RoofHeight = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Dimensions", meta = (ClampMin = "2.0", Units = "cm"))
	float WallThickness = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Openings", meta = (ClampMin = "10.0", Units = "cm"))
	float DoorWidth = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Openings", meta = (ClampMin = "10.0", Units = "cm"))
	float DoorHeight = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Openings", meta = (ClampMin = "10.0", Units = "cm"))
	float WindowWidth = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Openings", meta = (ClampMin = "10.0", Units = "cm"))
	float WindowHeight = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Rendering")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simple House|Collision")
	bool bEnableCollision = true;

	/** Recreates the outer shell and cuts the door and two front windows. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Simple House")
	void RebuildMesh();

	/** Removes a spherical volume from the house at a world-space impact location. */
	UFUNCTION(BlueprintCallable, Category = "Simple House|Destruction")
	void CarveSphereAtWorldLocation(FVector WorldLocation, float Radius, float DepthFraction = 0.65f);

private:
	UDynamicMesh* GetOrCreateDamageCutter(float Radius);

	/** Reused because projectile cavity radii are normally constant between impacts. */
	UPROPERTY(Transient)
	TObjectPtr<UDynamicMesh> CachedDamageCutterMesh;

	float CachedDamageCutterRadius = -1.0f;
};
