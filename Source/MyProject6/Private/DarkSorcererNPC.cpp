#include "DarkSorcererNPC.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NPCHealthBarComponent.h"
#include "NPCHitReactionComponent.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "EdGraph/EdGraph.h"
#endif

ADarkSorcererNPC::ADarkSorcererNPC()
{
	// Preserve this character's response independently of the Ghoul base defaults.
	HitReaction->ForwardBendAngle = 42.f;
	HitReaction->StaggerDuration = .34f;
	MaxHealth = 150.f;
	CurrentHealth = MaxHealth;
	PlayerSpeedFraction = .25f;
	ChaseSpeed = 200.f;
	HealthBar->HeadClearance = 28.f;
	// Reuse the base enemy's timers and animation blueprint. No live actor Tick is added.
	DeathFadeMaterials.Reset();
	Staff = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SorcererStaff"));
	Staff->SetupAttachment(GetMesh(), TEXT("hand_r"));
	Staff->SetRelativeLocation(FVector(.15218258, .58992743, -2.37251186));
	Staff->SetRelativeRotation(FQuat(.59846044, .009649329, .52059966, .60887450).GetNormalized());
	Staff->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Staff->SetGenerateOverlapEvents(false);
	Staff->SetCanEverAffectNavigation(false);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(
		TEXT("/Game/Characters/DarkSorcerer/Meshes/SK_DarkSorcerer_NPC.SK_DarkSorcerer_NPC"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Weapon(
		TEXT("/Game/Characters/DarkSorcerer/Meshes/SM_DarkSorcerer_Staff_NPC.SM_DarkSorcerer_Staff_NPC"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Attack(
		TEXT("/Game/Characters/DarkSorcerer/Animations/A_DS_Cast.A_DS_Cast"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> Locomotion(
		TEXT("/Game/Characters/DarkSorcerer/Animations/ABP_DarkSorcerer"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Death(
		TEXT("/Game/Characters/Mannequins/Anims/Death/MM_Death_Front_01.MM_Death_Front_01"));
	if (Body.Succeeded()) { GetMesh()->SetSkeletalMeshAsset(Body.Object); }
	if (Weapon.Succeeded()) { Staff->SetStaticMesh(Weapon.Object); }
	if (Attack.Succeeded()) { ContactAttackAnimation = Attack.Object; }
	if (Locomotion.Succeeded()) { GetMesh()->SetAnimInstanceClass(Locomotion.Class); }
	if (Death.Succeeded()) { DeathAnimation = Death.Object; }
}

int32 ADarkSorcererNPC::ConfigureLocomotion(UObject* AnimationBlueprint, UBlendSpace* Locomotion)
{
#if WITH_EDITOR
	UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(AnimationBlueprint);
	if (!Blueprint || !Locomotion) { return 0; }
	Locomotion->ValidateSampleData();
	Locomotion->ResampleData();
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	int32 Replaced = 0;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_BlendSpacePlayer* Player = Cast<UAnimGraphNode_BlendSpacePlayer>(Node))
			{
				Player->SetAnimationAsset(Locomotion);
				++Replaced;
			}
		}
	}
	return Replaced;
#else
	return 0;
#endif
}

void ADarkSorcererNPC::BeginPlay()
{
	Super::BeginPlay();
	OnNPCDeath.AddDynamic(this, &ADarkSorcererNPC::PrepareStaffDeathFade);
}

void ADarkSorcererNPC::PrepareStaffDeathFade()
{
	StaffDeathMaterial = Staff->CreateDynamicMaterialInstance(0);
}

void ADarkSorcererNPC::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// The base enables Tick only for death fading. Follow its actual opacity so
	// delayed landing/held death poses cannot leave an opaque floating staff.
	if (IsDead() && StaffDeathMaterial && !IsActorBeingDestroyed())
	{
		float Opacity = 1.f;
		if (UMaterialInterface* BodyMaterial = GetMesh()->GetMaterial(0))
		{
			BodyMaterial->GetScalarParameterValue(FMaterialParameterInfo(DeathOpacityParameter), Opacity);
		}
		StaffDeathMaterial->SetScalarParameterValue(DeathOpacityParameter, Opacity);
	}
}
