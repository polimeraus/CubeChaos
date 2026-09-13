// Copyright Epic Games, Inc. All Rights Reserved.

#include "CannonballProjectile.h"

ACannonballProjectile::ACannonballProjectile()
{
	// Red shots remove twice as much NPC health as the yellow ball.
	ActorDamage *= 2.0f;
	InitialLifeSpan = 8.0f;
	ProjectileColor = FLinearColor(1.0f, 0.015f, 0.005f, 1.0f);
	ProjectileCollisionRadius = 24.0f;
	ProjectileVisualScale = 0.48f;
	ProjectileInitialSpeed = 1800.0f;
	ProjectileMaxSpeed = 1800.0f;
	ProjectileGravityScale = 1.0f;
	bProjectileShouldBounce = false;
	bExplodeChunkedFloor = true;
	bDestroyChunkedCellsInBlast = true;
	bDestroyOnAnyImpact = true;
	FloorExplosionRadius = DestructionDiameter * 0.5f;
	ApplyProjectileConfiguration();
}

void ACannonballProjectile::BeginPlay()
{
	FloorExplosionRadius = FMath::Max(DestructionDiameter, 1.0f) * 0.5f;
	Super::BeginPlay();
}

void ACannonballProjectile::SetDestructionDiameter(float NewDiameter)
{
	DestructionDiameter = FMath::Max(NewDiameter, 1.0f);
	FloorExplosionRadius = DestructionDiameter * 0.5f;
}
