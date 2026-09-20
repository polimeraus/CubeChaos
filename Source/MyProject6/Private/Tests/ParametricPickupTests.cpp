#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ParametricPickup.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPickupTest, "MyProject6.Gameplay.Pickup.GeometryMaterialAndCollection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPickupTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    auto Advance = [World](float Seconds)
    {
        for (int32 I = 0; I < FMath::CeilToInt(Seconds * 60); ++I)
        {
            ++GFrameCounter;
            World->Tick(LEVELTICK_All, 1.f / 60);
        }
    };
    AParametricPickup* Pickup = World->SpawnActor<AParametricPickup>();
    Pickup->bAutoCollect = false;
    TestTrue(TEXT("New pickups default to stationary"), Pickup->Motion == EPickupMotion::Stationary);
    TestFalse(TEXT("Stationary pickups have no actor tick"), Pickup->IsActorTickEnabled());
    if (TestNotNull(TEXT("Default pickup sound is assigned"), Pickup->CollectionSound.Get()))
    {
        TestTrue(TEXT("Default sound is a finite one-shot"), Pickup->CollectionSound->GetDuration() > 0
            && Pickup->CollectionSound->GetDuration() < 5 && !Pickup->CollectionSound->IsLooping());
    }
    auto* Body = Pickup->FindComponentByClass<UDynamicMeshComponent>();
    for (EPickupShape Shape : {EPickupShape::Cube, EPickupShape::Sphere, EPickupShape::RoundedCube, EPickupShape::Pyramid})
    {
        Pickup->Shape = Shape;
        Pickup->Size = 120;
        Pickup->CornerRadius = 25;
        Pickup->RefreshPickup();
        const auto* Mesh = Body->GetMesh();
        TestTrue(TEXT("Shape has valid triangles and attribute overlays"), Mesh->CheckValidity());
        const auto Bounds = Mesh->GetBounds();
        TestNearlyEqual(TEXT("Parametric width"), Bounds.Max.X - Bounds.Min.X, 120., .01);
        TestNearlyEqual(TEXT("Parametric depth"), Bounds.Max.Y - Bounds.Min.Y, 120., .01);
        TestNearlyEqual(TEXT("Parametric height"), Bounds.Max.Z - Bounds.Min.Z, 120., .01);
        TestNearlyEqual(TEXT("Bottom pivot"), Bounds.Min.Z, 0., .01);
        bool bOutward = true;
        bool bUVs = true;
        for (int32 ID : Mesh->TriangleIndicesItr())
        {
            const FVector3d FromCentre = Mesh->GetTriCentroid(ID) - FVector3d(0, 0, 60);
            bOutward &= Mesh->GetTriNormal(ID).Dot(FromCentre) > 0;
            bUVs &= Mesh->Attributes()->PrimaryUV()->IsSetTriangle(ID);
        }
        TestTrue(TEXT("Every triangle faces outwards"), bOutward);
        TestTrue(TEXT("Every face has icon UVs"), bUVs);
        TestTrue(TEXT("Body does not block the collector"), Body->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    }

    auto* Second = World->SpawnActor<AParametricPickup>(FVector(1000, 0, 0), FRotator::ZeroRotator);
    Second->bAutoCollect = false;
    if (TestNotNull(TEXT("Saved default material loads on native actor"), Pickup->GetDynamicMaterial())
        && TestNotNull(TEXT("Second actor has its own material"), Second->GetDynamicMaterial()))
    {
        TestTrue(TEXT("Actors do not share dynamic material instances"), Pickup->GetDynamicMaterial() != Second->GetDynamicMaterial());
        Pickup->Icon = EPickupMotif::Rocket;
        Pickup->IconColor = FLinearColor::Red;
        Pickup->PulsePeriod = 3.5f;
        Pickup->RefreshMaterial();
        TestNearlyEqual(TEXT("Rocket selected at runtime"), Pickup->GetDynamicMaterial()->K2_GetScalarParameterValue(TEXT("Motif")), 4.f);
        TestNearlyEqual(TEXT("Other pickup stays a star"), Second->GetDynamicMaterial()->K2_GetScalarParameterValue(TEXT("Motif")), 0.f);
        TestNearlyEqual(TEXT("Period is in seconds"), Pickup->GetDynamicMaterial()->K2_GetScalarParameterValue(TEXT("PulsePeriod")), 3.5f);
        TestTrue(TEXT("Independent icon tint"), Pickup->GetDynamicMaterial()->K2_GetVectorParameterValue(TEXT("IconColor")).Equals(FLinearColor::Red));
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACharacter* Character = World->SpawnActor<ACharacter>(FVector(400, 0, 60), FRotator::ZeroRotator, Params);
    Character->GetCharacterMovement()->DisableMovement();
    Character->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    TestFalse(TEXT("Distant pawn cannot collect"), Pickup->TryCollect(Character));
    TestFalse(TEXT("Null pawn cannot collect"), Pickup->TryCollect(nullptr));
    Character->SetActorLocation(FVector(0, 0, 60));
    Advance(.04f);
    TestFalse(TEXT("Auto collection can be disabled"), Pickup->IsCollected());
    TestFalse(TEXT("NPC excluded by player-only filter"), Pickup->TryCollect(Character));
    APlayerController* Controller = World->SpawnActor<APlayerController>();
    Controller->Possess(Character);
    TestTrue(TEXT("Overlapping player collects"), Pickup->TryCollect(Character));
    TestFalse(TEXT("Repeated contact cannot collect twice"), Pickup->TryCollect(Character));
    TestFalse(TEXT("Collected mesh is hidden"), Body->IsVisible());
    TestFalse(TEXT("Collection stops actor animation"), Pickup->IsActorTickEnabled());
    TestTrue(TEXT("Collected trigger is disabled"), Pickup->FindComponentByClass<UBoxComponent>()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

    Second->bAutoCollect = true;
    Character->SetActorLocation(FVector(1000, 0, 60));
    Advance(.04f);
    TestTrue(TEXT("Player contact automatically collects"), Second->IsCollected());

    auto* Initial = World->SpawnActor<AParametricPickup>(FVector(1000, 0, 0), FRotator::ZeroRotator);
    Advance(.04f);
    TestTrue(TEXT("Spawned around an existing player is collectable"), Initial->IsCollected());

    TWeakObjectPtr<AParametricPickup> Collected = Pickup;
    Advance(.3f);
    TestFalse(TEXT("Collected actor is removed after replication window"), Collected.IsValid());
    World->EndPlay(EEndPlayReason::Quit);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FParametricPickupMotionTest, "MyProject6.Gameplay.Pickup.MotionAndMovingContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FParametricPickupMotionTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr, TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"), TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    auto* Pickup = World->SpawnActor<AParametricPickup>(FVector(100, 200, 300), FRotator(0, 35, 0));
    Pickup->bAutoCollect = false;
    Pickup->CollectionSound = nullptr;
    auto* Body = Pickup->FindComponentByClass<UDynamicMeshComponent>();
    auto* Trigger = Pickup->FindComponentByClass<UBoxComponent>();
    const FVector Anchor = Pickup->GetActorLocation();
    auto Centre = [&]() { return Body->GetComponentTransform().TransformPosition(FVector(0, 0, Pickup->Size * .5)); };
    const FVector RestCentre = Centre();
    Pickup->Motion = EPickupMotion::Spin;
    Pickup->SpinAxis = EPickupSpinAxis::X;
    Pickup->SpinSpeed = 90;
    Pickup->RefreshMotion();
    Pickup->Tick(1);
    TestNearlyEqual(TEXT("Horizontal-axis rotation stays about mesh centre"), Centre(), RestCentre, .001f);
    TestNearlyEqual(TEXT("Rotation follows chosen axis and speed"), Body->GetAttachParent()->GetRelativeRotation().RotateVector(FVector::UpVector), FVector(0, -1, 0), .001f);
    TestNearlyEqual(TEXT("Trigger follows spinning centre"), Trigger->GetComponentLocation(), Centre(), .001f);
    Pickup->SpinSpeed = -90;
    Pickup->RefreshMotion();
    Pickup->Tick(1);
    TestNearlyEqual(TEXT("Negative speed reverses rotation"), Body->GetAttachParent()->GetRelativeRotation().RotateVector(FVector::UpVector), FVector(0, 1, 0), .001f);

    Pickup->Motion = EPickupMotion::Bob;
    Pickup->BobHeight = 80;
    Pickup->BobPeriod = 4;
    Pickup->RefreshMotion();
    TestNearlyEqual(TEXT("Bobbing starts at placed height"), Centre(), RestCentre, .001f);
    Pickup->Tick(.01f);
    const double SlowTravel = Centre().Z - RestCentre.Z;
    TestTrue(TEXT("Smooth low-end acceleration"), SlowTravel > 0 && SlowTravel < .01);
    Pickup->RefreshMotion();
    Pickup->Tick(1);
    TestNearlyEqual(TEXT("Quarter-period reaches middle height"), Centre().Z, RestCentre.Z + 40, .001);
    Pickup->Tick(1);
    TestNearlyEqual(TEXT("Half-period reaches upper turning point"), Centre().Z, RestCentre.Z + 80, .001);
    Pickup->Tick(2);
    TestNearlyEqual(TEXT("One full period returns without drift"), Centre(), RestCentre, .001f);
    TestNearlyEqual(TEXT("Placed anchor never changes"), Pickup->GetActorLocation(), Anchor, .001f);

    Pickup->Motion = EPickupMotion::SpinAndBob;
    Pickup->SpinAxis = EPickupSpinAxis::Z;
    Pickup->SpinSpeed = 90;
    Pickup->RefreshMotion();
    Pickup->Tick(1);
    TestNearlyEqual(TEXT("Combined animation bobs"), Centre().Z, RestCentre.Z + 40, .001);
    TestNearlyEqual(TEXT("Combined animation spins"), Body->GetAttachParent()->GetRelativeRotation().Yaw, 90., .001);
    const FTransform OneStep = Body->GetComponentTransform();
    Pickup->RefreshMotion();
    for (int32 I = 0; I < 100; ++I) Pickup->Tick(.01f);
    TestNearlyEqual(TEXT("Motion is independent of frame subdivision"), Body->GetComponentTransform(), OneStep, .001f);

    for (EPickupMotion Mode : {EPickupMotion::Orbit, EPickupMotion::OrbitAndSpin})
    {
        Pickup->Motion = Mode;
        Pickup->OrbitRadius = 200;
        Pickup->OrbitPeriod = 4;
        Pickup->RefreshMotion();
        Pickup->Tick(1);
        const FVector LocalCentre = Pickup->GetActorTransform().InverseTransformPosition(Centre());
        TestNearlyEqual(TEXT("Orbit keeps requested radius"), FVector2D(LocalCentre.X, LocalCentre.Y).Size(), 200., .001);
        TestNearlyEqual(TEXT("Quarter-orbit position"), LocalCentre, FVector(0, 200, Pickup->Size * .5), .001f);
        TestNearlyEqual(TEXT("Orbit trigger follows visible mesh"), Trigger->GetComponentLocation(), Centre(), .001f);
    }

    // Moving the trigger, rather than merely the visible body, must generate collection.
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACharacter* Character = World->SpawnActor<ACharacter>(Anchor + FVector(0, 0, 30), FRotator::ZeroRotator, Params);
    Character->GetCharacterMovement()->DisableMovement();
    Character->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    Pickup->bPlayerOnly = false;
    TestFalse(TEXT("Original anchor cannot collect an orbiting distant pickup"), Pickup->TryCollect(Character));
    Pickup->Motion = EPickupMotion::Bob;
    Pickup->BobHeight = 250;
    Pickup->BobPeriod = 2;
    Pickup->RefreshMotion();
    Character->SetActorLocation(RestCentre + FVector(0, 0, 250));
    TestFalse(TEXT("Character starts outside resting trigger"), Trigger->IsOverlappingActor(Character));
    Pickup->bAutoCollect = true;
    Pickup->Tick(1);
    TestTrue(TEXT("Animation moving into character automatically collects"), Pickup->IsCollected());
    TestFalse(TEXT("Collected moving pickup stops ticking"), Pickup->IsActorTickEnabled());

    auto* Stationary = World->SpawnActor<AParametricPickup>(FVector(1000, 0, 0), FRotator::ZeroRotator);
    Stationary->Motion = EPickupMotion::SpinAndBob;
    Stationary->RefreshMotion();
    Stationary->Tick(10);
    Stationary->Motion = EPickupMotion::Stationary;
    Stationary->RefreshMotion();
    TestFalse(TEXT("Returning to stationary disables ticking"), Stationary->IsActorTickEnabled());
    auto* StillBody = Stationary->FindComponentByClass<UDynamicMeshComponent>();
    TestNearlyEqual(TEXT("Stationary restores original placed transform"), StillBody->GetComponentTransform(), Stationary->GetActorTransform(), .001f);

    World->EndPlay(EEndPlayReason::Quit);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
