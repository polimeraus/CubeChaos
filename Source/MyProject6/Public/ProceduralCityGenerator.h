#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralCityLayout.h"
#include "ProceduralCityGenerator.generated.h"

class AParametricLobbyPlatform;
class AParametricCityLink;
class AParametricGround;

/** Rule-driven city assembler. Owns only its generated actors, never source assets or
 * unrelated level actors. A valid new plan is staged before replacing the old city. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Procedural City Generator"))
class MYPROJECT6_API AProceduralCityGenerator : public AActor
{
    GENERATED_BODY()
public:
    AProceduralCityGenerator();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Destroyed() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") int32 Seed = 20261028;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation", meta=(ShowOnlyInnerProperties)) FProceduralCityRules Rules;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") bool bGenerateOnBeginPlay = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") bool bNewSeedOnBeginPlay = true;
    /** Off by default: large cities should be rebuilt using the explicit buttons. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") bool bAutoRebuildInEditor = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Generation") bool bMovePlayersToEntryOnRegenerate = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Catalog", meta=(TitleProperty="Id")) TArray<FCityBuildingCatalogEntry> BuildingCatalog;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Catalog") TSubclassOf<AParametricLobbyPlatform> PlatformClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Catalog") TSubclassOf<AParametricCityLink> LinkClass;
    /** Optional keep-out discs in generator-local coordinates; buildings/roads never enter them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Exclusions") TArray<FCityExclusionZone> ExclusionZones;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Appearance") TArray<FLinearColor> DistrictColors;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Appearance") bool bCreateFoundation = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Appearance") FLinearColor FoundationColor = FLinearColor(.025f,.035f,.065f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Appearance", meta=(ClampMin="0",ClampMax="10")) float GuideEmission = 1.5f;
    /** Explicit import only; saved Blueprint settings work without an external file. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="City|Rules File") FString RulesFile = TEXT("Config/ProceduralCityRules.ini");
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") FProceduralCityPlan GeneratedPlan;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") FString GenerationReport;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") FString LastError;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") int32 GenerationSerial = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="City|Result") int32 GeneratedActorCount = 0;

    UFUNCTION(BlueprintCallable, Category="City|Actions") bool RebuildSameSeed();
    UFUNCTION(BlueprintCallable, Category="City|Actions") bool GenerateNewSeed();
    UFUNCTION(BlueprintCallable, Category="City|Actions") bool GenerateWithSeed(int32 InSeed);
    UFUNCTION(BlueprintCallable, CallInEditor, Category="City|Actions") void ClearGenerated();
    UFUNCTION(BlueprintCallable, Category="City|Actions") bool ValidateCurrentSettings();
    UFUNCTION(BlueprintCallable, Category="City|Rules File") bool LoadRulesFromFile();
    /** Editor-only calibration of footprint, pivot and destructible cell cost from actual meshes. */
    UFUNCTION(BlueprintCallable, Category="City|Catalog") bool CalibrateCatalogFootprints();
    // Details-panel actions must have no parameters or return value. Blueprint APIs above retain success results.
    UFUNCTION(CallInEditor, Category="City|Actions", meta=(DisplayName="Rebuild Same Seed")) void BuildPreview();
    UFUNCTION(CallInEditor, Category="City|Actions", meta=(DisplayName="Generate New Seed")) void RandomizePreview();
    UFUNCTION(CallInEditor, Category="City|Actions", meta=(DisplayName="Validate Current Settings")) void CheckPreview();
    UFUNCTION(CallInEditor, Category="City|Rules File", meta=(DisplayName="Load Rules From File")) void ImportRules();
    UFUNCTION(CallInEditor, Category="City|Catalog", meta=(DisplayName="Calibrate Catalog Footprints")) void MeasureCatalog();
    UFUNCTION(BlueprintCallable, Category="City|Actions") void ApplyLayoutPreset(ECityLayout Layout);
    UFUNCTION(BlueprintPure, Category="City|Result") FTransform GetEntryTransform() const;
    UFUNCTION(BlueprintPure, Category="City|Result") TArray<AActor*> GetGeneratedActors() const;

private:
    /** Serialized ownership allows saved previews to reload and regenerate without duplicates. */
    UPROPERTY() TArray<TObjectPtr<AActor>> GeneratedActors;
    bool bGenerating = false;
    bool CheckTransform();
    bool CheckCatalogClasses();
    void DestroyOwned(TArray<TObjectPtr<AActor>>& Actors);
    void MovePlayersToEntry();
};
