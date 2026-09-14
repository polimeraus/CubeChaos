#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCHitReactionComponent.generated.h"

class ACharacter;
class AController;
struct FDamageEvent;

/** Shared directional recoil and collision-aware death flight for all NPC skeletons. */
UCLASS(ClassGroup = (NPC), meta = (BlueprintSpawnableComponent))
class MYPROJECT6_API UNPCHitReactionComponent : public UActorComponent
{

	GENERATED_BODY()
public:
	UNPCHitReactionComponent();
	static FVector ResolveImpactDirection(const ACharacter& Victim, const FDamageEvent& Event,
		const AController* Instigator, const AActor* Causer);
	void Stagger(const FVector& Direction);
	bool StartDeathFlight(const FVector& Direction);
	bool IsStaggering() const { return bStaggering; }
	bool IsDeathInFlight() const { return bDeathInFlight; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.0", Units = "cm"))
	float StaggerDistance = 22.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.05", Units = "s"))
	float StaggerDuration = 0.24f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "deg"))
	float StaggerLeanAngle = 7.0f;
	/** At normal gravity, 450 horizontal / 360 upward travels about 3.3 metres on level ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Flight", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DeathHorizontalSpeed = 450.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Flight", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DeathUpwardSpeed = 360.0f;

private:
	void ResetStagger();
	float StaggerElapsed = 0.0f;
	FVector StaggerDirection = FVector::ZeroVector;
	FQuat RestMeshRotation = FQuat::Identity;
	bool bStaggering = false;
	bool bDeathInFlight = false;
};
