#pragma once
#include "CoreMinimal.h"

/** Renderer-independent music animation. Planar masks and volumetric pieces sample the same
 * attack/release and traveling-strip timeline; renderers only map rows to their own geometry. */
namespace DestructibleAnimation
{
    inline float EqualizerHeight(double Time,double Start,float Duration,float StartHeight,float PeakHeight)
    {
        const float Progress=(Time-Start)/FMath::Max(Duration,.01f);
        if (Progress<0 || Progress>=1) { return 0; }
        constexpr float Attack=.12f;
        return Progress<Attack ? FMath::Lerp(StartHeight,PeakHeight,FMath::SmoothStep(0.f,Attack,Progress))
            : PeakHeight*(1.f-FMath::SmoothStep(Attack,1.f,Progress));
    }
    struct FMusicColumnSample
    {
        bool bActive=false, bFilled=false;
        float Progress=0, Envelope=0;
        int32 PeakRows=0, Head=0, Count=0, TailLength=1;
        int32 Row(int32 Index) const { return bFilled ? Index : Head-Index; }
        float Fade(int32 Index) const { return bFilled ? 1.f : 1.f-float(Index)/TailLength; }
    };
    inline FMusicColumnSample SampleMusicColumn(double Time,double Start,float Duration,float StartHeight,
        float PeakHeight,int32 Rows,int32 TailLength,bool bFilled)
    {
        FMusicColumnSample S; S.bFilled=bFilled; S.TailLength=FMath::Clamp(TailLength,1,8);
        S.Progress=(Time-Start)/FMath::Max(Duration,.01f);
        S.bActive=Rows>0 && S.Progress>=0 && S.Progress<1;
        if (!S.bActive) { return S; }
        S.Envelope=bFilled ? 1.f : FMath::Square(1.f-S.Progress);
        S.PeakRows=FMath::Clamp(FMath::RoundToInt(Rows*PeakHeight),1,Rows);
        S.Head=FMath::FloorToInt(S.Progress*(S.PeakRows+S.TailLength));
        S.Count=bFilled ? FMath::Clamp(FMath::RoundToInt(EqualizerHeight(Time,Start,Duration,StartHeight,PeakHeight)*Rows),0,Rows) : S.TailLength;
        return S;
    }
}
