#include "NPCSpawnPoint.h"

#include "ChasingNPC.h"
#include "RadicalMikeNPC.h"
#include "Components/ArrowComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogNPCSpawnPoint, Log, All);

namespace
{
	float SafeVisualValue(float Value, float Minimum, float Maximum, float Fallback)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Fallback;
	}

	bool AreSettingsFinite(const FNPCSpawnSettings& S)
	{
		return FMath::IsFinite(S.SpawnRadius) && FMath::IsFinite(S.InnerRadius)
			&& FMath::IsFinite(S.CapsuleGap) && FMath::IsFinite(S.GroundTraceUp)
			&& FMath::IsFinite(S.GroundTraceDown) && FMath::IsFinite(S.GroundClearance)
			&& FMath::IsFinite(S.SpawnInterval) && FMath::IsFinite(S.LaunchInterval)
			&& FMath::IsFinite(S.LaunchUpSpeed) && FMath::IsFinite(S.LaunchOutwardSpeed)
			&& FMath::IsFinite(S.LaunchSpeed) && FMath::IsFinite(S.LaunchElevationAngle)
			&& FMath::IsFinite(S.LaunchYawOffset) && FMath::IsFinite(S.LaunchYawSpread)
			&& FMath::IsFinite(S.RetryInterval);
	}

	float GetEmissionInterval(const FNPCSpawnSettings& S)
	{
		return S.Mode == ENPCSpawnMode::Launch ? FMath::Max(S.SpawnInterval, S.LaunchInterval) : S.SpawnInterval;
	}

	FAutoConsoleCommandWithWorldAndArgs SpawnCommand(
		TEXT("npc.Spawn"), TEXT("npc.Spawn <PointId> <Count> [NPCType]. Omit NPCType to use the point's default class."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			int32 Count = 0;
			if (Args.Num() < 2 || Args.Num() > 3 || !LexTryParseString(Count, *Args[1]) || Count <= 0)
			{
				UE_LOG(LogNPCSpawnPoint, Warning, TEXT("Usage: npc.Spawn Spawn_01 2 RadicalMike (NPCType is optional)."));
				return;
			}
			ANPCSpawnPoint::SpawnNPCTypeAtPoint(World, FName(*Args[0]), Args.Num() == 3 ? FName(*Args[2]) : NAME_None, Count);
		}));

	FAutoConsoleCommandWithWorldAndArgs CancelCommand(
		TEXT("npc.CancelSpawns"), TEXT("npc.CancelSpawns <PointId>. Cancels pending NPCs; existing NPCs remain."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() == 1)
			{
				if (ANPCSpawnPoint* Point = ANPCSpawnPoint::FindSpawnPoint(World, FName(*Args[0])))
				{
					Point->CancelPendingSpawns();
				}
			}
			else { UE_LOG(LogNPCSpawnPoint, Warning, TEXT("Usage: npc.CancelSpawns Spawn_01")); }
		}));

	FAutoConsoleCommandWithWorldAndArgs ListCommand(
		TEXT("npc.ListSpawnPoints"), TEXT("List point IDs, locations, pending counts and NPC type aliases in this world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World) { return; }
			for (TActorIterator<ANPCSpawnPoint> It(World); It; ++It)
			{
				UE_LOG(LogNPCSpawnPoint, Display, TEXT("%s [%s] at %s; pending=%d; default=%s"),
					*It->SpawnPointId.ToString(), *It->GetName(), *It->GetActorLocation().ToCompactString(),
					It->GetPendingNPCCount(), *GetNameSafe(It->DefaultNPCClass.Get()));
				for (const auto& Type : It->NPCTypes)
				{
					UE_LOG(LogNPCSpawnPoint, Display, TEXT("  %s = %s"), *Type.Key.ToString(), *GetNameSafe(Type.Value.Get()));
				}
			}
		}));
}

