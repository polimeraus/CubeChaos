// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BallProjectile.generated.h"

class UProjectileMovementComponent;
class USphereComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** A visible spherical projectile fired by the first-person character. */
UCLASS()
class MYPROJECT6_API ABallProjectile : public AActor
{
	GENERATED_BODY()

public:
	ABallProjectile();

protected:
	virtual void BeginPlay() override;
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
		bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	/** Basic surface material tinted at runtime with ProjectileColor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Visuals")
	TObjectPtr<UMaterialInterface> ProjectileMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Visuals", meta = (HideAlphaChannel))
	FLinearColor ProjectileColor = FLinearColor(1.0f, 0.72f, 0.0f, 1.0f);

	/** Multiplies the physical ball radius when creating an impact cavity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Destruction", meta = (ClampMin = "0.1"))
	float CavityRadiusMultiplier = 3.0f;

	/** Damage applied to one chunked-floor cell. Matches the floor's default health. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Floor Destruction", meta = (ClampMin = "0.0"))
	float FloorDamage = 25.0f;

	/** Turns this projectile into a bomb when it hits a chunked floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Floor Destruction")
	bool bExplodeChunkedFloor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Floor Destruction", meta = (ClampMin = "0.0", Units = "cm"))
	float FloorExplosionRadius = 350.0f;

	/** Standard point damage applied to damageable actors such as the chasing NPC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Damage", meta = (ClampMin = "0.0"))
	float ActorDamage = 100.0f;

private:
	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ProjectileMaterialInstance;

	bool bHasCarvedSurface = false;
};
