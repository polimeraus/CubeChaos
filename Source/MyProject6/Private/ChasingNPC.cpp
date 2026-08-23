// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChasingNPC.h"

#include "ChasingAIController.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MyProject6Character.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AChasingNPC::AChasingNPC()
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(true);

	AIControllerClass = AChasingAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);

	ContactSensor = CreateDefaultSubobject<USphereComponent>(TEXT("Contact Sensor"));
	ContactSensor->SetupAttachment(GetCapsuleComponent());
	ContactSensor->InitSphereRadius(ContactInteractionRadius);
	ContactSensor->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ContactSensor->SetCollisionObjectType(ECC_WorldDynamic);
	ContactSensor->SetCollisionResponseToAllChannels(ECR_Ignore);
	ContactSensor->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ContactSensor->SetGenerateOverlapEvents(true);
	ContactSensor->SetCanEverAffectNavigation(false);

	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetGenerateOverlapEvents(false);
	GetMesh()->SetCanEverAffectNavigation(false);
	// Off-screen replacement waves should not keep evaluating and refreshing every skeleton.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	GetMesh()->bEnableUpdateRateOptimizations = true;
	GetMesh()->bUpdateOverlapsOnAnimationFinalize = false;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = true;
	Movement->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	Movement->MaxWalkSpeed = ChaseSpeed;
	Movement->MaxAcceleration = 4096.0f;
	Movement->BrakingDecelerationWalking = 2048.0f;
	Movement->bUseRVOAvoidance = true;
	Movement->AvoidanceConsiderationRadius = 500.0f;

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> CharacterMesh(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
	if (CharacterMesh.Succeeded())
	{
		GetMesh()->SetSkeletalMeshAsset(CharacterMesh.Object);
	}

	static ConstructorHelpers::FClassFinder<UAnimInstance> AnimationBlueprint(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
	if (AnimationBlueprint.Succeeded())
	{
		GetMesh()->SetAnimInstanceClass(AnimationBlueprint.Class);
	}

	// A project sound is assigned by default and can be replaced per instance in Details.
	static ConstructorHelpers::FObjectFinder<USoundBase> DefaultDeathSound(
		TEXT("/Game/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02.FirstPersonTemplateWeaponFire02"));
	if (DefaultDeathSound.Succeeded())
	{
		DeathSound = DefaultDeathSound.Object;
		ContactDrainSound = DefaultDeathSound.Object;
		FallingSound = DefaultDeathSound.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultContactGlow(
		TEXT("/Game/NPC/Materials/M_NPCContactGlow.M_NPCContactGlow"));
	if (DefaultContactGlow.Succeeded())
	{
		ContactGlowMaterial = DefaultContactGlow.Object;
	}
}

void AChasingNPC::BeginPlay()
{
	Super::BeginPlay();

	CurrentHealth = FMath::Max(1.0f, MaxHealth);
	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.0f, ChaseSpeed);
	if (const ACharacter* PlayerCharacter = UGameplayStatics::GetPlayerCharacter(this, 0))
	{
		UpdateChaseSpeedFromPlayer(PlayerCharacter);
	}
	ContactSensor->SetSphereRadius(FMath::Max(48.0f, ContactInteractionRadius));

	ContactSensor->OnComponentBeginOverlap.AddDynamic(this, &AChasingNPC::OnContactBegin);
	ContactSensor->OnComponentEndOverlap.AddDynamic(this, &AChasingNPC::OnContactEnd);
}

void AChasingNPC::UpdateChaseSpeedFromPlayer(const ACharacter* PlayerCharacter)
{
	if (!PlayerCharacter)
	{
		return;
	}

	const UCharacterMovementComponent* PlayerMovement = PlayerCharacter->GetCharacterMovement();
	UCharacterMovementComponent* NPCMovement = GetCharacterMovement();
	if (!PlayerMovement || !NPCMovement)
	{
		return;
	}

	const float SafeFraction = FMath::Clamp(PlayerSpeedFraction, 0.0f, 2.0f);
	NPCMovement->MaxWalkSpeed = FMath::Max(0.0f, PlayerMovement->MaxWalkSpeed * SafeFraction);
}

void AChasingNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(ContactDrainTimerHandle);
	GetWorldTimerManager().ClearTimer(FallingSoundTimerHandle);
	ContactingPlayers.Reset();
	Super::EndPlay(EndPlayReason);
}

