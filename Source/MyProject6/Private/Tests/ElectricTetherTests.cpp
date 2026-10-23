#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElectricTetherWeapon.h"
#include "NPCElectricTetherComponent.h"
#include "NPCSleepComponent.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "MyProject6Character.h"
#include "RailgunWeapon.h"
#include "ParametricPickup.h"
#include "GameOverMenuTestController.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Materials/MaterialInterface.h"
#include "InputKeyEventArgs.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"
#include "SWeaponHUD.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"

namespace ArcTests
{
// Editor-only bridge for RenderElectricTetherLiveLevel.py. Exercise the live
// player's input stack (including physical-button tracking), not StartBeam().
static FAutoConsoleCommandWithWorldAndArgs LiveMouseInput(
    TEXT("MyProject6.Test.ArcMouse"),TEXT("PIE render test: down or up"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* World)
    {
        if(!World||World->WorldType!=EWorldType::PIE||Args.Num()!=1)return;
        if(Args[0]!=TEXT("down")&&Args[0]!=TEXT("up"))return;
        if(auto* PC=UGameplayStatics::GetPlayerController(World,0))
            PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,Args[0]==TEXT("down")?IE_Pressed:IE_Released,Args[0]==TEXT("down")?1.f:0.f));
    }));
struct FScene
{
    UWorld* World;
    FScene()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
        World->InitializeActorsForPlay(FURL());World->BeginPlay();Box(FVector(0,0,-50),FVector(10000,10000,50));
    }
    ~FScene(){World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
    void Advance(float Seconds,int FPS=60)
    {const int N=FMath::CeilToInt(Seconds*FPS);for(int I=0;I<N;++I){++GFrameCounter;World->Tick(LEVELTICK_All,Seconds/N);}}
    AActor* Box(FVector P,FVector Extent)
    {
        auto* A=World->SpawnActor<AActor>();auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);
        B->SetBoxExtent(Extent);B->SetCollisionProfileName(TEXT("BlockAll"));B->RegisterComponent();A->SetActorLocation(P);return A;
    }
    ACharacter* NPC(UClass* Class,FVector P=FVector(600,0,450),float Health=0)
    {
        const FTransform T(P);auto* N=World->SpawnActorDeferred<ACharacter>(Class,T,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        N->AutoPossessAI=EAutoPossessAI::Disabled;
        if(Health>0)if(auto* H=FindFProperty<FFloatProperty>(Class,TEXT("MaxHealth")))H->SetPropertyValue_InContainer(N,Health);
        if(auto* B=FindFProperty<FBoolProperty>(Class,TEXT("bSpawnReplacementsOnDeath")))B->SetPropertyValue_InContainer(N,false);
        N->FinishSpawning(T);N->GetCharacterMovement()->DisableMovement();
        N->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;return N;
    }
    AMyProject6Character* Player(FRotator SpawnRotation=FRotator::ZeroRotator)
    {
        auto* C=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
        auto* P=World->SpawnActor<AMyProject6Character>(C,FVector(0,0,400),SpawnRotation);
        auto* PC=World->SpawnActor<AGameOverMenuTestController>();PC->InitializeTestMenu();PC->Possess(P);P->PawnClientRestart();P->GetCharacterMovement()->DisableMovement();return P;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcVisualAlignmentTest,"MyProject6.ElectricTether.VisualAlignmentAndMouseInput",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FArcVisualAlignmentTest::RunTest(const FString&)
{
    // The shipping level's PlayerStart faces -180 degrees. Zero-yaw fixtures
    // conceal accidental double rotation of world-space electric vertices.
    for(float Yaw:{0.f,90.f,-180.f})
    {
        ArcTests::FScene S;auto* P=S.Player(FRotator(0,Yaw,0));P->EquipElectricTether();
        auto* PC=CastChecked<APlayerController>(P->GetController());PC->InitInputSystem();PC->SetControlRotation(FRotator(0,Yaw,0));S.Advance(.1f);
        auto* Gun=P->ElectricWeapon.Get();const FVector Eye=P->GetFirstPersonCameraComponent()->GetComponentLocation();
        auto* N=S.NPC(AChasingNPC::StaticClass(),Eye+PC->GetControlRotation().Vector()*650);
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f));S.Advance(.2f);
        TestTrue(TEXT("Left mouse input starts and sustains beam"),Gun->bFiring);
        TestTrue(TEXT("Left mouse input captures target"),Gun->HasTarget());
        TestTrue(TEXT("Electric field is visible"),Gun->Field->IsVisible());
        TestNotNull(TEXT("Saved electric material is assigned"),Gun->Field->GetMaterial(0));
        if(TestNotNull(TEXT("Saved plasma membrane material is assigned"),Gun->Field->GetMaterial(1)))
            TestEqual(TEXT("Captive remains visible through translucent membrane"),Gun->Field->GetMaterial(1)->GetBlendMode(),BLEND_Translucent);
        const FVector Muzzle=P->Railgun->GetVisualMuzzleLocation();double MuzzleError=DBL_MAX,TargetError=DBL_MAX;
        Gun->Field->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
        {
            TestTrue(TEXT("Rope and cage both have render geometry"),Mesh.TriangleCount()>900);
            int SurfaceTriangles=0;
            if(Mesh.Attributes()->HasMaterialID())for(int TID:Mesh.TriangleIndicesItr())if(Mesh.Attributes()->GetMaterialID()->GetValue(TID)==1)++SurfaceTriangles;
            TestTrue(TEXT("Captured NPC is enclosed by a continuous plasma surface"),SurfaceTriangles>1000);
            for(int VID:Mesh.VertexIndicesItr())
            {
                const FVector V=Gun->Field->GetComponentTransform().TransformPosition(Mesh.GetVertex(VID));
                MuzzleError=FMath::Min(MuzzleError,FVector::Distance(V,Muzzle));TargetError=FMath::Min(TargetError,FVector::Distance(V,N->GetActorLocation()));
            }
        });
        TestTrue(FString::Printf(TEXT("Rope starts at visible muzzle for spawn yaw %.0f (error %.1f cm)"),Yaw,MuzzleError),MuzzleError<5.);
        TestTrue(FString::Printf(TEXT("Rope reaches captive for spawn yaw %.0f (error %.1f cm)"),Yaw,TargetError),TargetError<5.);
        PC->SetControlRotation(FRotator(12,Yaw+35,0));S.Advance(.15f,20);
        double SwingError=DBL_MAX;
        Gun->Field->ProcessMesh([&](const UE::Geometry::FDynamicMesh3& Mesh)
        {
            for(int VID:Mesh.VertexIndicesItr())
                SwingError=FMath::Min(SwingError,FVector::Distance(Gun->Field->GetComponentTransform().TransformPosition(Mesh.GetVertex(VID)),N->GetActorLocation()));
        });
        TestTrue(TEXT("Low-frame-rate swing geometry follows the current NPC pose without a frame of lag"),SwingError<5.);
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Released,0.f));S.Advance(.2f);
        TestFalse(TEXT("Mouse release stops beam"),Gun->bFiring);TestFalse(TEXT("Mouse release removes electric mesh"),Gun->Field->IsVisible());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcAcquireTest,"MyProject6.ElectricTether.AcquisitionInventoryAndCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FArcAcquireTest::RunTest(const FString&)
{
    ArcTests::FScene S;auto* P=S.Player();auto* Gun=P->ElectricWeapon.Get();
    if(!TestNotNull(TEXT("Native weapon on saved player class"),Gun))return false;
    TestFalse(TEXT("Uncollected slot is locked"),P->SelectWeapon(EPlayerWeapon::ElectricTether));
    const FTransform PickupPose(P->GetActorLocation());
    auto* Pickup=S.World->SpawnActorDeferred<AParametricPickup>(AParametricPickup::StaticClass(),PickupPose);
    Pickup->WeaponReward=EPickupWeapon::ElectricTether;Pickup->Icon=EPickupMotif::ElectricTether;Pickup->FinishSpawning(PickupPose);S.Advance(.1f);
    TestTrue(TEXT("Overlap grants weapon"),Pickup->IsCollected());TestEqual(TEXT("Sixth slot selected"),P->GetActiveWeapon(),EPlayerWeapon::ElectricTether);
    TestNotNull(TEXT("Authored weapon mesh loaded"),P->Railgun->WeaponMesh->GetStaticMesh().Get());
    P->NextWeapon();TestEqual(TEXT("Wheel wraps from six to one"),P->GetActiveWeapon(),EPlayerWeapon::Default);P->PreviousWeapon();
    TestEqual(TEXT("Wheel wraps back to owned sixth slot"),P->GetActiveWeapon(),EPlayerWeapon::ElectricTether);
    const FVector Eye=P->GetFirstPersonCameraComponent()->GetComponentLocation();P->GetController()->SetControlRotation(FRotator::ZeroRotator);
    auto* N=S.NPC(AChasingNPC::StaticClass(),Eye+FVector(650,0,0));auto* T=N->FindComponentByClass<UNPCElectricTetherComponent>();
    auto* Wall=S.Box(Eye+FVector(300,0,0),FVector(10,250,350));Gun->StartBeam();S.Advance(.15f);
    TestFalse(TEXT("Cannot acquire through a wall"),Gun->HasTarget());Wall->Destroy();S.Advance(.2f);
    TestTrue(TEXT("Held beam acquires visible living NPC"),Gun->HasTarget());
    if(!GUsingNullRHI)
    {
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("ElectricTether/Preview");IFileManager::Get().MakeDirectory(*Dir,true);
        FWidgetRenderer Renderer(true);auto Widget=SNew(SWeaponHUD).Player(TWeakObjectPtr<AMyProject6Character>(P));
        auto* RT=Renderer.DrawWidget(Widget,FVector2D(330,142));FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
        if(RT&&RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))
        {TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(RT->SizeX,RT->SizeY,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/TEXT("HUD.png")));}
    }
    TestTrue(TEXT("AI is suppressed"),CastChecked<AChasingNPC>(N)->ShouldPauseChase());
    TestTrue(TEXT("Stationary capture has no throw momentum"),T->GetThrowVelocity().IsNearlyZero());
    Gun->StopBeam();TestFalse(TEXT("Mouse release immediately removes cage"),T->IsCaptured());
    S.Advance(2.f);TestFalse(TEXT("Dropped target recovers"),T->IsControlled());
    N->SetActorLocation(Eye+FVector(650,0,0));Gun->StartBeam();S.Advance(.1f);TestTrue(TEXT("Target can be recaptured"),Gun->HasTarget());
    P->SelectWeapon(EPlayerWeapon::Default);TestFalse(TEXT("Switching releases target"),T->IsCaptured());TestFalse(TEXT("Switching stops beam"),Gun->bFiring);
    P->SelectWeapon(EPlayerWeapon::ElectricTether);N->SetActorLocation(Eye+FVector(650,0,0));Gun->StartBeam();S.Advance(.1f);
    P->DrainEnergy(1000);TestFalse(TEXT("Player death releases target"),T->IsCaptured());TestFalse(TEXT("Death stops beam"),Gun->bFiring);UGameplayStatics::SetGamePaused(S.World,false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcSlamTest,"MyProject6.ElectricTether.SlamThrowAndDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FArcSlamTest::RunTest(const FString&)
{
    ArcTests::FScene S;auto* Source=S.World->SpawnActor<AActor>();auto* N=CastChecked<AChasingNPC>(S.NPC(AChasingNPC::StaticClass(),FVector(600,0,450),1000));
    auto* T=N->FindComponentByClass<UNPCElectricTetherComponent>();const float StartHealth=N->GetCurrentHealth();
    TestTrue(TEXT("Capture starts"),T->Capture(Source));T->SetHoldPoint(FVector(600,0,450));S.Advance(.5f);
    TestEqual(TEXT("Holding alone never damages"),N->GetCurrentHealth(),StartHealth);
    const FTransform WristA=N->GetMesh()->GetSocketTransform(TEXT("hand_r"));S.Advance(.19f);
    const FTransform WristB=N->GetMesh()->GetSocketTransform(TEXT("hand_r"));
    TestFalse(TEXT("Live skeleton struggles inside stationary cage"),WristA.Equals(WristB,.01f));
    S.Box(FVector(600,650,400),FVector(1000,20,700));T->SetHoldPoint(FVector(600,1100,450));S.Advance(.45f);
    TestTrue(TEXT("Wall collision damages NPC"),N->GetCurrentHealth()<StartHealth);TestTrue(TEXT("Swept capsule cannot tunnel through wall"),N->GetActorLocation().Y<650);
    const float AfterSlam=N->GetCurrentHealth();S.Advance(.7f);TestEqual(TEXT("Pressing against wall does not farm impact damage"),N->GetCurrentHealth(),AfterSlam);
    T->SetHoldPoint(FVector(600,-650,650));S.Advance(.18f);const FVector Expected=T->GetThrowVelocity();
    TestTrue(TEXT("Reverse swing has directional momentum"),Expected.Y < -300);const FVector ReleasePoint=N->GetActorLocation();T->Release();
    TestTrue(TEXT("Release keeps measured swing velocity"),T->MotionVelocity.Equals(Expected,.1f));S.Advance(.12f);
    TestTrue(TEXT("Released NPC flies in swing direction"),N->GetActorLocation().Y<ReleasePoint.Y-50);
    S.Advance(4.f);TestFalse(TEXT("Survivor exits thrown state after landing"),T->IsControlled());
    if(!N->IsDead())
    {
        TestTrue(TEXT("Reacquire after recovery"),T->Capture(Source));T->SetHoldPoint(N->GetActorLocation()+FVector(0,0,-1200));S.Advance(.5f);
        TestTrue(TEXT("Floor collision is an impact too"),T->ImpactCount>=2);
        UGameplayStatics::ApplyDamage(N,10000,nullptr,Source,nullptr);TestFalse(TEXT("Death clears control state"),T->IsControlled());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcCoverageTest,"MyProject6.ElectricTether.AllNPCClassesAndSleep",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FArcCoverageTest::RunTest(const FString&)
{
    const TCHAR* Paths[]={TEXT("/Script/MyProject6.ChasingNPC"),TEXT("/Game/NPC/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"),
    TEXT("/Game/NPC/BlackMonster/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C"),TEXT("/Game/NPC/ClownMonster/BP_ClownMonster_NPC.BP_ClownMonster_NPC_C"),
    TEXT("/Game/NPC/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C"),TEXT("/Game/NPC/DemonImp/BP_DemonImp_NPC.BP_DemonImp_NPC_C"),
    TEXT("/Game/NPC/DarkSorcerer/BP_DarkSorcerer_NPC.BP_DarkSorcerer_NPC_C"),TEXT("/Game/NPC/ParagonCrunch/BP_Crunch_NPC.BP_Crunch_NPC_C"),
    TEXT("/Game/NPC/ParagonFengMao/BP_FengMao_NPC.BP_FengMao_NPC_C"),TEXT("/Game/NPC/ParagonGRIMexe/BP_GRIMexe_NPC.BP_GRIMexe_NPC_C"),TEXT("/Game/NPC/ParagonSteel/BP_Steel_NPC.BP_Steel_NPC_C")};
    for(const TCHAR* Path:Paths)
    {
        UClass* C=LoadClass<ACharacter>(nullptr,Path);if(!TestNotNull(Path,C))continue;
        ArcTests::FScene S;auto* Source=S.World->SpawnActor<AActor>();auto* N=S.NPC(C);const FTransform Rest=N->GetMesh()->GetRelativeTransform();
        auto* T=N->FindComponentByClass<UNPCElectricTetherComponent>();if(!TestNotNull(TEXT("Inherited native component"),T))continue;
        auto* Sleep=N->FindComponentByClass<UNPCSleepComponent>();Sleep->ReceiveSignal(Source,5);S.Advance(.15f);
        TestTrue(TEXT("Capture interrupts sleep"),T->Capture(Source));TestFalse(TEXT("Sleep pose cleared"),Sleep->IsIncapacitated());
        Sleep->ReceiveSignal(Source,5);S.Advance(.2f);TestFalse(TEXT("Sleep field cannot steal captive movement"),Sleep->IsIncapacitated());
        TestTrue(TEXT("Shared skeleton pose active"),T->PoseAlpha>.9f);
        Source->Destroy();S.Advance(3.f);TestFalse(TEXT("Lost weapon releases and recovers"),T->IsControlled());
        TestTrue(TEXT("Original mesh transform restored"),N->GetMesh()->GetRelativeTransform().Equals(Rest,.1f));
        TestFalse(TEXT("Player is never a valid target"),UNPCElectricTetherComponent::IsLivingNPC(S.Player()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArcFrameRateTest,"MyProject6.ElectricTether.FrameRateIndependence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FArcFrameRateTest::RunTest(const FString&)
{
    FVector Baseline=FVector::ZeroVector;
    for(int FPS:{30,60,120})
    {
        ArcTests::FScene S;auto* Source=S.World->SpawnActor<AActor>();auto* N=S.NPC(AChasingNPC::StaticClass());auto* T=N->FindComponentByClass<UNPCElectricTetherComponent>();T->Capture(Source);
        T->SetHoldPoint(FVector(600,500,600));S.Advance(.3f,FPS);const FVector V=T->GetThrowVelocity();
        if(FPS==30)Baseline=V;else TestTrue(TEXT("Substep solver preserves release velocity across frame rates"),V.Equals(Baseline,5.f));
    }
    return true;
}
#endif

