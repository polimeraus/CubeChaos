#include "NPCSleepComponent.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "NPCHitReactionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"

namespace
{
float Ease(float T) { T=FMath::Clamp(T,0.f,1.f); return T*T*(3.f-2.f*T); }
FNPCSleepPose Blend(const FNPCSleepPose& A,const FNPCSleepPose& B,float T)
{
    FNPCSleepPose P; T=Ease(T);
    P.Hip=FMath::Lerp(A.Hip,B.Hip,T);P.Roll=FMath::Lerp(A.Roll,B.Roll,T);P.Pitch=FMath::Lerp(A.Pitch,B.Pitch,T);
    P.Curl=FMath::Lerp(A.Curl,B.Curl,T);P.Nod=FMath::Lerp(A.Nod,B.Nod,T);
    for(int I=0;I<4;++I){P.Targets[I]=FMath::Lerp(A.Targets[I],B.Targets[I],T);P.Poles[I]=FMath::Lerp(A.Poles[I],B.Poles[I],T);}
    return P;
}
FNPCSleepPose Drowsy()
{
    FNPCSleepPose P;P.Hip.Z=.92;P.Pitch=8;P.Nod=16;P.Curl=.15f;
    P.Targets[0]=FVector(.12,-.45,.72);P.Targets[1]=FVector(.06,.45,.78);return P;
}
FNPCSleepPose Buckle()
{
    FNPCSleepPose P;P.Hip=FVector(-.1,0,.61);P.Roll=9;P.Pitch=24;P.Nod=20;P.Curl=.25f;
    P.Targets[0]=FVector(.42,-.4,.35);P.Targets[1]=FVector(.2,.48,.45);return P;
}
FNPCSleepPose Kneel()
{
    FNPCSleepPose P;P.Hip=FVector(-.14,0,.36);P.Roll=8;P.Pitch=48;P.Nod=6;P.Curl=.28f;
    P.Targets[0]=FVector(.48,-.42,.07);P.Targets[1]=FVector(.42,.40,.07);
    P.Targets[2]=FVector(-.6,-.19,.11);P.Targets[3]=FVector(-.6,.19,.11);
    P.Poles[0]=FVector(.1,-.85,.4);P.Poles[1]=FVector(.1,.85,.4);
    P.Poles[2]=FVector(.5,-.25,0);P.Poles[3]=FVector(.5,.25,0);return P;
}
FNPCSleepPose SideSupport()
{
    FNPCSleepPose P;P.Hip=FVector(-.06,.01,.32);P.Roll=62;P.Pitch=10;P.Curl=.38f;P.Nod=-7;
    P.Targets[0]=FVector(.05,-.70,.08);P.Targets[1]=FVector(.48,-.23,.08);
    P.Targets[2]=FVector(-.25,.65,.13);P.Targets[3]=FVector(-.1,.6,.23);
    P.Poles[0]=FVector(.1,-.3,.09);P.Poles[1]=FVector(.65,-.15,.5);
    P.Poles[2]=FVector(.8,.25,.10);P.Poles[3]=FVector(.8,.3,.35);return P;
}
FNPCSleepPose Asleep()
{
    FNPCSleepPose P=SideSupport();P.Hip=FVector(0,0,.30);P.Roll=88;P.Pitch=0;P.Curl=.40f;P.Nod=7;
    P.Targets[0]=FVector(.13,-.81,.10);P.Targets[1]=FVector(.42,-.51,.12);
    P.Targets[2]=FVector(-.18,.80,.12);P.Targets[3]=FVector(.02,.72,.25);return P;
}
FNPCSleepPose HalfKneel()
{
    FNPCSleepPose P;P.Hip=FVector(-.12,0,.62);P.Pitch=24;P.Curl=.18f;P.Nod=-6;
    P.Targets[0]=FVector(.45,-.24,.50);P.Targets[1]=FVector(.16,.38,.42);
    P.Targets[2]=FVector(.38,-.20,.10);P.Targets[3]=FVector(-.60,.22,.11);
    P.Poles[0]=FVector(-.1,-.7,.8);P.Poles[1]=FVector(.1,.8,.4);P.Poles[3]=FVector(.5,.25,0);return P;
}
FNPCSleepPose Rise()
{
    FNPCSleepPose P;P.Hip=FVector(.12,0,.86);P.Pitch=13;P.Curl=.08f;P.Nod=-3;
    P.Targets[0]=FVector(.45,-.25,.58);P.Targets[1]=FVector(.15,.47,.60);
    P.Targets[2]=FVector(.38,-.20,.10);P.Targets[3]=FVector(-.17,.20,.11);return P;
}
struct FSleepKey {float Time;FNPCSleepPose Pose;};
FNPCSleepPose Sample(TConstArrayView<FSleepKey> Keys,float T)
{
    for(int I=1;I<Keys.Num();++I)if(T<=Keys[I].Time)return Blend(Keys[I-1].Pose,Keys[I].Pose,(T-Keys[I-1].Time)/(Keys[I].Time-Keys[I-1].Time));
    return Keys.Last().Pose;
}
}

