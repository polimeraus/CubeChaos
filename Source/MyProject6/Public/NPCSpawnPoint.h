#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NPCReplacementPresentation.h"
#include "NPCSpawnPoint.generated.h"

class ACharacter;
class ANPCSpawnPoint;
class UArrowComponent;
class USphereComponent;
class UTextRenderComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class ENPCSpawnMode : uint8
{
	Scatter UMETA(DisplayName = "Scatter Into Free Space"),
	Launch UMETA(DisplayName = "Launch Upward In Sequence")
};

UENUM(BlueprintType)
enum class ENPCLaunchVelocityMode : uint8
{
	SeparateSpeeds UMETA(DisplayName = "Vertical and Horizontal Speeds"),
	SpeedAndAngle UMETA(DisplayName = "Speed and Angle")
};

UENUM(BlueprintType)
enum class ENPCLaunchDirection : uint8
{
	Outward UMETA(DisplayName = "Outward From Spawn Point"),
	SpawnPointForward UMETA(DisplayName = "Spawn Point Forward")
};

/** Lightweight holographic marker; idle animation runs in the material without an actor Tick. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FNPCSpawnVisualSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	FLinearColor Color = FLinearColor(0.02f, 0.75f, 1.0f, 1.0f);

	/** Visual size only; NPC placement still uses Spawn Settings / Spawn Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "10.0", ClampMax = "1000.0", Units = "cm"))
	float Radius = 130.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "0.0", ClampMax = "600.0", Units = "cm"))
	float RiseHeight = 170.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float Brightness = 3.0f;

	/** Zero freezes the idle rings; successful spawns can still pulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float AnimationSpeed = 1.0f;

	/** Align the marker with the surface below the point, using the spawn ground trace settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bSnapToGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "0.5", ClampMax = "100.0", Units = "cm"))
	float GroundOffset = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal")
	bool bPulseOnSpawn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float PulseStrength = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Portal", meta = (ClampMin = "0.05", ClampMax = "3.0", Units = "s"))
	float PulseDuration = 0.65f;
};

/** Copied when a request is accepted; later edits affect only subsequent requests. Distances are in cm. */
USTRUCT(BlueprintType)
struct MYPROJECT6_API FNPCSpawnSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	ENPCSpawnMode Mode = ENPCSpawnMode::Scatter;

	/** Search a horizontal disk around the actor. Zero uses the exact point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0", Units = "cm"))
	float SpawnRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0", Units = "cm"))
	float InnerRadius = 0.0f;

	/** Extra space between character capsules, including other spawn points and the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0.0", Units = "cm"))
	float CapsuleGap = 20.0f;

	/** Find a walkable surface beneath each candidate. The point's pivot represents ground level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground")
	bool bSnapToGround = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "0.0", Units = "cm"))
	float GroundTraceUp = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "1.0", Units = "cm"))
	float GroundTraceDown = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground")
	TEnumAsByte<ECollisionChannel> GroundTraceChannel = ECC_Visibility;

	/** Additional height above the floor (or the point's pivot when ground snapping is off). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ground", meta = (ClampMin = "1.0", Units = "cm"))
	float GroundClearance = 4.0f;

	/** Zero emits a scatter batch together, subject to the spawn point's per-frame budget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Timing", meta = (ClampMin = "0.0", Units = "s"))
	float SpawnInterval = 0.0f;

	/** Launch mode always waits at least this long between characters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (ClampMin = "0.02", Units = "s", EditCondition = "Mode == ENPCSpawnMode::Launch"))
	float LaunchInterval = 0.25f;

	/** Separate speeds preserves existing points. Speed and Angle uses one initial speed and an elevation angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (EditCondition = "Mode == ENPCSpawnMode::Launch"))
	ENPCLaunchVelocityMode LaunchVelocityMode = ENPCLaunchVelocityMode::SeparateSpeeds;

	/** Increase this for a higher arc when using Vertical and Horizontal Speeds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (ClampMin = "0.0", Units = "cm/s", EditCondition = "Mode == ENPCSpawnMode::Launch && LaunchVelocityMode == ENPCLaunchVelocityMode::SeparateSpeeds", EditConditionHides))
	float LaunchUpSpeed = 800.0f;

	/** Horizontal speed along Launch Direction; defaults to outward as in existing points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (ClampMin = "0.0", Units = "cm/s", EditCondition = "Mode == ENPCSpawnMode::Launch && LaunchVelocityMode == ENPCLaunchVelocityMode::SeparateSpeeds", EditConditionHides))
	float LaunchOutwardSpeed = 250.0f;

	/** Initial launch strength expressed as speed, not force. Higher speed increases height and range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (ClampMin = "0.0", ClampMax = "100000.0", Units = "cm/s", EditCondition = "Mode == ENPCSpawnMode::Launch && LaunchVelocityMode == ENPCLaunchVelocityMode::SpeedAndAngle", EditConditionHides))
	float LaunchSpeed = 1200.0f;

	/** Degrees above the horizontal plane: 0 = horizontal, 45 = diagonal, 90 = straight up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg", EditCondition = "Mode == ENPCSpawnMode::Launch && LaunchVelocityMode == ENPCLaunchVelocityMode::SpeedAndAngle", EditConditionHides))
	float LaunchElevationAngle = 60.0f;

	/** Forward uses the point's current Yaw, independent of Random Yaw and actor Pitch/Roll. Outward uses each spawn position; at the exact center it picks a random heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (EditCondition = "Mode == ENPCSpawnMode::Launch"))
	ENPCLaunchDirection LaunchDirection = ENPCLaunchDirection::Outward;

	/** Horizontal rotation from the chosen direction. +90 turns right, -90 turns left. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (ClampMin = "-180.0", ClampMax = "180.0", Units = "deg", EditCondition = "Mode == ENPCSpawnMode::Launch"))
	float LaunchYawOffset = 0.0f;

	/** Random horizontal variation per NPC, from -Spread to +Spread degrees. Zero gives a fixed heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launch", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg", EditCondition = "Mode == ENPCSpawnMode::Launch"))
	float LaunchYawSpread = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	bool bRandomYaw = true;

	/** Candidate positions tested per attempt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blocked Space", meta = (ClampMin = "1", ClampMax = "256"))
	int32 PlacementAttempts = 48;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blocked Space", meta = (ClampMin = "0.02", Units = "s"))
	float RetryInterval = 0.2f;

	/** After this many consecutive failed attempts, finish the request with an incomplete count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blocked Space", meta = (ClampMin = "1", ClampMax = "1000"))
	int32 MaxBlockedAttempts = 25;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	bool bEnsureAIController = true;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FNPCSpawnedSignature, ANPCSpawnPoint*, SpawnPoint, int32, RequestId, ACharacter*, NPC);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FiveParams(FNPCSpawnRequestFinishedSignature, ANPCSpawnPoint*, SpawnPoint, int32, RequestId, int32, RequestedCount, int32, SpawnedCount, bool, bCancelled);

// Reflected storage keeps queued Blueprint classes referenced until they are used.
USTRUCT()
struct FNPCSpawnQueuedRequest
{
	GENERATED_BODY()
	UPROPERTY() TSubclassOf<ACharacter> NPCClass;
	UPROPERTY() FNPCSpawnSettings Settings;
	UPROPERTY() FNPCReplacementPresentation ReplacementPresentation;
	bool bReplacement = false;
	int32 Id = 0;
	int32 Requested = 0;
	int32 Spawned = 0;
	int32 BlockedAttempts = 0;
};

/** Place/duplicate freely in a level, assign unique IDs, then command from Blueprint or npc.Spawn. */
UCLASS(Blueprintable, meta = (DisplayName = "NPC Spawn Point"))
class MYPROJECT6_API ANPCSpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	ANPCSpawnPoint();
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Set a unique ID on every copy, e.g. Spawn_01, Spawn_02. None disables lookup by name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point")
	FName SpawnPointId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point")
	TSubclassOf<ACharacter> DefaultNPCClass;

	/** Optional short names for console/gameplay commands. Use Blueprint classes to retain their appearance/settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point")
	TMap<FName, TSubclassOf<ACharacter>> NPCTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 DefaultCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point")
	FNPCSpawnSettings SpawnSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point|Visual")
	FNPCSpawnVisualSettings VisualSettings;

	/** Uses the generated M_NPCSpawnPortal material and its named parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point|Visual", AdvancedDisplay)
	TObjectPtr<UMaterialInterface> PortalMaterial;

	/** Apply visual parameter changes made at runtime. Details edits refresh automatically. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "NPC Spawn Point|Visual")
	void RefreshPortalVisuals();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point")
	bool bSpawnOnBeginPlay = false;

	/** Limits work in a frame without changing a request's total count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point|Budget", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MaxSpawnsPerFrame = 10;

	/** Reject requests that would exceed the total outstanding count. Existing work is preserved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NPC Spawn Point|Budget", meta = (ClampMin = "1", ClampMax = "10000"))
	int32 MaxPendingNPCs = 1000;

	/** Enqueue a batch. Returns a positive request ID, or -1 if rejected. Nothing spawns before the next tick. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NPC Spawn Point")
	int32 SpawnNPCs(TSubclassOf<ACharacter> NPCClass, int32 Count);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NPC Spawn Point")
	int32 SpawnNPCsWithSettings(TSubclassOf<ACharacter> NPCClass, int32 Count, const FNPCSpawnSettings& Settings);

	/** Extra priority work, using this point's placement/launch settings without changing normal requests. */
	int32 SpawnReplacementNPCs(const AChasingNPC* Source, int32 Count);
	int32 GetPendingReplacementNPCCount() const;
	static ANPCSpawnPoint* FindNearestSpawnPoint(const UObject* WorldContextObject, const FVector& Location);

	/** NPCType None uses DefaultNPCClass; other names must exist in NPCTypes. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NPC Spawn Point")
	int32 SpawnNPCType(FName NPCType, int32 Count);

	/** Details-panel test button works during PIE/Simulate only. */
	UFUNCTION(BlueprintCallable, CallInEditor, BlueprintAuthorityOnly, Category = "NPC Spawn Point")
	void SpawnDefaultNPCs();

	/** Cancel queued work. Already spawned characters remain in the world. */
	UFUNCTION(BlueprintCallable, CallInEditor, BlueprintAuthorityOnly, Category = "NPC Spawn Point")
	void CancelPendingSpawns();

	UFUNCTION(BlueprintPure, Category = "NPC Spawn Point")
	int32 GetPendingNPCCount() const;

	UFUNCTION(BlueprintPure, Category = "NPC Spawn Point")
	TArray<ACharacter*> GetSpawnedNPCs() const;

	/** Missing or duplicate IDs return null, never an arbitrary spawn point. Lookup is scoped to this world. */
	UFUNCTION(BlueprintCallable, Category = "NPC Spawn Point", meta = (WorldContext = "WorldContextObject"))
	static ANPCSpawnPoint* FindSpawnPoint(const UObject* WorldContextObject, FName PointId);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NPC Spawn Point", meta = (WorldContext = "WorldContextObject"))
	static int32 SpawnNPCsAtPoint(const UObject* WorldContextObject, FName PointId, TSubclassOf<ACharacter> NPCClass, int32 Count);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "NPC Spawn Point", meta = (WorldContext = "WorldContextObject"))
	static int32 SpawnNPCTypeAtPoint(const UObject* WorldContextObject, FName PointId, FName NPCType, int32 Count);

	UPROPERTY(BlueprintAssignable, Category = "NPC Spawn Point|Events")
	FNPCSpawnedSignature OnNPCSpawned;

	/** Fired once per accepted request: complete, blocked (SpawnedCount < RequestedCount), or cancelled. */
	UPROPERTY(BlueprintAssignable, Category = "NPC Spawn Point|Events")
	FNPCSpawnRequestFinishedSignature OnSpawnRequestFinished;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Spawn Point|Visual")
	TObjectPtr<USceneComponent> PortalRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Spawn Point|Visual")
	TArray<TObjectPtr<UStaticMeshComponent>> PortalLayers;

