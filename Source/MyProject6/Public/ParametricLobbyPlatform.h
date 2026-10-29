#pragma once

#include "CoreMinimal.h"
#include "DynamicMeshActor.h"
#include "ParametricLobbyPlatform.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
namespace UE::Geometry { class FDynamicMesh3; }

UENUM(BlueprintType)
enum class ELobbyPlatformShape : uint8
{
	RoundedRectangle, Rectangle, Ellipse, Triangle, Hexagon, RegularPolygon
};

UENUM(BlueprintType)
enum class ELobbyPadPaint : uint8
{
	Disc, Ring
};

/** A named paint region and spawn socket. Positions scale with the platform dimensions. */
USTRUCT(BlueprintType)
struct FLobbyPlatformPad
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad")
	FName Id = NAME_None;
	/** Off: paths ending here extend to the platform boundary; this entry is a direction anchor, not a spawn pad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad")
	bool bShowCircle = true;

	/** -1..1 across the platform half extents; (0,0) is its centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad", meta=(ClampMin="-1", ClampMax="1"))
	FVector2D Position = FVector2D::ZeroVector;

	/** Fraction of the shorter platform half extent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad", meta=(ClampMin="0.001", ClampMax="0.95"))
	float Radius = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad")
	ELobbyPadPaint Paint = ELobbyPadPaint::Disc;

	/** Ring thickness as a fraction of its outer radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad", meta=(EditCondition="Paint == ELobbyPadPaint::Ring", ClampMin="0.01", ClampMax="1"))
	float RingWidth = 0.14f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad")
	bool bEnabled = true;

	/** Optional class for Spawn Configured Actors / automatic BeginPlay spawning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad|Spawn")
	TSubclassOf<AActor> ActorClass;

	/** Local offset from the painted centre, useful for actors with a centred pivot or a capsule. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad|Spawn", meta=(Units="cm"))
	FVector SpawnOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad|Spawn")
	FRotator SpawnRotation = FRotator::ZeroRotator;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pad|Spawn")
	FVector SpawnScale = FVector::OneVector;
};

/** Rounded polyline linking two named pads. Ends follow the effective, fitted pad centres. */
USTRUCT(BlueprintType)
struct FLobbyPlatformPath
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path")
	FName FromPad;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path")
	FName ToPad;
	/** Intermediate points in the same -1..1 coordinate system as pads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path")
	TArray<FVector2D> Waypoints;
	/** Fraction of the shorter full platform dimension. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path", meta=(ClampMin="0.001", ClampMax="0.5"))
	float Width = 0.055f;
	/** Gap from the inner surface boundary for an end without a circle. The end follows the boundary contour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path", meta=(ClampMin="0", Units="cm"))
	float EdgeInset = 65.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Path")
	bool bEnabled = true;
};

/** Glossy lobby slab with coplanar, analytically painted spawn pads and connecting paths. */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Parametric Lobby Platform"))
class MYPROJECT6_API AParametricLobbyPlatform : public ADynamicMeshActor
{
	GENERATED_BODY()
public:
	AParametricLobbyPlatform(const FObjectInitializer& ObjectInitializer);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape")
	ELobbyPlatformShape Shape = ELobbyPlatformShape::RoundedRectangle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape", meta=(ClampMin="100", Units="cm"))
	float Width = 3600;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape", meta=(ClampMin="100", Units="cm"))
	float Depth = 2800;
	/** Top surface is local Z=0; the slab extends downwards. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape", meta=(ClampMin="2", Units="cm"))
	float Thickness = 80;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape", meta=(ClampMin="0", Units="cm"))
	float CornerRadius = 300;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape", meta=(ClampMin="3", ClampMax="32", EditCondition="Shape == ELobbyPlatformShape::RegularPolygon"))
	int32 PolygonSides = 8;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape", meta=(ClampMin="4", ClampMax="32"))
	int32 CornerSegments = 16;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape", meta=(ClampMin="0", Units="cm"))
	float RimWidth = 28;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape", meta=(ClampMin="0", Units="cm"))
	float BevelSize = 10;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Shape")
	bool bEnableCollision = true;

	/** First 16 entries are supported. IDs must be nonempty and unique, including disabled entries. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Yellow Layout", meta=(TitleProperty="Id"))
	TArray<FLobbyPlatformPad> Pads;
	/** At most 32 straight segments across all enabled paths. Rounded joins are automatic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Yellow Layout", meta=(TitleProperty="FromPad"))
	TArray<FLobbyPlatformPath> Paths;
	/** Keep complete circles on the inner surface when changing shape or dimensions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Yellow Layout")
	bool bFitPadsInsidePlatform = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Yellow Layout")
	bool bShowYellowLayout = true;
	/** Reports invalid/duplicate IDs, out-of-bounds pads and capacity limits. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Lobby|Yellow Layout")
	FString LayoutWarning;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(HideAlphaChannel))
	FLinearColor SurfaceColor = FLinearColor(0.105f, 0.003f, 0.46f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(HideAlphaChannel))
	FLinearColor BodyColor = FLinearColor(0.025f, 0.002f, 0.085f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(HideAlphaChannel))
	FLinearColor RimColor = FLinearColor(0.25f, 0.008f, 0.48f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(HideAlphaChannel))
	FLinearColor YellowColor = FLinearColor(1.0f, 0.87f, 0.006f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(HideAlphaChannel))
	FLinearColor CyanColor = FLinearColor(0.0f, 0.75f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(ClampMin="0.02", ClampMax="1"))
	float SurfaceRoughness = 0.19f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(ClampMin="0", ClampMax="1"))
	float SurfaceMetallic = 0.32f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(ClampMin="0", ClampMax="20"))
	float SurfaceEmission = 0.18f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(ClampMin="0", ClampMax="20"))
	float YellowEmission = 0.55f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(ClampMin="0", ClampMax="100"))
	float NeonIntensity = 8.0f;
	/** The ONE luminous layer's centre, measured downwards as a fraction of slab thickness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(ClampMin="0.15", ClampMax="0.85"))
	float NeonLayerPosition = 0.72f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Appearance", meta=(ClampMin="0.01", ClampMax="0.5"))
	float NeonLayerThickness = 0.18f;
	/** Material contract: Color/Roughness/Metallic/Emission and analytic pad/path parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Materials")
	TObjectPtr<UMaterialInterface> SurfaceMaterial;
	/** Tintable lit material for the body, rim, single neon layer and cut walls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Materials")
	TObjectPtr<UMaterialInterface> TrimMaterial;

	/** Gates point/radial damage AND the explicit destruction functions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Destruction")
	bool bDestructible = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Destruction", meta=(ClampMin="1", Units="cm"))
	float PointDamageRadius = 110.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Destruction", meta=(ClampMin="0.01"))
	float MinimumDamage = 1.0f;
	/** Number of successful local cuts; runtime damage is not saved into class defaults. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Lobby|Destruction")
	int32 DamageCutCount = 0;
	/** Remove a world-space sphere, including its collision. Rebuild preserves successful cuts. */
	UFUNCTION(BlueprintCallable, Category="Lobby|Destruction")
	bool RemovePlatformSphere(FVector WorldLocation, float Radius);
	UFUNCTION(BlueprintCallable, Category="Lobby|Destruction")
	bool DestroyPlatformCompletely();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Lobby|Destruction")
	void RepairPlatform();
	UFUNCTION(BlueprintPure, Category="Lobby|Pads")
	bool IsPadSurfaceIntact(FName PadId) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Spawning")
	bool bSpawnOnBeginPlay = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Spawning")
	bool bAttachSpawnedActors = true;
	/** Default rejects blocked spawns instead of silently moving them away from the pad centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lobby|Spawning")
	ESpawnActorCollisionHandlingMethod SpawnCollisionHandling = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;

	/** Rebuild after changing properties at runtime. Does not create, move or delete spawned actors. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Lobby")
	void RebuildPlatform();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Lobby")
	void ResetReferenceLayout();
	/** Effective centre on the visible surface, including fitting, rotation and actor scale. */
	UFUNCTION(BlueprintPure, Category="Lobby|Pads")
	bool GetPadWorldCenter(FName PadId, FVector& WorldCenter) const;
	/** Includes per-pad spawn offset, rotation and scale, transformed by this platform. */
	UFUNCTION(BlueprintPure, Category="Lobby|Pads")
	bool GetPadSpawnTransform(FName PadId, FTransform& WorldTransform) const;
	UFUNCTION(BlueprintPure, Category="Lobby|Pads")
	TArray<FName> GetPadIds() const;
	/** Runtime only. Null ActorClass uses the configured pad class. An occupied pad returns its actor. */
	UFUNCTION(BlueprintCallable, Category="Lobby|Spawning")
	AActor* SpawnActorAtPad(FName PadId, TSubclassOf<AActor> ActorClass);
	UFUNCTION(BlueprintCallable, Category="Lobby|Spawning")
	TArray<AActor*> SpawnConfiguredActors();
	UFUNCTION(BlueprintPure, Category="Lobby|Spawning")
	AActor* GetSpawnedActorAtPad(FName PadId) const;
	UFUNCTION(BlueprintCallable, Category="Lobby|Spawning")
	bool ClearSpawnedActorAtPad(FName PadId);
	UFUNCTION(BlueprintCallable, Category="Lobby|Spawning")
	void ClearSpawnedActors();

private:
	struct FResolvedPad { int32 Index; FVector2D Center; double Radius; };
	TArray<FVector2D> MakeOutline(TArray<FVector2D>* Core = nullptr, double* Fillet = nullptr) const;
	void ResolvePads(TArray<FResolvedPad>& Result, FString* Warning = nullptr) const;
	const FLobbyPlatformPad* FindResolvedPad(FName Id, FResolvedPad& Result) const;
	void UpdateMaterials();
	bool CutMesh(UE::Geometry::FDynamicMesh3& Mesh, const FTransform& LocalSphere) const;
	void UpdateCutSurfaceAttributes(UE::Geometry::FDynamicMesh3& Mesh) const;
	void PublishMesh(UE::Geometry::FDynamicMesh3&& Mesh);
	TArray<FTransform> DamageCuts;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MaterialInstances;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<AActor>> SpawnedActors;
};
