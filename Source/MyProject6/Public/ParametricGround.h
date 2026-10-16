#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "ParametricGround.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EParametricGroundShape : uint8
{
	Circle,
	Square,
	Hexagon
};

/** A solid foundation with an inset painted boundary. Local Z=0 is always the top surface. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Parametric Ground"))
class MYPROJECT6_API AParametricGround : public ADynamicMeshActor
{
	GENERATED_BODY()

public:
	AParametricGround(const FObjectInitializer& ObjectInitializer);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Dimensions")
	EParametricGroundShape Shape = EParametricGroundShape::Circle;

	/** Circle diameter, square side length, or hexagon distance between opposite flat edges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Dimensions",
		meta = (ClampMin = "10.0", UIMin = "100.0", UIMax = "100000.0", Units = "cm"))
	float Size = 20000.0f;

	/** Extends downward from the actor origin, keeping buildings at the same surface height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Dimensions",
		meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "1000.0", Units = "cm"))
	float Thickness = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Dimensions",
		meta = (EditCondition = "Shape == EParametricGroundShape::Circle", ClampMin = "16", ClampMax = "512", UIMin = "32", UIMax = "256"))
	int32 CircleSegments = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Boundary")
	bool bShowBoundary = true;

	/** Perpendicular gap from the ground edge to the OUTER edge of the painted line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Boundary",
		meta = (EditCondition = "bShowBoundary", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float BoundaryInset = 500.0f;

	/** Width measured perpendicular to the boundary, automatically limited to fit the surface. Zero hides it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Boundary",
		meta = (EditCondition = "bShowBoundary", ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float BoundaryWidth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Boundary",
		meta = (EditCondition = "bShowBoundary", HideAlphaChannel))
	FLinearColor BoundaryColor = FLinearColor(1.0f, 0.55f, 0.025f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Appearance", meta = (HideAlphaChannel))
	FLinearColor GroundColor = FLinearColor(0.075f, 0.09f, 0.115f);

	/** Optional replacement material. Expose a vector parameter named Color to use Ground Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Appearance")
	TObjectPtr<UMaterialInterface> GroundMaterial;

	/** Optional replacement material. Expose a vector parameter named Color to use Boundary Color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Appearance")
	TObjectPtr<UMaterialInterface> BoundaryMaterial;

	/** UV repeat size for optional textured materials. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Appearance",
		meta = (ClampMin = "1.0", Units = "cm"))
	float UVTileSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground|Collision")
	bool bEnableCollision = true;

	/** Editor changes rebuild automatically. Call this after changing properties from Blueprint at runtime. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Ground")
	void RebuildGround();

private:
	void UpdateMaterials();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GroundMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BoundaryMID;
};
