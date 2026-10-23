#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NPCHitReactionComponent.generated.h"

class ACharacter;
class AController;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UAnimInstance;
struct FDamageEvent;

/** Shared forward torso fold, directional recoil and collision-aware death flight. */
UCLASS(ClassGroup = (NPC), meta = (BlueprintSpawnableComponent))
class MYPROJECT6_API UNPCHitReactionComponent : public UActorComponent
{

	GENERATED_BODY()
public:
	UNPCHitReactionComponent();
	static FVector ResolveImpactDirection(const ACharacter& Victim, const FDamageEvent& Event,
		const AController* Instigator, const AActor* Causer);
	void Stagger(const FVector& Direction);
	void CancelForTether();
	/** Accepted damage only; works with every original skin without replacing its material slots. */
	UFUNCTION(BlueprintCallable, Category = "Hit Reaction|Flash")
	void PlayDamageFlash();
	bool StartDeathFlight(const FVector& Direction);
	void StartBlastTumble(const FVector& Impulse);
	bool IsStaggering() const { return bStaggering || bBlastTumbling; }
	bool IsBlastTumbling() const { return bBlastTumbling; }
	FQuat GetBlastRotation() const { return BlastRotation; }
	bool IsDeathInFlight() const { return bDeathInFlight; }
	UFUNCTION(BlueprintPure, Category = "Hit Reaction")
	float GetCurrentForwardBendAngle() const { return CurrentBendAngle; }
	bool IsUsingForwardBend() const;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.0", Units = "cm"))
	float StaggerDistance = 28.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.05", Units = "s"))
	float StaggerDuration = 0.34f;
	/** Total forward fold distributed over the waist and chest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.0", ClampMax = "70.0", Units = "deg"))
	float ForwardBendAngle = 42.f;
	/** Time to reach the folded pose; the rest of StaggerDuration is recovery. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction", meta = (ClampMin = "0.01", ClampMax = "0.2", Units = "s"))
	float ForwardBendAttackTime = .065f;
	UPROPERTY(EditDefaultsOnly, Category = "Hit Reaction")
	TSoftClassPtr<UAnimInstance> ForwardBendAnimClass;
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
	/** Rotate about the hips during explosive flight, then recover the original pose on landing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Blast")
	bool bEnableBlastTumble = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hit Reaction|Blast", meta = (ClampMin="0", ClampMax="720", Units="deg/s"))
	float BlastTumbleSpeed = 285.f;

private:
	void ResetStagger();
	void FinishFlash();
	void RefreshTickEnabled();
	float StaggerElapsed = 0.0f;
	FVector StaggerDirection = FVector::ZeroVector;
	FVector RestMeshLocation = FVector::ZeroVector;
	float CurrentBendAngle = 0.f;
	float StartBendAngle = 0.f;
	float FlashRemaining = 0.0f;
	float ActiveFlashDuration = 0.12f;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlashInstance;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PreviousOverlay;
	bool bStaggering = false;
	bool bSleepingStagger = false;
	bool bDeathInFlight = false;
	bool bBlastTumbling = false;
	bool bBlastRecovering = false;
	float BlastElapsed = 0.f;
	float BlastRecovery = 0.f;
	float ActiveBlastSpeed = 0.f;
	FVector BlastAxis = FVector::RightVector;
	FVector BlastPivot = FVector::ZeroVector;
	FTransform BlastRestTransform;
	FQuat BlastRotation = FQuat::Identity;
	FQuat BlastLandingRotation = FQuat::Identity;
	void TickBlastTumble(float DeltaTime, ACharacter& Character);
};
