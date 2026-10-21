#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "BFGProjectile.h"
#include "BFGCameraShake.h"
#include "BFGTestShooterNPC.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBFGNPCPowerTest,"MyProject6.Weapons.BFGNPCPower",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FBFGNPCPowerTest::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->SetGameInstance(NewObject<UGameInstance>(GEngine));
    World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
    World->InitializeActorsForPlay(FURL());
    ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    World->BeginPlay();
    TArray<FString> Files;
    IFileManager::Get().FindFilesRecursive(Files,*(FPaths::ProjectContentDir()/TEXT("NPC")),TEXT("BP_*.uasset"),true,false);
    int32 VerifiedClasses=0;
    for (const FString& File:Files)
    {
        const FString Name=FPaths::GetBaseFilename(File);
        if (!Name.Contains(TEXT("NPC")) && !Name.Contains(TEXT("Follower"))) continue;
        const FString ClassPath=FPackageName::FilenameToLongPackageName(File)+TEXT(".")+Name+TEXT("_C");
        UClass* Class=LoadClass<ACharacter>(nullptr,*ClassPath);
        if (!TestNotNull(*ClassPath,Class)) continue;
        const bool bChasing=Class->IsChildOf(AChasingNPC::StaticClass());
        const bool bFollower=Class->IsChildOf(AWhisperFollower::StaticClass());
        if (!TestTrue(*(Name+TEXT(" uses a supported NPC health system")),bChasing||bFollower)) continue;
        for (bool bHighHealth:{false,true})
        {
            const FTransform Transform(FVector(1000,0,500));
            auto* NPC=World->SpawnActorDeferred<ACharacter>(Class,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if (!TestNotNull(*(Name+TEXT(" spawns")),NPC)) continue;
            if (auto* Replacements=FindFProperty<FBoolProperty>(Class,TEXT("bSpawnReplacementsOnDeath")))
                Replacements->SetPropertyValue_InContainer(NPC,false);
            if (bHighHealth) FindFProperty<FFloatProperty>(Class,TEXT("MaxHealth"))->SetPropertyValue_InContainer(NPC,25000.f);
            NPC->FinishSpawning(Transform);
            auto Health=[&]() { return bChasing?CastChecked<AChasingNPC>(NPC)->GetCurrentHealth():CastChecked<AWhisperFollower>(NPC)->GetCurrentHealth(); };
            const float Before=Health();
            TestTrue(TEXT("Live target starts with positive health"),Before>0);
            if (bHighHealth) TestEqual(TEXT("Instance health override is active"),Before,25000.f);
            auto* Shot=World->SpawnActor<ABFGProjectile>(FVector(800,0,500),FRotator::ZeroRotator);
            FHitResult Hit(NPC,NPC->GetCapsuleComponent(),FVector(950,0,500),-FVector::XAxisVector);
            Hit.bBlockingHit=true;
            Shot->Explode(Hit);
            TestEqual(*(Name+TEXT(" has zero health immediately after one hit")),Health(),0.f);
            TestTrue(*(Name+TEXT(" runs its normal death handler")),bChasing?CastChecked<AChasingNPC>(NPC)->IsDead():CastChecked<AWhisperFollower>(NPC)->IsDead());
            Shot->Explode(Hit);
            TestEqual(TEXT("Duplicate explosion cannot damage the corpse again"),Health(),0.f);
            AddInfo(FString::Printf(TEXT("%s: %.0f health -> %.0f in one BFG impact"),*Name,Before,Health()));
            Shot->Destroy();NPC->Destroy();
        }
        ++VerifiedClasses;
    }
    TestTrue(TEXT("All saved NPC families and skin variants were discovered"),VerifiedClasses>=23);
    AddInfo(FString::Printf(TEXT("Freshly loaded and verified %d saved NPC Blueprint classes"),VerifiedClasses));

    auto* Shooter=World->SpawnActor<ABFGTestShooterNPC>(FVector(1000,0,500),FRotator::ZeroRotator);
    Shooter->CurrentHP=25000;
    auto* Shot=World->SpawnActor<ABFGProjectile>(FVector(800,0,500),FRotator::ZeroRotator);
    FHitResult Hit(Shooter,Shooter->GetCapsuleComponent(),FVector(950,0,500),-FVector::XAxisVector);
    Hit.bBlockingHit=true;Shot->Explode(Hit);
    TestEqual(TEXT("Template shooter NPC also loses all 25000 health"),Shooter->CurrentHP,0.f);
    TestTrue(TEXT("Template shooter death handler runs"),Shooter->IsDeadForTest() && Shooter->ActorHasTag(TEXT("Dead")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBFGCameraShakeTest,"MyProject6.Weapons.BFGCameraShake",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FBFGCameraShakeTest::RunTest(const FString&)
{
    for (int32 FPS:{30,60,120})
    {
        UBFGCameraShake* Shake=NewObject<UBFGCameraShake>();
        Shake->StartShake(nullptr,1.f,ECameraShakePlaySpace::CameraLocal);
        float PeakPitch=0;
        for (int32 Frame=0;Frame<FPS;++Frame)
        {
            FMinimalViewInfo View;
            Shake->UpdateAndApplyCameraShake(1.f/FPS,1.f,View);
            PeakPitch=FMath::Max(PeakPitch,float(FMath::Abs(View.Rotation.Pitch)));
            TestTrue(TEXT("Camera impulse remains small and finite"),!View.Rotation.ContainsNaN()
                && View.Location.Size()<1.f && FMath::Abs(View.Rotation.Pitch)<1.1f);
            if (Frame>FPS/4) TestTrue(TEXT("Camera returns to rest after the discharge"),View.Location.IsNearlyZero() && View.Rotation.IsNearlyZero());
        }
        TestTrue(TEXT("Firing visibly shakes the rendered camera"),PeakPitch>.5f);
        TestTrue(TEXT("Shake finishes within the 300ms firing interval"),Shake->IsFinished());
        Shake->StartShake(nullptr,1.f,ECameraShakePlaySpace::CameraLocal);
        FMinimalViewInfo Restarted;
        Shake->UpdateAndApplyCameraShake(1.f/60,1.f,Restarted);
        TestTrue(TEXT("Reused single-instance shake restarts for later shots"),Restarted.Rotation.Pitch>.1f);
        Shake->StopShake(true);
        TestTrue(TEXT("An interrupted shake can stop immediately"),Shake->IsFinished());
        Shake->TeardownShake();
    }
    return true;
}
#endif
