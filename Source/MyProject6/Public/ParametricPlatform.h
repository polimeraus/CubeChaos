#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleDecorativeFloor.h"
#include "PlatformPadPathPattern.h"
#include "ParametricPlatform.generated.h"

UENUM(BlueprintType)
enum class EPlatformMode : uint8
{
    Solid UMETA(DisplayName="Unbreakable (Single Slab)"),
    Cellular UMETA(DisplayName="Destructible Cells")
};

UENUM(BlueprintType)
enum class EPlatformBorder : uint8
{
    None,
    CellBorder UMETA(DisplayName="Colored Edge Tiles"),
    InsetBand UMETA(DisplayName="Inset Surface Line")
};

UENUM(BlueprintType)
enum class EPlatformBlobProfile : uint8
{
    Decorative,
    Organic UMETA(DisplayName="Original Rounded Blob")
};

UENUM(BlueprintType)
enum class EPlatformPreset : uint8
{
    Ground,
    Triangle,
    RoundedBlob,
    DecorativeFloor
};

/** One platform authoring surface. Shape, palette, patterns, edge treatment and animation are
 * shared; Solid uses one perimeter slab, Cellular retains the existing weapon/destruction path. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Parametric Platform"))
class MYPROJECT6_API AParametricPlatform : public AChunkedDestructibleDecorativeFloor
{
    GENERATED_BODY()
    friend class FParametricPlatformTest;
    friend class FParametricPlatformSurfaceTest;
    friend class FParametricPlatformPadPathTest;
public:
    AParametricPlatform();
    virtual void GenerateFloor(bool bPreserveCellState = false) override;
    virtual void ApplyMaterialPattern() override;
    virtual void PostRegisterAllComponents() override;
    virtual void SetCellDimensions(FVector Dimensions, bool bPreserveCellState = true) override;
    virtual void ConfigureFloor(FVector2D Size, FVector Dimensions, FVector2D Spacing, bool bPreserveCellState = true) override;
    virtual void ConfigureDecorativeFloor(EChunkedDecorativeFloorShape Shape, FVector2D Size,
        FVector Dimensions, FVector2D Spacing, bool bPreserveCellState = true) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Mode")
    EPlatformMode PlatformMode = EPlatformMode::Solid;

    /** Surface stays at local Z=0 in both modes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Dimensions", meta=(ClampMin="1", Units="cm"))
    float Thickness = 50.f;

    /** Destruction cell size; also the visual tile size for grid patterns in Solid mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Dimensions", meta=(ClampMin="1", Units="cm"))
    FVector2D TileSize = FVector2D(100, 100);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Edge")
    EPlatformBorder BorderStyle = EPlatformBorder::InsetBand;

    /** Distance from the physical edge to the outer edge of the painted line. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Edge", meta=(ClampMin="0", Units="cm", EditCondition="BorderStyle == EPlatformBorder::InsetBand", EditConditionHides))
    float BoundaryInset = 150.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(EditCondition="OuterShape == EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides))
    EPlatformBlobProfile BlobProfile = EPlatformBlobProfile::Organic;

    /** Optional per-slot tints. Materials may expose Color, BaseColor, or Tint. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Materials")
    bool bTintPalette = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Materials", meta=(EditCondition="bTintPalette", EditConditionHides))
    TArray<FLinearColor> PaletteTints;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Materials", meta=(DisplayName="Circles & Paths", EditCondition="FloorPattern == EChunkedFloorPattern::CirclesAndPaths", EditConditionHides, DisplayAfter="FloorPattern", FullyExpand="true"))
    FPlatformPadPathPattern PadPathSettings;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Platform|Materials", meta=(EditCondition="FloorPattern == EChunkedFloorPattern::CirclesAndPaths", EditConditionHides))
    FString PadPathWarning;

    /** Solid geometry never allocates a destruction grid. Explicit tiled patterns have this
     * surface-detail budget; very large surfaces increase visual tile pitch to stay within it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Performance", meta=(ClampMin="16", ClampMax="65536", EditCondition="PlatformMode == EPlatformMode::Solid", EditConditionHides))
    int32 MaxSolidPatternTiles = 4096;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Platform|Performance")
    FVector2D EffectiveSolidTileSize = FVector2D::ZeroVector;

    /** Starting settings only. Apply explicitly; editing other parameters never resets them. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Presets")
    EPlatformPreset Preset = EPlatformPreset::Ground;

    UFUNCTION(CallInEditor, BlueprintCallable, Category="Platform|Presets")
    void ApplyPreset();

    /** Copies only platform settings, never transform, label, folder, or level actor references. */
    UFUNCTION(BlueprintCallable, Category="Platform|Presets")
    bool CopyPlatformSettingsFrom(AActor* Source);

    UFUNCTION(BlueprintCallable, CallInEditor, Category="Platform")
    void RebuildPlatform();

    UFUNCTION(BlueprintCallable, Category="Platform")
    void SetPlatformMode(EPlatformMode NewMode);

    UFUNCTION(BlueprintCallable, Category="Platform")
    void ConfigurePlatform(EChunkedDecorativeFloorShape Shape, FVector2D Size, float NewThickness, EPlatformMode Mode);

    UFUNCTION(BlueprintPure, Category="Platform")
    EChunkedDecorativeFloorShape GetPlatformShape() const { return OuterShape; }
    UFUNCTION(BlueprintPure, Category="Platform")
    FVector2D GetPlatformSize() const { return ShapeSize; }
    UFUNCTION(BlueprintPure, Category="Platform")
    int32 GetDestructibleCellCount() const { return PlatformMode == EPlatformMode::Cellular ? Cells.Num() : 0; }
    UFUNCTION(BlueprintPure, Category="Platform")
    int32 GetPlatformMeshCount() const { return ChunkComponents.Num(); }
    UFUNCTION(BlueprintPure, Category="Platform")
    void GetPlatformOutline(TArray<FVector2D>& Outline) const { Outline = CachedShapeOutline; }

