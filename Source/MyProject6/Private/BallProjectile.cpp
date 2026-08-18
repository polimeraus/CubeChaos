// Copyright Epic Games, Inc. All Rights Reserved.

#include "BallProjectile.h"

#include "ChunkedDestructibleConeStructure.h"
#include "ChunkedDestructibleFloor.h"
#include "RoundedBoxActor.h"
#include "SimpleHouseActor.h"
#include "SimpleHouseActor19.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ABallProjectile::ABallProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	InitialLifeSpan = 5.0f;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	CollisionComponent->InitSphereRadius(12.0f);
	CollisionComponent->SetCollisionProfileName(TEXT("Projectile"));
	CollisionComponent->SetNotifyRigidBodyCollision(true);
	RootComponent = CollisionComponent;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(CollisionComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetRelativeScale3D(FVector(0.24f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(SphereMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicProjectileMaterial(
		TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	if (BasicProjectileMaterial.Succeeded())
	{
		ProjectileMaterial = BasicProjectileMaterial.Object;
		MeshComponent->SetMaterial(0, ProjectileMaterial);
	}

	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->InitialSpeed = 2500.0f;
	ProjectileMovement->MaxSpeed = 2500.0f;
	ProjectileMovement->ProjectileGravityScale = 0.0f;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = true;
}

void ABallProjectile::BeginPlay()
{
	Super::BeginPlay();

	UMaterialInterface* ParentMaterial = ProjectileMaterial.Get();
	if (!ParentMaterial)
	{
		ParentMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/LevelPrototyping/Materials/M_FlatCol.M_FlatCol"));
	}
	if (!ParentMaterial)
	{
		ParentMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	ProjectileMaterialInstance = MeshComponent->CreateDynamicMaterialInstance(0, ParentMaterial);
	if (ProjectileMaterialInstance)
	{
		// M_FlatCol's exact vector parameter name contains a space. Keep the common aliases too
		// so a replacement material can use its own conventional parameter name.
		ProjectileMaterialInstance->SetVectorParameterValue(TEXT("Base Color"), ProjectileColor);
		ProjectileMaterialInstance->SetVectorParameterValue(TEXT("BaseColor"), ProjectileColor);
		ProjectileMaterialInstance->SetVectorParameterValue(TEXT("Color"), ProjectileColor);
		ProjectileMaterialInstance->SetVectorParameterValue(TEXT("Tint"), ProjectileColor);
		ProjectileMaterialInstance->SetScalarParameterValue(TEXT("Metallic"), 0.0f);
		ProjectileMaterialInstance->SetScalarParameterValue(TEXT("Roughness"), 0.3f);
	}
}

void ABallProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);

	if (bHasCarvedSurface)
	{
		return;
	}

	if (AChunkedDestructibleConeStructure* Cone = Cast<AChunkedDestructibleConeStructure>(Other))
	{
		bHasCarvedSurface = true;
		if (bExplodeChunkedFloor)
		{
			Cone->ApplyExplosionFromHit(Hit, FloorExplosionRadius, FloorDamage);
		}
		else
		{
			Cone->DestroyConeCellFromHit(Hit);
		}
		Destroy();
	}
	else if (AChunkedDestructibleFloor* Floor = Cast<AChunkedDestructibleFloor>(Other))
	{
		bHasCarvedSurface = true;
		if (bExplodeChunkedFloor)
		{
			Floor->ApplyExplosionFromHit(Hit, FloorExplosionRadius, FloorDamage);
		}
		else
		{
			Floor->DamageCellFromHit(Hit, FloorDamage);
		}
		Destroy();
	}
	else if (ARoundedBoxActor* RoundedBox = Cast<ARoundedBoxActor>(Other))
	{
		bHasCarvedSurface = true;
		RoundedBox->CarveSphereAtWorldLocation(
			Hit.ImpactPoint,
			CollisionComponent->GetScaledSphereRadius() * CavityRadiusMultiplier);
		Destroy();
	}
	else if (ASimpleHouseActor19* House19 = Cast<ASimpleHouseActor19>(Other))
	{
		if (House19->bEnableDamage)
		{
			bHasCarvedSurface = true;
			House19->CarveSphereAtWorldLocation(
				Hit.ImpactPoint,
				CollisionComponent->GetScaledSphereRadius() * CavityRadiusMultiplier);
			Destroy();
		}
	}
	else if (ASimpleHouseActor* House = Cast<ASimpleHouseActor>(Other))
	{
		bHasCarvedSurface = true;
		House->CarveSphereAtWorldLocation(
			Hit.ImpactPoint,
			CollisionComponent->GetScaledSphereRadius() * CavityRadiusMultiplier);
		Destroy();
	}
	else if (Other && Other != GetOwner())
	{
		const FVector ShotDirection = ProjectileMovement
			? ProjectileMovement->Velocity.GetSafeNormal()
			: GetActorForwardVector();
		const float AppliedDamage = UGameplayStatics::ApplyPointDamage(
			Other,
			ActorDamage,
			ShotDirection,
			Hit,
			GetInstigatorController(),
			this,
			nullptr);

		if (AppliedDamage > 0.0f)
		{
			bHasCarvedSurface = true;
			Destroy();
		}
	}
}