ANPCSpawnPoint::ANPCSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SpawnOrigin"));
	DefaultNPCClass = AChasingNPC::StaticClass();
	NPCTypes.Add(TEXT("Ghoul"), AChasingNPC::StaticClass());
	NPCTypes.Add(TEXT("RadicalMike"), ARadicalMikeNPC::StaticClass());

	PortalRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PortalVisualRoot"));
	PortalRoot->SetupAttachment(RootComponent);
	PortalRoot->SetAbsolute(false, true, true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PortalPlane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PortalShader(TEXT("/Game/NPC/Spawning/M_NPCSpawnPortal.M_NPCSpawnPortal"));
	PortalMaterial = PortalShader.Object;
	const FName LayerNames[] = { TEXT("PortalFloorGlyph"), TEXT("PortalRisingHaloA"), TEXT("PortalRisingHaloB") };
	for (const FName LayerName : LayerNames)
	{
		UStaticMeshComponent* Layer = CreateDefaultSubobject<UStaticMeshComponent>(LayerName);
		Layer->SetupAttachment(PortalRoot);
		Layer->SetStaticMesh(PortalPlane.Object);
		Layer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Layer->SetGenerateOverlapEvents(false);
		Layer->SetCanEverAffectNavigation(false);
		Layer->SetCastShadow(false);
		Layer->bReceivesDecals = false;
		Layer->bAffectDistanceFieldLighting = false;
		Layer->bAffectDynamicIndirectLighting = false;
		Layer->bUseAsOccluder = false;
		PortalLayers.Add(Layer);
	}

#if WITH_EDITORONLY_DATA
	Direction = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Direction"));
	Direction->SetupAttachment(RootComponent);
	Direction->ArrowColor = FColor(60, 220, 160);
	Direction->ArrowSize = 2.0f;
	Direction->SetHiddenInGame(true);
	Direction->SetIsVisualizationComponent(true);

	RadiusPreview = CreateEditorOnlyDefaultSubobject<USphereComponent>(TEXT("SpawnRadiusPreview"));
	RadiusPreview->SetupAttachment(RootComponent);
	RadiusPreview->SetAbsolute(false, false, true);
	RadiusPreview->InitSphereRadius(SpawnSettings.SpawnRadius);
	RadiusPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RadiusPreview->SetGenerateOverlapEvents(false);
	RadiusPreview->SetCanEverAffectNavigation(false);
	RadiusPreview->SetHiddenInGame(true);
	RadiusPreview->SetIsVisualizationComponent(true);
	RadiusPreview->ShapeColor = FColor(60, 220, 160);

	NamePreview = CreateEditorOnlyDefaultSubobject<UTextRenderComponent>(TEXT("SpawnPointName"));
	NamePreview->SetupAttachment(RootComponent);
	NamePreview->SetRelativeLocation(FVector(0, 0, 120));
	NamePreview->SetHorizontalAlignment(EHTA_Center);
	NamePreview->SetWorldSize(40.0f);
	NamePreview->SetTextRenderColor(FColor(60, 220, 160));
	NamePreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NamePreview->SetCanEverAffectNavigation(false);
	NamePreview->SetHiddenInGame(true);
	NamePreview->SetIsVisualizationComponent(true);
#endif
}

void ANPCSpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshPortalVisuals();
#if WITH_EDITORONLY_DATA
	RadiusPreview->SetSphereRadius(FMath::Max(0.0f, SpawnSettings.SpawnRadius));
	NamePreview->SetText(FText::FromString(SpawnPointId.IsNone() ? TEXT("NPC Spawn Point (set ID)") : SpawnPointId.ToString()));
	// In directed launch mode, show the central launch angle (without random spread).
	float PreviewPitch = 0.0f;
	float PreviewYaw = GetActorRotation().Yaw;
	if (SpawnSettings.Mode == ENPCSpawnMode::Launch && SpawnSettings.LaunchDirection == ENPCLaunchDirection::SpawnPointForward)
	{
		PreviewPitch = SpawnSettings.LaunchVelocityMode == ENPCLaunchVelocityMode::SpeedAndAngle
			? FMath::Clamp(SpawnSettings.LaunchElevationAngle, 0.0f, 90.0f)
			: FMath::RadiansToDegrees(FMath::Atan2(FMath::Max(0.0f, SpawnSettings.LaunchUpSpeed), FMath::Max(0.0f, SpawnSettings.LaunchOutwardSpeed)));
		PreviewYaw += SpawnSettings.LaunchYawOffset;
	}
	Direction->SetWorldRotation(FRotator(PreviewPitch, PreviewYaw, 0.0f));
#endif
}

void ANPCSpawnPoint::BeginPlay()
{
	Super::BeginPlay();
	RefreshPortalVisuals();
	if (HasAuthority() && bSpawnOnBeginPlay) { SpawnDefaultNPCs(); }
}

void ANPCSpawnPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	GetWorldTimerManager().ClearTimer(SpawnTimer);
	Queue.Reset();
	SpawnedNPCs.Reset();
	PortalInstances.Reset();
	Super::EndPlay(EndPlayReason);
}

void ANPCSpawnPoint::UpdatePortalAnchor()
{
	if (!PortalRoot || !GetWorld()) { return; }
	FVector Location = GetActorLocation();
	if (VisualSettings.bSnapToGround)
	{
		FCollisionQueryParams Query(SCENE_QUERY_STAT(NPCPortalGround), false, this);
		for (TActorIterator<ACharacter> It(GetWorld()); It; ++It) { Query.AddIgnoredActor(*It); }
		FHitResult Hit;
		if (SpawnSettings.GroundTraceChannel < ECC_MAX && GetWorld()->LineTraceSingleByChannel(Hit,
			Location + FVector(0, 0, SafeVisualValue(SpawnSettings.GroundTraceUp, 0.0f, 100000.0f, 200.0f)),
			Location - FVector(0, 0, SafeVisualValue(SpawnSettings.GroundTraceDown, 1.0f, 100000.0f, 1000.0f)),
			SpawnSettings.GroundTraceChannel, Query) && !Hit.bStartPenetrating && Hit.ImpactNormal.Z > 0.5f)
		{
			Location = Hit.ImpactPoint;
		}
	}
	Location.Z += SafeVisualValue(VisualSettings.GroundOffset, 0.5f, 100.0f, 4.0f);
	PortalRoot->SetWorldLocationAndRotation(Location, FRotator(0, GetActorRotation().Yaw, 0));
}

void ANPCSpawnPoint::RefreshPortalVisuals()
{
	const bool bVisible = VisualSettings.bEnabled && PortalMaterial && !IsRunningDedicatedServer();
	if (!PortalRoot) { return; }
	PortalRoot->SetVisibility(bVisible, true);
	PortalRoot->SetHiddenInGame(!bVisible, true);
	if (!bVisible) { return; }
	UpdatePortalAnchor();
	PortalInstances.SetNum(PortalLayers.Num());
	const float Radius = SafeVisualValue(VisualSettings.Radius, 10.0f, 1000.0f, 130.0f);
	for (int32 Index = 0; Index < PortalLayers.Num(); ++Index)
	{
		UStaticMeshComponent* Layer = PortalLayers[Index];
		Layer->SetRelativeScale3D(FVector(Radius / 50.0f, Radius / 50.0f, 1.0f));
		Layer->SetRelativeLocation(FVector(0, 0, Index == 0 ? 0.0f : 2.0f));
		UMaterialInstanceDynamic* Instance = PortalInstances[Index];
		if (!Instance || Instance->Parent != PortalMaterial)
		{
			Instance = UMaterialInstanceDynamic::Create(PortalMaterial, this);
			PortalInstances[Index] = Instance;
			Layer->SetMaterial(0, Instance);
		}
		Instance->SetVectorParameterValue(TEXT("Tint"), VisualSettings.Color.GetClamped(0.0f, 1.0f));
		Instance->SetScalarParameterValue(TEXT("Layer"), Index == 0 ? 0.0f : 1.0f);
		Instance->SetScalarParameterValue(TEXT("Phase"), Index == 2 ? 0.62f : 0.12f);
		Instance->SetScalarParameterValue(TEXT("Brightness"), SafeVisualValue(VisualSettings.Brightness, 0.0f, 30.0f, 3.0f));
		Instance->SetScalarParameterValue(TEXT("RiseHeight"), SafeVisualValue(VisualSettings.RiseHeight, 0.0f, 600.0f, 170.0f));
		Instance->SetScalarParameterValue(TEXT("AnimationSpeed"), SafeVisualValue(VisualSettings.AnimationSpeed, 0.0f, 10.0f, 1.0f));
		Instance->SetScalarParameterValue(TEXT("PulseDuration"), SafeVisualValue(VisualSettings.PulseDuration, 0.05f, 3.0f, 0.65f));
		Instance->SetScalarParameterValue(TEXT("PulseStrength"), VisualSettings.bPulseOnSpawn ? SafeVisualValue(VisualSettings.PulseStrength, 0.0f, 10.0f, 2.0f) : 0.0f);
		Instance->SetScalarParameterValue(TEXT("SpawnTime"), LastPortalPulseTime);
	}
}

