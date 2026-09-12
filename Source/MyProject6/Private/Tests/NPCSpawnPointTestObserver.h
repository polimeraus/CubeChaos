#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "UObject/Object.h"
#include "NPCSpawnPointTestObserver.generated.h"

class ANPCSpawnPoint;
class ACharacter;

UCLASS(Transient, NotPlaceable)
class ANPCSpawnPointLargeTestCharacter : public ACharacter
{
	GENERATED_BODY()
public:
	ANPCSpawnPointLargeTestCharacter();
};

struct FNPCSpawnTestResult
{
	int32 Id;
	int32 Requested;
	int32 Spawned;
	bool bCancelled;
};

/** Receives reflected events in the spawn point integration tests. */
UCLASS(Transient)
class UNPCSpawnPointTestObserver : public UObject
{
	GENERATED_BODY()
public:
	TArray<FNPCSpawnTestResult> Results;
	TArray<FVector> LaunchVelocities;
	TArray<FVector> SpawnLocations;
	TArray<int32> SpawnRequestIds;
	TArray<double> SpawnTimes;
	int32 SpawnEvents = 0;
	bool bCancelOnNextSpawn = false;
	bool bQueueOnNextFinish = false;

	UFUNCTION()
	void OnSpawned(ANPCSpawnPoint* Point, int32 RequestId, ACharacter* NPC);
	UFUNCTION()
	void OnFinished(ANPCSpawnPoint* Point, int32 RequestId, int32 Requested, int32 Spawned, bool bCancelled);
};
