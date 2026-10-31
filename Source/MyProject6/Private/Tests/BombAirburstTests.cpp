#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BombProjectile.h"
#include "ChunkedDestructibleConeStructure.h"
#include "QuadPanelConeStructure.h"
#include "ChunkedDestructibleHouse.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "MyProject6Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBombAirburstTest, "MyProject6.Weapons.BombAirburst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBombAirburstTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL()); World->BeginPlay();
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto Bomb = [&](FVector Origin, float Radius)
    {
        const FTransform Transform(Origin);
        auto* B = World->SpawnActorDeferred<ABombProjectile>(ABombProjectile::StaticClass(), Transform);
        B->LaunchVelocity = FVector::ZeroVector; B->BlastRadius = Radius;
        B->FinishSpawning(Transform); B->Movement->Deactivate();
        return B;
    };
    int32 Case = 0;
    auto CheckCone = [&](auto* Cone)
    {
        Cone->ConeAnimationType = EConeStructureAnimationType::None;
        Cone->bGenerateEntrance = false; Cone->bScatterDestroyedPieces = false;
        Cone->bPlayDestroyTickSound = false; Cone->FlashDuration = 0;
        Cone->PlatformStyle = EConeStructurePlatformStyle::RisingSpiral;
        Cone->PlatformOverhangBlocks = 5; Cone->LowestPlatformHeightBlocks = 2;
        Cone->SpiralEndHeightBlocks = 9; Cone->PlatformThicknessBlocks = 2;
        Cone->PlatformSpiralTurns = 1; Cone->SpiralSegmentsPerTurn = 32;
        Cone->CircularSegments = 32; Cone->MaxRuntimeCircularSegments = 32;
        FindFProperty<FBoolProperty>(Cone->GetClass(),TEXT("bUseDestructionRipple"))->SetPropertyValue_InContainer(Cone,false);
        for (auto Mode : {EConeStructureSpiralDestructionMode::ExistingCellDestruction, EConeStructureSpiralDestructionMode::OneSegmentPerHit,
            EConeStructureSpiralDestructionMode::OneTrianglePerHit})
        for (bool bRotated : {false, true})
        {
            const FTransform Transform(bRotated ? FRotator(27, 53, 11) : FRotator::ZeroRotator,
                FVector((++Case)*5000, 0, 5000), bRotated ? FVector(1.4,.7,1.8) : FVector::OneVector);
            Cone->SetActorTransform(Transform);
            Cone->SpiralDestructionMode = Mode;
            Cone->bScatterDestroyedPieces = Mode != EConeStructureSpiralDestructionMode::ExistingCellDestruction;
            Cone->bPlatformSpiralClockwise = bRotated;
            Cone->ConfigureConeStructure(12, 5, 2, .8f, FVector(50), FVector::ZeroVector);
            TInlineComponentArray<UDynamicMeshComponent*> ConeChunks(Cone);
            for (auto* Chunk : ConeChunks) Chunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            const int32 Segment = 10;
            FVector Inner[2], Outer[2];
            for (int32 End = 0; End < 2; ++End)
            {
                const float Alpha = float(Segment+End)/Cone->GetSpiralSegmentCount();
                const float Z = FMath::Lerp(Cone->GetSpiralBottomHeight(false), Cone->GetSpiralBottomHeight(true), Alpha)
                    + Cone->GetSpiralThickness();
                const float Angle = (bRotated ? -1 : 1)*UE_TWO_PI*Alpha;
                const FVector Radial(FMath::Cos(Angle),FMath::Sin(Angle),0);
                Inner[End] = Radial*FMath::Max(Cone->GetConeOuterRadiusAtHeight(Z)
                    - Cone->SpiralWallOverlapBlocks*Cone->CachedRadialBlockUnit*Cone->GetPlatformWidthScaleAtHeight(Z),0.f)+FVector(0,0,Z);
                Outer[End] = Radial*Cone->GetPlatformOuterRadiusAtHeight(Z)+FVector(0,0,Z);
            }
            // Near the outer edge, well away from the slice's centre and the cone's cells.
            const FVector A = Transform.TransformPosition(Inner[0]);
            const FVector B = Transform.TransformPosition(Outer[0]);
            const FVector C = Transform.TransformPosition(Outer[1]);
            FVector Normal = FVector::CrossProduct(B-A,C-A).GetSafeNormal();
            if (Normal.Dot(Transform.GetUnitAxis(EAxis::Z)) < 0) Normal *= -1;
            const FVector Surface = .05*A+.475*B+.475*C;
            auto* Shot = Bomb(Surface+Normal*12,30);
            auto ExistingCells = [&]() { int32 Count = 0; for (const auto& Cell : Cone->Cells) Count += Cell.bExists ? 1 : 0; return Count; };
            const int32 CellsBefore = ExistingCells();
            TestTrue(TEXT("Airburst is outside the platform and far from its centre"),
                FVector::Distance(Shot->GetActorLocation(),(A+B+C)/3)>Shot->BlastRadius);
            Shot->Explode();
            TestTrue(FString::Printf(TEXT("%s mode %d rotated %d: nearby airburst breaks the spiral surface"),
                *Cone->GetClass()->GetName(),int32(Mode),bRotated), Mode == EConeStructureSpiralDestructionMode::ExistingCellDestruction
                    ? ExistingCells() < CellsBefore : Cone->IsSpiralTriangleSliceDestroyed(Segment,0));
            TestFalse(TEXT("Distant spiral surface stays intact"),Cone->IsSpiralTriangleSliceDestroyed(25,0));
            if (Mode != EConeStructureSpiralDestructionMode::ExistingCellDestruction)
                TestTrue(TEXT("Destroyed smooth spiral emits pressure-driven debris"),Cone->GetActiveDebrisPieceCount()>0);
            Shot->Destroy();
        }
        Cone->Destroy();
    };
    FActorSpawnParameters Spawn; Spawn.bDeferConstruction = true;
    CheckCone(World->SpawnActor<AChunkedDestructibleConeStructure>(AChunkedDestructibleConeStructure::StaticClass(),FTransform::Identity,Spawn));
    CheckCone(World->SpawnActor<AQuadPanelConeStructure>(AQuadPanelConeStructure::StaticClass(),FTransform::Identity,Spawn));

    const FVector Origin(0,0,10000);
    auto* PlayerClass = LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    if (!TestNotNull(TEXT("Saved player class loads in a fresh process"),PlayerClass)) return false;
    auto* Player = World->SpawnActor<AMyProject6Character>(PlayerClass,Origin+FVector(180,0,0),FRotator::ZeroRotator);
    auto* NPC = World->SpawnActor<AChasingNPC>(Origin+FVector(-180,0,0),FRotator::ZeroRotator);
    auto* Follower = World->SpawnActor<AWhisperFollower>(Origin+FVector(0,180,0),FRotator::ZeroRotator);
    // An unpossessed test player has no movement mode until explicitly enabled.
    Player->GetCharacterMovement()->bRunPhysicsWithNoController = true;
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    Player->SetActorEnableCollision(false); NPC->SetActorEnableCollision(false); Follower->SetActorEnableCollision(false);
    const float Energy = Player->GetCurrentEnergy();
    auto* Shot = Bomb(Origin,300);
    Shot->BlastDamage = 10000; Shot->Explode();
    TestTrue(TEXT("Non-queryable player loses energy"),Player->GetCurrentEnergy()<Energy);
    TestTrue(TEXT("Non-queryable chasing NPC receives lethal damage"),NPC->IsDead());
    TestTrue(TEXT("Non-queryable follower receives lethal damage"),Follower->IsDead());
    TestTrue(TEXT("Non-queryable player is pushed outwards"),Player->GetCharacterMovement()->PendingLaunchVelocity.X>0);
    TestTrue(TEXT("Non-queryable NPC is pushed outwards"),NPC->GetCharacterMovement()->PendingLaunchVelocity.X<0);
    TestTrue(TEXT("Non-queryable follower is pushed outwards"),Follower->GetCharacterMovement()->PendingLaunchVelocity.Y>0);

    // Delayed destruction must retain the originating blast, after the bomb is gone.
    for (bool bRipple : {false,true})
    {
        const FVector HouseOrigin(0,bRipple ? 16000 : 8000,0);
        auto* House = World->SpawnActor<AChunkedDestructibleHouse>(HouseOrigin,FRotator::ZeroRotator);
        House->bGenerateDoor = false; House->bGenerateWindows = false;
        House->DebrisImpulse = 0; House->DebrisUpwardImpulse = 0; House->DebrisPiecesPerCell = 1;
        House->MaxActiveDebrisPieces = 1; House->MaxRuntimeDebrisPieces = 1;
        House->ConfigureHouse(FIntVector(8,8,6),2,1,FVector(50),FVector::ZeroVector,true);
        // Isolate the initial blast impulse from subsequent wall/debris ricochets.
        TInlineComponentArray<UDynamicMeshComponent*> HouseChunks(House);
        for (auto* Chunk : HouseChunks) Chunk->SetCollisionResponseToAllChannels(ECR_Ignore);
        FindFProperty<FBoolProperty>(House->GetClass(),TEXT("bUseDestructionRipple"))->SetPropertyValue_InContainer(House,bRipple);
        const FVector Blast = HouseOrigin+FVector(235,0,150);
        auto* DebrisShot = Bomb(Blast,90);
        DebrisShot->Explode(); DebrisShot->Destroy();
        for (int32 Frame=0;Frame<3;++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All,1.f/60); }
        TInlineComponentArray<UStaticMeshComponent*> Components(House);
        int32 Pushed = 0;
        for (auto* Component : Components)
        {
            if (!Component->IsSimulatingPhysics()) continue;
            const FVector Velocity = Component->GetPhysicsLinearVelocity();
            TestTrue(FString::Printf(TEXT("Ripple=%d debris %s at %s receives outward pressure: %s"),bRipple,
                *Component->GetName(),*Component->GetComponentLocation().ToCompactString(),*Velocity.ToCompactString()),Velocity.X < -100);
            ++Pushed;
        }
        TestTrue(bRipple ? TEXT("Ripple spawns debris with retained bomb pressure") : TEXT("Immediate destruction spawns pushed debris"),Pushed>0);
        House->Destroy();
    }
    return true;
}
#endif