void ANPCSpawnPoint::PulsePortal()
{
	if (!VisualSettings.bEnabled || !VisualSettings.bPulseOnSpawn || IsRunningDedicatedServer()) { return; }
	LastPortalPulseTime = GetWorld()->GetTimeSeconds();
	for (UMaterialInstanceDynamic* Instance : PortalInstances)
	{
		if (Instance) { Instance->SetScalarParameterValue(TEXT("SpawnTime"), LastPortalPulseTime); }
	}
}

int32 ANPCSpawnPoint::SpawnNPCs(TSubclassOf<ACharacter> NPCClass, int32 Count)
{
	return SpawnNPCsWithSettings(NPCClass, Count, SpawnSettings);
}

int32 ANPCSpawnPoint::SpawnNPCsWithSettings(TSubclassOf<ACharacter> NPCClass, int32 Count, const FNPCSpawnSettings& Settings)
{
	return EnqueueSpawn(NPCClass, Count, Settings);
}

int32 ANPCSpawnPoint::SpawnReplacementNPCs(const AChasingNPC* Source, int32 Count)
{
	if (!IsValid(Source)) { return INDEX_NONE; }
	FNPCSpawnSettings Settings = SpawnSettings;
	// Normal batch delays do not postpone combat reinforcements. Launch mode retains
	// its short clearance interval so the two capsules can leave the portal safely.
	Settings.SpawnInterval = 0.0f;
	return EnqueueSpawn(Source->GetClass(), Count, Settings, Source);
}

int32 ANPCSpawnPoint::EnqueueSpawn(TSubclassOf<ACharacter> NPCClass, int32 Count, const FNPCSpawnSettings& Settings, const AChasingNPC* ReplacementSource)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client || !HasAuthority() || bEndingPlay || IsActorBeingDestroyed()) { return INDEX_NONE; }
	if (!NPCClass || NPCClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
		|| Count <= 0 || Count > FMath::Clamp(MaxPendingNPCs, 1, 10000) - GetPendingNPCCount()
		|| !AreSettingsFinite(Settings) || NextRequestId == MAX_int32
		|| (Settings.Mode != ENPCSpawnMode::Scatter && Settings.Mode != ENPCSpawnMode::Launch)
		|| (Settings.LaunchVelocityMode != ENPCLaunchVelocityMode::SeparateSpeeds && Settings.LaunchVelocityMode != ENPCLaunchVelocityMode::SpeedAndAngle)
		|| (Settings.LaunchDirection != ENPCLaunchDirection::Outward && Settings.LaunchDirection != ENPCLaunchDirection::SpawnPointForward)
		|| Settings.GroundTraceChannel >= ECC_MAX)
	{
		UE_LOG(LogNPCSpawnPoint, Warning, TEXT("%s: rejected spawn request (invalid class/count/settings, or pending budget exceeded)."), *GetName());
		return INDEX_NONE;
	}

	FNPCSpawnQueuedRequest Request;
	Request.Id = NextRequestId++;
	Request.NPCClass = NPCClass;
	Request.Requested = Count;
	Request.Settings = Settings;
	Request.bReplacement = ReplacementSource != nullptr;
	if (ReplacementSource) { Request.ReplacementPresentation.Capture(*ReplacementSource); }
	FNPCSpawnSettings& S = Request.Settings;
	S.SpawnRadius = FMath::Clamp(S.SpawnRadius, 0.0f, 100000.0f);
	S.InnerRadius = FMath::Clamp(S.InnerRadius, 0.0f, S.SpawnRadius);
	S.CapsuleGap = FMath::Clamp(S.CapsuleGap, 0.0f, 10000.0f);
	S.GroundTraceUp = FMath::Clamp(S.GroundTraceUp, 0.0f, 100000.0f);
	S.GroundTraceDown = FMath::Clamp(S.GroundTraceDown, 1.0f, 100000.0f);
	S.GroundClearance = FMath::Clamp(S.GroundClearance, 1.0f, 100000.0f);
	S.SpawnInterval = FMath::Max(0.0f, S.SpawnInterval);
	S.LaunchInterval = FMath::Max(0.02f, S.LaunchInterval);
	S.LaunchUpSpeed = FMath::Clamp(S.LaunchUpSpeed, 0.0f, 100000.0f);
	S.LaunchOutwardSpeed = FMath::Clamp(S.LaunchOutwardSpeed, 0.0f, 100000.0f);
	S.LaunchSpeed = FMath::Clamp(S.LaunchSpeed, 0.0f, 100000.0f);
	S.LaunchElevationAngle = FMath::Clamp(S.LaunchElevationAngle, 0.0f, 90.0f);
	S.LaunchYawOffset = FMath::Clamp(S.LaunchYawOffset, -180.0f, 180.0f);
	S.LaunchYawSpread = FMath::Clamp(S.LaunchYawSpread, 0.0f, 180.0f);
	S.RetryInterval = FMath::Max(0.02f, S.RetryInterval);
	S.PlacementAttempts = FMath::Clamp(S.PlacementAttempts, 1, 256);
	S.MaxBlockedAttempts = FMath::Clamp(S.MaxBlockedAttempts, 1, 1000);
	if (!Queue.ContainsByPredicate([&Request](const FNPCSpawnQueuedRequest& Pending) { return Pending.bReplacement == Request.bReplacement; }))
	{
		(Request.bReplacement ? NextReplacementSpawnTime : NextNormalSpawnTime) = World->GetTimeSeconds();
	}
	Queue.Add(Request);
	if (!bProcessingQueue) { ScheduleProcessing(); }
	return Request.Id;
}