void AChasingNPC::OnMovementModeChanged(
	const EMovementMode PreviousMovementMode,
	const uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	GetWorldTimerManager().ClearTimer(FallingSoundTimerHandle);
	if (GetCharacterMovement()->IsFalling() && !bIsDead)
	{
		bFallingSoundPlayed = false;
		GetWorldTimerManager().SetTimer(
			FallingSoundTimerHandle,
			this,
			&AChasingNPC::PlayFallingSoundIfStillFalling,
			FMath::Max(0.01f, FallingSoundDelay),
			false);
	}
	else
	{
		bFallingSoundPlayed = false;
	}
}

void AChasingNPC::PlayFallingSoundIfStillFalling()
{
	if (bIsDead || bFallingSoundPlayed || !GetCharacterMovement()->IsFalling() || !FallingSound)
	{
		return;
	}

	bFallingSoundPlayed = true;
	UGameplayStatics::PlaySoundAtLocation(
		this,
		FallingSound,
		GetActorLocation(),
		FallingSoundVolume,
		FallingSoundPitch);
}

void AChasingNPC::FellOutOfWorld(const UDamageType& DamageType)
{
	if (!bIsDead)
	{
		// Do not create two more NPCs below the kill plane; that would recursively multiply.
		Die(false);
		return;
	}

	Super::FellOutOfWorld(DamageType);
}

void AChasingNPC::OnContactBegin(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	AMyProject6Character* PlayerCharacter = Cast<AMyProject6Character>(OtherActor);
	if (bIsDead || !PlayerCharacter || !PlayerCharacter->IsPlayerControlled())
	{
		return;
	}

	ContactingPlayers.Add(PlayerCharacter);
	SetContactGlowActive(true);
	ContactSoundCooldown = 0.0f;

	if (!GetWorldTimerManager().IsTimerActive(ContactDrainTimerHandle))
	{
		const float SafeInterval = FMath::Clamp(ContactDrainInterval, 0.02f, 0.5f);
		GetWorldTimerManager().SetTimer(
			ContactDrainTimerHandle,
			this,
			&AChasingNPC::ApplyContactEnergyDrain,
			SafeInterval,
			true,
			SafeInterval);
	}
}

void AChasingNPC::OnContactEnd(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex)
{
	if (AMyProject6Character* PlayerCharacter = Cast<AMyProject6Character>(OtherActor))
	{
		ContactingPlayers.Remove(PlayerCharacter);
	}

	if (ContactingPlayers.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(ContactDrainTimerHandle);
		SetContactGlowActive(false);
		ContactSoundCooldown = 0.0f;
	}
}

void AChasingNPC::ApplyContactEnergyDrain()
{
	const float SafeInterval = FMath::Clamp(ContactDrainInterval, 0.02f, 0.5f);
	ContactSoundCooldown -= SafeInterval;
	bool bDrainedAnyEnergy = false;
	for (auto It = ContactingPlayers.CreateIterator(); It; ++It)
	{
		AMyProject6Character* PlayerCharacter = It->Get();
		if (!PlayerCharacter || PlayerCharacter->IsEnergyDepleted())
		{
			It.RemoveCurrent();
			continue;
		}

		PlayerCharacter->DrainEnergy(FMath::Max(0.0f, ContactEnergyDrainPerSecond) * SafeInterval);
		bDrainedAnyEnergy = true;
	}

	if (bDrainedAnyEnergy && ContactDrainSound && ContactSoundCooldown <= 0.0f)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			ContactDrainSound,
			GetActorLocation(),
			ContactSoundVolume,
			ContactSoundPitch);
		ContactSoundCooldown = FMath::Max(0.1f, ContactSoundInterval);
	}

	if (ContactingPlayers.IsEmpty())
	{
		GetWorldTimerManager().ClearTimer(ContactDrainTimerHandle);
		SetContactGlowActive(false);
	}
}

