#if WITH_DEV_AUTOMATION_TESTS

#include "RailgunViewMotion.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRailgunMotionTest, "MyProject6.Weapons.RailgunMotion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRailgunMotionTest::RunTest(const FString& Parameters)
{
    auto Advance = [](FRailgunViewMotion& Motion, float Seconds, int32 FPS, FVector Velocity = FVector::ZeroVector,
        bool bAir = false, FVector2D Look = FVector2D::ZeroVector)
    {
        for (int32 Frame = 0; Frame < FMath::RoundToInt(Seconds * FPS); ++Frame)
            Motion.Advance(1.f / FPS, Velocity, FVector::ZeroVector, Look, bAir);
    };
    FRailgunViewMotion Idle;
    Advance(Idle, .5f, 60);
    const FVector IdleFirst = Idle.Location;
    Advance(Idle, 1.f, 60);
    TestTrue(TEXT("Idle breath is present but subtle"), FVector::Distance(IdleFirst, Idle.Location) > .03f && Idle.Location.Size() < .5f);

    auto GaitRange = [&Advance](float Speed)
    {
        FRailgunViewMotion Motion;
        Advance(Motion, 1.f, 60, FVector(Speed, 0, 0));
        float Low = BIG_NUMBER, High = -BIG_NUMBER;
        for (int32 Frame = 0; Frame < 120; ++Frame)
        {
            Motion.Advance(1.f / 60.f, FVector(Speed, 0, 0), FVector::ZeroVector, FVector2D::ZeroVector, false);
            Low = FMath::Min(Low, static_cast<float>(Motion.Location.Y));
            High = FMath::Max(High, static_cast<float>(Motion.Location.Y));
        }
        return High - Low;
    };
    TestTrue(TEXT("Running has more stride motion than walking"), GaitRange(900.f) > 1.5f * GaitRange(300.f));

    FRailgunViewMotion Ground, Air;
    Advance(Ground, 1.f, 60, FVector(900, 0, 0));
    Air = Ground;
    Advance(Ground, 1.f, 60, FVector(900, 0, 0));
    Advance(Air, 1.f, 60, FVector(900, 0, 600), true);
    TestTrue(TEXT("Jump replaces the grounded gait with vertical inertia"), FVector::Distance(Ground.Location, Air.Location) > .3f);
    FRailgunViewMotion Look;
    Advance(Look, .2f, 60, FVector::ZeroVector, false, FVector2D(180, 0));
    TestTrue(TEXT("Turning right makes the weapon lag left"), Look.Rotation.Yaw < -.5f);

    FRailgunViewMotion Baseline, Shot, Landing;
    Shot.AddShot(1.f);
    Landing.Land(800.f);
    Advance(Baseline, .1f, 60);
    Advance(Shot, .1f, 60);
    Advance(Landing, .1f, 60);
    TestTrue(TEXT("Firing kicks backward and lifts the muzzle"), Shot.Location.X < -1.f && Shot.Rotation.Pitch > 1.f);
    TestTrue(TEXT("Landing compresses downward"), Landing.Location.Z < Baseline.Location.Z - .5f);
    Advance(Baseline, 1.f, 60);
    Advance(Shot, 1.f, 60);
    Advance(Landing, 1.f, 60);
    TestTrue(TEXT("Shot settles back into breathing"), Shot.Location.Equals(Baseline.Location, .01f));
    TestTrue(TEXT("Landing settles back into breathing"), Landing.Location.Equals(Baseline.Location, .01f));

    FRailgunViewMotion PreviousBFG, HeavyBFG, HeavyIdle;
    PreviousBFG.AddShot(1.5f);
    HeavyBFG.AddShot(2.f, true);
    Advance(PreviousBFG, .1f, 60);
    Advance(HeavyBFG, .1f, 60);
    Advance(HeavyIdle, .1f, 60);
    TestTrue(TEXT("BFG-only heavy impulse increases translation and muzzle lift by over 30 percent"),
        HeavyBFG.Location.X < PreviousBFG.Location.X * 1.3f && HeavyBFG.Rotation.Pitch > PreviousBFG.Rotation.Pitch * 1.3f);
    Advance(HeavyBFG, 1.f, 60);
    Advance(HeavyIdle, 1.f, 60);
    TestTrue(TEXT("Heavy BFG recoil also settles"), HeavyBFG.Location.Equals(HeavyIdle.Location, .01f));
    for (int32 I=0;I<100;++I)
    {
        HeavyBFG.AddShot(3.f, true);
        Advance(HeavyBFG, .3f, 30);
        TestTrue(TEXT("Repeated maximum BFG recoil remains bounded"),HeavyBFG.Location.Size()<=6.001f
            && FVector(HeavyBFG.Rotation.Pitch,HeavyBFG.Rotation.Yaw,HeavyBFG.Rotation.Roll).Size()<=7.001f);
    }

    FRailgunViewMotion At30, At60, At120;
    for (FRailgunViewMotion* Motion : {&At30, &At60, &At120}) Motion->AddShot(1.f);
    Advance(At30, .3f, 30, FVector(700, 150, 0), false, FVector2D(60, -20));
    Advance(At60, .3f, 60, FVector(700, 150, 0), false, FVector2D(60, -20));
    Advance(At120, .3f, 120, FVector(700, 150, 0), false, FVector2D(60, -20));
    TestTrue(TEXT("Motion matches at 30/60/120 FPS"), At30.Location.Equals(At120.Location, .02f) && At60.Location.Equals(At120.Location, .02f));
    TestTrue(TEXT("Angular response matches at 30/120 FPS"), At30.Rotation.Equals(At120.Rotation, .02f));
    const FVector BeforePause = At30.Location;
    At30.Advance(0.f, FVector::ZeroVector, FVector::ZeroVector, FVector2D::ZeroVector, false);
    TestTrue(TEXT("Zero delta leaves presentation unchanged"), At30.Location.Equals(BeforePause));
    for (int32 Index = 0; Index < 100; ++Index)
    {
        At30.AddShot(.14f);
        At30.Advance(.05f, FVector(900, 900, -2000), FVector(100000), FVector2D(10000), true);
    }
    At30.Advance(2.f, FVector(900, 0, -20000), FVector(100000), FVector2D(10000), true);
    TestTrue(TEXT("Sustained fire, fast falls and hitches stay finite and bounded"), !At30.Location.ContainsNaN()
        && !At30.Rotation.ContainsNaN() && At30.Location.Size() <= 6.001f
        && FVector(At30.Rotation.Pitch, At30.Rotation.Yaw, At30.Rotation.Roll).Size() <= 7.001f);

    // Reproducible presentation samples for the offscreen renderer, using the production simulator.
    FString Samples(TEXT("state,time,x,y,z,pitch,yaw,roll\n"));
    for (const FString& State : {TEXT("idle"), TEXT("walk"), TEXT("run"), TEXT("jump"), TEXT("land"), TEXT("fire"), TEXT("turn"), TEXT("rapid_fire")})
    {
        FRailgunViewMotion Motion;
        for (int32 Frame = 0; Frame < 120; ++Frame)
        {
            FVector Velocity = FVector::ZeroVector;
            FVector2D LookRate = FVector2D::ZeroVector;
            const float T = Frame / 60.f;
            if (State == TEXT("walk")) Velocity.X = 300;
            if (State == TEXT("run")) Velocity.X = 900;
            if (State == TEXT("jump")) Velocity = FVector(600, 0, 600 - 980 * T);
            if (State == TEXT("turn")) LookRate.X = 240 * FMath::Sin(T * 4.f);
            if (State == TEXT("fire") && Frame == 30) Motion.AddShot(1.f);
            if (State == TEXT("land") && Frame == 30) Motion.Land(850.f);
            if (State == TEXT("rapid_fire") && Frame % 3 == 0) Motion.AddShot(.14f);
            Motion.Advance(1.f / 60.f, Velocity, FVector::ZeroVector, LookRate, State == TEXT("jump"));
            if (Frame % 6 == 0)
                Samples += FString::Printf(TEXT("%s,%.3f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f\n"), *State, T,
                    Motion.Location.X, Motion.Location.Y, Motion.Location.Z, Motion.Rotation.Pitch, Motion.Rotation.Yaw, Motion.Rotation.Roll);
        }
    }
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Railgun/Motion");
    IFileManager::Get().MakeDirectory(*Directory, true);
    TestTrue(TEXT("Motion preview samples saved"), FFileHelper::SaveStringToFile(Samples, *(Directory / TEXT("Samples.csv"))));
    return true;
}
#endif
