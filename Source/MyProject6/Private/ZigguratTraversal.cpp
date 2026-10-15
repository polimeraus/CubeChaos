#include "ZigguratTraversal.h"
#include "Components/ChildActorComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace ZigguratTraversal
{
void InitializeMaterials(FZigguratTraversalSettings& S)
{
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Arrows(TEXT("/Game/GameplayActors/LaunchPad/Materials/M_LaunchPad"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Rings(TEXT("/Game/GameplayActors/LaunchPad/Materials/M_LaunchPad_Rings"));
    S.ArrowsMaterial=Arrows.Get(); S.RingsMaterial=Rings.Get();
}
double FitOpening(const FZigguratTraversalSettings& S,double RoomWidth)
{
    const double Requested=FMath::IsFinite(S.OpeningWidth)?FMath::Clamp(double(S.OpeningWidth),100.0,2000.0):200.0;
    return FMath::Min(Requested,FMath::Max(20.0,RoomWidth-20.0));
}
void UpdatePad(UChildActorComponent* C,const FZigguratTraversalSettings& S,double Width,double Foundation,double Height)
{
    if (!C) return;
    C->SetRelativeLocation(FVector(0,0,Foundation));
    C->SetChildActorClass(S.bGroundLaunchPad?ALaunchPad::StaticClass():nullptr);
    auto* Pad=Cast<ALaunchPad>(C->GetChildActor());
    if (!Pad) return;
    Pad->bEnabled=true;
    Pad->Shape=ELaunchPadShape::Square;
    Pad->VelocityMode=ELaunchPadVelocityMode::SeparateSpeeds;
    Pad->Size=FMath::Clamp(FMath::IsFinite(S.PadWidth)?S.PadWidth:160.f,50.f,float(FMath::Max(50.0,Width-20.0)));
    Pad->ForwardSpeed=0;
    Pad->bPreserveHorizontalVelocity=false;
    Pad->bCenterBeforeLaunch=true;
    Pad->bUseTargetHeight=S.bAutoLaunchHeight;
    const double ScaleZ=FMath::Abs(C->GetComponentScale().Z);
    Pad->TargetRiseHeight=FMath::Max(50.0,(Height-Foundation-8)*ScaleZ+
        FMath::Clamp(FMath::IsFinite(S.HeightAboveRoof)?double(S.HeightAboveRoof):300.0,50.0,3000.0));
    Pad->LaunchUpSpeed=FMath::Clamp(FMath::IsFinite(S.LaunchUpSpeed)?S.LaunchUpSpeed:2400.f,100.f,10000.f);
    Pad->Cooldown=S.Cooldown;
    Pad->bAffectPlayers=true; Pad->bAffectNPCs=S.bAffectNPCs;
    Pad->Pattern=S.Pattern; Pad->Color=S.Color; Pad->Brightness=S.Brightness;
    Pad->AnimationSpeed=S.AnimationSpeed; Pad->RingCount=S.RingCount; Pad->RingThickness=S.RingThickness;
    Pad->PadMaterial=S.ArrowsMaterial; Pad->RingsMaterial=S.RingsMaterial;
    Pad->RefreshPad();
}
void SolidSections(FVector2D Bottom,FVector2D Top,double Z0,double Z1,double Width,
    TFunctionRef<void(FVector2D,FVector2D,FVector2D,FVector2D,double,double)> Emit)
{
    if (Width<=0) { Emit(-Bottom*.5,Bottom*.5,-Top*.5,Top*.5,Z0,Z1);return; }
    const double Half=Width*.5;
    TArray<double> Cuts={0,1};
    for (int32 Axis=0;Axis<2;++Axis)
        if (!FMath::IsNearlyEqual(Bottom[Axis],Top[Axis]))
        {
            const double A=(Width-Bottom[Axis])/(Top[Axis]-Bottom[Axis]);
            if (A>0 && A<1) Cuts.AddUnique(A);
        }
    Cuts.Sort();
    for (int32 I=0;I<Cuts.Num()-1;++I)
    {
        const FVector2D B=FMath::Lerp(Bottom,Top,Cuts[I])*.5,T=FMath::Lerp(Bottom,Top,Cuts[I+1])*.5;
        const double Lo=FMath::Lerp(Z0,Z1,Cuts[I]),Hi=FMath::Lerp(Z0,Z1,Cuts[I+1]);
        // No tapered zero-width slivers: those strips end at a crossing and are
        // absent beyond it. Each emitted strip has positive width at both ends.
        // Move a vanishing edge by a tiny amount inside the solid, never into the shaft.
        auto Strip=[&](FVector2D B0,FVector2D B1,FVector2D T0,FVector2D T1)
        {
            const FVector2D BS=B1-B0,TS=T1-T0;
            if (BS.GetMin()<=.001 || TS.GetMin()<=.001)
            {
                // A slope exactly meeting the shaft yields a triangular wedge.
                // Keep it quadrilateral by omitting at most .01 cm of its tip.
                const double Epsilon=FMath::Min(.001,.01/FMath::Max(Hi-Lo,.01));
                if (BS.GetMin()<=.001 && TS.GetMin()>.001)
                    Emit(FMath::Lerp(B0,T0,Epsilon),FMath::Lerp(B1,T1,Epsilon),T0,T1,FMath::Lerp(Lo,Hi,Epsilon),Hi);
                else if (TS.GetMin()<=.001 && BS.GetMin()>.001)
                    Emit(B0,B1,FMath::Lerp(T0,B0,Epsilon),FMath::Lerp(T1,B1,Epsilon),Lo,FMath::Lerp(Hi,Lo,Epsilon));
                return;
            }
            Emit(B0,B1,T0,T1,Lo,Hi);
        };
        if (FMath::Max(B.X,T.X)>Half+.001)
        {
            Strip({-B.X,-B.Y},{-Half,B.Y},{-T.X,-T.Y},{-Half,T.Y});
            Strip({Half,-B.Y},{B.X,B.Y},{Half,-T.Y},{T.X,T.Y});
        }
        if (FMath::Max(B.Y,T.Y)>Half+.001)
        {
            const double BX=FMath::Min(B.X,Half),TX=FMath::Min(T.X,Half);
            Strip({-BX,-B.Y},{BX,-Half},{-TX,-T.Y},{TX,-Half});
            Strip({-BX,Half},{BX,B.Y},{-TX,Half},{TX,T.Y});
        }
    }
}
}
