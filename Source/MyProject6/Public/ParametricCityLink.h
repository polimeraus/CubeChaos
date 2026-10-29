#pragma once
#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "ProceduralCityLayout.h"
#include "ParametricCityLink.generated.h"

/** Reusable walkable road, bridge, ramp or staircase. Paint is coplanar material
 * partitioning of the deck, not an overlapping decal or a collision obstacle. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Parametric City Link"))
class MYPROJECT6_API AParametricCityLink : public ADynamicMeshActor
{
    GENERATED_BODY()
public:
    AParametricCityLink(const FObjectInitializer& Initializer);
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
    UFUNCTION(BlueprintCallable, CallInEditor, Category="City Link") void RebuildLink();
    /** Local-space centreline; endpoints are top-of-surface heights. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Route") TArray<FVector> Points;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Route") ECityLinkKind Kind = ECityLinkKind::Bridge;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Route", meta=(ClampMin="120",Units="cm")) float Width = 420;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Route", meta=(ClampMin="5",Units="cm")) float Thickness = 40;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Stairs", meta=(ClampMin="8",ClampMax="30",Units="cm")) float MaxStepRise = 18;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Stairs", meta=(ClampMin="25",Units="cm")) float MinStepTread = 35;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Rails") bool bGuardRails = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Rails", meta=(ClampMin="60",Units="cm")) float RailHeight = 105;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Appearance") FLinearColor RoadColor = FLinearColor(.035f,.045f,.08f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Appearance") FLinearColor GuideColor = FLinearColor(0,.65f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Appearance") FLinearColor DashColor = FLinearColor(.95f,.8f,.3f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Appearance", meta=(ClampMin="2",Units="cm")) float GuideWidth = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Appearance", meta=(ClampMin="20",Units="cm")) float DashLength = 140;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Appearance", meta=(ClampMin="10",Units="cm")) float DashGap = 120;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Materials") TObjectPtr<UMaterialInterface> SurfaceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City Link|Collision") bool bEnableCollision = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="City Link|Statistics") float GeneratedLength = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="City Link|Statistics") int32 GeneratedSteps = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="City Link|Statistics") FString GenerationWarning;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
    bool bBuildingMesh = false;
};