int32 ANPCSpawnPoint::SpawnNPCType(FName NPCType, int32 Count)
{
	if (NPCType.IsNone()) { return SpawnNPCs(DefaultNPCClass, Count); }
	if (const TSubclassOf<ACharacter>* NPCClass = NPCTypes.Find(NPCType)) { return SpawnNPCs(*NPCClass, Count); }
	UE_LOG(LogNPCSpawnPoint, Warning, TEXT("%s: unknown NPC type '%s'. Add it to NPCTypes in Details."), *GetName(), *NPCType.ToString());
	return INDEX_NONE;
}

void ANPCSpawnPoint::SpawnDefaultNPCs()
{
	SpawnNPCs(DefaultNPCClass, DefaultCount);
}

int32 ANPCSpawnPoint::GetPendingNPCCount() const
{
	int32 Count = 0;
	for (const FNPCSpawnQueuedRequest& Request : Queue) { Count += Request.Requested - Request.Spawned; }
	return Count;
}

int32 ANPCSpawnPoint::GetPendingReplacementNPCCount() const
{
	int32 Count = 0;
	for (const FNPCSpawnQueuedRequest& Request : Queue)
	{
		if (Request.bReplacement)
		{
			Count += Request.Requested - Request.Spawned;
		}
	}
	return Count;
}

TArray<ACharacter*> ANPCSpawnPoint::GetSpawnedNPCs() const
{
	TArray<ACharacter*> Result;
	for (const TWeakObjectPtr<ACharacter>& NPC : SpawnedNPCs)
	{
		if (NPC.IsValid() && !NPC->IsActorBeingDestroyed()) { Result.Add(NPC.Get()); }
	}
	return Result;
}

void ANPCSpawnPoint::CancelPendingSpawns()
{
	if (!HasAuthority() || GetNetMode() == NM_Client || bEndingPlay) { return; }
	GetWorldTimerManager().ClearTimer(SpawnTimer);
	// Detach before callbacks: listeners may enqueue new requests or cancel again.
	const TArray<FNPCSpawnQueuedRequest> Cancelled = MoveTemp(Queue);
	Queue.Reset();
	NextNormalSpawnTime = NextReplacementSpawnTime = 0.0;
	for (const FNPCSpawnQueuedRequest& Request : Cancelled)
	{
		OnSpawnRequestFinished.Broadcast(this, Request.Id, Request.Requested, Request.Spawned, true);
	}
}

ANPCSpawnPoint* ANPCSpawnPoint::FindNearestSpawnPoint(const UObject* WorldContextObject, const FVector& Location)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World) { return nullptr; }
	ANPCSpawnPoint* Nearest = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<ANPCSpawnPoint> It(World); It; ++It)
	{
		if (!IsValid(*It) || It->IsActorBeingDestroyed() || It->bEndingPlay) { continue; }
		const double Distance = FVector::DistSquared(Location, It->GetActorLocation());
		if (Distance < BestDistance) { Nearest = *It; BestDistance = Distance; }
	}
	return Nearest;
}