#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* Property) const override;
#endif
protected:
    virtual bool CalculateLayout() override;
    virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& Mesh) const override;
    virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& Collision) const override;
    virtual void ConfigureChunkComponent(UDynamicMeshComponent* Component) const override;
    virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
    virtual bool CanDestroyCell(FIntPoint Cell) const override { return PlatformMode == EPlatformMode::Cellular; }
    virtual int32 SelectCellsInRadius(FVector Location, float Radius, EChunkedFloorBlastShape Shape,
        TFunctionRef<void(int32, float)> CellFunction) override;
    virtual void RebuildBlobCache() override;
    virtual float SampleBlobRadius(float Angle) const override;
    virtual void AppendPolygonSurface(UE::Geometry::FDynamicMesh3& Mesh, int32 ChunkIndex,
        const TArray<FVector2D>& Outline, int32 MaterialIndex, double SurfaceZ) const override;
    virtual void ClipSpiralSurface(const TArray<TArray<FVector2D>>& CellPolygons,
        TArray<FChunkedCachedSpiralTriangle>& OutTriangles) const override;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> TintedMaterials;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PadPathMaterialInstance;
    bool bGeneratedPlatform = false;
    EPlatformMode GeneratedMode = EPlatformMode::Solid;
    bool bBuildingPlatform = false;
    void UpdateTintMaterials();
    void UpdatePadPathMaterial();
    void ApplyPadPathSurface(UE::Geometry::FDynamicMesh3& Mesh) const;
    int32 PadPathMaterialIndex() const { return FMath::Max(FloorMaterials.Num(), 1) + 1; }
    void BuildSolidMesh(UE::Geometry::FDynamicMesh3& Mesh) const;
    void AppendSolidTiles(UE::Geometry::FDynamicMesh3& Mesh) const;
    void AppendInsetBorder(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& Mesh) const;
    void AppendSolidSurfaceTriangle(UE::Geometry::FDynamicMesh3& Mesh,
        const FVector2D& A, const FVector2D& B, const FVector2D& C, int32 MaterialIndex, double SurfaceZ) const;
    bool UsesRegularGroundSlab() const;
};
