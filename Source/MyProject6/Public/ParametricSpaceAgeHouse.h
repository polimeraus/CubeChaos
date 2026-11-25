#pragma once

#include "CoreMinimal.h"
#include "ChunkedPanelStructure.h"
#include "ParametricSpaceAgeHouse.generated.h"

class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class ESpaceAgeRoof : uint8
{
    Orbit UMETA(DisplayName="Orbit / Saucer"),
    Comet UMETA(DisplayName="Comet / Butterfly")
};

USTRUCT(BlueprintType)
struct MYPROJECT6_API FSpaceAgePalette
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enamel", meta=(HideAlphaChannel))
    FLinearColor Shell = FLinearColor(.92f,.77f,.53f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enamel", meta=(HideAlphaChannel))
    FLinearColor Roof = FLinearColor(.028f,.39f,.35f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enamel", meta=(HideAlphaChannel))
    FLinearColor Accent = FLinearColor(.84f,.16f,.045f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enamel", meta=(HideAlphaChannel))
    FLinearColor Glass = FLinearColor(.018f,.105f,.14f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enamel", meta=(HideAlphaChannel))
    FLinearColor Metal = FLinearColor(.49f,.55f,.52f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enamel", meta=(HideAlphaChannel))
    FLinearColor Lamp = FLinearColor(1.f,.54f,.16f,1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enamel", meta=(HideAlphaChannel))
    FLinearColor Terrazzo = FLinearColor(.54f,.45f,.34f,1);
};

/** Mid-century space-age residence. The pivot is at ground level and the open entrance faces local -Y. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Parametric Space Age House", PrioritizeCategories="House"))
class MYPROJECT6_API AParametricSpaceAgeHouse : public AChunkedPanelStructure
{
    GENERATED_BODY()
public:
    AParametricSpaceAgeHouse();
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Architecture")
    ESpaceAgeRoof RoofForm = ESpaceAgeRoof::Orbit;
    /** Full width and depth of the inhabited shell. Eaves and entrance project beyond it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Architecture", meta=(Units="cm"))
    FVector2D Footprint = FVector2D(1360,920);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Architecture", meta=(ClampMin="34",ClampMax="280",Units="cm"))
    float DeckHeight = 132;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Architecture", meta=(ClampMin="240",ClampMax="520",Units="cm"))
    float WallHeight = 285;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Architecture", meta=(ClampMin="55",ClampMax="370",Units="cm"))
    float RoofRise = 154;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Architecture", meta=(ClampMin="35",ClampMax="300",Units="cm"))
    float EaveDepth = 118;
    /** Adds a glass-banded lantern to the saucer or a raised monitor to the folded roof. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Architecture")
    bool bClerestory = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Facade", meta=(ClampMin="8",ClampMax="24"))
    int32 FacadeBays = 14;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Facade", meta=(ClampMin="40",ClampMax="145",Units="cm"))
    float SillHeight = 78;
    /** Zero disables the round openings. Other bays receive panoramic glazing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Facade", meta=(ClampMin="0",ClampMax="9"))
    int32 PortholeSpacing = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Facade", meta=(ClampMin="0",ClampMax="65",Units="cm"))
    float SunshadeDepth = 23;
    /** Changes porthole placement and the placement of secondary louvres. The plan remains reproducible. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Facade")
    int32 FacadeSeed = 1964;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Entrance", meta=(ClampMin="120",ClampMax="310",Units="cm"))
    float EntranceWidth = 174;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Entrance", meta=(ClampMin="75",ClampMax="260",Units="cm"))
    float PorchDepth = 136;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Entrance")
    bool bEntranceCanopy = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Entrance")
    bool bStairRails = true;
    /** Rows in the pierced ceramic privacy screen alongside the porch. Zero leaves the side open. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Entrance", meta=(ClampMin="0",ClampMax="5"))
    int32 ScreenCourses = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Details", meta=(ClampMin="4",ClampMax="12"))
    int32 SupportCount = 7;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Details")
    bool bStarAerial = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Details")
    bool bInteriorLounge = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Finish", meta=(ShowOnlyInnerProperties))
    FSpaceAgePalette Palette;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Finish")
    TObjectPtr<UMaterialInterface> SurfaceMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Finish")
    TObjectPtr<UMaterialInterface> GlazingMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Finish", meta=(ClampMin=".15",ClampMax=".9"))
    float GlassOpacity = .49f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Finish", meta=(ClampMin="0",ClampMax="5"))
    float LampGlow = 1.35f;

    /** Target size of floor, roof and glazing fragments. Frames and details keep their structural joints. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Destruction", meta=(ClampMin="55",ClampMax="210",Units="cm"))
    float FragmentSpan = 115;
    /** Maximum number of loose, physics-only fragments for this house. Zero disables flying debris. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Destruction", meta=(ClampMin="0",ClampMax="32"))
    int32 LoosePanelLimit = 14;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Destruction", meta=(ClampMin=".2",ClampMax="4",Units="s"))
    float LoosePanelLifetime = 1.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="House|Destruction")
    bool bRemoveWhenEmpty = true;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="House|Destruction")
    int32 GeneratedFragments = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="House|Destruction")
    FString GenerationWarning;

    /** Rebuild from the current parameters and restore every fragment. Editor edits regenerate automatically. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="House")
    void RebuildHouse();
protected:
    virtual void BeginPlay() override;
    virtual bool CalculateLayout() override;
    virtual void ClearAdditionalRuntimeEffects() override;
    virtual void FinalizeDestructionBatch(double CurrentTime) override;
private:
    friend class FSpaceAgeHouseTest;
    struct FBuilder;
    void UpdateFinishes();
    UFUNCTION()
    void ReleasePanel(FIntPoint Cell,FVector WorldLocation,bool bRadialDestruction);
    void RemoveEmptyHouse();
    bool HasIntactPanels() const;
    FTimerHandle DemolitionTimer;
    TArray<TWeakObjectPtr<AActor>> LoosePanels;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> Finishes;
};
