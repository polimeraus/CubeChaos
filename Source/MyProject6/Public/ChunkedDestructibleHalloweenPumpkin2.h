// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "ChunkedDestructibleHalloweenPumpkin2.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UPointLightComponent;

/** Read-only measurements for the generated hollow jack-o'-lantern. */
USTRUCT(BlueprintType)
struct FChunkedDestructibleHalloweenPumpkin2Metrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin 2")
	FIntVector BackingGridBlockCount = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin 2")
	FVector WorldDimensions = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin 2")
	FVector ApproximateCavityDimensions = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin 2")
	int32 GeneratedShellBlockCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Halloween Pumpkin 2")
	int32 CarvedFaceBlockCount = 0;
};

/**
 * A hollow, internally lit voxel jack-o'-lantern inspired by the classic round Halloween pumpkin.
 *
 * The orange ribbed shell, face cutouts and curved stem use the same lightweight destructible-cell
 * system as AChunkedDestructibleFloor. The cavity is physically empty; warm inner cut faces and a
 * real point light make the angry eyes, triangular nose and rising toothed grin readable. The face
 * points along local +X.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Destructible Halloween Pumpkin 2 (Hollow)"))
class MYPROJECT6_API AChunkedDestructibleHalloweenPumpkin2 : public AChunkedDestructibleFloor
{
	GENERATED_BODY()

public:
	virtual bool UsesPlanarAnimation() const override { return false; }

	AChunkedDestructibleHalloweenPumpkin2();
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

	/** Rebuilds the hollow pumpkin from the current Details-panel settings. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 2|Generation")
	void RebuildPumpkin2(bool bPreserveCellState = false);

	/** Configures the principal body, cavity and block dimensions in one rebuild. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 2|Generation")
	void ConfigureHollowPumpkin(
		FVector2D NewBodyRadiusBlocks,
		float NewBodyHalfHeightBlocks,
		float NewShellThicknessBlocks,
		float NewStemHeightBlocks,
		FVector NewBlockDimensions,
		FVector NewBlockSpacing,
		bool bPreserveCellState = false);

	/** Enables or removes the through-cut angry jack-o'-lantern face. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 2|Face")
	void SetPumpkin2FaceEnabled(bool bEnabled, bool bPreserveCellState = false);

	/** Updates the built-in body, groove, inner-flesh and stem colors immediately. */
	UFUNCTION(BlueprintCallable, Category = "Destructible Halloween Pumpkin 2|Materials")
	void SetPumpkin2Colors(
		FLinearColor NewBodyColor,
		FLinearColor NewRibColor,
		FLinearColor NewInteriorColor,
		FLinearColor NewStemColor);

	/** True only for points inside the empty ellipsoidal cavity. */
	UFUNCTION(BlueprintPure, Category = "Destructible Halloween Pumpkin 2|Cavity")
	bool IsWorldLocationInsideCavity(FVector WorldLocation) const;

	UFUNCTION(BlueprintPure, Category = "Destructible Halloween Pumpkin 2|Generation")
	FChunkedDestructibleHalloweenPumpkin2Metrics GetPumpkin2Metrics() const;

	virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const override;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Destructible Halloween Pumpkin 2|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPointLightComponent> InteriorLight;

	/** Non-colliding zero-thickness candle glow drawn immediately behind the true face holes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Destructible Halloween Pumpkin 2|Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDynamicMeshComponent> FaceGlowSurface;

	/** Local X is front/back and local Y is the visible width, both measured in blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Body", meta = (ClampMin = "4.0", ClampMax = "32.0"))
	FVector2D BodyRadiusBlocks = FVector2D(9.5, 11.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Body", meta = (ClampMin = "4.0", ClampMax = "32.0"))
	float BodyHalfHeightBlocks = 8.0f;

	/** Higher values make the body fuller through the middle and flatter at the poles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Body", meta = (ClampMin = "1.5", ClampMax = "6.0"))
	float VerticalProfilePower = 2.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Body", meta = (ClampMin = "6", ClampMax = "24"))
	int32 RibCount = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Body", meta = (ClampMin = "0.0", ClampMax = "0.35"))
	float RibDepth = 0.16f;

	/** Radial thickness of the real destructible shell; the middle contains no cells or collision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Cavity", meta = (ClampMin = "1.0", ClampMax = "8.0"))
	float ShellThicknessBlocks = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Blocks", meta = (ClampMin = "1.0", Units = "cm"))
	FVector BlockDimensions = FVector(30.0, 30.0, 30.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Blocks", meta = (ClampMin = "0.0", Units = "cm"))
	FVector BlockSpacing = FVector(0.5, 0.5, 0.5);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Face")
	bool bCarveJackOLanternFace = true;

	/** Draws a non-colliding emissive surface behind the openings without filling the cavity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Face")
	bool bEnableFaceGlowSurface = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Face", meta = (HideAlphaChannel, EditCondition = "bEnableFaceGlowSurface"))
	FLinearColor FaceGlowColor = FLinearColor(1.0f, 0.22f, 0.004f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Face", meta = (ClampMin = "0.0", EditCondition = "bEnableFaceGlowSurface"))
	float FaceGlowIntensity = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Face", meta = (EditCondition = "bEnableFaceGlowSurface"))
	TObjectPtr<UMaterialInterface> FaceGlowMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Stem", meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float StemRadiusBlocks = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Stem", meta = (ClampMin = "2.0", ClampMax = "20.0"))
	float StemHeightBlocks = 8.0f;

	/** Net displacement of the stem tip in local X/Y, measured in blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Stem", meta = (ClampMin = "-10.0", ClampMax = "10.0"))
	FVector2D StemLeanBlocks = FVector2D(0.75, -3.5);

	/** Middle-of-stem bow added with a sine curve, producing a visibly curved silhouette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Stem", meta = (ClampMin = "-10.0", ClampMax = "10.0"))
	FVector2D StemCurveBlocks = FVector2D(0.55, -2.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Lighting")
	bool bEnableInteriorLight = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Lighting", meta = (HideAlphaChannel))
	FLinearColor InteriorLightColor = FLinearColor(1.0f, 0.30f, 0.01f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Lighting", meta = (ClampMin = "0.0", Units = "Lumens"))
	float InteriorLightIntensity = 800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Lighting", meta = (ClampMin = "1.0", Units = "cm"))
	float InteriorLightAttenuationRadius = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Lighting")
	bool bInteriorLightCastsShadows = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Materials", meta = (HideAlphaChannel))
	FLinearColor BodyColor = FLinearColor(1.0f, 0.105f, 0.004f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Materials", meta = (HideAlphaChannel))
	FLinearColor RibColor = FLinearColor(0.36f, 0.018f, 0.0015f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Materials", meta = (HideAlphaChannel))
	FLinearColor InteriorColor = FLinearColor(1.0f, 0.32f, 0.012f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Materials", meta = (HideAlphaChannel))
	FLinearColor StemColor = FLinearColor(0.10f, 0.035f, 0.009f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Materials|Overrides")
	TObjectPtr<UMaterialInterface> BodyMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Materials|Overrides")
	TObjectPtr<UMaterialInterface> RibMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Materials|Overrides")
	TObjectPtr<UMaterialInterface> InteriorMaterialOverride;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Destructible Halloween Pumpkin 2|Materials|Overrides")
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

protected:
	static constexpr int32 BodyMaterialSlot = 0;
	static constexpr int32 RibMaterialSlot = 1;
	static constexpr int32 InteriorMaterialSlot = 2;
	static constexpr int32 StemMaterialSlot = 3;

	FIntVector PumpkinGridCellCount = FIntVector::ZeroValue;
	FVector PumpkinGridLocalMin = FVector::ZeroVector;
	FVector CachedWorldDimensions = FVector::ZeroVector;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> DefaultFlatMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> DefaultFaceGlowMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> RibMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> InteriorMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> StemMaterialInstance;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FaceGlowMaterialInstance;

	bool DecodeCell(FIntPoint Cell, int32& OutX, int32& OutY, int32& OutZ) const;
	FIntPoint EncodeCell(int32 X, int32 Y, int32 Z) const;
	FVector GetPhysicalCellLocalCenter(int32 X, int32 Y, int32 Z) const;
	bool IsInsideBodyAtLocalPosition(const FVector& LocalPosition, float InsetBlocks) const;
	bool IsOuterBodyCell(int32 X, int32 Y, int32 Z) const;
	bool IsInnerCavityCell(int32 X, int32 Y, int32 Z) const;
	bool IsBodyShellCell(int32 X, int32 Y, int32 Z) const;
	bool IsStemCell(int32 X, int32 Y, int32 Z) const;
	virtual bool IsFaceProjection(const FVector& LocalCenter) const;
	bool IsFaceOpeningCell(int32 X, int32 Y, int32 Z) const;
	bool CellExists3D(int32 X, int32 Y, int32 Z) const;
	bool CellBelongsToShape3D(int32 X, int32 Y, int32 Z) const;
	int32 GetExposedFaceMaterial(
		int32 CellMaterial,
		int32 NeighborX,
		int32 NeighborY,
		int32 NeighborZ) const;
	float GetBodyCenterZ() const;
	float GetStemBottomZ() const;
	int32 GetEffectivePumpkinChunkSideLength() const;

	void EnsurePumpkinMaterials();
	void UpdatePumpkinMaterialParameters();
	void UpdateInteriorLight();
	void RebuildFaceGlowSurface();
	UMaterialInterface* ResolvePumpkinMaterial(int32 MaterialSlot) const;
	static void ApplyLitColorParameters(
		UMaterialInstanceDynamic* MaterialInstance,
		FLinearColor Color,
		float Roughness);
};
