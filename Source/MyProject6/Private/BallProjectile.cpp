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
	CollisionComponent->InitSphereRadius(ProjectileCollisionRadius);
	CollisionComponent->SetCollisionProfileName(TEXT("Projectile"));
	CollisionComponent->SetNotifyRigidBodyCollision(true);
	CollisionComponent->SetGenerateOverlapEvents(false);
	CollisionComponent->SetCanEverAffectNavigation(false);
	RootComponent = CollisionComponent;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(CollisionComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetCanEverAffectNavigation(false);
	MeshComponent->SetCastShadow(false);
	MeshComponent->SetRelativeScale3D(FVector(ProjectileVisualScale));

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
	ProjectileMovement->InitialSpeed = ProjectileInitialSpeed;
	ProjectileMovement->MaxSpeed = ProjectileMaxSpeed;
	ProjectileMovement->ProjectileGravityScale = ProjectileGravityScale;
	ProjectileMovement->bRotationFollowsVelocity = true;
	ProjectileMovement->bShouldBounce = bProjectileShouldBounce;
}

void ABallProjectile::BeginPlay()
{
	Super::BeginPlay();
	ApplyProjectileConfiguration();
	if (AActor* OwnerActor = GetOwner())
	{
		CollisionComponent->IgnoreActorWhenMoving(OwnerActor, true);
	}
	if (APawn* InstigatorPawn = GetInstigator())
	{
		CollisionComponent->IgnoreActorWhenMoving(InstigatorPawn, true);
	}

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

void ABallProjectile::ApplyProjectileConfiguration()
{
	if (CollisionComponent)
	{
		CollisionComponent->SetSphereRadius(FMath::Max(ProjectileCollisionRadius, 1.0f), true);
	}
	if (MeshComponent)
	{
		MeshComponent->SetRelativeScale3D(FVector(FMath::Max(ProjectileVisualScale, 0.01f)));
	}
	if (ProjectileMovement)
	{
		ProjectileMovement->InitialSpeed = FMath::Max(ProjectileInitialSpeed, 1.0f);
		ProjectileMovement->MaxSpeed = FMath::Max(ProjectileMaxSpeed, ProjectileMovement->InitialSpeed);
		ProjectileMovement->ProjectileGravityScale = ProjectileGravityScale;
		ProjectileMovement->bShouldBounce = bProjectileShouldBounce;
		ProjectileMovement->Velocity = GetActorForwardVector() * ProjectileMovement->InitialSpeed;
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
			if (bDestroyChunkedCellsInBlast)
			{
				Cone->DestroyConeCellsFromHit(Hit, FloorExplosionRadius);
			}
			else
			{
				Cone->ApplyExplosionFromHit(Hit, FloorExplosionRadius, FloorDamage);
			}
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
			if (bDestroyChunkedCellsInBlast)
			{
				Floor->DestroyCellsFromHit(
					Hit,
					FloorExplosionRadius,
					EChunkedFloorBlastShape::Circle);
			}
			else
			{
				Floor->ApplyExplosionFromHit(Hit, FloorExplosionRadius, FloorDamage);
			}
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

	if (bDestroyOnAnyImpact && !bHasCarvedSurface && Other && Other != GetOwner())
	{
		bHasCarvedSurface = true;
		Destroy();
	}
}
