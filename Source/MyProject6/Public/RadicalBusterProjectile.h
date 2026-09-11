#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RadicalBusterProjectile.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;

/** Visible, non-homing energy shot. World geometry and other pawns stop it. */
UCLASS()
class MYPROJECT6_API ARadicalBusterProjectile : public AActor
{
	GENERATED_BODY()
public:
	ARadicalBusterProjectile();
	void ConfigureShot(float EnergyDamage, float Speed, float Range);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USphereComponent> Collision;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProjectileMovementComponent> Movement;

private:
	UFUNCTION()
	void OnShotHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
	float Damage = 10.0f;
	bool bHitProcessed = false;
};
