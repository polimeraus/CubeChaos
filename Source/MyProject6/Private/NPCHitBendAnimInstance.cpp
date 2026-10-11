#include "NPCHitBendAnimInstance.h"

#include "NPCHitReactionComponent.h"
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

		virtual void PreUpdate(UAnimInstance* Instance, float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(Instance, DeltaSeconds);
			InputPose = Instance->GetLinkedInputPoseNode();
			BendRadians = 0.f;
			if (AActor* Actor = Instance->GetOwningActor())
			{
				if (const auto* Reaction = Actor->FindComponentByClass<UNPCHitReactionComponent>())
				{
					BendRadians = FMath::DegreesToRadians(Reaction->GetCurrentForwardBendAngle());
					// Imported meshes face different local axes. Actor right consistently bends
					// the torso forwards, including shots from behind or from the side.
					BendAxis = Instance->GetSkelMeshComponent()->GetComponentQuat()
						.UnrotateVector(Actor->GetActorRightVector()).GetSafeNormal();
				}
			}
		}

		virtual void CacheBones() override
		{
			FAnimInstanceProxy::CacheBones();
			const FBoneContainer& Bones = GetRequiredBones();
			const FReferenceSkeleton& Ref = Bones.GetReferenceSkeleton();
			SpineWeights.Init(0.f, Bones.GetCompactPoseNumBones());
			ComponentRotations.SetNum(SpineWeights.Num());
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
			if (FMath::IsNearlyZero(BendRadians) || SpineWeights.Num() != Output.Pose.GetNumBones()) return true;
			// Only local spine rotations change; hips/legs and the incoming animation stay intact.
			// All UObject reads were copied in PreUpdate on the game thread.
			for (const FCompactPoseBoneIndex Bone : Output.Pose.ForEachBoneIndex())
			{
				const FCompactPoseBoneIndex Parent = Output.Pose.GetParentBoneIndex(Bone);
				const FQuat ParentRotation = Parent.IsValid() ? ComponentRotations[Parent.GetInt()] : FQuat::Identity;
				FTransform& Local = Output.Pose[Bone];
				const float Weight = SpineWeights[Bone.GetInt()];
				if (Weight > 0.f)
				{
					const FVector LocalAxis = ParentRotation.UnrotateVector(BendAxis);
					Local.SetRotation((FQuat(LocalAxis, BendRadians * Weight) * Local.GetRotation()).GetNormalized());
				}
				ComponentRotations[Bone.GetInt()] = ParentRotation * Local.GetRotation();
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
