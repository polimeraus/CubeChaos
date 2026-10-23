#include "NPCElectricTetherComponent.h"
#include "ChasingNPC.h"
#include "WhisperFollower.h"
#include "NPCSleepComponent.h"
#include "NPCHitReactionComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

UNPCElectricTetherComponent::UNPCElectricTetherComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
void UNPCElectricTetherComponent::BeginPlay()
{
    Super::BeginPlay();
    if(auto* N=Cast<ACharacter>(GetOwner()))
    {
        AddTickPrerequisiteComponent(N->GetCharacterMovement());
        N->GetMesh()->AddTickPrerequisiteComponent(this);
    }
}
bool UNPCElectricTetherComponent::IsLivingNPC(const AActor* Actor)
{
    if(!IsValid(Actor)||Actor->IsActorBeingDestroyed())return false;
    if(auto* N=Cast<AChasingNPC>(Actor))return !N->IsDead();
    if(auto* N=Cast<AWhisperFollower>(Actor))return !N->IsDead();
    return false;
}
bool UNPCElectricTetherComponent::Capture(AActor* Source)
{
    if(!IsValid(Source)||!GetOwner()->HasAuthority()||!IsLivingNPC(GetOwner())||IsCaptured())return false;
    if(IsControlled())Restore(true);
    auto* N=CastChecked<ACharacter>(GetOwner());auto* M=N->GetCharacterMovement();auto* Mesh=N->GetMesh();
    if(auto* S=N->FindComponentByClass<UNPCSleepComponent>())S->CancelSleep();
    if(auto* R=N->FindComponentByClass<UNPCHitReactionComponent>())R->CancelForTether();
    RestMesh=Mesh->GetRelativeTransform();RestMode=M->MovementMode;RestCustom=M->CustomMovementMode;
    RestVisibility=uint8(Mesh->VisibilityBasedAnimTickOption);bRestUpdateRate=Mesh->bEnableUpdateRateOptimizations;
    bRestOrient=M->bOrientRotationToMovement;bRestDesired=M->bUseControllerDesiredRotation;bRestRunPhysics=M->bRunPhysicsWithNoController;
    bRestControllerYaw=N->bUseControllerRotationYaw;N->bUseControllerRotationYaw=false;
    if(N->GetController())N->GetController()->StopMovement();
    M->StopMovementImmediately();M->PendingLaunchVelocity=FVector::ZeroVector;M->DisableMovement();
    M->bOrientRotationToMovement=M->bUseControllerDesiredRotation=false;
    N->ConsumeMovementInputVector();
    Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;Mesh->bEnableUpdateRateOptimizations=false;
    Captor=Source;DamageOwner=Source->GetInstigator();HoldPoint=N->GetActorLocation();
    MotionVelocity=SmoothedVelocity=FVector::ZeroVector;Tumble=FQuat::Identity;
    State=EElectricCaptiveState::Captured;StruggleTime=FlightAge=RecoveryAge=0;PoseAlpha=0;NextImpactTime=0;
    AddTickPrerequisiteActor(Source);SetComponentTickEnabled(true);return true;
}
FVector UNPCElectricTetherComponent::GetThrowVelocity() const
{
    return SmoothedVelocity.Size()>250.f?(SmoothedVelocity*1.25f).GetClampedToMaxSize(3800.f):FVector::ZeroVector;
}
void UNPCElectricTetherComponent::Release(bool bThrow)
{
    if(!IsCaptured())return;
    if(Captor.IsValid())RemoveTickPrerequisiteActor(Captor.Get());Captor.Reset();
    MotionVelocity=bThrow?GetThrowVelocity():FVector::ZeroVector;
    State=EElectricCaptiveState::Thrown;FlightAge=0;
}
void UNPCElectricTetherComponent::Restore(bool bFalling)
{
    if(!IsControlled())return;
    auto* N=Cast<ACharacter>(GetOwner());
    if(Captor.IsValid())RemoveTickPrerequisiteActor(Captor.Get());Captor.Reset();
    if(N)
    {
        auto* M=N->GetCharacterMovement();auto* Mesh=N->GetMesh();
        Mesh->SetRelativeTransform(RestMesh);Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption(RestVisibility);Mesh->bEnableUpdateRateOptimizations=bRestUpdateRate;
        N->bUseControllerRotationYaw=bRestControllerYaw;
        M->bOrientRotationToMovement=bRestOrient;M->bUseControllerDesiredRotation=bRestDesired;M->bRunPhysicsWithNoController=bRestRunPhysics;
        M->PendingLaunchVelocity=FVector::ZeroVector;M->StopMovementImmediately();
        if(IsLivingNPC(N))M->SetMovementMode(bFalling?MOVE_Falling:EMovementMode(RestMode),bFalling?0:RestCustom);
    }
    State=EElectricCaptiveState::Free;PoseAlpha=0;MotionVelocity=SmoothedVelocity=FVector::ZeroVector;SetComponentTickEnabled(false);
}
FVector UNPCElectricTetherComponent::FinishForDeath()
{
    const FVector V=IsControlled()?MotionVelocity:FVector::ZeroVector;Restore(true);return V;
}
void UNPCElectricTetherComponent::Impact(const FHitResult& Hit,float Speed)
{
    const double Now=GetWorld()->GetTimeSeconds();
    if(Speed<MinimumImpactSpeed||Now<NextImpactTime)return;
    NextImpactTime=Now+FMath::Max(.1f,ImpactCooldown);ImpactFlash=1; ++ImpactCount;
    const float Damage=FMath::Clamp((Speed-MinimumImpactSpeed)*ImpactDamageScale,1.f,65.f);
    APawn* P=DamageOwner.Get();
    UGameplayStatics::ApplyPointDamage(GetOwner(),Damage,MotionVelocity.GetSafeNormal(),Hit,P?P->GetController():nullptr,P,nullptr);
}
void UNPCElectricTetherComponent::Step(float Dt)
{
    auto* N=CastChecked<ACharacter>(GetOwner());
    const FVector Before=N->GetActorLocation();
    if(IsCaptured())
    {
        const FVector Acceleration=(HoldPoint-Before)*110.f-MotionVelocity*18.f;
        MotionVelocity=(MotionVelocity+Acceleration*Dt).GetClampedToMaxSize(3600.f);
    }
    else MotionVelocity.Z+=GetWorld()->GetGravityZ()*Dt;
    FHitResult Hit;N->AddActorWorldOffset(MotionVelocity*Dt,true,&Hit);
    if(Hit.bBlockingHit)
    {
        const float Into=FMath::Max(0.f,float(-FVector::DotProduct(MotionVelocity,Hit.Normal)));
        if(!Hit.bStartPenetrating && Hit.Time>.001f && FVector::DistSquared(Before,N->GetActorLocation())>.01f)Impact(Hit,Into);
        if(!IsControlled()||!IsLivingNPC(N))return;
        // A blocking surface consumes its normal velocity. A stationary press cannot repeatedly deal damage.
        MotionVelocity=FVector::VectorPlaneProject(MotionVelocity,Hit.Normal)+(IsCaptured()?0.f:.3f)*Hit.Normal*Into;
        if(!IsCaptured()&&Hit.Normal.Z>.55f)
        {
            // Tangential friction ends the flight after landing instead of sliding forever.
            const float Grip=FMath::Exp(-Dt*14.f)*(Into>150.f?.65f:1.f);
            MotionVelocity.X*=Grip;MotionVelocity.Y*=Grip;
        }
        if(!IsCaptured()&&Hit.Normal.Z>.55f&&FlightAge>.1f&&MotionVelocity.Size()<650.f)
        {State=EElectricCaptiveState::Recovering;RecoveryAge=0;MotionVelocity=FVector::ZeroVector;}
        else
        {
            FHitResult Slide;N->AddActorWorldOffset(FVector::VectorPlaneProject(MotionVelocity*Dt*(1-Hit.Time),Hit.Normal),true,&Slide);
        }
    }
    const FVector Actual=(N->GetActorLocation()-Before)/Dt;
    SmoothedVelocity=FMath::Lerp(SmoothedVelocity,Actual,1.f-FMath::Exp(-Dt*24.f));
}
void UNPCElectricTetherComponent::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,Type,Function);
    if(!IsLivingNPC(GetOwner())){Restore(true);return;}
    if(IsCaptured()&&(!Captor.IsValid()||Captor->IsActorBeingDestroyed()))Release(false);
    auto* N=CastChecked<ACharacter>(GetOwner());auto* M=N->GetCharacterMovement();
    // No latent AI, attack montage root motion or old projectile impulse may escape the cage.
    M->DisableMovement();M->PendingLaunchVelocity=FVector::ZeroVector;N->ConsumeMovementInputVector();
    StruggleTime+=Delta;FlightAge+=Delta;ImpactFlash=FMath::Max(0.f,ImpactFlash-Delta*5.f);
    if(State==EElectricCaptiveState::Recovering)
    {
        RecoveryAge+=Delta;PoseAlpha=FMath::Max(0.f,1.f-RecoveryAge/.55f);
        Tumble=FQuat::Slerp(Tumble,FQuat::Identity,1.f-FMath::Exp(-Delta*12.f));
        if(RecoveryAge>=.55f){Restore(RestMode!=MOVE_None);return;}
    }
    else
    {
        PoseAlpha=FMath::Min(1.f,PoseAlpha+Delta*8.f);
        float Remaining=FMath::Min(Delta,.2f);
        while(Remaining>UE_SMALL_NUMBER&&IsControlled()&&State!=EElectricCaptiveState::Recovering)
        {const float Dt=FMath::Min(Remaining,1.f/120.f);Step(Dt);Remaining-=Dt;}
        if(!IsControlled())return;
        if(IsCaptured())
        {
            const FVector Local=N->GetActorQuat().UnrotateVector(MotionVelocity);
            Tumble=FQuat::Slerp(Tumble,FRotator(FMath::Clamp(float(Local.X)*.012f,-35.f,35.f),0,FMath::Clamp(float(-Local.Y)*.016f,-45.f,45.f)).Quaternion(),1.f-FMath::Exp(-Delta*8.f));
        }
        else if(MotionVelocity.Size()>150.f)
        {
            const FVector Axis=N->GetActorQuat().UnrotateVector(FVector::CrossProduct(MotionVelocity,FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER,FVector::RightVector));
            Tumble=(FQuat(Axis,Delta*FMath::Clamp(float(MotionVelocity.Size())*.002f,.5f,5.f))*Tumble).GetNormalized();
        }
    }
    // Rotate about capsule centre; imported skeletons have different foot origins.
    FTransform Pose=RestMesh;Pose.SetLocation(Tumble.RotateVector(RestMesh.GetLocation()));Pose.SetRotation((Tumble*RestMesh.GetRotation()).GetNormalized());
    N->GetMesh()->SetRelativeTransform(Pose);
    if(FlightAge>30.f&&!IsCaptured())Restore(true);
}
void UNPCElectricTetherComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Restore(true);Super::EndPlay(Reason);
}
