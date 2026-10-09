#pragma once

#include "CoreMinimal.h"

/** Camera-local presentation only. Never changes the camera, aim ray or movement. */
struct FRailgunViewMotion
{
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;

    void AddShot(float Strength)
    {
        Strength = FMath::Clamp(Strength, 0.f, 1.5f);
        const float Side = (++ShotIndex % 2) ? 1.f : -1.f;
        KickVelocity += FVector(-105.f, Side * 4.f, 22.f) * Strength;
        AngularKickVelocity += FVector(95.f, Side * 8.f, -Side * 13.f) * Strength;
        KickVelocity = KickVelocity.GetClampedToMaxSize(150.f);
        AngularKickVelocity = AngularKickVelocity.GetClampedToMaxSize(140.f);
    }

    void Land(float DownwardSpeed)
    {
        const float Impact = FMath::Clamp((DownwardSpeed - 100.f) / 750.f, 0.f, 1.f);
        KickVelocity.Z -= 48.f * Impact;
        AngularKickVelocity.X -= 52.f * Impact;
    }

    void Advance(float DeltaSeconds, const FVector& LocalVelocity, const FVector& LocalAcceleration,
        const FVector2D& LookRate, bool bFalling)
    {
        if (DeltaSeconds <= 0.f || !FMath::IsFinite(DeltaSeconds)) return;
        // Bound hitch recovery; fixed-size substeps keep the springs stable at low frame rates.
        const float Duration = FMath::Min(DeltaSeconds, .1f);
        const int32 Steps = FMath::CeilToInt(Duration * 120.f);
        const float Step = Duration / Steps;
        const float Speed = LocalVelocity.Size2D();
        const float GroundMotion = bFalling ? 0.f : FMath::Clamp(Speed / 900.f, 0.f, 1.f);
        const float Run = bFalling ? 0.f : FMath::Clamp((Speed - 420.f) / 480.f, 0.f, 1.f);
        const FVector Acceleration = LocalAcceleration.GetClampedToMaxSize(4000.f);
        const float YawRate = FMath::Clamp(LookRate.X, -300.f, 300.f);
        const float PitchRate = FMath::Clamp(LookRate.Y, -250.f, 250.f);
        for (int32 Index = 0; Index < Steps; ++Index)
        {
            BreathPhase = FMath::Fmod(BreathPhase + Step * 2.f * PI * .24f, 2.f * PI);
            if (!bFalling && Speed > 5.f)
                StridePhase = FMath::Fmod(StridePhase + Step * 2.f * PI * (1.35f + FMath::Min(Speed, 900.f) / 900.f), 2.f * PI);
            Gait += (GroundMotion - Gait) * (1.f - FMath::Exp(-9.f * Step));
            RunBlend += (Run - RunBlend) * (1.f - FMath::Exp(-7.f * Step));
            const float Breath = FMath::Sin(BreathPhase);
            const float Stride = FMath::Sin(StridePhase);
            const float StepBounce = FMath::Cos(StridePhase * 2.f);
            const float SideSpeed = FMath::Clamp(LocalVelocity.Y / 900.f, -1.f, 1.f);
            const float AirLift = bFalling ? FMath::Clamp(-LocalVelocity.Z / 600.f, -1.f, 1.f) : 0.f;
            const FVector TargetLocation(
                -.9f * RunBlend - Acceleration.X * .00018f + .18f * Gait * StepBounce,
                .10f * FMath::Cos(BreathPhase * 2.f) + 1.05f * Gait * Stride - YawRate * .002f - Acceleration.Y * .00015f,
                .20f * Breath + .60f * Gait * StepBounce - .8f * RunBlend + .85f * AirLift - PitchRate * .0015f);
            // Angular vector stores pitch, yaw, roll (degrees).
            const FVector TargetAngles(
                .16f * Breath + .55f * Gait * StepBounce - 1.1f * RunBlend + .7f * AirLift - PitchRate * .006f,
                .55f * Gait * Stride - YawRate * .006f - .5f * SideSpeed,
                .12f * FMath::Cos(BreathPhase) + 1.15f * Gait * Stride - 1.7f * SideSpeed - .7f * RunBlend + YawRate * .003f);
            const float Follow = 1.f - FMath::Exp(-18.f * Step);
            DriftLocation += (TargetLocation - DriftLocation) * Follow;
            DriftAngles += (TargetAngles - DriftAngles) * Follow;
            Spring(Kick, KickVelocity, Step);
            Spring(AngularKick, AngularKickVelocity, Step);
        }
        Location = (DriftLocation + Kick).GetClampedToMaxSize(6.f);
        const FVector Angles = (DriftAngles + AngularKick).GetClampedToMaxSize(7.f);
        Rotation = FRotator(Angles.X, Angles.Y, Angles.Z);
    }

private:
    static void Spring(FVector& Position, FVector& Velocity, float Step)
    {
        Velocity += (-400.f * Position - 30.f * Velocity) * Step;
        Position += Velocity * Step;
    }
    float BreathPhase = 0.f;
    float StridePhase = 0.f;
    float Gait = 0.f;
    float RunBlend = 0.f;
    int32 ShotIndex = 0;
    FVector DriftLocation = FVector::ZeroVector;
    FVector DriftAngles = FVector::ZeroVector;
    FVector Kick = FVector::ZeroVector;
    FVector KickVelocity = FVector::ZeroVector;
    FVector AngularKick = FVector::ZeroVector;
    FVector AngularKickVelocity = FVector::ZeroVector;
};
