#pragma once

#include "GameFramework/SpringArmComponent.h"
#include "PlayerPerspectiveArm.generated.h"

/** Reports the resolved camera position after the spring arm has swept for obstacles. */
UCLASS()
class UPlayerPerspectiveArm : public USpringArmComponent
{
	GENERATED_BODY()

public:
	DECLARE_DELEGATE_OneParam(FOnPositionResolved, float);
	FOnPositionResolved OnPositionResolved;

	void RefreshPosition()
	{
		UpdateDesiredArmLocation(bDoCollisionTest, false, false, 0.f);
	}

protected:
	virtual void UpdateDesiredArmLocation(bool bTrace, bool bLocationLag, bool bRotationLag, float DeltaTime) override
	{
		Super::UpdateDesiredArmLocation(bTrace, bLocationLag, bRotationLag, DeltaTime);
		OnPositionResolved.ExecuteIfBound(DeltaTime);
	}
};
