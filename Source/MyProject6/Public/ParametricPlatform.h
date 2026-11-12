#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleDecorativeFloor.h"
#include "PlatformPadPathPattern.h"
#include "PlatformBlockShape.h"
#include "PlatformReferenceShape.h"
#include "PlatformParametricShape.h"
#include "Curve/GeneralPolygon2.h"
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
    InsetBand UMETA(DisplayName="Inset Surface Line"),
    Rim UMETA(DisplayName="Edge Rim")
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
    DecorativeFloor,
    Lobby
};

/** One platform authoring surface. Shape, palette, patterns, edge treatment and animation are
 * shared; Solid uses one perimeter slab, Cellular retains the existing weapon/destruction path. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Parametric Platform"))
class MYPROJECT6_API AParametricPlatform : public AChunkedDestructibleDecorativeFloor
{
    GENERATED_BODY()
    // The city assembler configures inherited settings before deferred construction builds geometry.
    friend class AParametricPlatformCity;
    friend class FParametricPlatformTest;
    friend class FParametricPlatformSurfaceTest;
    friend class FParametricPlatformPadPathTest;
    friend class FParametricPlatformBlockShapeTest;
    friend class FParametricPlatformReferenceShapeTest;
    friend class FParametricPlatformOrganicShapeTest;
    friend class FParametricPlatformFinishTest;
public:
    AParametricPlatform();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& Event, AController* EventInstigator, AActor* Causer) override;
    UFUNCTION(BlueprintPure, Category="Platform")
    bool UsesReferenceShape() const;
    virtual void GenerateFloor(bool bPreserveCellState = false) override;
    virtual void ApplyMaterialPattern() override;
    virtual void PostRegisterAllComponents() override;
    virtual void SetCellDimensions(FVector Dimensions, bool bPreserveCellState = true) override;
    virtual void ConfigureFloor(FVector2D Size, FVector Dimensions, FVector2D Spacing, bool bPreserveCellState = true) override;
    virtual void ConfigureDecorativeFloor(EChunkedDecorativeFloorShape Shape, FVector2D Size,
        FVector Dimensions, FVector2D Spacing, bool bPreserveCellState = true) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Mode")
    EPlatformMode PlatformMode = EPlatformMode::Solid;

    /** 0 restores the original bright equalizer exactly; 1 hides its light. Intermediate
     * values reveal the underlying material with softly backlit squares. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation|Music", meta=(ClampMin="0", ClampMax="1", UIMin="0", UIMax="1", EditCondition="bReactToMusic && MusicAnimationType == EMusicAnimationType::Equalizer", EditConditionHides))
    float EqualizerTransparency = 0.8f;

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

    /** Bevel the upper and lower silhouette edges, including holes. Fracture edges stay sharp. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Edge", meta=(DisplayName="Vertical Edge Bevel"))
    bool bBevelEdges = false;

    /** Limited automatically by thickness and narrow details to avoid intersecting bevels. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Edge", meta=(ClampMin="0", Units="cm", EditCondition="bBevelEdges", EditConditionHides))
    float BevelSize = 10.f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Platform|Edge", meta=(EditCondition="bBevelEdges", EditConditionHides, Units="cm"))
    float EffectiveBevelSize = 0.f;

    /** -1 follows the surface palette; otherwise use this existing palette slot for sides and bottom. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Platform|Materials", meta=(ClampMin="-1"))
    int32 BodyMaterialIndex = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(EditCondition="OuterShape == EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides))
    EPlatformBlobProfile BlobProfile = EPlatformBlobProfile::Organic;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(DisplayName="Horizontal Corner Rounding", EditCondition="!UsesReferenceShape() && OuterShape != EChunkedDecorativeFloorShape::ParametricShape && OuterShape != EChunkedDecorativeFloorShape::Circle && OuterShape != EChunkedDecorativeFloorShape::Ellipse && OuterShape != EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides))
    // Keep enabled by default so existing Blueprints/placed radius overrides retain their shape.
    // A zero radius still produces sharp corners, exactly as before this switch was introduced.
    bool bRoundCorners = true;

    /** Zero keeps sharp corners. Positive values fillet convex and concave polygon corners.
     * Radius is locally limited by adjacent edges; Circle/Blob already have their own curve controls. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(ClampMin="0", Units="cm", EditCondition="bRoundCorners && !UsesReferenceShape() && OuterShape != EChunkedDecorativeFloorShape::ParametricShape && OuterShape != EChunkedDecorativeFloorShape::Circle && OuterShape != EChunkedDecorativeFloorShape::Ellipse && OuterShape != EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides))
    float CornerRadius = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(ClampMin="1", ClampMax="32", EditCondition="bRoundCorners && CornerRadius > 0 && !UsesReferenceShape() && OuterShape != EChunkedDecorativeFloorShape::ParametricShape && OuterShape != EChunkedDecorativeFloorShape::Circle && OuterShape != EChunkedDecorativeFloorShape::Ellipse && OuterShape != EChunkedDecorativeFloorShape::RoundedBlob", EditConditionHides))
    int32 CornerSegments = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(ClampMin="3", ClampMax="32", EditCondition="OuterShape == EChunkedDecorativeFloorShape::RegularPolygon", EditConditionHides))
    int32 PolygonSides = 8;

    /** Free polygon boundary in local cm. Dimensions derive from these vertices; no catalog shape is used. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(EditCondition="OuterShape == EChunkedDecorativeFloorShape::DynamicPolygon", EditConditionHides))
    TArray<FVector2D> PolygonOutline = {{-500,-400}, {500,-400}, {500,400}, {-500,400}};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(EditCondition="OuterShape == EChunkedDecorativeFloorShape::LShape || OuterShape == EChunkedDecorativeFloorShape::TShape || OuterShape == EChunkedDecorativeFloorShape::Zigzag || OuterShape == EChunkedDecorativeFloorShape::UShape || OuterShape == EChunkedDecorativeFloorShape::Cross || OuterShape == EChunkedDecorativeFloorShape::CustomBlocks", EditConditionHides))
    FPlatformBlockShape BlockShape;

    /** Reference pieces include their characteristic rounded outline even at Corner Radius = 0. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(EditCondition="UsesReferenceShape()", EditConditionHides))
    FPlatformReferenceShape ReferenceShape;

    /** Circular hub with curved, tapered arms and circular end platforms, authored in cm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Platform|Shape", meta=(DisplayName="Parametric Shape", EditCondition="OuterShape == EChunkedDecorativeFloorShape::ParametricShape", EditConditionHides))
    FPlatformParametricShape ParametricShapeSettings;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Platform|Shape")
    FString ShapeWarning;

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

    UFUNCTION(BlueprintPure, Category="Platform|Pads")
    TArray<FName> GetPadIds() const;
    UFUNCTION(BlueprintPure, Category="Platform|Pads")
    bool GetPadWorldCenter(FName PadId, FVector& WorldCenter) const;
    UFUNCTION(BlueprintPure, Category="Platform|Pads")
    bool GetPadSpawnTransform(FName PadId, FTransform& WorldTransform) const;
    UFUNCTION(BlueprintPure, Category="Platform|Pads")
    bool IsPadSurfaceIntact(FName PadId) const;
    UFUNCTION(BlueprintCallable, Category="Platform|Pads")
    AActor* SpawnActorAtPad(FName PadId, TSubclassOf<AActor> ActorClass);
    UFUNCTION(BlueprintCallable, Category="Platform|Pads")
    TArray<AActor*> SpawnConfiguredActors();
    UFUNCTION(BlueprintPure, Category="Platform|Pads")
    AActor* GetSpawnedActorAtPad(FName PadId) const;
    UFUNCTION(BlueprintCallable, Category="Platform|Pads")
    bool ClearSpawnedActorAtPad(FName PadId);
    UFUNCTION(BlueprintCallable, Category="Platform|Pads")
    void ClearSpawnedActors();

    UFUNCTION(BlueprintCallable, Category="Platform")
    void SetPlatformMode(EPlatformMode NewMode);

    UFUNCTION(BlueprintCallable, Category="Platform")
    void ConfigurePlatform(EChunkedDecorativeFloorShape Shape, FVector2D Size, float NewThickness, EPlatformMode Mode);

    UFUNCTION(BlueprintCallable, Category="Platform")
    bool ConfigurePolygon(const TArray<FVector2D>& Outline, float NewThickness, float NewCornerRadius, int32 NewCornerSegments);

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

    UFUNCTION(BlueprintPure, Category="Platform")
    int32 GetPlatformHoleCount() const { return CachedShapeHoles.Num(); }

    UFUNCTION(BlueprintPure, Category="Platform")
    void GetPlatformHole(int32 Index, TArray<FVector2D>& Outline) const
    { Outline = CachedShapeHoles.IsValidIndex(Index) ? CachedShapeHoles[Index] : TArray<FVector2D>(); }

#if WITH_EDITOR
    virtual bool CanEditChange(const FProperty* Property) const override;
#endif
protected:
    virtual bool CalculateLayout() override;
    virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& Mesh) const override;
    virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& Collision) const override;
    virtual void ConfigureChunkComponent(UDynamicMeshComponent* Component) const override;
    virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override;
    virtual bool CanDestroyCell(FIntPoint Cell) const override { return PlatformMode == EPlatformMode::Cellular && CanBeDamaged() && HasAuthority(); }
    virtual void FinalizeDestructionBatch(double CurrentTime) override;
    virtual int32 SelectCellsInRadius(FVector Location, float Radius, EChunkedFloorBlastShape Shape,
        TFunctionRef<void(int32, float)> CellFunction) override;
    virtual void RebuildBlobCache() override;
    virtual void RebuildShapeOutline() override;
    virtual bool IsPointInsideOuterShape(const FVector2D& LocalPoint, float Inset = 0.f) const override;
    virtual float SampleBlobRadius(float Angle) const override;
    virtual void AppendPolygonSurface(UE::Geometry::FDynamicMesh3& Mesh, int32 ChunkIndex,
        const TArray<FVector2D>& Outline, int32 MaterialIndex, double SurfaceZ) const override;
    virtual void ClipSpiralSurface(const TArray<TArray<FVector2D>>& CellPolygons,
        TArray<FChunkedCachedSpiralTriangle>& OutTriangles) const override;
private:
    struct FBevelPatch
    {
        FVector3d P[3];
        FBox2D Bounds = FBox2D(ForceInit);
    };
    TArray<FBevelPatch> BevelPatches;
    UE::Geometry::FGeneralPolygon2d BevelTopPolygon;
    void PrepareBevel();
    void ApplyEdgeFinish(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& Mesh) const;
    void BevelSimpleCollision(FKAggregateGeom& Collision) const;
    UE::Geometry::FGeneralPolygon2d GetPadSurfacePolygon() const;
    bool ImportLobbySettings(const class AParametricLobbyPlatformDestructable* Lobby);
    struct FResolvedPlatformPad { FVector2D Center; double Radius; bool Show; int32 Index; };
    TMap<FName, FResolvedPlatformPad> ResolvedPads;
    UPROPERTY(Transient) TMap<FName, TObjectPtr<AActor>> SpawnedPadActors;
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
    bool UsesBlockShape() const;
    bool UsesRoundedCorners() const;
    bool BuildReferenceShape();
    void PrepareParametricShape();
    TArray<FVector2D> PreparedParametricOutline;
    TArray<TArray<FVector2D>> PreparedParametricHoles;
    FString PreparedParametricWarning;
    UE::Geometry::FGeneralPolygon2d GetPlatformPolygon() const;
};
