#pragma once

#include "CoreMinimal.h"
#include "ChunkedDestructibleFloor.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "DestructibleRadialSculpture.generated.h"

class UDynamicMeshComponent;
class UMaterialInterface;
namespace UE::Geometry { class FDynamicMesh3; }
struct FKAggregateGeom;

UENUM(BlueprintType)
enum class ERadialSculptureGlowAnimation : uint8
{
    None,
    SequentialFins UMETA(DisplayName="Sequential Leg Glow"),
    BottomToTop UMETA(DisplayName="Bottom To Top Glow")
};

/** A smooth white sphere supported by curved radial fins on a square plinth.
 * Uses the same small-cell damage, flash, sound and explosion ripple as other structures.
 * Curved geometry is preserved while both horizontal and vertical cuts bound fragment size.
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Destructible Radial Sculpture"))
class MYPROJECT6_API ADestructibleRadialSculpture : public AChunkedDestructibleFloor
{
    GENERATED_BODY()
    friend class FRadialSculptureTest;
    friend class FRadialSculpturePresentationTest;
public:
    ADestructibleRadialSculpture();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;
    virtual bool UsesPlanarAnimation() const override { return false; }
    virtual bool WorldLocationToCell(FVector WorldLocation, FIntPoint& OutCell) const override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="4", ClampMax="32"))
    int32 FinCount = 16;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="50", ClampMax="3000", Units="cm"))
    float OuterRadius = 450.f;
    /** Leg height above the base. Increasing it extends the legs and raises the sphere without scaling its radius. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="50", ClampMax="3000", Units="cm"))
    float FinHeight = 480.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="1", ClampMax="30", Units="cm"))
    float FinThickness = 5.f;
    /** Radius of the top sphere; diameter is twice this value. Legs remain attached as it changes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="10", ClampMax="1000", Units="cm"))
    float SphereRadius = 115.f;
    /** Local-space shell thickness. Clamped to 1 cm..90% of Sphere Radius; rebuild after runtime edits. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="1", Units="cm"))
    float SphereWallThickness = 15.f;
    /** Named center/attachment point for runtime contents; follows the sculpture transform. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Radial Sculpture|Interior")
    TObjectPtr<USceneComponent> SphereInterior;
    /** Radius of the fin tips where they meet the lower hemisphere. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="5", ClampMax="900", Units="cm"))
    float NeckRadius = 55.f;
    /** One is straight; larger values make the fin silhouette bow inward. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="1", ClampMax="4"))
    float CurveExponent = 2.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape")
    bool bIncludeBase = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="0", ClampMax="500", Units="cm", EditCondition="bIncludeBase"))
    float BaseMargin = 65.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Shape", meta=(ClampMin="2", ClampMax="100", Units="cm", EditCondition="bIncludeBase"))
    float BaseThickness = 8.f;
    /** Persistent surface material for legs, sphere and base. Change this instead of generated chunk material slots. Glow is added separately. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetSculptureMaterial, Category="Radial Sculpture|Appearance")
    TObjectPtr<UMaterialInterface> SculptureMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter=SetGlowAnimationType, Category="Radial Sculpture|Glow", meta=(DisplayName="Glow Animation"))
    ERadialSculptureGlowAnimation GlowAnimation = ERadialSculptureGlowAnimation::SequentialFins;
    /** Seconds for one complete trip around all legs, or from the base past the top of the sphere. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Glow", meta=(ClampMin="0.1", Units="s", EditCondition="GlowAnimation != ERadialSculptureGlowAnimation::None"))
    float GlowDuration = 4.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Glow", meta=(ClampMin="0", Units="s", EditCondition="GlowAnimation != ERadialSculptureGlowAnimation::None"))
    float GlowLoopDelay = .25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Glow", meta=(HideAlphaChannel, EditCondition="GlowAnimation != ERadialSculptureGlowAnimation::None"))
    FLinearColor SculptureGlowColor = FLinearColor(.025f,.55f,1.f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Glow", meta=(ClampMin="0", EditCondition="GlowAnimation != ERadialSculptureGlowAnimation::None"))
    float SculptureGlowIntensity = 6.f;
    /** Width of the fading light in number of legs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Glow", meta=(ClampMin="1", ClampMax="8", EditCondition="GlowAnimation == ERadialSculptureGlowAnimation::SequentialFins", EditConditionHides))
    float GlowLegWidth = 2.f;
    /** Full vertical width of the traveling band in local centimetres. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Glow", meta=(ClampMin="1", Units="cm", EditCondition="GlowAnimation == ERadialSculptureGlowAnimation::BottomToTop", EditConditionHides))
    float GlowBandHeight = 100.f;
    /** -1 animates normally. Nonnegative seconds freeze the effect at that time for editor previews. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Radial Sculpture|Glow", meta=(ClampMin="-1", Units="s"))
    float GlowPreviewTime = -1.f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, AdvancedDisplay, Category="Radial Sculpture|Glow")
    TSoftObjectPtr<UMaterialInterface> SculptureGlowMaterial = TSoftObjectPtr<UMaterialInterface>(
        FSoftObjectPath(TEXT("/Game/Structures/Destructible/RadialSculpture/Materials/M_RadialSculptureGlow.M_RadialSculptureGlow")));

    UFUNCTION(BlueprintSetter, Category="Radial Sculpture|Appearance")
    void SetSculptureMaterial(UMaterialInterface* Material);
    UFUNCTION(BlueprintSetter, Category="Radial Sculpture|Glow")
    void SetGlowAnimationType(ERadialSculptureGlowAnimation Animation);
    /** Applies runtime glow parameter edits without repairing damage. Editor property edits apply automatically. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Radial Sculpture|Glow")
    void RefreshSculptureGlow();

    /** Approximate local-space cell edge length. Smaller values generate more fragments;
     * actor scale also scales fragments in the world. Rebuild Sculpture applies runtime edits. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Destruction", meta=(ClampMin="20", ClampMax="200", Units="cm"))
    float PieceSize = 40.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Radial Sculpture|Destruction", meta=(ClampMin="1"))
    float PieceHealth = 25.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Radial Sculpture|Statistics")
    FString GenerationWarning;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Radial Sculpture|Statistics")
    int32 GeneratedPieceCount = 0;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Radial Sculpture|Statistics")
    int32 RemainingPieceCount = 0;

    /** Regenerates geometry and restores all health, including after runtime damage. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="Radial Sculpture")
    void RebuildSculpture();
    /** Uniform damage to all intact pieces within a world-space radius. */
    UFUNCTION(BlueprintCallable, Category="Radial Sculpture|Destruction")
    int32 ApplyExplosion(FVector WorldOrigin, float Radius = 250.f, float Damage = 100.f);
    UFUNCTION(BlueprintCallable, Category="Radial Sculpture|Destruction")
    void ShatterSculpture(FVector WorldImpulseDirection);
    UFUNCTION(BlueprintPure, Category="Radial Sculpture")
    FVector GetSphereLocalCenter() const;
    UFUNCTION(BlueprintPure, Category="Radial Sculpture|Interior")
    FVector GetSphereWorldCenter() const;
    /** Nominal inner radius in local centimetres. */
    UFUNCTION(BlueprintPure, Category="Radial Sculpture|Interior")
    float GetSphereInnerRadius() const;
    /** Conservative world-space clearance, accounting for tessellation and nonuniform actor scale. */
    UFUNCTION(BlueprintPure, Category="Radial Sculpture|Interior")
    float GetSphereInteriorWorldRadius() const;
    /** Prepare a normal-sized actor spawn in the cavity. ActorRadius is a world-space bounding sphere
     * around its pivot; LocalOffset/LocalRotation are relative to SphereInterior. Returns false if
     * the actor would extend outside the cavity or overlap blocking geometry. Does not spawn anything. */
    UFUNCTION(BlueprintCallable, Category="Radial Sculpture|Interior")
    bool TryGetSphereSpawnTransform(FTransform& OutTransform, float ActorRadius = 30.f,
        FVector LocalOffset = FVector::ZeroVector, FRotator LocalRotation = FRotator::ZeroRotator,
        TEnumAsByte<ECollisionChannel> CollisionChannel = ECC_Pawn) const;

