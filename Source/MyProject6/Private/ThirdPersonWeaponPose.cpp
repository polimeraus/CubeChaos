#include "ThirdPersonWeaponPose.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "Components/StaticMeshComponent.h"
#include "MyProject6Character.h"
#include "RailgunWeapon.h"
#include "TwoBoneIK.h"

namespace
{
	// Middle knuckle just beneath the contact surface, in the weapon mesh's own centimetres.
	TOptional<FVector> SupportGrip(EPlayerWeapon Slot)
	{
		switch (Slot)
		{
		case EPlayerWeapon::Default:        return FVector(1.6, 29., 1.8);
		case EPlayerWeapon::Railgun:        return FVector(2.2, 23., -.1);
		case EPlayerWeapon::BFG:            return FVector(2.8, 29., -6.8);
		case EPlayerWeapon::PurpleRailgun:  return FVector(2.4, 29., -.9);
		case EPlayerWeapon::ElectricTether: return FVector(2.8, 32., -5.);
		default: return {};
		}
	}

	struct FWeaponPoseProxy : FAnimInstanceProxy
	{
		explicit FWeaponPoseProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
		FAnimNode_LinkedInputPose* Input = nullptr;
		UAnimSequence* Hold = nullptr;
		double Time = 0.;
		FQuat AimRotation = FQuat::Identity;
		FTransform WeaponToHand = FTransform::Identity;
		TOptional<FVector> Support;
		FCompactPoseBoneIndex UpperArm{INDEX_NONE}, Forearm{INDEX_NONE}, Wrist{INDEX_NONE}, Knuckle{INDEX_NONE}, TriggerHand{INDEX_NONE};
		FCompactPoseBoneIndex Spine{INDEX_NONE};
		TArray<FCompactPoseBoneIndex> UpperBody;

		virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
			Input = Instance->GetLinkedInputPoseNode();
			Hold = nullptr;
			Support.Reset();
			const auto* Player = Cast<AMyProject6Character>(Instance->TryGetPawnOwner());
			if (!Player || !Player->Railgun) return;
			auto* Grip = Player->Railgun->Arms->GetSingleNodeInstance();
			if (!Grip) return;
			Hold = Cast<UAnimSequence>(Grip->GetCurrentAsset());
			Time = Grip->GetCurrentTime();
			const FVector Axis = Player->GetMesh()->GetComponentQuat().UnrotateVector(Player->GetActorRightVector());
			const float Pitch = FMath::Clamp(FRotator::NormalizeAxis(Player->GetBaseAimRotation().Pitch), -65.f, 70.f);
			AimRotation = FQuat(Axis, -FMath::DegreesToRadians(Pitch));
			const EPlayerWeapon Slot = Player->GetActiveWeapon();
			if (Slot != EPlayerWeapon::Bomb && Slot != EPlayerWeapon::SleepSignal)
			{
				const FTransform GripPose = Player->Railgun->Arms->GetSocketTransform(TEXT("HandGrip_R"), RTS_Component);
				const FVector Barrel = GripPose.GetUnitAxis(EAxis::Y);
				const FVector Forward = Player->GetMesh()->GetComponentQuat().UnrotateVector(Player->GetActorForwardVector());
				AimRotation = AimRotation * FQuat::FindBetweenNormals(Barrel, Forward);
				const auto* Mesh = Player->Railgun->WeaponMesh.Get();
				if (Mesh->GetAttachParent() == Player->GetMesh())
				{
					WeaponToHand = Mesh->GetRelativeTransform() * Player->GetMesh()->GetSocketTransform(TEXT("HandGrip_R"), RTS_ParentBoneSpace);
					Support = SupportGrip(Slot);
				}
			}
		}

