#pragma once

#include "CoreMinimal.h"
#include "LaunchPad.h"
#include "ZigguratTraversal.generated.h"

class UChildActorComponent;

/** Shared settings for the aligned floor shaft and its ground-floor launch pad. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FZigguratTraversalSettings
{
    GENERATED_BODY()

    /** Open the center of every upper slab, roof cornice and crown; the foundation stays solid. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Floor Opening")
    bool bFloorOpenings = true;
    /** Square clear width in local centimetres. Fitted to the smallest occupied room. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Floor Opening", meta=(ClampMin="100", ClampMax="2000", Units="cm", EditCondition="bFloorOpenings"))
    float OpeningWidth = 200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Launch Pad")
    bool bGroundLaunchPad = true;
    /** Automatically fitted inside the shaft. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Launch Pad", meta=(ClampMin="50", ClampMax="1000", Units="cm", EditCondition="bGroundLaunchPad"))
    float PadWidth = 160;
    /** Use each character's gravity and the generated building height to clear the roof. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Launch Pad", meta=(EditCondition="bGroundLaunchPad"))
    bool bAutoLaunchHeight = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Launch Pad", meta=(ClampMin="50", ClampMax="3000", Units="cm", EditCondition="bGroundLaunchPad && bAutoLaunchHeight"))
    float HeightAboveRoof = 300;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Launch Pad", meta=(ClampMin="100", ClampMax="10000", Units="cm/s", EditCondition="bGroundLaunchPad && !bAutoLaunchHeight"))
    float LaunchUpSpeed = 2400;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Launch Pad", meta=(ClampMin="0.1", ClampMax="10", Units="s", EditCondition="bGroundLaunchPad"))
    float Cooldown = .6f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ground Launch Pad", meta=(EditCondition="bGroundLaunchPad"))
    bool bAffectNPCs = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animated Material", meta=(EditCondition="bGroundLaunchPad"))
    ELaunchPadPattern Pattern = ELaunchPadPattern::Arrows;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animated Material", meta=(EditCondition="bGroundLaunchPad", HideAlphaChannel))
    FLinearColor Color = FLinearColor(.02f,.75f,1.f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animated Material", meta=(ClampMin="0", ClampMax="30", EditCondition="bGroundLaunchPad"))
    float Brightness = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animated Material", meta=(ClampMin="0", ClampMax="10", EditCondition="bGroundLaunchPad"))
    float AnimationSpeed = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animated Material", meta=(ClampMin="1", ClampMax="8", EditCondition="bGroundLaunchPad"))
    int32 RingCount = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animated Material", meta=(ClampMin="0.005", ClampMax="0.1", EditCondition="bGroundLaunchPad"))
    float RingThickness = .02f;
    /** Custom materials can use Tint, Brightness, AnimationSpeed and SpawnTime parameters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animated Material", meta=(EditCondition="bGroundLaunchPad"))
    TObjectPtr<UMaterialInterface> ArrowsMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animated Material", meta=(EditCondition="bGroundLaunchPad"))
    TObjectPtr<UMaterialInterface> RingsMaterial;
};

namespace ZigguratTraversal
{
    void InitializeMaterials(FZigguratTraversalSettings& Settings);
    double FitOpening(const FZigguratTraversalSettings& Settings, double RoomWidth);
    void UpdatePad(UChildActorComponent* Component, const FZigguratTraversalSettings& Settings,
        double OpeningWidth, double FoundationHeight, double BuildingHeight);
    /** Emit four disjoint strips around a square shaft. Splits at slope/shaft crossings,
     * so even a crown narrower than the opening leaves no covering triangles or collision. */
    void SolidSections(FVector2D BottomSize, FVector2D TopSize, double Z0, double Z1, double OpeningWidth,
        TFunctionRef<void(FVector2D,FVector2D,FVector2D,FVector2D,double,double)> Emit);
}
