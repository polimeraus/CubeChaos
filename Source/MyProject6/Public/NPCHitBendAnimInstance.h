#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "NPCHitBendAnimInstance.generated.h"

class UPoseableMeshComponent;

/** Post-process pose: keeps locomotion, then folds the spine towards the actor's front. */
UCLASS(Transient, Blueprintable)
class MYPROJECT6_API UNPCHitBendAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	// Used by SetupNPCForwardBend.py to build the skeleton-independent Input Pose graph.
	UFUNCTION(BlueprintCallable, Category = "NPC|Editor")
	static bool ConfigureInputPose(UObject* AnimationBlueprint);
	/** Refresh exported test poses in an editor scene, which does not tick poseable meshes. */
	UFUNCTION(BlueprintCallable, Category = "NPC|Editor")
	static void RefreshPreviewPose(UPoseableMeshComponent* Mesh);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
