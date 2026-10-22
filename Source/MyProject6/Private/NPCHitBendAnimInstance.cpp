#include "NPCHitBendAnimInstance.h"

#include "NPCHitReactionComponent.h"
#include "NPCSleepComponent.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_LinkedInputPose.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "GameFramework/Actor.h"

#if WITH_EDITOR
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_Root.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#endif

namespace
{
	struct FNPCHitBendProxy : FAnimInstanceProxy
	{
		explicit FNPCHitBendProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}
		FAnimNode_LinkedInputPose* InputPose = nullptr;
		TArray<float> SpineWeights;
		TArray<FQuat> ComponentRotations;
		FVector BendAxis = FVector::RightVector;
		float BendRadians = 0.f;
		float SleepAlpha = 0.f, Curl = 0.f, Nod = 0.f, Breath = 0.f;
		FVector SleepAxis = FVector::RightVector;
		FVector SleepForward = FVector::ForwardVector;
		TArray<FVector2f> SleepWeights;
		TArray<FVector> ArmChildDirections;
		TArray<FTransform> ComponentPose;
		FCompactPoseBoneIndex LimbBones[4][3];
		FVector Targets[4], Poles[4];
		FQuat GroundRotation = FQuat::Identity;
		FVector GroundPoint=FVector::ZeroVector, GroundUp=FVector::UpVector;

		virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
			InputPose = Instance->GetLinkedInputPoseNode();
			BendRadians = 0.f;
			SleepAlpha = Curl = Nod = Breath = 0.f;
			if (AActor* Actor = Instance->GetOwningActor())
			{
				if (const auto* Sleep = Actor->FindComponentByClass<UNPCSleepComponent>())
				{
					SleepAlpha = Sleep->PoseAlpha; Curl = Sleep->Curl; Nod = Sleep->Nod; Breath = Sleep->Breath;
					SleepAxis = Sleep->PoseBendAxis; SleepForward = Sleep->PoseForwardAxis;
					if (SleepAlpha>0.f) for (int I=0;I<4;++I) { Targets[I]=Sleep->LimbTargets[I]; Poles[I]=Sleep->LimbPoles[I]; }
					GroundRotation=Sleep->GroundRotation;
					GroundPoint=Sleep->GroundPoint;GroundUp=Sleep->GroundUp;
				}
				if (const auto* Reaction = Actor->FindComponentByClass<UNPCHitReactionComponent>())
				{
					BendRadians = FMath::DegreesToRadians(Reaction->GetCurrentForwardBendAngle());
					// Imported meshes face different local axes. Actor right consistently bends
					// the torso forwards, including shots from behind or from the side.
					BendAxis = Instance->GetSkelMeshComponent()->GetComponentQuat()
						.UnrotateVector(Actor->GetActorRightVector()).GetSafeNormal();
					if (SleepAlpha > .5f) BendAxis=SleepAxis;
				}
			}
		}

		virtual void CacheBones() override
		{
			FAnimInstanceProxy::CacheBones();
			const FBoneContainer& Bones = GetRequiredBones();
			const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
			SpineWeights.Init(0.f, Bones.GetCompactPoseNumBones());
			SleepWeights.Init(FVector2f::ZeroVector, Bones.GetCompactPoseNumBones());
			ArmChildDirections.Init(FVector::ZeroVector, Bones.GetCompactPoseNumBones());
			for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
			{
				const auto Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
				if (!Compact.IsValid()) continue;
				const FString Name = Ref.GetBoneName(Index).ToString().ToLower();
				if (Name.Contains(TEXT("twist")) || Name.Contains(TEXT("correct"))) continue;
				FVector2f Weight(0,0);
				const bool bLeft = Name.EndsWith(TEXT("_l")) || Name.Contains(TEXT("left"));
				if (Name == TEXT("thigh_l") || Name == TEXT("thigh_r") || Name.Contains(TEXT("upleg"))) Weight.Y = bLeft ? -3.f : 3.f;
				else if (Name == TEXT("calf_l") || Name == TEXT("calf_r") || Name.Contains(TEXT("leftleg")) || Name.Contains(TEXT("rightleg"))) Weight.Y = bLeft ? -4.f : 4.f;
				else if (Name == TEXT("upperarm_l") || Name == TEXT("upperarm_r") || Name.Contains(TEXT("leftarm")) || Name.Contains(TEXT("rightarm")))
				{ Weight.Y = bLeft ? -1.f : 1.f; }
				else if (Name == TEXT("lowerarm_l") || Name == TEXT("lowerarm_r") || Name.Contains(TEXT("forearm"))) Weight.Y = bLeft ? -2.f : 2.f;
				else if (Name == TEXT("head") || Name == TEXT("head_01") || Name.Contains(TEXT("_head_"))) Weight.X = 13.f;
				SleepWeights[Compact.GetInt()] = Weight;
				if (Weight.Y != 0.f)
				{
					for (int32 Child=Index+1;Child<Ref.GetNum();++Child)
					{
						if (Ref.GetParentIndex(Child)!=Index) continue;
						const FString ChildName=Ref.GetBoneName(Child).ToString().ToLower();
						if ((FMath::Abs(Weight.Y)==1.f && (ChildName.Contains(TEXT("lowerarm")) || ChildName.Contains(TEXT("forearm"))))
							|| (FMath::Abs(Weight.Y)==2.f && ChildName.Contains(TEXT("hand")))
							|| (FMath::Abs(Weight.Y)==3.f && (ChildName.StartsWith(TEXT("calf_")) || ChildName.Contains(TEXT("leftleg")) || ChildName.Contains(TEXT("rightleg"))))
							|| (FMath::Abs(Weight.Y)==4.f && ChildName.Contains(TEXT("foot"))))
						{
							ArmChildDirections[Compact.GetInt()]=Ref.GetRefBonePose()[Child].GetTranslation().GetSafeNormal(); break;
						}
					}
				}
			}
			ComponentRotations.SetNum(SpineWeights.Num());
			ComponentPose.SetNum(SpineWeights.Num());
			for (auto& Chain:LimbBones) for (auto& Bone:Chain) Bone=FCompactPoseBoneIndex(INDEX_NONE);
			for (int32 I=0;I<SleepWeights.Num();++I)
			{
				const float W=SleepWeights[I].Y;
				if (FMath::Abs(W)!=1.f && FMath::Abs(W)!=3.f) continue;
				const int L=(FMath::Abs(W)==1.f?0:2)+(W>0?1:0);
				LimbBones[L][0]=FCompactPoseBoneIndex(I);
				for (int32 J=I+1;J<SleepWeights.Num();++J)
					if (Bones.GetParentBoneIndex(FCompactPoseBoneIndex(J))==FCompactPoseBoneIndex(I) && FMath::Abs(SleepWeights[J].Y)==FMath::Abs(W)+1)
					{
						LimbBones[L][1]=FCompactPoseBoneIndex(J);
						for (int32 K=J+1;K<SleepWeights.Num();++K)
						{
							if (Bones.GetParentBoneIndex(FCompactPoseBoneIndex(K))!=FCompactPoseBoneIndex(J)) continue;
							const FString Name=Ref.GetBoneName(Bones.MakeMeshPoseIndex(FCompactPoseBoneIndex(K)).GetInt()).ToString().ToLower();
							if ((L<2 && Name.Contains(TEXT("hand"))) || (L>=2 && (Name.Contains(TEXT("foot"))||Name.StartsWith(TEXT("heel_"))))) { LimbBones[L][2]=FCompactPoseBoneIndex(K); break; }
						}
						break;
					}
			}
			TArray<FCompactPoseBoneIndex> Spine;
			// Follow the head's ancestry instead of sorting names: Black Monster's
			// Spine02 is BELOW Spine01, and UE5 includes spine-named corrective bones.
			for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
			{
				const FString Name = Ref.GetBoneName(Index).ToString().ToLower();
				if (Name != TEXT("head") && Name != TEXT("head_01") && !Name.Contains(TEXT("_head_"))) continue;
				for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
				{
					if (Ref.GetBoneName(Parent).ToString().Contains(TEXT("spine")))
					{
						const FCompactPoseBoneIndex Compact = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Parent));
						if (Compact.IsValid()) Spine.Insert(Compact, 0);
					}
				}
				break;
			}
			for (int32 Index = 0; Index < Spine.Num(); ++Index)
			{
				// Most of the fold starts at the waist, with a softer curve through the chest.
				SpineWeights[Spine[Index].GetInt()] = Spine.Num() == 1 ? 1.f : (Index == 0 ? .65f : .35f / (Spine.Num() - 1));
			}
		}

		virtual bool Evaluate(FPoseContext& Output) override
		{
			if (!InputPose) return false;
			InputPose->Evaluate_AnyThread(Output);
			if ((FMath::IsNearlyZero(BendRadians) && SleepAlpha <= 0.f) || SpineWeights.Num() != Output.Pose.GetNumBones()) return true;
			// Only local spine rotations change; hips/legs and the incoming animation stay intact.
			// All UObject reads were copied in PreUpdate on the game thread.
			for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
			{
				const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Bone);
				const FQuat ParentRotation = Parent.IsValid() ? ComponentRotations[Parent.GetInt()] : FQuat::Identity;
				FTransform& Local = Output.Pose[Bone];
				if (SleepAlpha > 0.f)
				{
					// Relax into the skeleton's anatomical pose; never freeze a running stride.
					const FTransform& RefPose = Output.Pose.GetRefPose(Bone);
					Local.Blend(Local, RefPose, SleepAlpha);
					const FVector2f Weights = SleepWeights[Bone.GetInt()];
					const float Fold = Weights.X*Curl + SpineWeights[Bone.GetInt()]*(Curl*22.f+Nod+Breath*1.8f);
					const FQuat Flex(ParentRotation.UnrotateVector(SleepAxis), FMath::DegreesToRadians(Fold));
					Local.SetRotation((Flex*Local.GetRotation()).GetNormalized());
				}
				const float Weight = SpineWeights[Bone.GetInt()];
				if (Weight > 0.f)
				{
					const FVector LocalAxis = ParentRotation.UnrotateVector(BendAxis);
					Local.SetRotation((FQuat(LocalAxis, BendRadians * Weight) * Local.GetRotation()).GetNormalized());
				}
				ComponentRotations[Bone.GetInt()] = ParentRotation * Local.GetRotation();
			}
			if (SleepAlpha>0.f)
			{
				auto Rebuild=[&]()
				{
					for (const auto Bone:Output.Pose.ForEachBoneIndex())
					{
						const auto Parent=Output.Pose.GetParentBoneIndex(Bone);
						ComponentPose[Bone.GetInt()]=Parent.IsValid()?Output.Pose[Bone]*ComponentPose[Parent.GetInt()]:Output.Pose[Bone];
					}
				};
				Rebuild();
				for (int L=0;L<4;++L)
				{
					const auto Upper=LimbBones[L][0], Lower=LimbBones[L][1], End=LimbBones[L][2];
					if (!Upper.IsValid()||!Lower.IsValid()||!End.IsValid()) continue;
					const FTransform A=ComponentPose[Upper.GetInt()], B=ComponentPose[Lower.GetInt()], C=ComponentPose[End.GetInt()];
					const FVector Root=A.GetLocation(),Joint=B.GetLocation(),Tip=C.GetLocation();
					FVector Goal=FMath::Lerp(Tip,Targets[L],SleepAlpha);
					const double LenA=FVector::Dist(Root,Joint),LenB=FVector::Dist(Joint,Tip);
					const double Distance=FMath::Clamp(FVector::Dist(Root,Goal),FMath::Abs(LenA-LenB)+.01,(LenA+LenB)*.995);
					const FVector Axis=(Goal-Root).GetSafeNormal();Goal=Root+Axis*Distance;
					const FVector Pole=Poles[L]-Root;
					FVector Plane=(Pole-Axis*FVector::DotProduct(Pole,Axis)).GetSafeNormal();
					const double Along=(LenA*LenA-LenB*LenB+Distance*Distance)/(2*Distance);
					const double Height=FMath::Sqrt(FMath::Max(0.,LenA*LenA-Along*Along));
					const FVector Center=Root+Axis*Along;
					const FVector ProjectedUp=GroundUp-Axis*FVector::DotProduct(GroundUp,Axis);
					const double ReachUp=ProjectedUp.Size()*Height;
					if (ReachUp>UE_SMALL_NUMBER)
					{
						// Constrain the bend plane continuously; never flip a knee/elbow to the opposite IK solution.
						const FVector Up=ProjectedUp.GetSafeNormal();
						const double MinimumUp=FMath::Clamp((LenA*.12-FVector::DotProduct(Center-GroundPoint,GroundUp))/ReachUp,-1.,1.);
						if (FVector::DotProduct(Plane,Up)<MinimumUp)
						{
							FVector Side=(Plane-Up*FVector::DotProduct(Plane,Up)).GetSafeNormal();
							if (Side.IsNearlyZero()) Side=FVector::CrossProduct(Axis,Up)*(L%2==0?-1:1);
							Plane=Up*MinimumUp+Side*FMath::Sqrt(1.-MinimumUp*MinimumUp);
						}
					}
					const FVector Knee=Center+Plane*Height;
					const FQuat QA=(FQuat::FindBetweenNormals((Joint-Root).GetSafeNormal(),(Knee-Root).GetSafeNormal())*A.GetRotation()).GetNormalized();
					const FQuat QB=(FQuat::FindBetweenNormals((Tip-Joint).GetSafeNormal(),(Goal-Knee).GetSafeNormal())*B.GetRotation()).GetNormalized();
					const auto Parent=Output.Pose.GetParentBoneIndex(Upper);
					Output.Pose[Upper].SetRotation((ComponentPose[Parent.GetInt()].GetRotation().Inverse()*QA).GetNormalized());
					Output.Pose[Lower].SetRotation((QA.Inverse()*QB).GetNormalized());
					// Feet stay flat against the support plane during kneeling and rising.
					FQuat EndRotation=C.GetRotation();
					if (L>=2)
					{
						FTransform Ref=Output.Pose.GetRefPose(End);
						for (auto P=Output.Pose.GetParentBoneIndex(End);P.IsValid();P=Output.Pose.GetParentBoneIndex(P)) Ref=Ref*Output.Pose.GetRefPose(P);
						EndRotation=FQuat::Slerp(EndRotation,GroundRotation*Ref.GetRotation(),SleepAlpha);
					}
					Output.Pose[End].SetRotation((QB.Inverse()*EndRotation).GetNormalized());
					Rebuild();
				}
			}
			return true;
		}
	};
}

