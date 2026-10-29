#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BombProjectile.h"
#include "ChunkedDestructibleHouse.h"
#include "DestructibleRadialSculpture.h"
#include "ChasingNPC.h"
#include "MyProject6Character.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombSphereTest, "MyProject6.Weapons.BombSphere",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBombSphereTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL()); World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto Flag = [&](UObject* Object, const TCHAR* Name, bool Value)
    {
        auto* Property = FindFProperty<FBoolProperty>(Object->GetClass(), Name);
        if (TestNotNull(Name, Property)) Property->SetPropertyValue_InContainer(Object, Value);
    };
    auto Advance = [&](float Seconds)
    {
        const int32 Steps = FMath::CeilToInt(Seconds*60);
        for (int32 I=0; I<Steps; ++I) { ++GFrameCounter; World->Tick(LEVELTICK_All, Seconds/Steps); }
    };
    auto Bomb = [&](FVector Origin)
    {
        const FTransform Transform(Origin);
        auto* B = World->SpawnActorDeferred<ABombProjectile>(ABombProjectile::StaticClass(), Transform);
        B->LaunchVelocity = FVector::ZeroVector;
        B->FinishSpawning(Transform); B->Movement->Deactivate();
        return B;
    };
    auto Floor = [&](FTransform Transform, FVector Dimensions)
    {
        auto* F = World->SpawnActorDeferred<AChunkedDestructibleFloor>(AChunkedDestructibleFloor::StaticClass(), Transform);
        Flag(F, TEXT("bUseDestructionRipple"), false);
        F->ConfigureFloor(FVector2D(Dimensions.X, Dimensions.Y), Dimensions, FVector2D::ZeroVector, false);
        F->FinishSpawning(Transform);
        return F;
    };
    auto Exists = [](AChunkedDestructibleFloor* F)
    {
        FChunkedFloorCellInfo Info;
        return F->GetCellInfo(FIntPoint(0,0), Info) && Info.bExists;
    };
    auto Box = [&](FVector Location, FVector Extent, ECollisionEnabled::Type Collision)
    {
        auto* A = World->SpawnActor<AActor>();
        auto* C = NewObject<UBoxComponent>(A); A->SetRootComponent(C);
        C->SetBoxExtent(Extent); C->SetCollisionProfileName(TEXT("PhysicsActor"));
        C->SetCollisionEnabled(Collision); C->RegisterComponent(); A->SetActorLocation(Location);
        C->SetEnableGravity(false); C->SetSimulatePhysics(true);
        return C;
    };

    const FVector Origin(0,0,5000);
    TArray<AChunkedDestructibleFloor*> InRange;
    for (const FVector Direction : {FVector(1,0,0), FVector(-1,0,0), FVector(0,1,0),
        FVector(0,-1,0), FVector(0,0,1), FVector(0,0,-1)})
        InRange.Add(Floor(FTransform(Origin+Direction*200+FVector(0,0,25)), FVector(50)));
    // Destruction is driven by cell geometry even if a chunk is not queryable.
    InRange.Last()->SetActorEnableCollision(false);
    auto* Diagonal = Floor(FTransform(Origin+FVector(300,300,25)), FVector(50));
    // Rotate a long slab into a tall side wall. Its centre is outside the blast,
    // while its side is only 100 cm away. Include nonuniform scale.
    const FTransform WallTransform(FRotator(90,0,0), Origin+FVector(-100,900,0), FVector(1,2,1));
    auto* TallWall = Floor(WallTransform, FVector(100,1000,50));
    auto* Airburst = Bomb(Origin);
    Airburst->Explode();
    for (auto* F : InRange) TestFalse(TEXT("Airburst breaks cells in all six directions"), Exists(F));
    TestTrue(TEXT("Diagonal cell outside the sphere survives"), Exists(Diagonal));
    TestFalse(TEXT("A transformed wall intersecting only at its edge still breaks"), Exists(TallWall));
    Airburst->Destroy();

    // A sphere above a floor must not turn into a 2D disk through the whole chunk.
    const FVector PlaneOrigin(5000,0,5000);
    auto* Plane = World->SpawnActorDeferred<AChunkedDestructibleFloor>(AChunkedDestructibleFloor::StaticClass(), FTransform(PlaneOrigin));
    Flag(Plane, TEXT("bUseDestructionRipple"), false);
    Plane->ConfigureFloor(FVector2D(1000,100), FVector(100,100,50), FVector2D::ZeroVector, false);
    Plane->FinishSpawning(FTransform(PlaneOrigin));
    auto* Above = Bomb(PlaneOrigin+FVector(0,0,280)); Above->Explode();
    FChunkedFloorCellInfo NearCell, FarCell;
    Plane->GetCellInfo(FIntPoint(5,0), NearCell); Plane->GetCellInfo(FIntPoint(7,0), FarCell);
    TestFalse(TEXT("Sphere reaches the floor directly below"), NearCell.bExists);
    TestTrue(TEXT("Vertical separation limits the floor damage footprint"), FarCell.bExists);
    Above->Destroy();

    // Real house side wall, timed airborne detonation, with and without contact.
    for (float Gap : {12.f, 150.f})
    {
        const FTransform T(FVector(10000+Gap*20,0,2000));
        auto* House = World->SpawnActorDeferred<AChunkedDestructibleHouse>(AChunkedDestructibleHouse::StaticClass(), T);
        Flag(House, TEXT("bGenerateWindows"), false); Flag(House, TEXT("bGenerateDoor"), false);
        Flag(House, TEXT("bUseDestructionRipple"), false); Flag(House, TEXT("bCollapseUnsupportedSections"), false);
        House->ConfigureHouse(FIntVector(12,10,12), 2, 0, FVector(100), FVector::ZeroVector, true, false);
        House->FinishSpawning(T);
        FHitResult Hit;
        const FVector Start = T.GetLocation()+FVector(900,0,650);
        TestTrue(TEXT("House side wall has collision far above the foundation"),
            World->LineTraceSingleByChannel(Hit, Start, Start-FVector(500,0,0), ECC_Visibility));
        FIntPoint Cell = FIntPoint::NoneValue;
        if (!TestTrue(TEXT("Side hit resolves its destructible cell"),
            Hit.GetActor()==House && House->WorldLocationToCell(Hit.ImpactPoint-Hit.ImpactNormal*5, Cell))) return false;
        auto* B = Bomb(Hit.ImpactPoint+Hit.ImpactNormal*Gap);
        // Use the production timer installed by BeginPlay (three seconds).
        Advance(2.9f);
        FChunkedFloorCellInfo Info; House->GetCellInfo(Cell, Info);
        TestTrue(TEXT("Side wall remains intact before the fuse expires"), Info.bExists);
        Advance(.15f); House->GetCellInfo(Cell, Info);
        TestTrue(TEXT("Fuse detonates without ground contact"), B->bExploded);
        TestFalse(TEXT("Timed airburst breaks the side wall without requiring a hit"), Info.bExists);
        House->Destroy(); B->Destroy();
    }

    const FVector PhysicsOrigin(20000,0,5000);
    auto* Large = Box(PhysicsOrigin+FVector(800,0,0), FVector(300,20,20), ECollisionEnabled::QueryAndPhysics);
    auto* PhysicsOnly = Box(PhysicsOrigin+FVector(0,200,0), FVector(20), ECollisionEnabled::PhysicsOnly);
    auto* Outside = Box(PhysicsOrigin+FVector(0,-700,0), FVector(20), ECollisionEnabled::QueryAndPhysics);
    auto* PhysicsBomb = Bomb(PhysicsOrigin); PhysicsBomb->Explode();
    Advance(.05f);
    TestTrue(TEXT("Surface in range pushes a body whose centre lies outside"), Large->GetPhysicsLinearVelocity().X>100);
    TestTrue(TEXT("PhysicsOnly bodies receive pressure too"), PhysicsOnly->GetPhysicsLinearVelocity().Y>100);
    TestTrue(TEXT("Physics outside the sphere stays still"), Outside->GetPhysicsLinearVelocity().IsNearlyZero());
    PhysicsBomb->Destroy();

    // Sculptures implement TakeDamage as well as the structure destruction API.
    // A bomb must use only one path, even when many chunks overlap its sphere.
    const FVector SculptureOrigin(28000,0,5000);
    auto* SculptureClass = LoadClass<ADestructibleRadialSculpture>(nullptr,
        TEXT("/Game/Structures/Destructible/RadialSculpture/BP_DestructibleRadialSculpture.BP_DestructibleRadialSculpture_C"));
    if (!TestNotNull(TEXT("Saved sculpture class loads"), SculptureClass)) return false;
    auto* Sculpture = World->SpawnActor<ADestructibleRadialSculpture>(SculptureClass, SculptureOrigin, FRotator::ZeroRotator);
    TArray<float> BeforeHealth;
    for (int32 I=0; I<Sculpture->GetCellCount().X; ++I)
    {
        FChunkedFloorCellInfo Info; Sculpture->GetCellInfo(FIntPoint(I,0), Info); BeforeHealth.Add(Info.Health);
    }
    auto* Gentle = Bomb(SculptureOrigin+FVector(330,-100,38)); Gentle->BlastDamage=1; Gentle->Explode();
    int32 DamagedPieces=0;
    for (int32 I=0; I<BeforeHealth.Num(); ++I)
    {
        FChunkedFloorCellInfo Info; Sculpture->GetCellInfo(FIntPoint(I,0), Info);
        const float Loss = BeforeHealth[I]-Info.Health;
        if (Loss>0) ++DamagedPieces;
        if (!TestTrue(TEXT("Each sculpture fragment receives at most one damage application"),
            Loss<=Gentle->SculptureDamageMultiplier+.001f)) return false;
    }
    TestTrue(TEXT("Saved sculpture is damaged by an airborne bomb"), DamagedPieces>0);
    Gentle->Destroy();

    // Load saved character classes in this new editor process and verify the
    // native behavior reaches future spawns without changing per-instance data.
    const FVector CharacterOrigin(24000,0,5000);
    auto* PlayerClass = LoadClass<AMyProject6Character>(nullptr,
        TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    auto* NPCClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/NPC/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"));
    if (!TestNotNull(TEXT("Saved player class loads"), PlayerClass) || !TestNotNull(TEXT("Saved NPC class loads"), NPCClass)) return false;
    auto* Player = World->SpawnActor<AMyProject6Character>(PlayerClass, CharacterOrigin+FVector(0,0,200), FRotator::ZeroRotator);
    const float Energy = Player->GetCurrentEnergy();
    auto* NPC = World->SpawnActor<ACharacter>(NPCClass, CharacterOrigin-FVector(0,0,200), FRotator::ZeroRotator);
    auto* Health = FindFProperty<FFloatProperty>(NPCClass, TEXT("CurrentHealth"));
    if (!TestNotNull(TEXT("Saved NPC exposes its real health"), Health)) return false;
    const float HealthBefore = Health->GetPropertyValue_InContainer(NPC);
    auto* CharacterBomb = Bomb(CharacterOrigin); CharacterBomb->SetOwner(Player);
    CharacterBomb->BlastDamage = 5;
    CharacterBomb->Explode();
    TestTrue(TEXT("Saved player above the airburst loses HUD energy"), Player->GetCurrentEnergy()<Energy);
    TestTrue(TEXT("Saved NPC below the airburst loses health"), Health->GetPropertyValue_InContainer(NPC)<HealthBefore);
    const float EnergyAfter = Player->GetCurrentEnergy();
    const float HealthAfter = Health->GetPropertyValue_InContainer(NPC);
    CharacterBomb->Explode();
    TestEqual(TEXT("Player damage applies once"), Player->GetCurrentEnergy(), EnergyAfter);
    TestEqual(TEXT("NPC damage applies once"), Health->GetPropertyValue_InContainer(NPC), HealthAfter);

    const FVector RagdollOrigin(32000,0,5000);
    auto* RagdollActor = World->SpawnActor<AActor>();
    auto* Ragdoll = NewObject<USkeletalMeshComponent>(RagdollActor); RagdollActor->SetRootComponent(Ragdoll);
    Ragdoll->SetSkeletalMeshAsset(NPC->GetMesh()->GetSkeletalMeshAsset());
    Ragdoll->SetPhysicsAsset(NPC->GetMesh()->GetPhysicsAsset());
    Ragdoll->SetCollisionProfileName(TEXT("PhysicsActor"));
    Ragdoll->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    // Set the transform before registering: registration creates the rigid bodies.
    RagdollActor->SetActorLocation(RagdollOrigin+FVector(200,0,0)); Ragdoll->RegisterComponent();
    Ragdoll->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Ragdoll->RefreshBoneTransforms();
    Ragdoll->SetEnableGravity(false); Ragdoll->SetSimulatePhysics(true);
    TestTrue(TEXT("Saved NPC mesh provides simulating ragdoll bodies"), Ragdoll->IsAnySimulatingPhysics());
    TestTrue(TEXT("Ragdoll physics bodies start inside the blast sphere"), FVector::Distance(Ragdoll->GetCenterOfMass(),RagdollOrigin)<400);
    auto* RagdollBomb = Bomb(RagdollOrigin); RagdollBomb->Explode();
    Advance(.05f);
    TestTrue(TEXT("Skeletal physics bodies receive outward pressure"), Ragdoll->GetPhysicsLinearVelocity().X>100);
    return true;
}
#endif