protected:
    virtual bool SupportsFixedAnimation() const override { return false; }
    virtual bool CalculateLayout() override;
    virtual bool ShouldCellExistInGeneratedShape(FIntPoint Cell) const override;
    virtual void BuildChunkMesh(int32 ChunkIndex, UE::Geometry::FDynamicMesh3& Mesh) const override;
    virtual void BuildChunkSimpleCollision(int32 ChunkIndex, FKAggregateGeom& Collision) const override;
    virtual FVector GetCellLocalCenter(FIntPoint Cell) const override;
    virtual FTransform GetCellFlashTransform(FIntPoint Cell) const override;
    virtual bool ResolveExistingCellFromHit(const FHitResult& Hit, FIntPoint& Cell) const override;
    virtual int32 EvaluatePatternMaterial(FIntPoint Cell) const override { return 0; }
    virtual int32 SelectCellsInRadius(FVector Origin, float Radius, EChunkedFloorBlastShape Shape,
        TFunctionRef<void(int32, float)> Function) override;
    virtual void MarkCellChunksDirty(FIntPoint Cell) override;
    virtual void FinalizeDestructionBatch(double CurrentTime) override;

private:
    enum class EPart : uint8 { Fin, Sphere, Base };
    struct FPiece
    {
        UE::Geometry::FDynamicMesh3 Mesh;
        FKAggregateGeom Collision;
        FBox Bounds = FBox(ForceInit);
        EPart Part = EPart::Fin;
        int32 FinIndex = INDEX_NONE;
        // Convex strip planes for fins/base; analytic radial/cubemap bounds for sphere.
        TArray<TArray<FPlane>> HullPlanes;
        FVector SphereCenter, FaceNormal, AxisU, AxisV;
        double InnerRadius=0, OuterRadius=0, U0=0, U1=0, V0=0, V1=0;
        bool Contains(FVector Position, double Tolerance) const;
    };
    TArray<FPiece> Pieces;
    bool bBudgetExceeded = false;
    float InteriorClearanceRadius = 0.f;
    float GeneratedGlowHeight = 1.f;
    int32 GeneratedFinCount = 16;
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> SculptureGlowInstance;
    void UpdateSculptureMaterials();
    void AddPiece(FPiece&& Piece);
    bool FindPiece(FVector LocalPosition, FIntPoint& Cell, double Tolerance, int32 ChunkIndex = INDEX_NONE) const;
};
