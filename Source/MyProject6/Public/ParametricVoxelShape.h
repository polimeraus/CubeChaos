#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ParametricVoxelShape.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EVoxelShapePreset : uint8
{
	Heart UMETA(DisplayName = "Heart / Kalp"),
	InvaderCrab UMETA(DisplayName = "Arcade Invader - Crab"),
	InvaderSquid UMETA(DisplayName = "Arcade Invader - Squid"),
	Ghost UMETA(DisplayName = "Arcade Ghost / Hayalet"),
	Chomper UMETA(DisplayName = "Arcade Chomper"),
	Star UMETA(DisplayName = "Star / Yildiz"),
	Custom UMETA(DisplayName = "Custom Pixel Pattern")
};

UENUM(BlueprintType)
enum class EVoxelShapeAppearance : uint8
{
	NeonWireCubes UMETA(DisplayName = "Neon Wire Cubes"),
	SolidCubes UMETA(DisplayName = "Solid Cubes")
};

/** Pixel silhouettes extruded into real cubes. X = width, Y = depth, Z = height. */
UCLASS(Blueprintable, meta = (DisplayName = "Parametric Voxel Shape"))
class MYPROJECT6_API AParametricVoxelShape : public AActor
{
	GENERATED_BODY()

public:
	AParametricVoxelShape();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	/** Also call after changing properties from a runtime Blueprint. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Voxel Shape")
	void RebuildShape();

	/** Runtime-friendly setter; rebuilds immediately. */
	UFUNCTION(BlueprintCallable, Category = "Voxel Shape")
	void ConfigureShape(EVoxelShapePreset NewShape, int32 NewWidthCubes, int32 NewDepthCubes);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Shape|Components")
	TObjectPtr<UInstancedStaticMeshComponent> WireEdges;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Voxel Shape|Components")
	TObjectPtr<UInstancedStaticMeshComponent> SolidCubes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Shape")
	EVoxelShapePreset Shape = EVoxelShapePreset::Heart;

	/** Exact number of grid columns, not a world-space scale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Shape", meta = (ClampMin = "1", ClampMax = "128", UIMin = "5", UIMax = "48"))
	int32 WidthCubes = 15;

	/** Keep the original pixel-art proportions when changing width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Shape")
	bool bPreserveAspectRatio = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Shape", meta = (EditCondition = "!bPreserveAspectRatio", ClampMin = "1", ClampMax = "128"))
	int32 HeightCubes = 13;

	/** Number of complete cube layers along local Y (front to back). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Shape", meta = (DisplayName = "Depth / Thickness Cubes", ClampMin = "1", ClampMax = "32", UIMax = "8"))
	int32 DepthCubes = 2;

	/** Rows are read top to bottom. 1, # and X are filled; 0, dot and space are empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Shape", meta = (EditCondition = "Shape == EVoxelShapePreset::Custom", EditConditionHides))
	TArray<FString> CustomPatternRows;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Cubes", meta = (ClampMin = "1.0", ClampMax = "1000.0", Units = "cm"))
	float CubeSize = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Cubes", meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm"))
	float CubeGap = 0.0f;

	/** Center the pivot vertically; otherwise the bottom of the cubes is at Z = 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Cubes")
	bool bCenterPivot = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Appearance")
	EVoxelShapeAppearance Appearance = EVoxelShapeAppearance::NeonWireCubes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Appearance", meta = (EditCondition = "Appearance == EVoxelShapeAppearance::NeonWireCubes", ClampMin = "0.1", Units = "cm"))
	float WireThickness = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Appearance")
	FLinearColor Color = FLinearColor(1.0f, 0.025f, 0.32f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Appearance", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float EmissiveIntensity = 5.0f;

	/** Default uses M_VoxelShapeGlow's Color parameter (HDR color). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel Shape|Appearance")
	TObjectPtr<UMaterialInterface> ShapeMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Voxel Shape|Generated")
	FIntVector GeneratedGrid = FIntVector::ZeroValue;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Voxel Shape|Generated")
	int32 GeneratedCubeCount = 0;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Voxel Shape|Generated")
	int32 GeneratedEdgeCount = 0;

	/** Cube envelope excluding the small wire radius. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Voxel Shape|Generated")
	FVector GeneratedSize = FVector::ZeroVector;

	/** Invalid custom patterns and oversized requests produce no partial shape. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Voxel Shape|Generated")
	FString GenerationWarning;

private:
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShapeMaterialInstance;

	TArray<FString> GetPattern() const;
	void UpdateMaterial();
};