#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Spawn Point|Preview")
	TObjectPtr<UArrowComponent> Direction;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Spawn Point|Preview")
	TObjectPtr<USphereComponent> RadiusPreview;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "NPC Spawn Point|Preview")
	TObjectPtr<UTextRenderComponent> NamePreview;
#endif

private:
	int32 EnqueueSpawn(TSubclassOf<ACharacter> NPCClass, int32 Count, const FNPCSpawnSettings& Settings, const AChasingNPC* ReplacementSource = nullptr);
	bool FindSpawnTransform(const FNPCSpawnQueuedRequest& Request, FTransform& OutTransform) const;
	FVector CalculateLaunchVelocity(const FNPCSpawnSettings& Settings, const FVector& SpawnLocation) const;
	void ProcessQueue();
	void ScheduleProcessing();
	void UpdatePortalAnchor();
	void PulsePortal();
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PortalInstances;
	float LastPortalPulseTime = -100000.0f;
	UPROPERTY(Transient) TArray<FNPCSpawnQueuedRequest> Queue;
	TArray<TWeakObjectPtr<ACharacter>> SpawnedNPCs;
	FTimerHandle SpawnTimer;
	int32 NextRequestId = 1;
	double NextNormalSpawnTime = 0.0;
	double NextReplacementSpawnTime = 0.0;
	bool bProcessingQueue = false;
	bool bEndingPlay = false;
};