ANPCSpawnPoint* ANPCSpawnPoint::FindSpawnPoint(const UObject* WorldContextObject, FName PointId)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || PointId.IsNone()) { return nullptr; }
	ANPCSpawnPoint* Match = nullptr;
	for (TActorIterator<ANPCSpawnPoint> It(World); It; ++It)
	{
		if (It->SpawnPointId != PointId || It->IsActorBeingDestroyed()) { continue; }
		if (Match)
		{
			UE_LOG(LogNPCSpawnPoint, Warning, TEXT("Duplicate spawn point ID '%s': '%s' and '%s'. Assign a unique ID to each point."),
				*PointId.ToString(), *Match->GetName(), *It->GetName());
			return nullptr;
		}
		Match = *It;
	}
	if (!Match) { UE_LOG(LogNPCSpawnPoint, Warning, TEXT("Spawn point '%s' was not found in this world."), *PointId.ToString()); }
	return Match;
}

int32 ANPCSpawnPoint::SpawnNPCsAtPoint(const UObject* WorldContextObject, FName PointId, TSubclassOf<ACharacter> NPCClass, int32 Count)
{
	ANPCSpawnPoint* Point = FindSpawnPoint(WorldContextObject, PointId);
	return Point ? Point->SpawnNPCs(NPCClass, Count) : INDEX_NONE;
}

int32 ANPCSpawnPoint::SpawnNPCTypeAtPoint(const UObject* WorldContextObject, FName PointId, FName NPCType, int32 Count)
{
	ANPCSpawnPoint* Point = FindSpawnPoint(WorldContextObject, PointId);
	return Point ? Point->SpawnNPCType(NPCType, Count) : INDEX_NONE;
}

bool ANPCSpawnPoint::FindSpawnTransform(const FNPCSpawnQueuedRequest& Request, FTransform& OutTransform) const
{
	const ACharacter* Defaults = Request.NPCClass.GetDefaultObject();
	const UCapsuleComponent* Capsule = Defaults ? Defaults->GetCapsuleComponent() : nullptr;
	if (!Capsule) { return false; }
	const FNPCSpawnSettings& S = Request.Settings;
	// A CDO's component-to-world transform is not updated. Use its authored relative scale,
	// which SpawnActor multiplies into the unit spawn transform, including Blueprint defaults.
	const FVector ClassScale = Capsule->GetRelativeScale3D().GetAbs();
	const float Radius = Capsule->GetUnscaledCapsuleRadius() * FMath::Min(ClassScale.X, ClassScale.Y);
	const float HalfHeight = Capsule->GetUnscaledCapsuleHalfHeight() * ClassScale.Z;
	if (!FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius <= 0.0f || HalfHeight < Radius) { return false; }
	// Gap applies between characters. Inflating the capsule by a large gap would intersect the floor.
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	const FVector Origin = GetActorLocation();
	const float AngleOffset = FMath::FRandRange(0.0f, 2.0f * PI);
	FCollisionQueryParams ClearanceQuery(SCENE_QUERY_STAT(NPCSpawnClearance), false, this);
	FCollisionQueryParams GroundQuery(SCENE_QUERY_STAT(NPCSpawnGround), false, this);
	const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());

	// Character separation is independent of collision presets; even IgnorePawn characters cannot stack.
	struct FOccupiedCapsule { FVector Location; float Radius; float HalfHeight; };
	TArray<FOccupiedCapsule> Occupied;
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsActorBeingDestroyed()) { continue; }
		const UCapsuleComponent* OtherCapsule = It->GetCapsuleComponent();
		Occupied.Add({ OtherCapsule->GetComponentLocation(), OtherCapsule->GetScaledCapsuleRadius(), OtherCapsule->GetScaledCapsuleHalfHeight() });
		GroundQuery.AddIgnoredActor(*It); // Never use a character's head as a floor.
	}

	for (int32 Attempt = 0; Attempt < S.PlacementAttempts; ++Attempt)
	{
		// A rotated sunflower pattern covers the disk evenly, including its center and outer edge.
		const float Fraction = S.PlacementAttempts == 1 ? 0.0f : float(Attempt) / float(S.PlacementAttempts - 1);
		const float Distance = FMath::Sqrt(FMath::Lerp(FMath::Square(S.InnerRadius), FMath::Square(S.SpawnRadius), Fraction));
		const float Angle = AngleOffset + Attempt * 2.39996323f;
		FVector Location = Origin + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.0f);
		if (S.bSnapToGround)
		{
			FHitResult Ground;
			if (!GetWorld()->LineTraceSingleByChannel(Ground, Location + FVector(0, 0, S.GroundTraceUp),
				Location - FVector(0, 0, S.GroundTraceDown), S.GroundTraceChannel, GroundQuery)
				|| Ground.bStartPenetrating || Cast<APawn>(Ground.GetActor())
				|| Ground.ImpactNormal.Z < Defaults->GetCharacterMovement()->GetWalkableFloorZ())
			{
				continue;
			}
			Location = Ground.ImpactPoint;
		}
		Location.Z += HalfHeight + S.GroundClearance;
		bool bOccupied = false;
		for (const FOccupiedCapsule& Other : Occupied)
		{
			if (FMath::Abs(Location.Z - Other.Location.Z) < HalfHeight + Other.HalfHeight + S.CapsuleGap
				&& FVector::DistSquared2D(Location, Other.Location) < FMath::Square(Radius + Other.Radius + S.CapsuleGap))
			{
				bOccupied = true;
				break;
			}
		}
		if (bOccupied || GetWorld()->OverlapBlockingTestByChannel(Location, FQuat::Identity,
			Capsule->GetCollisionObjectType(), Shape, ClearanceQuery, Responses)) { continue; }
		const float Yaw = S.bRandomYaw ? FMath::FRandRange(-180.0f, 180.0f) : GetActorRotation().Yaw;
		// The spawner's scale/pitch/roll do not resize or tilt the spawned character.
		OutTransform = FTransform(FRotator(0, Yaw, 0), Location);
		return true;
	}
	return false;
}

