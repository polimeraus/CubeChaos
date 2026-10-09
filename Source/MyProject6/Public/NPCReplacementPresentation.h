#pragma once

#include "CoreMinimal.h"
#include "NPCReplacementPresentation.generated.h"

class AChasingNPC;
class USoundBase;
class UAnimSequence;
class UMaterialInterface;

/** Value snapshot survives the defeated actor's fade/destruction while a spawn is queued. */
USTRUCT()
struct FNPCReplacementPresentation
{
	GENERATED_BODY()
	void Capture(const AChasingNPC& Source);
	void Apply(AChasingNPC& Target) const;

	UPROPERTY() TObjectPtr<USoundBase> HitSound;
	UPROPERTY() TObjectPtr<USoundBase> DeathSound;
	UPROPERTY() TObjectPtr<UAnimSequence> DeathAnimation;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> DeathFadeMaterials;
	float HitSoundVolume = 1.0f;
	float HitSoundPitch = 1.0f;
	float DeathSoundVolume = 1.0f;
	float DeathSoundPitch = 1.0f;
	float DeathAnimationPlayRate = 1.0f;
	float DeathHoldDuration = 3.0f;
	float DeathFadeDuration = 2.0f;
	float StaggerDistance = 28.0f;
	float StaggerDuration = 0.34f;
	float StaggerLeanAngle = 7.0f;
	float StaggerCompression = 2.0f;
	bool bEnableHitFlash = true;
	FLinearColor HitFlashColor = FLinearColor::White;
	float HitFlashDuration = .12f;
	float HitFlashIntensity = 2.f;
	UPROPERTY() TObjectPtr<UMaterialInterface> HitFlashMaterial;
	float DeathHorizontalSpeed = 450.0f;
	float DeathUpwardSpeed = 360.0f;
	FName DeathOpacityParameter;
};