FAnimInstanceProxy* UNPCHitBendAnimInstance::CreateAnimInstanceProxy()
{
	return new FNPCHitBendProxy(this);
}

void UNPCHitBendAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy)
{
	delete static_cast<FNPCHitBendProxy*>(Proxy);
}

void UNPCHitBendAnimInstance::RefreshPreviewPose(UPoseableMeshComponent* Mesh)
{
#if WITH_EDITOR
	if (Mesh) Mesh->RefreshBoneTransforms();
#endif
}

bool UNPCHitBendAnimInstance::ConfigureInputPose(UObject* AnimationBlueprint)
{
#if WITH_EDITOR
	UAnimBlueprint* Blueprint = Cast<UAnimBlueprint>(AnimationBlueprint);
	if (!Blueprint || !Blueprint->ParentClass->IsChildOf(StaticClass())) return false;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		UAnimGraphNode_Root* Root = nullptr;
		UAnimGraphNode_LinkedInputPose* Input = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Found = Cast<UAnimGraphNode_Root>(Node)) Root = Found;
			if (auto* Found = Cast<UAnimGraphNode_LinkedInputPose>(Node)) Input = Found;
		}
		if (!Root) continue;
		Blueprint->Modify();
		Graph->Modify();
		if (!Input)
		{
			FGraphNodeCreator<UAnimGraphNode_LinkedInputPose> Creator(*Graph);
			Input = Creator.CreateNode();
			Input->Node.Name = FAnimNode_LinkedInputPose::DefaultInputPoseName;
			Input->NodePosX = -250;
			Creator.Finalize();
		}
		UEdGraphPin* OutputPin = nullptr;
		UEdGraphPin* InputPin = nullptr;
		for (UEdGraphPin* Pin : Input->Pins) if (Pin->Direction == EGPD_Output) OutputPin = Pin;
		for (UEdGraphPin* Pin : Root->Pins) if (Pin->Direction == EGPD_Input) InputPin = Pin;
		if (!OutputPin || !InputPin || !Graph->GetSchema()->TryCreateConnection(OutputPin, InputPin)) return false;
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return true;
	}
#endif
	return false;
}
