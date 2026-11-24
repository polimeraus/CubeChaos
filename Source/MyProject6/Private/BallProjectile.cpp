// Copyright Epic Games, Inc. All Rights Reserved.

#include "BallProjectile.h"

#include "ChunkedDestructibleConeStructure.h"
#include "QuadPanelConeStructure.h"
#include "ChunkedDestructibleFloor.h"
#include "RoundedBoxActor.h"
#include "SimpleHouseActor.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ABallProjectile::ABallProjectile()
	: ABallProjectile(EAppearance::LaserBolt)
{
}

ABallProjectile::ABallProjectile(EAppearance Appearance)
{
	bLaserBolt = Appearance == EAppearance::LaserBolt;
	PrimaryActorTick.bCanEverTick = bLaserBolt;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.bAllowTickOnDedicatedServer = false;
	PrimaryActorTick.TickGroup = TG_PostPhysics;
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
	MeshComponent->SetRelativeScale3D(GetVisualScale());

	if (bLaserBolt)
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Pulse(
			TEXT("/Game/Weapons/DefaultWeapon/Meshes/SM_SunPulse.SM_SunPulse"));
		if (Pulse.Succeeded())
		{
			MeshComponent->SetStaticMesh(Pulse.Object);
			ProjectileMaterial = MeshComponent->GetMaterial(MeshComponent->GetMaterialIndex(TEXT("PulseCore")));
		}
	}
	if (!MeshComponent->GetStaticMesh())
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flat(
			TEXT("/Game/Materials/Prototyping/M_FlatCol.M_FlatCol"));
		MeshComponent->SetStaticMesh(Sphere.Object);
		ProjectileMaterial = Flat.Object;
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
	if (bLaserBolt)
	{
		AddTickPrerequisiteComponent(ProjectileMovement);
		const int32 GlowSlot = MeshComponent->GetMaterialIndex(TEXT("PulseGlow"));
		UMaterialInterface* Glow = GlowSlot != INDEX_NONE ? MeshComponent->GetMaterial(GlowSlot) : nullptr;
		FLinearColor DefaultTint;
		if (Glow && Glow->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")), DefaultTint)
			&& !DefaultTint.Equals(ProjectileColor))
		{
			if (auto* Instance = MeshComponent->CreateDynamicMaterialInstance(GlowSlot, Glow))
				Instance->SetVectorParameterValue(TEXT("Tint"), ProjectileColor);
		}
	}
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
			TEXT("/Game/Materials/Prototyping/M_FlatCol.M_FlatCol"));
	}
	if (!ParentMaterial)
	{
		ParentMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
	}
	const int32 CoreSlot = MeshComponent->GetMaterialIndex(TEXT("PulseCore"));
	// Standard automatic fire shares one core material across all live shots.
	FLinearColor SurfaceTint;
	if (CoreSlot != INDEX_NONE && MeshComponent->GetMaterial(CoreSlot) == ParentMaterial
		&& ParentMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Tint")), SurfaceTint)
		&& SurfaceTint.Equals(ProjectileColor))
	{
		return;
	}
	ProjectileMaterialInstance = MeshComponent->CreateDynamicMaterialInstance(CoreSlot == INDEX_NONE ? 0 : CoreSlot, ParentMaterial);
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
		MeshComponent->SetRelativeScale3D(GetVisualScale());
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

FVector ABallProjectile::GetVisualScale() const
{
	const float Size = FMath::Max(ProjectileVisualScale, .01f);
	if (!bLaserBolt) return FVector(Size);
	const float Width = FMath::Clamp(BoltWidthScale, .1f, 4.f);
	return Size * FVector(FMath::Clamp(BoltLengthScale, .1f, 4.f), Width, Width);
}

void ABallProjectile::StartMuzzlePresentation(const FVector& MuzzleLocation)
{
	if (!bLaserBolt || !MeshComponent->GetStaticMesh() || GetNetMode() == NM_DedicatedServer) return;
	LaunchOrigin = GetActorLocation();
	LaunchOffset = MuzzleLocation - LaunchOrigin;
	// Use the actual rearward mesh extent so changing the bolt length cannot send its tail through the player.
	const FBox Bounds = MeshComponent->GetStaticMesh()->GetBoundingBox();
	TailLength = FMath::Max(1., -Bounds.Min.X * GetVisualScale().X * FMath::Abs(GetActorScale3D().X));
	LaunchBlendRange = FMath::Max3(double(MuzzleBlendDistance), TailLength, LaunchOffset.Size()*2.5);
	SetActorTickEnabled(true);
	UpdateMuzzlePresentation();
}

void ABallProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateMuzzlePresentation();
}

void ABallProjectile::UpdateMuzzlePresentation()
{
	const FVector Location = GetActorLocation();
	const double Distance = FVector::Distance(Location, LaunchOrigin);
	if (Distance >= LaunchBlendRange)
	{
		FinishMuzzlePresentation();
		return;
	}
	const double Remaining = 1.-Distance/LaunchBlendRange;
	// This tangent follows the rendered nose. Only the mesh moves off-axis during the launch.
	const FVector Tangent = ProjectileMovement->Velocity.GetSafeNormal() - LaunchOffset*(2.*Remaining/LaunchBlendRange);
	FVector Scale = GetVisualScale();
	Scale.X *= FMath::Clamp(Distance/TailLength, .015, 1.);
	MeshComponent->SetRelativeScale3D(Scale);
	MeshComponent->SetWorldLocationAndRotation(Location+LaunchOffset*FMath::Square(Remaining), Tangent.Rotation());
}

void ABallProjectile::FinishMuzzlePresentation()
{
	MeshComponent->SetRelativeTransform(FTransform(FQuat::Identity, FVector::ZeroVector, GetVisualScale()));
	SetActorTickEnabled(false);
}

void ABallProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
	bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	if (bLaserBolt && IsActorTickEnabled()) FinishMuzzlePresentation();

	if (bHasCarvedSurface)
	{
		return;
	}

	if (AQuadPanelConeStructure* QuadCone = Cast<AQuadPanelConeStructure>(Other))
	{
		bHasCarvedSurface = true;
		if (bExplodeChunkedFloor)
		{
			if (bDestroyChunkedCellsInBlast)
			{
				QuadCone->DestroyConeCellsFromHit(Hit, FloorExplosionRadius);
			}
			else
			{
				QuadCone->ApplyExplosionFromHit(Hit, FloorExplosionRadius, FloorDamage);
			}
		}
		else
		{
			QuadCone->DestroyConeCellFromHit(Hit);
		}
		Destroy();
	}
	else if (AChunkedDestructibleConeStructure* Cone = Cast<AChunkedDestructibleConeStructure>(Other))
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
	else if (ASimpleHouseActor* House = Cast<ASimpleHouseActor>(Other))
	{
		if (House->bEnableDamage)
		{
			bHasCarvedSurface = true;
			House->CarveSphereAtWorldLocation(
				Hit.ImpactPoint,
				CollisionComponent->GetScaledSphereRadius() * CavityRadiusMultiplier);
			Destroy();
		}
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