		virtual void CacheBones() override
		{
			FAnimInstanceProxy::CacheBones();
			const FBoneContainer& Bones = GetRequiredBones();
			auto Find = [&Bones](FName Name)
			{
				const int32 Index = Bones.GetReferenceSkeleton().FindBoneIndex(Name);
				return Index == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
			};
			Spine = Find(TEXT("spine_01"));
			UpperArm = Find(TEXT("upperarm_l"));
			Forearm = Find(TEXT("lowerarm_l"));
			Wrist = Find(TEXT("hand_l"));
			Knuckle = Find(TEXT("middle_01_l"));
			TriggerHand = Find(TEXT("hand_r"));
			UpperBody.Reset();
			if (!Spine.IsValid()) return;
			for (int32 I = Spine.GetInt(); I < Bones.GetCompactPoseNumBones(); ++I)
			{
				const FCompactPoseBoneIndex Bone(I);
				for (auto Parent = Bone; Parent.IsValid(); Parent = Bones.GetParentBoneIndex(Parent))
				{
					if (Parent == Spine) { UpperBody.Add(Bone); break; }
				}
			}
		}

		void FitSupportHand(FCompactPose& Pose) const
		{
			if (!Support.IsSet() || !UpperArm.IsValid() || !Forearm.IsValid() || !Wrist.IsValid() || !Knuckle.IsValid() || !TriggerHand.IsValid()) return;
			FCSPose<FCompactPose> CS;
			CS.InitPose(Pose);
			const FTransform Gun = WeaponToHand * CS.GetComponentSpaceTransform(TriggerHand);
			FTransform Shoulder = CS.GetComponentSpaceTransform(UpperArm);
			FTransform Elbow = CS.GetComponentSpaceTransform(Forearm);
			FTransform Hand = CS.GetComponentSpaceTransform(Wrist);
			const FVector PalmOffset = CS.GetComponentSpaceTransform(Knuckle).GetLocation() - Hand.GetLocation();
			const FQuat Wrap(Gun.GetUnitAxis(EAxis::Y), FMath::DegreesToRadians(18.));
			const FVector WristTarget = Gun.TransformPosition(Support.GetValue()) - Wrap.RotateVector(PalmOffset);
			const FQuat HandRotation = Wrap * Hand.GetRotation();
			const FVector Pole = Elbow.GetLocation();
			AnimationCore::SolveTwoBoneIK(Shoulder, Elbow, Hand, Pole, WristTarget, false, 1., 1.);
			// Leave arm lengths and the authored finger curls intact.
			const FQuat Clavicle = CS.GetComponentSpaceTransform(Pose.GetParentBoneIndex(UpperArm)).GetRotation();
			Pose[UpperArm].SetRotation((Clavicle.Inverse() * Shoulder.GetRotation()).GetNormalized());
			Pose[Forearm].SetRotation((Shoulder.GetRotation().Inverse() * Elbow.GetRotation()).GetNormalized());
			Pose[Wrist].SetRotation((Elbow.GetRotation().Inverse() * HandRotation).GetNormalized());
		}

		virtual bool Evaluate(FPoseContext& Output) override
		{
			if (!Input) return false;
			Input->Evaluate_AnyThread(Output);
			if (!Hold || !Spine.IsValid()) return true;
			FPoseContext WeaponPose(Output);
			FAnimationPoseData PoseData(WeaponPose);
			Hold->GetAnimationPose(PoseData, FAnimExtractContext(Time));
			for (const auto Bone : UpperBody)
				Output.Pose[Bone] = WeaponPose.Pose[Bone];
			FQuat ParentRotation = FQuat::Identity;
			FQuat HoldParentRotation = FQuat::Identity;
			for (auto Bone = Output.Pose.GetParentBoneIndex(Spine); Bone.IsValid(); Bone = Output.Pose.GetParentBoneIndex(Bone))
			{
				ParentRotation = Output.Pose[Bone].GetRotation() * ParentRotation;
				HoldParentRotation = WeaponPose.Pose[Bone].GetRotation() * HoldParentRotation;
			}
			auto& Chest = Output.Pose[Spine];
			// Preserve the authored chest orientation even when the locomotion pelvis turns under it.
			Chest.SetRotation((ParentRotation.Inverse() * AimRotation * HoldParentRotation * Chest.GetRotation()).GetNormalized());
			FitSupportHand(Output.Pose);
			return true;
		}
	};
}

FAnimInstanceProxy* UThirdPersonWeaponPose::CreateAnimInstanceProxy() { return new FWeaponPoseProxy(this); }
void UThirdPersonWeaponPose::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete static_cast<FWeaponPoseProxy*>(Proxy); }
