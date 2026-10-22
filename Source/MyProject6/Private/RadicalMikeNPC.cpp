#include "RadicalMikeNPC.h"

#include "RadicalBusterProjectile.h"
#include "NPCHealthBarComponent.h"
#include "NPCHitReactionComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MyProject6Character.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ARadicalMikeNPC::ARadicalMikeNPC()
{
	// Keep Mike's own response when the Ghoul base defaults are tuned.
	HitReaction->ForwardBendAngle = 42.f;
	HitReaction->StaggerDuration = .34f;
	MaxHealth = 200.0f;
	CurrentHealth = MaxHealth;
	// Mike uses his own armed animation system instead of Ghoul's contact swipe.
	ContactAttackAnimation = nullptr;
	// The head bone sits inside the oversized helmet, below its antenna.
	HealthBar->HeadClearance = 60.0f;
	PrimaryActorTick.bStartWithTickEnabled = true;
	BusterWeapon = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BusterWeapon"));
	// Same socket and identity transform as the asset pack's BP_ZMikeShowcase_R.
	BusterWeapon->SetupAttachment(GetMesh(), TEXT("Buster_RSocket"));
	// RunChase shrinks Buster_R to 13.5%; keep an equipped weapon at character scale.
	BusterWeapon->SetAbsolute(false, false, true);
	BusterWeapon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BusterWeapon->SetGenerateOverlapEvents(false);
	BusterWeapon->SetCanEverAffectNavigation(false);
	BusterMuzzle = CreateDefaultSubobject<USceneComponent>(TEXT("BusterMuzzle"));
	BusterMuzzle->SetupAttachment(BusterWeapon);
	// The supplied mesh's barrel runs along local +Y, ending at Y=42.84 cm.
	BusterMuzzle->SetRelativeLocation(FVector(0.0f, 44.0f, 0.0f));
	MuzzleFlash = CreateDefaultSubobject<UPointLightComponent>(TEXT("MuzzleFlash"));
	MuzzleFlash->SetupAttachment(BusterMuzzle);
	MuzzleFlash->SetLightColor(FLinearColor(0.05f, 0.65f, 1.0f));
	MuzzleFlash->SetIntensity(2500.0f);
	MuzzleFlash->SetAttenuationRadius(180.0f);
	MuzzleFlash->SetCastShadows(false);
	MuzzleFlash->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Weapon(
		TEXT("/Game/NPC/RadicalMike/Mesh/SM_RadicalBuster.SM_RadicalBuster"));
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(
		TEXT("/Game/NPC/RadicalMike/Mesh/SKM_MegaMikeZ.SKM_MegaMikeZ"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Fire(
		TEXT("/Game/NPC/RadicalMike/Animations/Anim_ZMIKE_FireR.Anim_ZMIKE_FireR"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Idle(
		TEXT("/Game/NPC/RadicalMike/Animations/Anim_ZMIKE_BusterIdle_R.Anim_ZMIKE_BusterIdle_R"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Run(
		TEXT("/Game/NPC/RadicalMike/Animations/Anim_ZMIKE_RunChase.Anim_ZMIKE_RunChase"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Death(
		TEXT("/Game/NPC/RadicalMike/Animations/Anim_ZMIKE_DeathState.Anim_ZMIKE_DeathState"));
	static ConstructorHelpers::FObjectFinder<USoundBase> Sound(
		TEXT("/Game/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02.FirstPersonTemplateWeaponFire02"));
	if (Weapon.Succeeded()) { BusterWeapon->SetStaticMesh(Weapon.Object); }
	if (Body.Succeeded()) { GetMesh()->SetSkeletalMeshAsset(Body.Object); }
	if (Fire.Succeeded()) { FireAnimation = Fire.Object; }
	if (Idle.Succeeded()) { ArmedIdleAnimation = Idle.Object; }
	if (Run.Succeeded()) { RunAnimation = Run.Object; }
	if (Death.Succeeded()) { DeathAnimation = Death.Object; }
	if (Sound.Succeeded()) { FireSound = Sound.Object; }
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// Ghoul-specific fade materials from the base class do not match this character.
	DeathFadeMaterials.Reset();
}

void ARadicalMikeNPC::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(true);
	OnNPCDeath.AddDynamic(this, &ARadicalMikeNPC::StopCombatOnDeath);
	if (ArmedIdleAnimation) { GetMesh()->PlayAnimation(ArmedIdleAnimation, true); }
	if (HasAuthority()) { ScheduleAttack(); }
}

void ARadicalMikeNPC::ScheduleAttack()
{
	if (!IsDead())
	{
		GetWorldTimerManager().SetTimer(AttackTimer, this, &ARadicalMikeNPC::TryStartAttack,
			FMath::Max(0.8f, FireInterval), false);
	}
}

bool ARadicalMikeNPC::CanFireAt(const ACharacter* Target, const FVector& Origin) const
{
	if (!IsValid(Target) || Target == this || IsDead() || IsStaggering() || !bEnableShooting ||
		!GetCharacterMovement()->IsMovingOnGround() ||
		FVector::DistSquared(GetActorLocation(), Target->GetActorLocation()) > FMath::Square(FMath::Max(100.0f, FireRange)))
	{
		return false;
	}
	if (const AMyProject6Character* Player = Cast<AMyProject6Character>(Target))
	{
		if (Player->IsEnergyDepleted()) { return false; }
	}
	FHitResult Hit;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RadicalMikeSight), false, this);
	return !GetWorld()->LineTraceSingleByChannel(Hit, Origin, Target->GetActorLocation(), ECC_Visibility, Query)
		|| Hit.GetActor() == Target;
}

void ARadicalMikeNPC::FaceTarget(const ACharacter* Target)
{
	const FVector Direction = Target->GetActorLocation() - GetActorLocation();
	SetActorRotation(FRotator(0.0f, Direction.Rotation().Yaw, 0.0f));
}

void ARadicalMikeNPC::TryStartAttack()
{
	ScheduleAttack();
	ACharacter* Target = UGameplayStatics::GetPlayerCharacter(this, 0);
	if (bAttackInProgress || !CanFireAt(Target, GetActorLocation() + FVector(0, 0, 35))) { return; }
	bAttackInProgress = true;
	if (GetController()) { GetController()->StopMovement(); }
	GetCharacterMovement()->StopMovementImmediately();
	ConsumeMovementInputVector();
	FaceTarget(Target);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GetMesh()->bEnableUpdateRateOptimizations = false;
	if (ArmedIdleAnimation) { GetMesh()->PlayAnimation(ArmedIdleAnimation, true); }
	const float Duration = FireAnimation ? FireAnimation->GetPlayLength() / FMath::Max(0.01f, FireAnimation->RateScale) : 0.65f;
	GetWorldTimerManager().SetTimer(ShotTimer, this, &ARadicalMikeNPC::FireAtPlayer,
		0.18f, false);
	GetWorldTimerManager().SetTimer(RecoverTimer, this, &ARadicalMikeNPC::FinishAttack, 0.18f + FMath::Max(0.3f, Duration), false);
}

void ARadicalMikeNPC::FireAtPlayer()
{
	ACharacter* Target = UGameplayStatics::GetPlayerCharacter(this, 0);
	if (!bAttackInProgress || !CanFireAt(Target, GetActorLocation() + FVector(0, 0, 35))) { return; }
	FaceTarget(Target);
	// FireR begins aimed forwards and then recoils upwards. Emit at the initial pose.
	if (FireAnimation) { GetMesh()->PlayAnimation(FireAnimation, false); }
	GetMesh()->TickAnimation(0.0f, false);
	GetMesh()->RefreshBoneTransforms();
	BusterWeapon->SetWorldScale3D(GetMesh()->GetComponentScale());
	const FVector Muzzle = BusterMuzzle->GetComponentLocation();
	if (!CanFireAt(Target, Muzzle)) { return; }
	// Do not let an arm extending through cover spawn a projectile on the other side.
	FHitResult Obstruction;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RadicalMikeMuzzle), false, this);
	if (GetWorld()->LineTraceSingleByChannel(Obstruction, GetActorLocation() + FVector(0, 0, 35), Muzzle, ECC_Visibility, Query)
		&& Obstruction.GetActor() != Target) { return; }
	const FTransform ShotTransform((Target->GetActorLocation() - Muzzle).Rotation(), Muzzle);
	ARadicalBusterProjectile* Shot = GetWorld()->SpawnActorDeferred<ARadicalBusterProjectile>(
		ARadicalBusterProjectile::StaticClass(), ShotTransform, this, this,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Shot) { return; }
	Shot->ConfigureShot(ShotEnergyDamage, ProjectileSpeed, FireRange);
	Shot->FinishSpawning(ShotTransform);
	MuzzleFlash->SetVisibility(true);
	GetWorldTimerManager().SetTimer(FlashTimer, this, &ARadicalMikeNPC::HideMuzzleFlash, 0.08f, false);
	if (FireSound) { UGameplayStatics::PlaySoundAtLocation(this, FireSound, Muzzle, 0.55f, 1.25f); }
}

void ARadicalMikeNPC::FinishAttack()
{
	bAttackInProgress = false;
	if (IsDead() || IsStaggering()) { return; }
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	GetMesh()->bEnableUpdateRateOptimizations = true;
	if (ArmedIdleAnimation) { GetMesh()->PlayAnimation(ArmedIdleAnimation, true); }
}

void ARadicalMikeNPC::HideMuzzleFlash() { MuzzleFlash->SetVisibility(false); }

void ARadicalMikeNPC::StopCombatOnDeath()
{
	GetWorldTimerManager().ClearTimer(AttackTimer);
	GetWorldTimerManager().ClearTimer(ShotTimer);
	GetWorldTimerManager().ClearTimer(RecoverTimer);
	GetWorldTimerManager().ClearTimer(FlashTimer);
	bAttackInProgress = false;
	HideMuzzleFlash();
	BusterWeapon->SetVisibility(false);
	// The base class enables Tick again only when the corpse begins fading.
	SetActorTickEnabled(false);
}

void ARadicalMikeNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopCombatOnDeath();
	Super::EndPlay(EndPlayReason);
}

void ARadicalMikeNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsDead() || IsStaggering()) { return; }
	BusterWeapon->SetWorldScale3D(GetMesh()->GetComponentScale());
	if (bAttackInProgress) { return; }
	UAnimSequence* Desired = GetVelocity().SizeSquared2D() > 100.0f ? RunAnimation.Get() : ArmedIdleAnimation.Get();
	UAnimSingleNodeInstance* Instance = GetMesh()->GetSingleNodeInstance();
	if (Desired && (!Instance || Instance->GetCurrentAsset() != Desired)) { GetMesh()->PlayAnimation(Desired, true); }
}