UNPCSleepComponent::UNPCSleepComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
}
void UNPCSleepComponent::BeginPlay()
{
    Super::BeginPlay();
    if(auto* NPC=Cast<ACharacter>(GetOwner()))
    {
        AddTickPrerequisiteComponent(NPC->GetCharacterMovement());
        if(auto* Hit=NPC->FindComponentByClass<UNPCHitReactionComponent>())AddTickPrerequisiteComponent(Hit);
        NPC->GetMesh()->AddTickPrerequisiteComponent(this);
    }
}
bool UNPCSleepComponent::IsLivingNPC() const
{
    if(const auto* NPC=Cast<AChasingNPC>(GetOwner()))return !NPC->IsDead();
    if(const auto* NPC=Cast<AWhisperFollower>(GetOwner()))return !NPC->IsDead();
    return false;
}
void UNPCSleepComponent::ReceiveSignal(AActor* Source,float HoldSeconds)
{
    if(!IsValid(Source)||!GetOwner()->HasAuthority()||!IsLivingNPC())return;
    Sources.FindOrAdd(Source)=GetWorld()->GetTimeSeconds()+FMath::Clamp(HoldSeconds,.2f,60.f);SetComponentTickEnabled(true);
}

void UNPCSleepComponent::BuildBodyCollision()
{
    if(!BodyCollision.IsEmpty())return;
    auto* NPC=CastChecked<ACharacter>(GetOwner());auto* Mesh=NPC->GetMesh();
    if(!Mesh->GetSkeletalMeshAsset())return;
    const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();TArray<FTransform> CS;CS.SetNum(Ref.GetNum());
    for(int I=0;I<Ref.GetNum();++I)CS[I]=Ref.GetParentIndex(I)>=0?Ref.GetRefBonePose()[I]*CS[Ref.GetParentIndex(I)]:Ref.GetRefBonePose()[I];
    for(int I=0;I<Ref.GetNum();++I)
    {
        FString Name=Ref.GetBoneName(I).ToString().ToLower();
        if(Name.Contains(TEXT("twist"))||Name.Contains(TEXT("correct"))||Name.StartsWith(TEXT("ik_")))continue;
        const bool bArm=Name==TEXT("upperarm_l")||Name==TEXT("upperarm_r")||Name==TEXT("lowerarm_l")||Name==TEXT("lowerarm_r")||Name.Contains(TEXT("leftarm"))||Name.Contains(TEXT("rightarm"))||Name.Contains(TEXT("forearm"));
        const bool bLeg=Name==TEXT("thigh_l")||Name==TEXT("thigh_r")||Name==TEXT("calf_l")||Name==TEXT("calf_r")||Name.Contains(TEXT("upleg"))||Name.Contains(TEXT("leftleg"))||Name.Contains(TEXT("rightleg"));
        const bool bHead=Name==TEXT("head")||Name==TEXT("head_01")||Name.Contains(TEXT("_head_"));
        const bool bTorso=Name.Contains(TEXT("spine"))||Name==TEXT("pelvis")||Name.EndsWith(TEXT("hips"))||Name.Contains(TEXT("_hips_"));
        if(!bArm&&!bLeg&&!bHead&&!bTorso)continue;
        FVector End(0,0,8);
        for(int J=I+1;J<Ref.GetNum();++J)if(Ref.GetParentIndex(J)==I)
        {
            const FString Child=Ref.GetBoneName(J).ToString().ToLower();
            if(Child.Contains(TEXT("twist"))||Child.Contains(TEXT("correct")))continue;
            if(bTorso&&!Child.Contains(TEXT("spine"))&&!Child.Contains(TEXT("neck")))continue;
            End=CS[I].InverseTransformPosition(CS[J].GetLocation());break;
        }
        auto* Shape=NewObject<UCapsuleComponent>(NPC);
        Shape->SetupAttachment(Mesh,Ref.GetBoneName(I));
        const float Scale=FMath::Max(.01f,float(RestMesh.GetScale3D().GetMin()));
        const float Radius=StandingHeight*(bTorso?.15f:bHead?.13f:bLeg?.085f:.065f)/Scale;
        Shape->InitCapsuleSize(Radius,End.Size()*.5f+Radius*.5f);
        Shape->SetRelativeLocationAndRotation(End*.5,FQuat::FindBetweenNormals(FVector::UpVector,End.GetSafeNormal()));
        Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);Shape->SetCollisionObjectType(ECC_Pawn);
        Shape->SetCollisionResponseToAllChannels(ECR_Ignore);
        Shape->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);Shape->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
        Shape->SetGenerateOverlapEvents(false);Shape->SetCanEverAffectNavigation(false);Shape->RegisterComponent();BodyCollision.Add(Shape);
    }
}
void UNPCSleepComponent::SetBodyCollision(bool bEnabled)
{
    bEnabled = bEnabled && !BodyCollision.IsEmpty();
    for(auto C:BodyCollision)C->SetCollisionEnabled(bEnabled?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    auto* Capsule=CastChecked<ACharacter>(GetOwner())->GetCapsuleComponent();
    Capsule->SetCollisionResponseToChannel(ECC_GameTraceChannel1,bEnabled?ECR_Ignore:RestProjectileResponse);
    Capsule->SetCollisionResponseToChannel(ECC_Visibility,bEnabled?ECR_Ignore:RestVisibilityResponse);
}
void UNPCSleepComponent::StartSleep()
{
    auto* NPC=CastChecked<ACharacter>(GetOwner());auto* Movement=NPC->GetCharacterMovement();auto* Mesh=NPC->GetMesh();
    RestMesh=Mesh->GetRelativeTransform();RestMovementMode=Movement->MovementMode;RestCustomMode=Movement->CustomMovementMode;
    RestVisibilityTick=uint8(Mesh->VisibilityBasedAnimTickOption);bRestUpdateRate=Mesh->bEnableUpdateRateOptimizations;
    bRestOrient=Movement->bOrientRotationToMovement;bRestDesiredRotation=Movement->bUseControllerDesiredRotation;bRestRunPhysics=Movement->bRunPhysicsWithNoController;
    RestProjectileResponse=NPC->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_GameTraceChannel1);
    RestVisibilityResponse=NPC->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Visibility);
    Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Mesh->bEnableUpdateRateOptimizations=false;
    PoseBendAxis=RestMesh.GetRotation().UnrotateVector(FVector::RightVector);PoseForwardAxis=RestMesh.GetRotation().UnrotateVector(FVector::ForwardVector);
    CapsuleHalf=NPC->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();RefHip=FVector(0,0,CapsuleHalf);
    if(Mesh->GetSkeletalMeshAsset())
    {
        const auto& Ref=Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
        for(int I=0;I<Ref.GetNum();++I)
        {
            const FString Name=Ref.GetBoneName(I).ToString().ToLower();
            if(Name!=TEXT("pelvis")&&!Name.EndsWith(TEXT("hips"))&&!Name.Contains(TEXT("_hips_")))continue;
            FTransform Bone=Ref.GetRefBonePose()[I];
            for(int P=Ref.GetParentIndex(I);P>=0;P=Ref.GetParentIndex(P))Bone=Bone*Ref.GetRefBonePose()[P];
            RefHip=Bone.GetLocation();break;
        }
    }
    StandingHeight=FMath::Max(CapsuleHalf*.6f,float(RestMesh.TransformPosition(RefHip).Z+CapsuleHalf));
    float ShoulderWidth=0.f;
    for(int I=0;I<Mesh->GetNumBones();++I)
    {
        const FName Bone=Mesh->GetBoneName(I);const FString Name=Bone.ToString().ToLower();
        if(Name==TEXT("upperarm_l")||Name==TEXT("upperarm_r")||Name.Contains(TEXT("leftarm"))||Name.Contains(TEXT("rightarm")))
            ShoulderWidth=FMath::Max(ShoulderWidth,float(FMath::Abs(NPC->GetActorTransform().InverseTransformPosition(Mesh->GetSocketLocation(Bone)).Y)));
    }
    // Wide robots and tiny imported rigs require the same anatomical ground clearance.
    SideSupportLift=FMath::Max(0.f,ShoulderWidth+StandingHeight*.14f-StandingHeight*.30f);
    BuildBodyCollision();
    if(NPC->GetController())NPC->GetController()->StopMovement();NPC->ConsumeMovementInputVector();Movement->StopMovementImmediately();
    // Freeze intent, not physics: projectiles, launch pads, gravity and bomb pressure still work.
    Movement->bOrientRotationToMovement=false;Movement->bUseControllerDesiredRotation=false;Movement->bRunPhysicsWithNoController=true;
    if(Movement->MovementMode==MOVE_None)Movement->SetMovementMode(MOVE_Walking);
    State=ENPCSleepState::Drowsy;Elapsed=Age=0;Pose=FNPCSleepPose();bDeathPose=false;
    if(!SleepIndicator)
    {
        SleepIndicator=NewObject<UTextRenderComponent>(NPC,TEXT("SleepIndicator"));SleepIndicator->SetupAttachment(NPC->GetRootComponent());
        SleepIndicator->SetText(FText::FromString(TEXT("z Z")));SleepIndicator->SetHorizontalAlignment(EHTA_Center);SleepIndicator->SetWorldSize(17.f);
        SleepIndicator->SetTextRenderColor(FColor(125,215,255));SleepIndicator->SetCollisionEnabled(ECollisionEnabled::NoCollision);SleepIndicator->SetCastShadow(false);SleepIndicator->RegisterComponent();
    }
    SleepIndicator->SetVisibility(false);
}
void UNPCSleepComponent::CancelSleep()
{
    Sources.Reset();
    if(HasPose())
    {
        auto* NPC=CastChecked<ACharacter>(GetOwner());auto* Move=NPC->GetCharacterMovement();SetBodyCollision(false);
        NPC->GetMesh()->SetRelativeTransform(RestMesh);NPC->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption(RestVisibilityTick);
        NPC->GetMesh()->bEnableUpdateRateOptimizations=bRestUpdateRate;
        if(IsLivingNPC())
        {
            Move->bOrientRotationToMovement=bRestOrient;Move->bUseControllerDesiredRotation=bRestDesiredRotation;Move->bRunPhysicsWithNoController=bRestRunPhysics;
            if(!Move->IsFalling())Move->SetMovementMode(EMovementMode(RestMovementMode),RestCustomMode);
        }
    }
    State=ENPCSleepState::Awake;bDeathPose=false;PoseAlpha=Curl=Nod=Breath=0;
    if(SleepIndicator)SleepIndicator->SetVisibility(false);SetComponentTickEnabled(false);
}
bool UNPCSleepComponent::FinishForDeath()
{
    if(!IsIncapacitated())return false;
    // Leave the folded skeleton and current transform intact; a corpse never plays get-up.
    Sources.Reset();SetBodyCollision(false);State=ENPCSleepState::Awake;bDeathPose=true;Breath=0;
    if(SleepIndicator)SleepIndicator->SetVisibility(false);SetComponentTickEnabled(false);return true;
}
FTransform UNPCSleepComponent::ApplyBlastRotation(const FTransform& Base,const FQuat& Rotation) const
{
    const FVector Pivot=Base.TransformPosition(RefHip);FTransform Result=Base;
    Result.SetRotation((Rotation*Base.GetRotation()).GetNormalized());
    Result.SetLocation(Pivot-Result.GetRotation().RotateVector(RefHip*Base.GetScale3D()));return Result;
}
void UNPCSleepComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function);
    if(!IsLivingNPC()){if(!bDeathPose)CancelSleep();return;}
    const double Now=GetWorld()->GetTimeSeconds();
    for(auto It=Sources.CreateIterator();It;++It)if(!It.Key().IsValid()||It.Value()<Now)It.RemoveCurrent();
    auto* NPC=CastChecked<ACharacter>(GetOwner());auto* Move=NPC->GetCharacterMovement();auto* Hit=NPC->FindComponentByClass<UNPCHitReactionComponent>();
    if(State==ENPCSleepState::Awake)
    {
        if(Sources.IsEmpty()){SetComponentTickEnabled(false);return;}
        if(Move->IsFalling()||(Hit&&Hit->IsStaggering()))return;StartSleep();
    }
    NPC->ConsumeMovementInputVector();
    const bool bAirborne=Move->IsFalling()||!Move->PendingLaunchVelocity.IsNearlyZero()||(Hit&&Hit->IsBlastTumbling());
    if(!bAirborne&&State==ENPCSleepState::Sleeping&&Sources.IsEmpty()){State=ENPCSleepState::Waking;Elapsed=0;}
    if(State==ENPCSleepState::Waking&&!Sources.IsEmpty())
    {
        State=ENPCSleepState::FallingAsleep;Elapsed=0;FallStart=Pose;FallStartAlpha=PoseAlpha;
    }
    // Never attempt a standing recovery while flying. Leases keep aging in real time.
    Animate(bAirborne?0.f:Delta);
}
void UNPCSleepComponent::Animate(float Delta)
{
    auto* NPC=CastChecked<ACharacter>(GetOwner());Elapsed+=Delta;Age+=Delta;Breath=0;
    if(State==ENPCSleepState::Drowsy)
    {
        const float T=FMath::Clamp(Elapsed/FMath::Max(.1f,DrowsyDuration),0.f,1.f);PoseAlpha=Ease(T);Pose=Blend(FNPCSleepPose(),Drowsy(),T);
        Pose.Nod+=FMath::Sin(T*UE_PI*3)*10*FMath::Sin(T*UE_PI);Pose.Roll=FMath::Sin(T*UE_TWO_PI)*3;
        if(T>=1){State=ENPCSleepState::FallingAsleep;Elapsed=0;FallStart=Pose;FallStartAlpha=PoseAlpha;}
    }
    else if(State==ENPCSleepState::FallingAsleep)
    {
        const float T=FMath::Clamp(Elapsed/FMath::Max(.1f,FallDuration),0.f,1.f);
        // On interruption, return from the current support pose rather than teleporting to upright.
        if(FallStart.Hip.Z<.85f)Pose=Blend(FallStart,Asleep(),T);
        else {const FSleepKey Keys[]={{0,FallStart},{.28f,Buckle()},{.53f,Kneel()},{.76f,SideSupport()},{1,Asleep()}};Pose=Sample(Keys,T);}
        PoseAlpha=FMath::Lerp(FallStartAlpha,1.f,Ease(T));if(T>=1){State=ENPCSleepState::Sleeping;Elapsed=0;}
    }
    else if(State==ENPCSleepState::Sleeping){Pose=Asleep();PoseAlpha=1;Breath=FMath::Sin(Age*UE_TWO_PI/3.2f);}
    else if(State==ENPCSleepState::Waking)
    {
        const float T=FMath::Clamp(Elapsed/FMath::Max(.1f,WakeDuration),0.f,1.f);
        const FSleepKey Keys[]={{0,Asleep()},{.18f,SideSupport()},{.39f,Kneel()},{.61f,HalfKneel()},{.84f,Rise()},{1,FNPCSleepPose()}};
        Pose=Sample(Keys,T);PoseAlpha=1.f-Ease((T-.9f)/.1f);
        if(T>=1){CancelSleep();return;}
    }
    Curl=Pose.Curl;Nod=Pose.Nod;
    const FQuat Turn=FQuat(FVector::RightVector,FMath::DegreesToRadians(Pose.Pitch))*FQuat(FVector::ForwardVector,FMath::DegreesToRadians(Pose.Roll));
    FTransform Base=RestMesh;Base.SetRotation(Turn*RestMesh.GetRotation());
    const FVector RestHip=RestMesh.TransformPosition(RefHip);
    const FVector Hip=FVector(RestHip.X+Pose.Hip.X*StandingHeight,RestHip.Y+Pose.Hip.Y*StandingHeight,-CapsuleHalf+Pose.Hip.Z*StandingHeight+SideSupportLift*Ease(Pose.Roll/88.f)+Breath*.45f);
    Base.SetLocation(Hip-Base.GetRotation().RotateVector(RefHip*Base.GetScale3D()));
    GroundRotation=Base.GetRotation().Inverse()*RestMesh.GetRotation();
    GroundPoint=Base.InverseTransformPosition(FVector(0,0,-CapsuleHalf));GroundUp=Base.GetRotation().UnrotateVector(FVector::UpVector);
    // Targets are solved in the untumbled frame, so a blast carries the entire folded pose.
    for(int I=0;I<4;++I)
    {
        LimbTargets[I]=Base.InverseTransformPosition(Pose.Targets[I]*StandingHeight+FVector(RestHip.X,RestHip.Y,-CapsuleHalf));
        LimbPoles[I]=Base.InverseTransformPosition(Pose.Poles[I]*StandingHeight+FVector(RestHip.X,RestHip.Y,-CapsuleHalf));
    }
    if(auto* Hit=NPC->FindComponentByClass<UNPCHitReactionComponent>();Hit&&Hit->IsBlastTumbling())Base=ApplyBlastRotation(Base,Hit->GetBlastRotation());
    NPC->GetMesh()->SetRelativeTransform(Base);SetBodyCollision(PoseAlpha>.3f);
    if(SleepIndicator)
    {
        SleepIndicator->SetVisibility(State==ENPCSleepState::Sleeping&&!NPC->GetCharacterMovement()->IsFalling());
        SleepIndicator->SetRelativeLocation(FVector(0,-StandingHeight*.7f,-CapsuleHalf+StandingHeight*.8f+FMath::Sin(Age*2)*5));
        if(const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))SleepIndicator->SetWorldRotation((Camera->GetCameraLocation()-SleepIndicator->GetComponentLocation()).Rotation());
    }
}
void UNPCSleepComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelSleep();Super::EndPlay(Reason);
}
