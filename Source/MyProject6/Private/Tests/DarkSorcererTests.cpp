#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "DarkSorcererNPC.h"
#include "MyProject6Character.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDarkSorcererIntegrationTest, "MyProject6.NPC.DarkSorcerer.Integration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDarkSorcererIntegrationTest::RunTest(const FString& Parameters)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    World->BeginPlay();
    auto Advance = [World](float Seconds)
    {
        const int32 Frames = FMath::CeilToInt(Seconds * 60.f);
        for (int32 I = 0; I < Frames; ++I) { ++GFrameCounter; World->Tick(LEVELTICK_All, Seconds / Frames); }
    };
    AActor* Floor = World->SpawnActor<AActor>();
    UBoxComponent* Shape = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Shape);
    Shape->SetBoxExtent(FVector(5000, 5000, 50));
    Shape->SetCollisionProfileName(TEXT("BlockAll"));
    Shape->RegisterComponent();
    Floor->SetActorLocation(FVector(0, 0, -50));

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    UClass* PlayerClass = LoadClass<AMyProject6Character>(nullptr,
        TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if (!TestNotNull(TEXT("Player Blueprint loads"), PlayerClass)) { return false; }
    auto* Player = World->SpawnActor<AMyProject6Character>(PlayerClass, FVector(800, 0, 100), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Player spawns"), Player)) { return false; }
    auto* Controller = World->SpawnActor<APlayerController>();
    Controller->SetAsLocalPlayerController();
    Controller->Possess(Player);
    Player->GetCharacterMovement()->DisableMovement();
    UClass* Class = LoadClass<ADarkSorcererNPC>(nullptr,
        TEXT("/Game/NPC/DarkSorcerer/BP_DarkSorcerer_NPC.BP_DarkSorcerer_NPC_C"));
    if (!TestNotNull(TEXT("Saved NPC Blueprint loads"), Class)) { return false; }
    auto* NPC = World->SpawnActor<ADarkSorcererNPC>(Class, FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Saved NPC spawns"), NPC)) { return false; }
    FindFProperty<FBoolProperty>(Class, TEXT("bSpawnReplacementsOnDeath"))->SetPropertyValue_InContainer(NPC, false);
    auto* Mesh = NPC->GetMesh();
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->bEnableUpdateRateOptimizations = false;
    TestEqual(TEXT("Production mesh is selected"), Mesh->GetSkeletalMeshAsset()->GetFName(), FName(TEXT("SK_DarkSorcerer_NPC")));
    TestEqual(TEXT("Staff is attached to the hand"), NPC->Staff->GetAttachSocketName(), FName(TEXT("hand_r")));
    TestEqual(TEXT("Staff cannot block attacks or movement"), NPC->Staff->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
    TestNotNull(TEXT("Staff mesh is loaded"), NPC->Staff->GetStaticMesh().Get());
    TestFalse(TEXT("Alive actor does not tick"), NPC->IsActorTickEnabled());
    Advance(.6f);
    TestTrue(TEXT("Existing AI drives chase movement"), NPC->GetVelocity().Size2D() > 10.f);
    UClass* Locomotion = Mesh->GetAnimClass();
    TestEqual(TEXT("Private walk locomotion graph is active"), Locomotion->GetFName(), FName(TEXT("ABP_DarkSorcerer_C")));
    TestTrue(TEXT("Staff grip remains at right hand"), FVector::Dist(NPC->Staff->GetComponentLocation(), Mesh->GetSocketLocation(TEXT("hand_r"))) < 4.f);
    NPC->GetCharacterMovement()->DisableMovement();
    Player->SetActorLocation(NPC->GetActorLocation() + FVector(120, 0, 0));
    Advance(.2f);
    TestTrue(TEXT("Contact drains player energy"), Player->GetCurrentEnergy() < Player->GetMaxEnergy());
    if (!TestNotNull(TEXT("Contact attack starts"), Mesh->GetSingleNodeInstance())) { return false; }
    TestEqual(TEXT("Sorcerer plays its configured attack"), Mesh->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(), FName(TEXT("A_DS_Cast")));
    const FVector LeftFoot = Mesh->GetSocketLocation(TEXT("foot_l"));
    const FVector RightFoot = Mesh->GetSocketLocation(TEXT("foot_r"));
    const FVector CastingHand = Mesh->GetSocketLocation(TEXT("hand_l"));
    Advance(.4f);
    TestTrue(TEXT("Casting keeps left foot planted"), Mesh->GetSocketLocation(TEXT("foot_l")).Equals(LeftFoot, .05f));
    TestTrue(TEXT("Casting keeps right foot planted"), Mesh->GetSocketLocation(TEXT("foot_r")).Equals(RightFoot, .05f));
    TestTrue(TEXT("Casting still animates the free hand"), FVector::Dist(CastingHand, Mesh->GetSocketLocation(TEXT("hand_l"))) > 2.f);
    Player->SetActorLocation(FVector(2000, 0, 100));
    Advance(.3f);
    TestNotNull(TEXT("Leaving contact restores locomotion"), Mesh->GetAnimInstance());
    TestEqual(TEXT("Correct locomotion class restored"), Mesh->GetAnimInstance()->GetClass(), Locomotion);
    UGameplayStatics::ApplyDamage(NPC, 25.f, nullptr, nullptr, nullptr);
    TestEqual(TEXT("Projectile damage reduces health"), NPC->GetCurrentHealth(), 125.f);
    UGameplayStatics::ApplyDamage(NPC, 125.f, nullptr, nullptr, nullptr);
    TestTrue(TEXT("Lethal hit starts death"), NPC->IsDead());
    TestFalse(TEXT("Death disables capsule blocking"), NPC->GetActorEnableCollision());
    Advance(5.1f);
    auto* BodyFade = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
    auto* StaffFade = Cast<UMaterialInstanceDynamic>(NPC->Staff->GetMaterial(0));
    if (!TestNotNull(TEXT("Body fading material"), BodyFade) || !TestNotNull(TEXT("Staff fading material"), StaffFade)) { return false; }
    const float Opacity = BodyFade->K2_GetScalarParameterValue(TEXT("NPCDeathOpacity"));
    TestTrue(TEXT("Body is partly faded"), Opacity > .1f && Opacity < .9f);
    TestEqual(TEXT("Staff fades with body"), StaffFade->K2_GetScalarParameterValue(TEXT("NPCDeathOpacity")), Opacity, .001f);
    TestEqual(TEXT("Staff material supports opacity mask"), StaffFade->GetBlendMode(), BLEND_Masked);
    Advance(1.2f);
    TestTrue(TEXT("Death removes body and attached staff"), NPC->IsActorBeingDestroyed());
    return true;
}
#endif
