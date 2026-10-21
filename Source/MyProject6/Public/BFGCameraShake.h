#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "BFGCameraShake.generated.h"

/** A brief recoil pulse followed by damped vibration, in camera-local space. */
UCLASS()
class MYPROJECT6_API UBFGCameraShakePattern : public UCameraShakePattern
{
    GENERATED_BODY()
private:
    float Elapsed = 0.f;
    static constexpr float Duration = .2f;
    virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
    virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
    virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
    virtual void ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
    virtual bool IsFinishedImpl() const override { return Elapsed >= Duration; }
    virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params) override { Elapsed = Duration; }
    void Evaluate(FCameraShakePatternUpdateResult& OutResult) const;
};

UCLASS()
class MYPROJECT6_API UBFGCameraShake : public UCameraShakeBase
{
    GENERATED_BODY()
public:
    UBFGCameraShake(const FObjectInitializer& ObjectInitializer);
};
