#pragma once

#include "Animation/AnimInstance.h"
#include "ThirdPersonWeaponPose.generated.h"

/** Overlays the equipped weapon's upper-body pose on the player's locomotion. */
UCLASS(Transient, Blueprintable)
class MYPROJECT6_API UThirdPersonWeaponPose : public UAnimInstance
{
	GENERATED_BODY()
protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
