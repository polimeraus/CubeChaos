#pragma once

#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

/** A short, translucent corner pulse. No tick, draw calls or timers while idle. */
class SPlayerDamageFeedback : public SLeafWidget
{
    friend class FPlayerDamageFeedbackTest;
    friend class FPlayerDamageFeedbackRenderTest;
public:
    SLATE_BEGIN_ARGS(SPlayerDamageFeedback) {} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        SetVisibility(EVisibility::HitTestInvisible);
        Vertices.Reserve(1400); Indices.Reserve(7000);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
    void Trigger(float DamageFraction)
    {
        if (!FMath::IsFinite(DamageFraction) || DamageFraction <= 0.f) return;
        const double Now = FSlateApplication::Get().GetCurrentTime();
        // Even small contact hits remain visible; repeated damage never stacks to opaque red.
        Peak = FMath::Max(OpacityAt(Now), FMath::Clamp(.65f+DamageFraction*1.5f,.65f,.90f));
        StartedAt = Now;
        Invalidate(EInvalidateWidgetReason::Paint);
        if (!bTimerActive)
        {
            bTimerActive = true;
            RegisterActiveTimer(0.f,FWidgetActiveTimerDelegate::CreateSP(this,&SPlayerDamageFeedback::Animate));
        }
    }
    void Clear()
    {
        Peak = 0.f;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
private:
    static constexpr double Duration = .85;
    static constexpr double HoldDuration = .09;
    double StartedAt = -100.;
    float Peak = 0.f;
    bool bTimerActive = false;
    mutable TArray<FSlateVertex> Vertices;
    mutable TArray<SlateIndex> Indices;
    float OpacityAt(double Now) const
    {
        const float T = FMath::Clamp(float((Now-StartedAt-HoldDuration)/(Duration-HoldDuration)),0.f,1.f);
        return Peak*(1.f-T)*(1.f-T);
    }
    EActiveTimerReturnType Animate(double Now, float)
    {
        Invalidate(EInvalidateWidgetReason::Paint);
        if (OpacityAt(Now) <= 0.f) { bTimerActive = false; return EActiveTimerReturnType::Stop; }
        return EActiveTimerReturnType::Continue;
    }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle& Style, bool) const override
    {
        const double Now = FSlateApplication::Get().GetCurrentTime();
        const float Strength = OpacityAt(Now)*Style.GetColorAndOpacityTint().A;
        if (Strength <= .001f) return Layer;
        const float Age = FMath::Clamp(float((Now-StartedAt)/Duration),0.f,1.f);
        const FVector2f Size(Geometry.GetLocalSize());
        if (Size.X <= 0 || Size.Y <= 0) return Layer;
        Vertices.Reset(); Indices.Reset();
        auto Vertex = [&](FVector2f Point, FLinearColor Color)
        {
            Color.A *= Strength;
            Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(
                Geometry.GetAccumulatedRenderTransform(),Point,FVector2f(.5f),Color.ToFColor(true)));
        };
        auto Triangle = [&](int32 A,int32 B,int32 C)
        {
            Indices.Add(SlateIndex(A)); Indices.Add(SlateIndex(B)); Indices.Add(SlateIndex(C));
        };
        // A translucent red wash communicates damage even when the player is
        // concentrating on the middle of the screen. Geometry remains readable.
        const FLinearColor ScreenTint(1.f,.006f,.018f,.19f);
        Vertex(FVector2f::ZeroVector,ScreenTint);
        Vertex(FVector2f(Size.X,0),ScreenTint);
        Vertex(Size,ScreenTint);
        Vertex(FVector2f(0,Size.Y),ScreenTint);
        Triangle(0,1,2); Triangle(0,2,3);
        for (int32 Corner=0;Corner<4;++Corner)
        {
            const FVector2f Sign((Corner&1) ? -1.f : 1.f,(Corner&2) ? -1.f : 1.f);
            const FVector2f Origin((Corner&1) ? Size.X : 0.f,(Corner&2) ? Size.Y : 0.f);
            auto Position = [&](FVector2f P) { return Origin+P*Sign; };
            // A wider, smoothly feathered corner wash still leaves the aiming area clear.
            const int32 WashStart = Vertices.Num();
            constexpr int32 Rings=8, Segments=16;
            for (int32 Ring=0;Ring<=Rings;++Ring)
            {
                const float R=float(Ring)/Rings;
                const float Fade=1.f-R;
                for (int32 Segment=0;Segment<=Segments;++Segment)
                {
                    const float Angle=UE_HALF_PI*Segment/Segments;
                    Vertex(Position(FVector2f(FMath::Cos(Angle)*Size.X*.46f*R,
                        FMath::Sin(Angle)*Size.Y*.52f*R)),FLinearColor(.9f,.002f,.015f,.60f*Fade*Fade));
                    if (Ring && Segment)
                    {
                        const int32 D=WashStart+Ring*(Segments+1)+Segment, A=D-Segments-2;
                        Triangle(A,A+1,D); Triangle(A,D,D-1);
                    }
                }
            }
            // Retain the narrow neon core inside a much broader red halo. As the
            // pulse fades, the halo spreads and the pale core softens with it.
            const FVector2f Start(Size.X*.012f,Size.Y*.018f), End(Size.X*.27f,Size.Y*.31f);
            const FVector2f Direction=(End-Start).GetSafeNormal();
            const FVector2f Normal(-Direction.Y,Direction.X);
            const float Width=FMath::Clamp(FMath::Min(Size.X/1280.f,Size.Y/720.f),.4f,3.f)*84.f*(1.f+.35f*Age);
            const int32 LineStart=Vertices.Num();
            static constexpr float Cross[] = {-1.f,-.82f,-.60f,-.40f,-.22f,-.10f,-.035f,0.f,.035f,.10f,.22f,.40f,.60f,.82f,1.f};
            static constexpr float Glow[] = {0.f,.018f,.07f,.16f,.29f,.45f,.72f,1.f,.72f,.45f,.29f,.16f,.07f,.018f,0.f};
            constexpr int32 Columns=UE_ARRAY_COUNT(Cross);
            constexpr int32 Steps=10;
            for (int32 Step=0;Step<=Steps;++Step)
            {
                const float T=float(Step)/Steps;
                const float Fade=1.f-T;
                for (int32 C=0;C<Columns;++C)
                {
                    const FVector2f P=FMath::Lerp(Start,End,T)+Normal*Cross[C]*Width*(1.f-.8f*T);
                    Vertex(Position(P),FLinearColor(1.f,C==Columns/2 ? FMath::Lerp(.16f,.008f,Age) : .008f,
                        C==Columns/2 ? FMath::Lerp(.22f,.035f,Age) : .035f,
                        Glow[C]*Fade));
                    if (Step && C)
                    {
                        const int32 D=LineStart+Step*Columns+C,A=D-Columns-1;
                        Triangle(A,A+1,D); Triangle(A,D,D-1);
                    }
                }
            }
        }
        const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
        FSlateDrawElement::MakeCustomVerts(Out,Layer,Resource,Vertices,Indices,nullptr,0,0);
        return Layer;
    }
};