FVector ANPCSpawnPoint::CalculateLaunchVelocity(const FNPCSpawnSettings& Settings, const FVector& SpawnLocation) const
{
	float Yaw = GetActorRotation().Yaw;
	if (Settings.LaunchDirection == ENPCLaunchDirection::Outward)
	{
		const FVector Offset = (SpawnLocation - GetActorLocation()).GetSafeNormal2D();
		Yaw = Offset.IsNearlyZero() ? FMath::FRandRange(-180.0f, 180.0f) : Offset.Rotation().Yaw;
	}
	Yaw += Settings.LaunchYawOffset;
	if (Settings.LaunchYawSpread > 0.0f)
	{
		Yaw += FMath::FRandRange(-Settings.LaunchYawSpread, Settings.LaunchYawSpread);
	}

	float HorizontalSpeed = Settings.LaunchOutwardSpeed;
	float VerticalSpeed = Settings.LaunchUpSpeed;
	if (Settings.LaunchVelocityMode == ENPCLaunchVelocityMode::SpeedAndAngle)
	{
		const float Angle = FMath::DegreesToRadians(Settings.LaunchElevationAngle);
		HorizontalSpeed = Settings.LaunchSpeed * FMath::Cos(Angle);
		VerticalSpeed = Settings.LaunchSpeed * FMath::Sin(Angle);
	}
	return FRotator(0.0f, Yaw, 0.0f).Vector() * HorizontalSpeed + FVector(0.0f, 0.0f, VerticalSpeed);
}

void ANPCSpawnPoint::ScheduleProcessing()
{
	if (Queue.IsEmpty() || bEndingPlay || IsActorBeingDestroyed()) { return; }
	double NextTime = TNumericLimits<double>::Max();
	for (const FNPCSpawnQueuedRequest& Request : Queue)
	{
		NextTime = FMath::Min(NextTime, Request.bReplacement ? NextReplacementSpawnTime : NextNormalSpawnTime);
	}
	const float Delay = FMath::Max(0.0, NextTime - GetWorld()->GetTimeSeconds());
	GetWorldTimerManager().ClearTimer(SpawnTimer);
	if (Delay > 0.0f)
	{
		GetWorldTimerManager().SetTimer(SpawnTimer, this, &ANPCSpawnPoint::ProcessQueue, Delay, false);
	}
	else
	{
		SpawnTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ANPCSpawnPoint::ProcessQueue);
	}
}

