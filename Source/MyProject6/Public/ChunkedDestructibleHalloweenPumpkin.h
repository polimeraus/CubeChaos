// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleHalloweenPumpkin.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/** Read-only measurements for the generated jack-o'-lantern. */
USTRUCT(BlueprintType)
struct FChunkedDestructibleHalloweenPumpkinMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin")
	FIntVector BackingGridBlockCount = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin")
	FVector WorldDimensions = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin")
	int32 GeneratedBlockCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin")
	int32 CarvedFaceBlockCount = 0;
};

/**
 * An iconic voxel jack-o'-lantern assembled from the standard destructible cells.
 *
 * The pumpkin body, carved face and leaning stem are addressed as a 3D block grid while all
 * health, projectile damage, explosions, ripple timing, flashes, sounds, restoration and events
 * continue to come from AChunkedDestructibleFloor. The actor faces local +X.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Destructible Halloween Pumpkin"))
class MYPROJECT6_API AChunkedDestructibleHalloweenPumpkin : public AChunkedDestructibleFloor
{
	GENERATED_BODY()

public:
	virtual bool UsesPlanarAnimation() const override { return false; }

	AChunkedDestructibleHalloweenPumpkin();
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	/** Rebuilds the complete pumpkin from the current Details-panel settings. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin|Generation")
	void RebuildPumpkin(bool bPreserveCellState = false);

	/** Configures the principal pumpkin dimensions and rebuilds all chunks. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin|Generation")
	void ConfigurePumpkin(
		FVector2D NewBodyRadiusBlocks,
		float NewBodyHalfHeightBlocks,
		float NewStemHeightBlocks,
		FVector NewBlockDimensions,
		FVector NewBlockSpacing,
		bool bPreserveCellState = false);

	/** Enables or removes the classic eyes, nose and toothed grin. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin|Face")
	void SetJackOLanternFace(
		bool bEnabled,
		float NewCarveDepthBlocks = 2.0f,
		bool bPreserveCellState = false);

	/** Updates the built-in colored materials immediately. Material overrides remain untouched. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin|Materials")
	void SetPumpkinColors(
		FLinearColor NewBodyColor,
		FLinearColor NewRibColor,
		FLinearColor NewFaceColor,
		FLinearColor NewStemColor,
		bool bNewUseGlowingFace = false,
		float NewFaceGlowIntensity = 3.0f);

	UFUNCTION(BlueprintPure, Category = "Destructible Halloween Pumpkin|Generation")
	FChunkedDestructibleHalloweenPumpkinMetrics GetPumpkinMetrics() const;

	virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const override;

protected:
	virtual void BeginPlay() override;

	/** Horizontal ellipsoid radii, measured in destruction blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Body", meta = (ClampMin = "3.0", ClampMax = "32.0"))
	FVector2D BodyRadiusBlocks = FVector2D(9.0, 8.0);

	/** Half of the orange body's height, measured in destruction blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Body", meta = (ClampMin = "3.0", ClampMax = "32.0"))
	float BodyHalfHeightBlocks = 6.5f;

	/** Higher values produce the characteristic full sides and flatter pumpkin poles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Body", meta = (ClampMin = "1.5", ClampMax = "6.0"))
	float VerticalProfilePower = 2.6f;

	/** Number of vertical lobes around the orange body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Body", meta = (ClampMin = "6", ClampMax = "20"))
	int32 RibCount = 10;

	/** Depth of the alternating vertical grooves in the silhouette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Body", meta = (ClampMin = "0.0", ClampMax = "0.32"))
	float RibDepth = 0.13f;

	/** Dimensions of every independently destructible pumpkin block. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Blocks", meta = (ClampMin = "1.0", Units = "cm"))
	FVector BlockDimensions = FVector(42.0, 42.0, 42.0);

	/** Small positive gaps make the individual destructible blocks clearly readable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Blocks", meta = (ClampMin = "0.0", Units = "cm"))
	FVector BlockSpacing = FVector(1.5, 1.5, 1.5);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Face")
	bool bCarveJackOLanternFace = true;

	/** Number of front block layers removed for the eyes, nose and grin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Face", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float FaceCarveDepthBlocks = 2.0f;

	/** Uses the bundled glow material on the block layer behind the carved openings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Face")
	bool bUseGlowingFace = false;

	/** Radius of the stem, measured in destruction blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Stem", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float StemRadiusBlocks = 1.25f;

	/** Height of the destructible stem, measured in blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Stem", meta = (ClampMin = "1.0", ClampMax = "16.0"))
	float StemHeightBlocks = 4.0f;

	/** Horizontal displacement of the top of the stem, measured in blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Stem", meta = (ClampMin = "-8.0", ClampMax = "8.0"))
	FVector2D StemLeanBlocks = FVector2D(0.7, 0.35);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials", meta = (HideAlphaChannel))
	FLinearColor BodyColor = FLinearColor(1.0f, 0.17f, 0.012f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials", meta = (HideAlphaChannel))
	FLinearColor RibColor = FLinearColor(0.48f, 0.045f, 0.003f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials", meta = (HideAlphaChannel))
	FLinearColor FaceColor = FLinearColor(1.0f, 0.32f, 0.015f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials", meta = (HideAlphaChannel))
	FLinearColor StemColor = FLinearColor(0.08f, 0.20f, 0.025f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials", meta = (ClampMin = "0.0", EditCondition = "bUseGlowingFace"))
	float FaceGlowIntensity = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials|Overrides")
	TObjectPtr<UMaterialInterface> BodyMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials|Overrides")
	TObjectPtr<UMaterialInterface> RibMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials|Overrides")
	TObjectPtr<UMaterialInterface> FaceMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin|Materials|Overrides")
	TObjectPtr<UMaterialInterface> StemMaterialOverride;

	virtual bool CalculateLayout() override;
	virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
	virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& OutMesh) const override;
	virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& OutCollision) const override;
	virtual void ConfigureChunkComponent(UDynamicMeshComponent* Component) const override;
	virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
	virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& OutCell) const override;
	virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
	virtual int32 SelectCellsInRadius(
		FVector WorldLocation,
		float Radius,
		EChunkedFloorBlastShape Shape,
		TFunctionRef<void(int32, float)> CellFunction) override;
	virtual void MarkCellChunksDirty(FIntPoint Cell) override;

private:
	static constexpr int32 BodyMaterialSlot = 0;
	static constexpr int32 RibMaterialSlot = 1;
	static constexpr int32 FaceMaterialSlot = 2;
	static constexpr int32 StemMaterialSlot = 3;

	FIntVector PumpkinGridCellCount = FIntVector::ZeroValue;
	FVector PumpkinGridLocalMin = FVector::ZeroVector;
	FVector CachedWorldDimensions = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> DefaultFlatMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> DefaultGlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RibMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DarkFaceMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowFaceMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StemMaterialInstance;

	bool DecodeCell(FIntPoint Cell, int32& OutX, int32& OutY, int32& OutZ) const;
	FIntPoint EncodeCell(int32 X, int32 Y, int32 Z) const;
	FVector GetPhysicalCellLocalCenter(int32 X, int32 Y, int32 Z) const;
	bool IsPumpkinBodyCell(int32 X, int32 Y, int32 Z) const;
	bool IsStemCell(int32 X, int32 Y, int32 Z) const;
	bool IsFaceCarvedCell(int32 X, int32 Y, int32 Z) const;
	bool IsFaceBackingCell(int32 X, int32 Y, int32 Z) const;
	bool IsFaceProjection(const FVector& LocalCenter) const;
	int32 CountBodyCellsTowardFront(int32 X, int32 Y, int32 Z) const;
	bool CellExists3D(int32 X, int32 Y, int32 Z) const;
	float GetBodyCenterZ() const;
	float GetStemBottomZ() const;
	int32 GetEffectivePumpkinChunkSideLength() const;

	void EnsurePumpkinMaterials();
	void UpdatePumpkinMaterialParameters();
	UMaterialInterface* ResolvePumpkinMaterial(int32 MaterialSlot) const;
	static void ApplyColorParameters(
		UMaterialInstanceDynamic* MaterialInstance,
		FLinearColor Color,
		bool bEmissive,
		float Intensity);
};
