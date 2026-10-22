#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "ChasingNPC.h"
#include "NPCSpawnPoint.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChasingNPCDeathTest,
	"MyProject6.NPC.DeathAnimationHoldFade",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChasingNPCDeathTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->SetGameInstance(NewObject<UGameInstance>(GEngine));
	World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->BeginPlay();
	auto SetBool = [](AChasingNPC* NPC, const FName Name, bool Value)
	{
		FindFProperty<FBoolProperty>(NPC->GetClass(), Name)->SetPropertyValue_InContainer(NPC, Value);
	};
	auto SetFloat = [](AChasingNPC* NPC, const FName Name, float Value)
	{
		FindFProperty<FFloatProperty>(NPC->GetClass(), Name)->SetPropertyValue_InContainer(NPC, Value);
	};
	auto Advance = [World](float Duration)
	{
		const int32 Frames = FMath::CeilToInt(Duration * 60.0f);
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Duration / Frames);
		}
	};
	auto* NPC = World->SpawnActor<AChasingNPC>();
	if (!TestNotNull(TEXT("NPC spawns"), NPC)) { return false; }
	SetBool(NPC, TEXT("bSpawnReplacementsOnDeath"), false);
	TestFalse(TEXT("Living NPC has no actor tick cost"), NPC->IsActorTickEnabled());
	TestEqual(TEXT("Nonlethal hit applies damage"), UGameplayStatics::ApplyDamage(NPC, 25, nullptr, nullptr, nullptr), 25.0f);
	TestEqual(TEXT("Nonlethal hit retains remaining health"), NPC->GetCurrentHealth(), 75.0f);
	TestFalse(TEXT("Nonlethal hit does not start death"), NPC->IsDead());
	UGameplayStatics::ApplyDamage(NPC, 75, nullptr, nullptr, nullptr);
	TestTrue(TEXT("Lethal hit marks the NPC dead"), NPC->IsDead());
	TestFalse(TEXT("Death does not immediately destroy the actor"), NPC->IsActorBeingDestroyed());
	TestFalse(TEXT("Corpse no longer blocks the player/projectiles"), NPC->GetActorEnableCollision());
	TestNull(TEXT("Death releases the chasing controller"), NPC->GetController());
	TestTrue(TEXT("Death disables movement"), NPC->GetCharacterMovement()->MovementMode == MOVE_None);
	TestEqual(TEXT("Repeated hits cannot damage the corpse"), UGameplayStatics::ApplyDamage(NPC, 100, nullptr, nullptr, nullptr), 0.0f);
	UAnimSingleNodeInstance* Animation = NPC->GetMesh()->GetSingleNodeInstance();
	if (!TestNotNull(TEXT("Death switches from locomotion to a sequence"), Animation)) { return false; }
	TestEqual(TEXT("Configured death animation is playing"), Animation->GetCurrentAsset()->GetFName(), FName(TEXT("A_Ghoul_Death_FullFall")));
	const float DeathLength = CastChecked<UAnimSequence>(Animation->GetCurrentAsset())->GetPlayLength();
	Advance(DeathLength + .1f);
	TestEqual(TEXT("Sequence reaches its end even off-screen"), Animation->GetCurrentTime(), DeathLength, 0.03f);
	Advance(2.7f);
	TestFalse(TEXT("Corpse waits after the animation before fading"), NPC->IsActorTickEnabled());
	TestFalse(TEXT("Corpse remains for the hold duration"), NPC->IsActorBeingDestroyed());
	Advance(0.3f);
	TestTrue(TEXT("Only fading corpses tick"), NPC->IsActorTickEnabled());
	for (int32 Index = 0; Index < NPC->GetMesh()->GetNumMaterials(); ++Index)
	{
		auto* Material = Cast<UMaterialInstanceDynamic>(NPC->GetMesh()->GetMaterial(Index));
		if (!TestNotNull(TEXT("Every slot receives a dynamic fade material"), Material)) { return false; }
		AddInfo(FString::Printf(TEXT("Fade material %s: blend=%d, base=%s, dither=%d"), *Material->GetPathName(), int32(Material->GetBlendMode()), *Material->GetMaterial()->GetPathName(), int32(Material->GetMaterial()->DitherOpacityMask)));
		TestTrue(TEXT("Fade shader uses a dithered opacity mask"), Material->GetBlendMode() == BLEND_Masked && Material->GetMaterial()->DitherOpacityMask);
		const float Opacity = Material->K2_GetScalarParameterValue(TEXT("NPCDeathOpacity"));
		TestTrue(TEXT("Fade starts near full visibility"), Opacity > 0.95f && Opacity < 1.0f);
	}
	Advance(0.9f);
	for (int32 Index = 0; Index < NPC->GetMesh()->GetNumMaterials(); ++Index)
	{
		const float Opacity = CastChecked<UMaterialInstanceDynamic>(NPC->GetMesh()->GetMaterial(Index))->K2_GetScalarParameterValue(TEXT("NPCDeathOpacity"));
		TestTrue(TEXT("Body and clothes gradually reach half opacity"), Opacity > 0.4f && Opacity < 0.6f);
	}
	Advance(1.2f);
	TestTrue(TEXT("Actor is removed after fade completion"), NPC->IsActorBeingDestroyed());

	// Replacement generations inherit the editable presentation, and repeated hits never multiply them.
	auto* Point = World->SpawnActor<ANPCSpawnPoint>(FVector(2000, 0, 0), FRotator::ZeroRotator);
	Point->SpawnSettings.bSnapToGround = false;
	auto* Parent = World->SpawnActor<AChasingNPC>();
	SetFloat(Parent, TEXT("HitSoundPitch"), 1.4f);
	SetFloat(Parent, TEXT("DeathHoldDuration"), 0.0f);
	SetFloat(Parent, TEXT("DeathFadeDuration"), 0.3f);
	FindFProperty<FObjectPropertyBase>(Parent->GetClass(), TEXT("DeathAnimation"))->SetObjectPropertyValue_InContainer(Parent, nullptr);
	UGameplayStatics::ApplyDamage(Parent, 100, nullptr, nullptr, nullptr);
	UGameplayStatics::ApplyDamage(Parent, 100, nullptr, nullptr, nullptr);
	TestEqual(TEXT("One death queues exactly two at the spawn point"), Point->GetPendingNPCCount(), 2);
	Advance(1.0f / 60.0f);
	int32 LivingCount = 0;
	for (TActorIterator<AChasingNPC> It(World); It; ++It)
	{
		if (!It->IsDead())
		{
			++LivingCount;
			TestTrue(TEXT("Replacements appear at the portal, away from the death"), It->GetActorLocation().X > 1300.0);
			TestEqual(TEXT("Replacements inherit the chosen hit pitch"),
				FindFProperty<FFloatProperty>(It->GetClass(), TEXT("HitSoundPitch"))->GetPropertyValue_InContainer(*It), 1.4f);
		}
	}
	TestEqual(TEXT("One death creates exactly two replacements"), LivingCount, 2);
	Advance(0.4f);
	TestTrue(TEXT("Missing animation and zero hold still fade and clean up"), Parent->IsActorBeingDestroyed());
	return true;
}
#endif

