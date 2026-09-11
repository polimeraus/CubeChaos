#include "RadicalBusterProjectile.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "MyProject6Character.h"
#include "UObject/ConstructorHelpers.h"

ARadicalBusterProjectile::ARadicalBusterProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	InitialLifeSpan = 3.0f;
	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	SetRootComponent(Collision);
	Collision->InitSphereRadius(6.0f);
	Collision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Collision->SetCollisionObjectType(ECC_WorldDynamic);
	Collision->SetCollisionResponseToAllChannels(ECR_Block);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Collision->SetGenerateOverlapEvents(false);
	Collision->SetCanEverAffectNavigation(false);

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EnergyBolt"));
	Visual->SetupAttachment(Collision);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetGenerateOverlapEvents(false);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetCastShadow(false);
	Visual->SetRelativeScale3D(FVector(0.30f, 0.12f, 0.12f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded()) { Visual->SetStaticMesh(Sphere.Object); }
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glow(
		TEXT("/Game/Characters/RadicalMike/Materials/M_BusterEnergy.M_BusterEnergy"));
	if (Glow.Succeeded()) { Visual->SetMaterial(0, Glow.Object); }

	Movement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	Movement->SetUpdatedComponent(Collision);
	Movement->InitialSpeed = 2200.0f;
	Movement->MaxSpeed = 2200.0f;
	Movement->ProjectileGravityScale = 0.0f;
	Movement->bRotationFollowsVelocity = true;
	Movement->bShouldBounce = false;
}

void ARadicalBusterProjectile::ConfigureShot(float EnergyDamage, float Speed, float Range)
{
	Damage = FMath::Max(0.0f, EnergyDamage);
	Movement->InitialSpeed = FMath::Max(100.0f, Speed);
	Movement->MaxSpeed = Movement->InitialSpeed;
	InitialLifeSpan = FMath::Clamp(Range / Movement->InitialSpeed, 0.05f, 10.0f);
}

void ARadicalBusterProjectile::BeginPlay()
{
	Super::BeginPlay();
	Collision->IgnoreActorWhenMoving(GetOwner(), true);
	Collision->IgnoreActorWhenMoving(GetInstigator(), true);
	Collision->OnComponentHit.AddDynamic(this, &ARadicalBusterProjectile::OnShotHit);
}

void ARadicalBusterProjectile::OnShotHit(UPrimitiveComponent* HitComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bHitProcessed || OtherActor == GetOwner() || OtherActor == GetInstigator()) { return; }
	bHitProcessed = true;
	if (HasAuthority())
	{
		if (AMyProject6Character* Player = Cast<AMyProject6Character>(OtherActor))
		{
			if (Player->IsPlayerControlled() && !Player->IsEnergyDepleted()) { Player->DrainEnergy(Damage); }
		}
	}
	Destroy();
}