void ANPCSpawnPoint::ProcessQueue()
{
	if (bEndingPlay || IsActorBeingDestroyed() || !HasAuthority() || GetNetMode() == NM_Client || bProcessingQueue) { return; }
	if (VisualSettings.bEnabled && !IsRunningDedicatedServer()) { UpdatePortalAnchor(); }
	TGuardValue<bool> ProcessingGuard(bProcessingQueue, true);
	const int32 Budget = FMath::Clamp(MaxSpawnsPerFrame, 1, 100);
	for (int32 Index = 0; Index < Budget && !Queue.IsEmpty() && !bEndingPlay && !IsActorBeingDestroyed(); ++Index)
	{
		// Do not retain a reference across spawning or Blueprint callbacks, which may change the queue.
		const double Now = GetWorld()->GetTimeSeconds();
		int32 RequestIndex = INDEX_NONE;
		if (NextReplacementSpawnTime <= Now)
		{
			RequestIndex = Queue.IndexOfByPredicate([](const FNPCSpawnQueuedRequest& Request) { return Request.bReplacement; });
		}
		if (RequestIndex == INDEX_NONE && NextNormalSpawnTime <= Now)
		{
			RequestIndex = Queue.IndexOfByPredicate([](const FNPCSpawnQueuedRequest& Request) { return !Request.bReplacement; });
		}
		if (RequestIndex == INDEX_NONE) { break; }
		const FNPCSpawnQueuedRequest Snapshot = Queue[RequestIndex];
		FTransform Transform;
		ACharacter* NPC = nullptr;
		if (FindSpawnTransform(Snapshot, Transform))
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding;
			Params.bDeferConstruction = Snapshot.bReplacement;
			NPC = GetWorld()->SpawnActor<ACharacter>(Snapshot.NPCClass, Transform, Params);
			if (Snapshot.bReplacement && IsValid(NPC))
			{
				if (AChasingNPC* Chaser = Cast<AChasingNPC>(NPC)) { Snapshot.ReplacementPresentation.Apply(*Chaser); }
				NPC->FinishSpawning(Transform);
			}
		}
		RequestIndex = Queue.IndexOfByPredicate([&Snapshot](const FNPCSpawnQueuedRequest& Request) { return Request.Id == Snapshot.Id; });
		if (bEndingPlay || IsActorBeingDestroyed() || RequestIndex == INDEX_NONE) { break; }
		double& NextTime = Snapshot.bReplacement ? NextReplacementSpawnTime : NextNormalSpawnTime;
		if (!IsValid(NPC) || NPC->IsActorBeingDestroyed())
		{
			NextTime = Now + Snapshot.Settings.RetryInterval;
			if (++Queue[RequestIndex].BlockedAttempts >= Snapshot.Settings.MaxBlockedAttempts)
			{
				Queue.RemoveAt(RequestIndex);
				UE_LOG(LogNPCSpawnPoint, Display, TEXT("%s request %d finished: %d/%d spawned; no free placement found after %d attempts."),
					*SpawnPointId.ToString(), Snapshot.Id, Snapshot.Spawned, Snapshot.Requested, Snapshot.Settings.MaxBlockedAttempts);
				OnSpawnRequestFinished.Broadcast(this, Snapshot.Id, Snapshot.Requested, Snapshot.Spawned, false);
			}
			continue;
		}

		Queue[RequestIndex].BlockedAttempts = 0;
		const int32 SpawnedCount = ++Queue[RequestIndex].Spawned;
		const bool bFinished = SpawnedCount == Snapshot.Requested;
		if (bFinished) { Queue.RemoveAt(RequestIndex); }
		SpawnedNPCs.RemoveAllSwap([](const TWeakObjectPtr<ACharacter>& Entry) { return !Entry.IsValid(); });
		SpawnedNPCs.Add(NPC);
		if (!SpawnPointId.IsNone()) { NPC->Tags.AddUnique(SpawnPointId); }
		if (Snapshot.Settings.bEnsureAIController && !NPC->GetController()) { NPC->SpawnDefaultController(); }
		if (Snapshot.Settings.Mode == ENPCSpawnMode::Launch)
		{
			NPC->LaunchCharacter(CalculateLaunchVelocity(Snapshot.Settings, NPC->GetActorLocation()), true, true);
		}
		NextTime = Now + GetEmissionInterval(Snapshot.Settings);
		PulsePortal();
		OnNPCSpawned.Broadcast(this, Snapshot.Id, NPC);
		if (bFinished)
		{
			OnSpawnRequestFinished.Broadcast(this, Snapshot.Id, Snapshot.Requested, SpawnedCount, false);
		}
	}
	ScheduleProcessing();
}
