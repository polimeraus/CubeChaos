#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SpiralPlatformGlow.generated.h"

class AActor;
class UDynamicMeshComponent;
class UMaterialInterface;
namespace UE::Geometry { class FDynamicMesh3; }

UENUM(BlueprintType)
enum class ESpiralPlatformGlowDirection : uint8
{
	BottomToTop UMETA(DisplayName = "Bottom To Top"),
	TopToBottom UMETA(DisplayName = "Top To Bottom")
};

/** Independent of the cone wall's music and fixed animations. */
USTRUCT(BlueprintType)
struct FSpiralPlatformGlowSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral Platform Glow")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral Platform Glow")
	ESpiralPlatformGlowDirection Direction = ESpiralPlatformGlowDirection::BottomToTop;

	/** Leave empty for the built-in white glow. Custom material colors/textures are retained. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral Platform Glow")
	TObjectPtr<UMaterialInterface> Material;

	/** Brightness for the built-in glow or a custom material's recognized emissive intensity parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral Platform Glow", meta = (ClampMin = "0", ClampMax = "1000", UIMax = "50"))
	float GlowIntensity = 8.f;

	/** Optional custom scalar name. Empty detects Glow Intensity, GlowIntensity, EmissiveIntensity or Intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = "Spiral Platform Glow")
	FName IntensityParameterName = NAME_None;

	/** Time for each triangle slice; smaller values move faster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral Platform Glow", meta = (ClampMin = "0.05", ClampMax = "10", Units = "s"))
	float SliceDuration = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral Platform Glow")
	bool bLoop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spiral Platform Glow", meta = (ClampMin = "0", Units = "s", EditCondition = "bLoop"))
	float LoopDelay = 0.35f;
};

/** One collision-free surface per cone. Only the current slice is generated on each timer step. */
UCLASS(Transient)
class USpiralPlatformGlow : public UObject
{
	GENERATED_BODY()
	friend class FSpiralPlatformGlowTest;

public:
	void Start(AActor* InOwner, const FSpiralPlatformGlowSettings& InSettings,
		UMaterialInterface* DefaultMaterial, int32 InSliceCount, bool bSectionsAscend,
		TFunction<void(UE::Geometry::FDynamicMesh3&, int32)> InBuildSlice);
	void RefreshCurrentSlice();
	void Release();
	static int32 SliceAtTime(double Elapsed, int32 Count, const FSpiralPlatformGlowSettings& Settings, bool bSectionsAscend);

private:
	void Update();
	void RenderSlice(int32 Slice, bool bForce = false);
	UPROPERTY(Transient) TObjectPtr<UDynamicMeshComponent> Surface;
	UPROPERTY(Transient) FSpiralPlatformGlowSettings Settings;
	TWeakObjectPtr<AActor> Owner;
	TFunction<void(UE::Geometry::FDynamicMesh3&, int32)> BuildSlice;
	FTimerHandle Timer;
	double StartTime = 0;
	int32 SliceCount = 0;
	int32 CurrentSlice = INDEX_NONE;
	bool bAscendingSections = true;
};
