#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCHitReactionComponent.generated.h"

class ACharacter;
class AController;
class UMaterialInterface;
class UMaterialInstanceDynamic;
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
	/** Accepted damage only; works with every original skin without replacing its material slots. */
	UFUNCTION(BlueprintCallable, Category = "Hit Reaction|Flash")
	void PlayDamageFlash();
	bool StartDeathFlight(const FVector& Direction);
	bool IsStaggering() const { return bStaggering; }
	bool IsDeathInFlight() const { return bDeathInFlight; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.0", Units = "cm"))
	float StaggerDistance = 28.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.05", Units = "s"))
	float StaggerDuration = 0.34f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.0", ClampMax = "20.0", Units = "deg"))
	float StaggerLeanAngle = 7.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.0", ClampMax = "6.0", Units = "cm"))
	float StaggerCompression = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Flash")
	bool bEnableHitFlash = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Flash")
	FLinearColor HitFlashColor = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Flash", meta = (ClampMin = "0.02", ClampMax = "0.5", Units = "s"))
	float HitFlashDuration = 0.12f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Flash", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float HitFlashIntensity = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Flash")
	TObjectPtr<UMaterialInterface> HitFlashMaterial;
	/** At normal gravity, 450 horizontal / 360 upward travels about 3.3 metres on level ground. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Flight", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DeathHorizontalSpeed = 450.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death Flight", meta = (ClampMin = "0.0", Units = "cm/s"))
	float DeathUpwardSpeed = 360.0f;

private:
	void ResetStagger();
	void FinishFlash();
	void RefreshTickEnabled();
	float StaggerElapsed = 0.0f;
	FVector StaggerDirection = FVector::ZeroVector;
	FQuat RestMeshRotation = FQuat::Identity;
	FVector RestMeshLocation = FVector::ZeroVector;
	FVector Tilt = FVector::ZeroVector;
	FVector TiltVelocity = FVector::ZeroVector;
	float Compression = 0.0f;
	float CompressionVelocity = 0.0f;
	float FlashRemaining = 0.0f;
	float ActiveFlashDuration = 0.12f;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlashInstance;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PreviousOverlay;
	bool bStaggering = false;
	bool bDeathInFlight = false;
};