void AChasingNPC::SetContactGlowActive(const bool bActive)
{
	if (bContactGlowActive == bActive || !GetMesh())
	{
		return;
	}

	if (bActive)
	{
		if (!ContactGlowMaterial)
		{
			return;
		}

		OriginalMeshMaterials.Reset();
		const int32 MaterialCount = GetMesh()->GetNumMaterials();
		OriginalMeshMaterials.Reserve(MaterialCount);
		for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
		{
			OriginalMeshMaterials.Add(GetMesh()->GetMaterial(MaterialIndex));
			GetMesh()->SetMaterial(MaterialIndex, ContactGlowMaterial);
		}
		bContactGlowActive = true;
		return;
	}

	for (int32 MaterialIndex = 0; MaterialIndex < OriginalMeshMaterials.Num(); ++MaterialIndex)
	{
		GetMesh()->SetMaterial(MaterialIndex, OriginalMeshMaterials[MaterialIndex]);
	}
	OriginalMeshMaterials.Reset();
	bContactGlowActive = false;
}

float AChasingNPC::TakeDamage(
	const float DamageAmount,
	const FDamageEvent& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{
	if (bIsDead || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	const float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (AppliedDamage <= 0.0f)
	{
		return 0.0f;
	}

	CurrentHealth = FMath::Max(0.0f, CurrentHealth - AppliedDamage);
	if (CurrentHealth <= 0.0f)
	{
		Die();
	}

	return AppliedDamage;
}

void AChasingNPC::Die(const bool bShouldSpawnReplacements)
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;
	OnNPCDeath.Broadcast();
	GetWorldTimerManager().ClearTimer(ContactDrainTimerHandle);
	GetWorldTimerManager().ClearTimer(FallingSoundTimerHandle);
	ContactingPlayers.Reset();

	if (AController* ExistingController = GetController())
	{
		ExistingController->StopMovement();
	}

	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);

	if (DeathSound)
	{
		UGameplayStatics::PlaySoundAtLocation(
			this,
			DeathSound,
			GetActorLocation(),
			DeathSoundVolume,
			DeathSoundPitch);
	}

	if (bShouldSpawnReplacements)
	{
		SpawnReplacements();
	}

	// PlaySoundAtLocation is fire-and-forget, so the audio continues after the actor is gone.
	Destroy();
}

void AChasingNPC::SpawnReplacements()
{
	UWorld* World = GetWorld();
	const int32 RequestedReplacementCount = FMath::Clamp(ReplacementCount, 0, 8);
	if (!bSpawnReplacementsOnDeath || !World || RequestedReplacementCount == 0)
	{
		return;
	}

	int32 SafeReplacementCount = RequestedReplacementCount;
	const int32 SafePopulationLimit = FMath::Clamp(MaxActiveNPCCount, 0, 512);
	if (SafePopulationLimit > 0)
	{
		int32 LivingNPCCount = 0;
		for (TActorIterator<AChasingNPC> It(World); It; ++It)
		{
			if (IsValid(*It) && !It->IsDead())
			{
				++LivingNPCCount;
			}
		}
		SafeReplacementCount = FMath::Min(
			RequestedReplacementCount,
			FMath::Max(SafePopulationLimit - LivingNPCCount, 0));
		if (SafeReplacementCount == 0)
		{
			return;
		}
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = GetOwner();
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	const FVector DeathLocation = GetActorLocation();
	const float SafeRadius = FMath::Max(100.0f, ReplacementSpawnRadius);
	const float AngleStep = 360.0f / static_cast<float>(SafeReplacementCount);
	const float InitialAngle = FMath::FRandRange(0.0f, 360.0f);

	for (int32 Index = 0; Index < SafeReplacementCount; ++Index)
	{
		const float AngleRadians = FMath::DegreesToRadians(InitialAngle + AngleStep * Index);
		const FVector Offset(
			FMath::Cos(AngleRadians) * SafeRadius,
			FMath::Sin(AngleRadians) * SafeRadius,
			10.0f);

		World->SpawnActor<AChasingNPC>(
			GetClass(),
			DeathLocation + Offset,
			GetActorRotation(),
			SpawnParameters);
	}
}
