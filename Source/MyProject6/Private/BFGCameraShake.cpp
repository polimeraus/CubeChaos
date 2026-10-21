#include "BFGCameraShake.h"

UBFGCameraShake::UBFGCameraShake(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    bSingleInstance = true;
    SetRootShakePattern(CreateDefaultSubobject<UBFGCameraShakePattern>(TEXT("BFGRecoilPulse")));
}

void UBFGCameraShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
    OutInfo.Duration = FCameraShakeDuration(Duration);
}

void UBFGCameraShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& Params)
{
    Elapsed = 0.f;
}

void UBFGCameraShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
    Elapsed = FMath::Clamp(Elapsed + Params.DeltaTime, 0.f, Duration);
    Evaluate(OutResult);
}

void UBFGCameraShakePattern::ScrubShakePatternImpl(const FCameraShakePatternScrubParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
    Elapsed = FMath::Clamp(Params.AbsoluteTime, 0.f, Duration);
    Evaluate(OutResult);
}

void UBFGCameraShakePattern::Evaluate(FCameraShakePatternUpdateResult& OutResult) const
{
    const float T = Elapsed / Duration;
    const float Envelope = FMath::Square(1.f - T);
    const float Kick = FMath::Sin(UE_PI * T) * Envelope;
    const float Vibration = FMath::Sin(UE_TWO_PI * 14.f * Elapsed) * Envelope;
    OutResult.Location = FVector(-1.6f * Kick, .3f * Vibration, .7f * Kick);
    OutResult.Rotation = FRotator(2.2f * Kick + .3f * Vibration, .2f * Vibration, .45f * Vibration);
}
