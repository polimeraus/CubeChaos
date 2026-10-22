#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "NPCSleepComponent.h"
#include "SleepSignalDevice.h"
#include "BombProjectile.h"
#include "BallProjectile.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "MyProject6Character.h"
#include "RailgunWeapon.h"
#include "ParametricPickup.h"
#include "NPCHitReactionComponent.h"
#include "GameOverMenuTestController.h"
#include "EnergyHUDWidget.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"

namespace SleepTests
{
struct FScene
{
    UWorld* World;
    FScene()
    {
        World=UWorld::CreateWorld(EWorldType::Game,false);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->SetGameInstance(NewObject<UGameInstance>(GEngine));
        World->SetGameMode(FURL(nullptr,TEXT("/Engine/Maps/Entry?game=/Script/Engine.GameModeBase"),TRAVEL_Absolute));
        World->InitializeActorsForPlay(FURL());World->BeginPlay();
        auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);
        Box->SetBoxExtent(FVector(10000,10000,50));Box->SetCollisionProfileName(TEXT("BlockAll"));Box->RegisterComponent();Floor->SetActorLocation(FVector(0,0,-50));
    }
    ~FScene(){World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
    void Advance(float Seconds,int32 FPS=60)
    {
        int32 N=FMath::CeilToInt(Seconds*FPS);
        for(int32 I=0;I<N;++I){++GFrameCounter;World->Tick(LEVELTICK_All,Seconds/N);}
    }
    ACharacter* NPC(UClass* Class,FVector Location=FVector(0,0,150))
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* N=World->SpawnActor<ACharacter>(Class,Location,FRotator::ZeroRotator,P);
        if(auto* B=FindFProperty<FBoolProperty>(Class,TEXT("bSpawnReplacementsOnDeath")))B->SetPropertyValue_InContainer(N,false);
        N->GetCharacterMovement()->bRunPhysicsWithNoController=true;
        N->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        N->GetMesh()->bEnableUpdateRateOptimizations=false;
        return N;
    }
    ASleepSignalDevice* Device(FVector Location,float Diameter=1000,float Duration=3)
    {
        const FTransform T(Location);
        auto* D=World->SpawnActorDeferred<ASleepSignalDevice>(ASleepSignalDevice::StaticClass(),T);
        D->SignalSphereDiameter=Diameter;D->SignalDuration=Duration;D->WakeDelay=.4f;D->LaunchVelocity=FVector::ZeroVector;D->FinishSpawning(T);return D;
    }
};
float Health(ACharacter* NPC)
{
    if(auto* N=Cast<AChasingNPC>(NPC))return N->GetCurrentHealth();
    return CastChecked<AWhisperFollower>(NPC)->GetCurrentHealth();
}
void Export(ACharacter* NPC,const TCHAR* Stage)
{
    const FString Dir=FPaths::ProjectSavedDir()/TEXT("SleepSignal/Poses");IFileManager::Get().MakeDirectory(*Dir,true);
    FString CSV=NPC->GetClass()->GetPathName()+TEXT("\n");auto* Mesh=NPC->GetMesh();
    for(int32 I=0;I<Mesh->GetNumBones();++I)
    {
        const FName Name=Mesh->GetBoneName(I);const FTransform T=Mesh->GetSocketTransform(Name,RTS_World);
        const FVector P=T.GetLocation()-FVector(NPC->GetActorLocation().X,NPC->GetActorLocation().Y,0),S=T.GetScale3D();const FRotator R=T.Rotator();
        CSV+=FString::Printf(TEXT("%s,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f,%.7f\n"),*Name.ToString(),P.X,P.Y,P.Z,R.Pitch,R.Yaw,R.Roll,S.X,S.Y,S.Z);
    }
    FFileHelper::SaveStringToFile(CSV,*(Dir/(NPC->GetClass()->GetName()+TEXT("_")+Stage+TEXT(".csv"))));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNPCSleepCycleTest,"MyProject6.SleepSignal.NPCsSleepAndWake",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FNPCSleepCycleTest::RunTest(const FString&)
{
    const TCHAR* Paths[]={TEXT("/Script/MyProject6.ChasingNPC"),
        TEXT("/Game/NPC/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"),
        TEXT("/Game/NPC/BlackMonster/BP_BlackMonster_Follower.BP_BlackMonster_Follower_C"),
        TEXT("/Game/NPC/ClownMonster/BP_ClownMonster_NPC.BP_ClownMonster_NPC_C"),
        TEXT("/Game/NPC/RadicalMike/BP_RadicalMike_NPC.BP_RadicalMike_NPC_C"),
        TEXT("/Game/NPC/DemonImp/BP_DemonImp_NPC.BP_DemonImp_NPC_C"),
        TEXT("/Game/NPC/DarkSorcerer/BP_DarkSorcerer_NPC.BP_DarkSorcerer_NPC_C"),
        TEXT("/Game/NPC/ParagonCrunch/BP_Crunch_NPC.BP_Crunch_NPC_C"),
        TEXT("/Game/NPC/ParagonFengMao/BP_FengMao_NPC.BP_FengMao_NPC_C"),
        TEXT("/Game/NPC/ParagonGRIMexe/BP_GRIMexe_NPC.BP_GRIMexe_NPC_C"),
        TEXT("/Game/NPC/ParagonSteel/BP_Steel_NPC.BP_Steel_NPC_C")};
    for(const TCHAR* Path:Paths)
    {
        UClass* Class=LoadClass<ACharacter>(nullptr,Path);if(!TestNotNull(TEXT("Saved NPC class"),Class))return false;
        for(int32 FPS:{30,120})
        {
            SleepTests::FScene Scene;auto* NPC=Scene.NPC(Class);Scene.Advance(.8f,FPS);
            auto* Sleep=NPC->FindComponentByClass<UNPCSleepComponent>();if(!TestNotNull(TEXT("New NPC inherits sleep component"),Sleep))return false;
            const FTransform Rest=NPC->GetMesh()->GetRelativeTransform();const FVector Location=NPC->GetActorLocation();
            const float Health=SleepTests::Health(NPC);auto* Source=Scene.World->SpawnActor<AActor>();
            SleepTests::Export(NPC,TEXT("Awake"));Sleep->ReceiveSignal(Source,3.f);
            Scene.Advance(.4f,FPS);TestEqual(TEXT("Signal starts drowsiness"),Sleep->State,ENPCSleepState::Drowsy);SleepTests::Export(NPC,TEXT("Drowsy"));
            Scene.Advance(.85f,FPS);TestEqual(TEXT("Drowsiness progresses to fall"),Sleep->State,ENPCSleepState::FallingAsleep);SleepTests::Export(NPC,TEXT("Falling"));
            Scene.Advance(.65f,FPS);TestEqual(TEXT("NPC lies asleep"),Sleep->State,ENPCSleepState::Sleeping);SleepTests::Export(NPC,TEXT("Sleeping"));
            TestTrue(TEXT("Body rotates into a grounded sleeping pose"),!NPC->GetMesh()->GetRelativeTransform().Equals(Rest,.1f));
            TestTrue(TEXT("Sleep leaves the actor in place"),NPC->GetActorLocation().Equals(Location,1.f));
            TestEqual(TEXT("Sleep never consumes NPC energy"),SleepTests::Health(NPC),Health);
            TestTrue(TEXT("Sleep pauses intent while preserving impulse physics"),Sleep->IsIncapacitated() && NPC->GetCharacterMovement()->MovementMode!=MOVE_None);
            Scene.Advance(1.6f,FPS);TestEqual(TEXT("Signal expiration starts get-up"),Sleep->State,ENPCSleepState::Waking);SleepTests::Export(NPC,TEXT("Waking"));
            Scene.Advance(.65f,FPS);SleepTests::Export(NPC,TEXT("WakeHands"));
            Scene.Advance(.63f,FPS);SleepTests::Export(NPC,TEXT("WakeKneel"));
            Scene.Advance(.64f,FPS);SleepTests::Export(NPC,TEXT("WakeRise"));
            Scene.Advance(1.f,FPS);TestEqual(TEXT("NPC wakes"),Sleep->State,ENPCSleepState::Awake);SleepTests::Export(NPC,TEXT("Recovered"));
            TestTrue(TEXT("Get-up restores exact mesh transform and scale"),NPC->GetMesh()->GetRelativeTransform().Equals(Rest,.001f));
            TestTrue(TEXT("Movement restored"),NPC->GetCharacterMovement()->MovementMode!=MOVE_None);
            TestEqual(TEXT("Full cycle preserves health"),SleepTests::Health(NPC),Health);
            Sleep->ReceiveSignal(Source,3);Scene.Advance(2,FPS);
            UGameplayStatics::ApplyDamage(NPC,100000,nullptr,nullptr,nullptr);Scene.Advance(.1f,FPS);
            TestFalse(TEXT("Death cancels sleep without resurrection"),Sleep->IsIncapacitated());
        }
        AddInfo(FString::Printf(TEXT("Sleep cycle validated: %s at 30/120 FPS"),*Class->GetName()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSleepDeviceFieldTest,"MyProject6.SleepSignal.FieldAndMultipleSources",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSleepDeviceFieldTest::RunTest(const FString&)
{
    SleepTests::FScene Scene;
    auto* Inside=Scene.NPC(AChasingNPC::StaticClass(),FVector(160,0,130));
    auto* Outside=Scene.NPC(AChasingNPC::StaticClass(),FVector(600,0,130));
    auto* Above=Scene.NPC(AChasingNPC::StaticClass(),FVector(0,0,800));Above->GetCharacterMovement()->DisableMovement();
    Scene.Advance(.7f);
    auto* D=Scene.Device(FVector(0,0,60),800,3);
    TestFalse(TEXT("Airborne device does not emit"),D->bEmitting);Scene.Advance(.6f);
    TestTrue(TEXT("Contact with ground starts signal"),D->bEmitting);
    const FVector Planted=D->GetActorLocation();const float Health=SleepTests::Health(Inside);
    auto* S=Inside->FindComponentByClass<UNPCSleepComponent>();
    TestTrue(TEXT("NPC inside sphere receives signal"),S->IsIncapacitated());
    TestFalse(TEXT("NPC outside sphere stays awake"),Outside->FindComponentByClass<UNPCSleepComponent>()->IsIncapacitated());
    TestFalse(TEXT("Sphere excludes an NPC far above it"),Above->FindComponentByClass<UNPCSleepComponent>()->IsIncapacitated());
    Outside->SetActorLocation(FVector(250,100,100));Scene.Advance(.2f);
    TestTrue(TEXT("Entering an already active field also causes sleep"),Outside->FindComponentByClass<UNPCSleepComponent>()->IsIncapacitated());
    auto* Second=Scene.Device(FVector(0,100,60),800,6);Scene.Advance(3.f);
    TestTrue(TEXT("Second source keeps NPC asleep after first expires"),S->State==ENPCSleepState::Sleeping);
    TestTrue(TEXT("Device stays planted"),D->IsActorBeingDestroyed()||D->GetActorLocation().Equals(Planted,.1f));
    TestEqual(TEXT("Repeated broadcasts never apply damage"),SleepTests::Health(Inside),Health);
    Second->Destroy();Scene.Advance(S->WakeDuration+.2f);
    TestEqual(TEXT("Destroying last emitter still allows recovery"),S->State,ENPCSleepState::Awake);
    auto* Source=Scene.World->SpawnActor<AActor>();S->ReceiveSignal(Source,2);Scene.Advance(2.4f);
    TestEqual(TEXT("Recovery has started"),S->State,ENPCSleepState::Waking);
    const FTransform Before=Inside->GetMesh()->GetRelativeTransform();S->ReceiveSignal(Source,2);Scene.Advance(.016f);
    TestTrue(TEXT("Renewed signal never snaps the mesh upright"),FVector::Dist(Before.GetLocation(),Inside->GetMesh()->GetRelativeLocation())<12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSleepDeviceInventoryTest,"MyProject6.SleepSignal.PickupThrowAndHUD",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSleepDeviceInventoryTest::RunTest(const FString&)
{
    SleepTests::FScene Scene;
    auto* Class=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
    auto* PickupClass=LoadClass<AParametricPickup>(nullptr,TEXT("/Game/GameplayActors/Pickups/SleepSignal/BP_SleepSignalPickup.BP_SleepSignalPickup_C"));
    if(!TestNotNull(TEXT("Player Blueprint"),Class)||!TestNotNull(TEXT("Saved pickup Blueprint"),PickupClass))return false;
    auto* Player=Scene.World->SpawnActor<AMyProject6Character>(Class,FVector(0,0,100),FRotator::ZeroRotator);
    auto* Controller=Scene.World->SpawnActor<AGameOverMenuTestController>();Controller->InitializeTestMenu();Controller->Possess(Player);Player->PawnClientRestart();
    Player->GetCharacterMovement()->DisableMovement();auto* Gun=Player->Railgun.Get();
    TestFalse(TEXT("Weapon starts locked"),Player->HasWeapon(EPlayerWeapon::SleepSignal));TestFalse(TEXT("Locked device cannot fire"),Gun->FireSleepSignal());
    auto* Pickup=Scene.World->SpawnActor<AParametricPickup>(PickupClass,FVector(500,0,100),FRotator::ZeroRotator);
    Player->SetActorLocation(FVector(500,0,130));Scene.Advance(.1f);
    TestTrue(TEXT("Real pickup overlap grants weapon"),Pickup->IsCollected()&&Player->HasWeapon(EPlayerWeapon::SleepSignal));
    TestEqual(TEXT("Pickup immediately selects new HUD slot"),Player->GetActiveWeapon(),EPlayerWeapon::SleepSignal);
    TestEqual(TEXT("Running first-person hold asset"),Gun->Arms->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(),FName(TEXT("A_SleepSignalHold")));
    Player->SetActorLocation(FVector(0,0,100));Controller->SetControlRotation(FRotator(75,0,0));
    const FVector HeldWrist=Gun->Arms->GetSocketTransform(TEXT("hand_r"),RTS_Component).GetLocation();
    TestTrue(TEXT("Throw begins"),Gun->FireSleepSignal());TestFalse(TEXT("Repeated input cannot duplicate throw"),Gun->FireSleepSignal());
    Scene.Advance(.2f);TestEqual(TEXT("Device stays in hand before release"),Gun->SleepDevicesThrown,0);
    TestTrue(TEXT("Authored throw actually moves the wrist bones"),FVector::Dist(HeldWrist,Gun->Arms->GetSocketTransform(TEXT("hand_r"),RTS_Component).GetLocation())>5.f);
    TestEqual(TEXT("Throw plays actual skeletal animation"),Gun->Arms->GetSingleNodeInstance()->GetCurrentAsset()->GetFName(),FName(TEXT("A_SleepSignalThrow")));
    Scene.Advance(.15f);TestEqual(TEXT("Release frame spawns one device"),Gun->SleepDevicesThrown,1);
    ASleepSignalDevice* Device=nullptr;for(TActorIterator<ASleepSignalDevice> It(Scene.World);It;++It)Device=*It;
    if(!TestNotNull(TEXT("Spawned device"),Device))return false;
    TestTrue(TEXT("Throw ignores upward aim and stays slow"),Device->LaunchVelocity.X>200&&Device->LaunchVelocity.Z<100);
    Scene.Advance(1.f);
    TestTrue(TEXT("Near-foot throw lands"),Device->bEmitting);
    const float Distance=FVector::Dist2D(Player->GetActorLocation(),Device->GetActorLocation());
    AddInfo(FString::Printf(TEXT("Near-foot landing distance: %.1f cm"),Distance));
    TestTrue(TEXT("Landing is one to three metres ahead"),Distance>100&&Distance<300);
    TestTrue(TEXT("Held model returns after throw"),Gun->WeaponMesh->IsVisible());
    TestTrue(TEXT("Throw is much slower than bomb"),Gun->SleepThrowSpeed<Gun->BombThrowSpeed*.4f);
    const float Energy=Player->GetCurrentEnergy();Scene.Advance(.2f);TestEqual(TEXT("Field cannot drain throwing player"),Player->GetCurrentEnergy(),Energy);
    Player->NextWeapon();TestEqual(TEXT("Wheel cycles to rifle"),Player->GetActiveWeapon(),EPlayerWeapon::Default);
    Player->PreviousWeapon();TestEqual(TEXT("Wheel cycles to device"),Player->GetActiveWeapon(),EPlayerWeapon::SleepSignal);
    TestTrue(TEXT("Another throw begins"),Gun->FireSleepSignal());Player->SelectWeapon(EPlayerWeapon::Default);Scene.Advance(.5f);
    TestEqual(TEXT("Switching before release cancels pending throw"),Gun->SleepDevicesThrown,1);
    Player->SelectWeapon(EPlayerWeapon::SleepSignal);TestFalse(TEXT("Switching does not reset cooldown"),Gun->FireSleepSignal());
    if(!GUsingNullRHI)
    {
        const FString Dir=FPaths::ProjectSavedDir()/TEXT("SleepSignal/Preview");IFileManager::Get().MakeDirectory(*Dir,true);
        auto* Widget=CreateWidget<UEnergyHUDWidget>(Controller,UEnergyHUDWidget::StaticClass());FWidgetRenderer Renderer(true);
        auto* RT=Renderer.DrawWidget(Widget->TakeWidget(),FVector2D(1280,720));FlushRenderingCommands();
        TArray<FColor> Pixels;FReadSurfaceDataFlags Flags(RCM_UNorm);Flags.SetLinearToGamma(false);
        if(RT&&RT->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))
        {
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(RT->SizeX,RT->SizeY,Pixels,PNG);FFileHelper::SaveArrayToFile(PNG,*(Dir/TEXT("HUD.png")));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSleepContactTest,"MyProject6.SleepSignal.NoAttacksDuringSleep",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSleepContactTest::RunTest(const FString&)
{
    for(UClass* NPCClass:{AChasingNPC::StaticClass(),AWhisperFollower::StaticClass()})
    {
        SleepTests::FScene Scene;
        auto* NPC=Scene.NPC(NPCClass);Scene.Advance(.7f);
        auto* Sleep=NPC->FindComponentByClass<UNPCSleepComponent>();auto* Source=Scene.World->SpawnActor<AActor>();
        Sleep->ReceiveSignal(Source,4);Scene.Advance(2);
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* PlayerClass=LoadClass<AMyProject6Character>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
        if (!TestNotNull(TEXT("Concrete player class"),PlayerClass)) return false;
        auto* Player=Scene.World->SpawnActor<AMyProject6Character>(PlayerClass,FVector(1500,0,100),FRotator::ZeroRotator,Params);
        if (!TestNotNull(TEXT("Contact test player"),Player)) return false;
        auto* PC=Scene.World->SpawnActor<APlayerController>();PC->SetAsLocalPlayerController();PC->Possess(Player);Player->GetCharacterMovement()->DisableMovement();
        Player->SetActorLocation(NPC->GetActorLocation()+FVector(100,0,0));
        const float Energy=Player->GetCurrentEnergy();const float Health=SleepTests::Health(NPC);
        Scene.Advance(.8f);
        TestEqual(TEXT("Sleeping contact cannot drain player"),Player->GetCurrentEnergy(),Energy);
        TestEqual(TEXT("Signal keeps NPC health unchanged"),SleepTests::Health(NPC),Health);
        Source->Destroy();Scene.Advance(Sleep->WakeDuration+.2f);
        TestFalse(TEXT("NPC recovers after source destruction"),Sleep->IsIncapacitated());
        TestTrue(TEXT("Contact behavior resumes on wake"),Player->GetCurrentEnergy()<Energy);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSleepDamageTest,"MyProject6.SleepSignal.DamageAndBlast",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSleepDamageTest::RunTest(const FString&)
{
    UClass* Whisper=LoadClass<AWhisperFollower>(nullptr,TEXT("/Game/NPC/Whisper/BP_Whisper_Follower.BP_Whisper_Follower_C"));
    if(!TestNotNull(TEXT("Visible saved Whisper class"),Whisper))return false;
    for(UClass* Class:{AChasingNPC::StaticClass(),Whisper})for(int32 FPS:{30,120})
    {
        SleepTests::FScene Scene;auto* NPC=Scene.NPC(Class);Scene.Advance(.8f,FPS);
        auto* S=NPC->FindComponentByClass<UNPCSleepComponent>();auto* Hit=NPC->FindComponentByClass<UNPCHitReactionComponent>();
        auto* Source=Scene.World->SpawnActor<AActor>();S->ReceiveSignal(Source,30);Scene.Advance(2,FPS);
        const float Health=SleepTests::Health(NPC);const FVector Before=NPC->GetActorLocation();const FTransform SleepingMesh=NPC->GetMesh()->GetRelativeTransform();
        FName Head=NAME_None;
        for(int I=0;I<NPC->GetMesh()->GetNumBones();++I)
        {
            FName B=NPC->GetMesh()->GetBoneName(I);FString N=B.ToString().ToLower();
            if(N==TEXT("head")||N==TEXT("head_01")||N.Contains(TEXT("_head_"))){Head=B;break;}
        }
        const FVector Target=NPC->GetMesh()->GetSocketLocation(Head);
        FHitResult Trace;Scene.World->LineTraceSingleByChannel(Trace,Target+FVector(300,0,0),Target-FVector(30,0,0),ECC_GameTraceChannel1);
        TestTrue(TEXT("Lying head is hittable away from the upright movement capsule"),Trace.GetActor()==NPC&&Trace.GetComponent()!=NPC->GetCapsuleComponent());
        const FTransform ShotTransform(FRotator(0,180,0),Target+FVector(180,0,0));
        auto* Shot=Scene.World->SpawnActorDeferred<ABallProjectile>(ABallProjectile::StaticClass(),ShotTransform);
        FindFProperty<FFloatProperty>(ABallProjectile::StaticClass(),TEXT("ProjectileCollisionRadius"))->SetPropertyValue_InContainer(Shot,3.f);
        Shot->FinishSpawning(ShotTransform);Scene.Advance(.12f,FPS);
        TestTrue(TEXT("A real moving projectile damages the sleeping body"),SleepTests::Health(NPC)<Health);
        TestTrue(TEXT("Projectile visibly bends the sleeping torso"),Hit->GetCurrentForwardBendAngle()>5);
        TestTrue(TEXT("Hit drags sleeper in projectile direction"),NPC->GetActorLocation().X<Before.X-4);
        TestEqual(TEXT("Bullet hit does not wake the NPC"),S->State,ENPCSleepState::Sleeping);
        Scene.Advance(.5f,FPS);TestTrue(TEXT("Recoil returns to sleeping pose, never upright"),NPC->GetMesh()->GetRelativeTransform().Equals(SleepingMesh,1.1f));
        SleepTests::Export(NPC,TEXT("ShotSleeping"));
        auto Detonate=[&](float Damage)
        {
            const FTransform T(NPC->GetActorLocation()+FVector(-180,0,0));
            auto* Bomb=Scene.World->SpawnActorDeferred<ABombProjectile>(ABombProjectile::StaticClass(),T);
            Bomb->BlastDamage=Damage;Bomb->ImpulseSpeed=1100;Bomb->ImpulseLift=600;Bomb->LaunchVelocity=FVector::ZeroVector;Bomb->FinishSpawning(T);Bomb->Explode();
        };
        const FVector Ground=NPC->GetActorLocation();Detonate(0);
        TestTrue(TEXT("Bomb queues pressure while asleep"),NPC->GetCharacterMovement()->PendingLaunchVelocity.X>500);
        Scene.Advance(.25f,FPS);
        TestTrue(TEXT("Sleeping NPC flies outward and upward"),NPC->GetActorLocation().X>Ground.X+100&&NPC->GetActorLocation().Z>Ground.Z+40);
        TestTrue(TEXT("Sleep and blast tumble coexist"),S->IsIncapacitated()&&Hit->IsBlastTumbling());
        SleepTests::Export(NPC,TEXT("BlastSleeping"));
        UGameplayStatics::ApplyPointDamage(NPC,1,FVector::ForwardVector,FHitResult(),nullptr,nullptr,nullptr);Scene.Advance(.09f,FPS);
        TestTrue(TEXT("A projectile also bends the airborne sleeping body"),Hit->GetCurrentForwardBendAngle()>24);
        TestTrue(TEXT("Mid-air hit preserves blast flight"),NPC->GetCharacterMovement()->IsFalling()&&NPC->GetCharacterMovement()->Velocity.X>100);
        // Signal expiry in flight must wait for a safe landing before get-up.
        Source->Destroy();Scene.Advance(.1f,FPS);TestEqual(TEXT("No mid-air get-up"),S->State,ENPCSleepState::Sleeping);
        Scene.Advance(2.4f,FPS);TestEqual(TEXT("Landing permits staged recovery"),S->State,ENPCSleepState::Waking);
        Scene.Advance(S->WakeDuration,FPS);TestFalse(TEXT("NPC recovers after landing"),S->IsIncapacitated());
        Source=Scene.World->SpawnActor<AActor>();S->ReceiveSignal(Source,30);Scene.Advance(2,FPS);Detonate(0);Scene.Advance(.2f,FPS);
        const FVector Flight=NPC->GetCharacterMovement()->Velocity;
        UGameplayStatics::ApplyPointDamage(NPC,100000,FVector::ForwardVector,FHitResult(),nullptr,nullptr,nullptr);
        TestEqual(TEXT("Lethal airborne hit reduces energy to zero"),SleepTests::Health(NPC),0.f);
        TestFalse(TEXT("Death cannot resume sleeping AI"),S->IsIncapacitated());
        TestTrue(TEXT("Death preserves the grounded/folded pose"),S->HasPose()&&S->PoseAlpha>.9f);
        TestTrue(TEXT("Death retains pre-existing blast momentum"),NPC->GetCharacterMovement()->PendingLaunchVelocity.X>=Flight.X);
        Scene.Advance(.12f,FPS);SleepTests::Export(NPC,TEXT("LethalBlast"));
        TestTrue(TEXT("Sleep body hitboxes are disabled on death"),!NPC->CanBeDamaged());
        NPC=Scene.NPC(Class,FVector(4000,0,150));Scene.Advance(.8f,FPS);S=NPC->FindComponentByClass<UNPCSleepComponent>();
        S->ReceiveSignal(Source,30);Scene.Advance(2,FPS);Detonate(10000);
        TestEqual(TEXT("A lethal bomb kills a sleeping NPC immediately"),SleepTests::Health(NPC),0.f);
        TestFalse(TEXT("Lethal blast cannot start a later get-up"),S->IsIncapacitated());
        TestTrue(TEXT("Lethal bomb still launches the corpse"),NPC->GetCharacterMovement()->PendingLaunchVelocity.X>500);
    }
    return true;
}
#endif
