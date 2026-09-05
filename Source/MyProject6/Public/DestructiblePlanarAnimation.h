#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DestructiblePlanarAnimation.generated.h"

class AChunkedDestructibleFloor;
class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;

UENUM(BlueprintType)
enum class EPlanarAnimationType : uint8
{
	None,
	FlowingWave,
	SineBands,
	Spiral
};

UENUM(BlueprintType)
enum class EPlanarFlowMode : uint8
{
	Directional UMETA(DisplayName = "Edge To Edge / Diagonal"),
	Radial UMETA(DisplayName = "Origin To Edges")
};

/** Actor-local coordinates: rotating or scaling an actor carries its animation with it. */
USTRUCT(BlueprintType)
struct FDestructiblePlanarFlow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow")
	EPlanarFlowMode Mode = EPlanarFlowMode::Directional;

	/** 0 = local +X, 90 = local +Y. Other angles sweep diagonally across the footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow", meta = (Units = "deg"))
	float AngleDegrees = 0.0f;

	/** Normalized footprint position; (0.5, 0.5) is the center, (0, 0) a corner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow", meta = (ClampMin = "0", ClampMax = "1"))
	FVector2D Origin = FVector2D(0.5, 0.5);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flow")
	bool bReverse = false;

	/** X = travel, Y = transverse position (or angle for radial flow), both in [0, 1]. */
	FVector2D Coordinates(FVector2D NormalizedPosition, FVector2D Size) const;
	double TravelDistance(FVector2D Size) const;
};

/** One shared material and a small music mask per actor; no tick and no per-frame mesh edits. */
UCLASS(Transient)
class UDestructiblePlanarAnimation : public UObject
{
	GENERATED_BODY()
	friend class FDestructiblePlanarAnimationTest;

public:
	bool Initialize(AChunkedDestructibleFloor* InOwner);
	void RefreshChunk(int32 ChunkIndex);
	void SetFixedAnimation(TConstArrayView<float> Alphas);
	void SetMusicMask(TConstArrayView<uint8> Mask, int32 Columns, int32 Rows);
	void ClearMusic();
	void Release();

private:
	void UpdateVisibility();
	UPROPERTY(Transient) TObjectPtr<AChunkedDestructibleFloor> Owner;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient) TObjectPtr<UTexture2D> MusicTexture;
	UPROPERTY(Transient) TArray<TObjectPtr<UDynamicMeshComponent>> Surfaces;
	bool bFixedVisible = false;
	bool bMusicVisible = false;
	bool bSurfacesVisible = false;
};
