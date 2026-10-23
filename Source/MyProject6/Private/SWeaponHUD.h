#pragma once

#include "MyProject6Character.h"
#include "ElectricTetherWeapon.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

/** Resolution independent weapon silhouettes; all state comes from the possessed pawn. */
class SWeaponHUD : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SWeaponHUD) {}
        SLATE_ATTRIBUTE(TWeakObjectPtr<AMyProject6Character>, Player)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Player = Args._Player;
        SetVisibility(EVisibility::HitTestInvisible);
        // Weapon ownership is external state; refresh even inside an invalidation panel.
        ForceVolatile(true);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(330, 142); }

    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
        int32 Layer, const FWidgetStyle&, bool) const override
    {
        const AMyProject6Character* Pawn = Player.Get().Get();
        const EPlayerWeapon Active = Pawn ? Pawn->GetActiveWeapon() : EPlayerWeapon::Default;
        const FLinearColor Colors[] = {FLinearColor(1,.62f,.18f), FLinearColor(.15f,.83f,1), FLinearColor(.36f,1,.2f), FLinearColor(1,.34f,.09f), FLinearColor(.32f,.78f,1.f), FLinearColor(.4f,.86f,1.f)};
        const FLinearColor Accent = Colors[int32(Active)];
        auto Box = [&](float X, float Y, float W, float H, FLinearColor C)
        {
            FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(FVector2f(W,H), FSlateLayoutTransform(FVector2f(X,Y))),
                FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")), ESlateDrawEffect::None, C);
        };
        auto Text = [&](float X, float Y, const FString& Value, int32 Size, FLinearColor C, bool Bold = false)
        {
            FSlateDrawElement::MakeText(Out, Layer+2, G.ToPaintGeometry(FVector2f(300,30), FSlateLayoutTransform(FVector2f(X,Y))),
                Value, FCoreStyle::GetDefaultFontStyle(Bold ? TEXT("Bold") : TEXT("Regular"), Size), ESlateDrawEffect::None, C);
        };
        auto Line = [&](std::initializer_list<FVector2f> Points, float Width, FLinearColor C)
        {
            TArray<FVector2f> P; P.Append(Points.begin(), Points.size());
            FSlateDrawElement::MakeLines(Out, Layer+1, G.ToPaintGeometry(), P, ESlateDrawEffect::None, C, true, Width);
        };
        Box(0,0,330,142,FLinearColor(.008f,.016f,.025f,.91f));
        Box(0,0,3,142,Accent);
        Box(16,32,298,1,FLinearColor(.15f,.24f,.29f,.45f));
        Text(16,10,TEXT("AKTİF SİLAH"),10,FLinearColor(.65f,.74f,.78f),true);
        Text(177,10,TEXT("TEKERLEK / 1–6"),9,FLinearColor(.48f,.59f,.65f));
        if (Active == EPlayerWeapon::ElectricTether)
        {
            Line({{27,76},{27,61},{47,55},{74,55},{84,47},{110,47},{110,55},{91,59}},3,Accent);
            Line({{47,76},{74,76},{84,85},{110,85},{110,77},{91,73}},3,Accent);
            Line({{39,76},{34,92},{46,92},{53,76}},3,Accent);
            Line({{88,52},{78,65},{97,65},{84,80}},2,FLinearColor(.7f,.3f,1));
            const float Power=Pawn&&Pawn->ElectricWeapon?Pawn->ElectricWeapon->GetSwingPower():0.f;
            Box(138,94,170,4,FLinearColor(.1f,.15f,.2f));Box(138,94,170*Power,4,FLinearColor(.65f,.25f,1));
        }
        else if (Active == EPlayerWeapon::SleepSignal)
        {
            Line({{38,82},{38,70},{48,64},{82,64},{93,70},{93,82},{38,82}},3,Accent);
            Line({{47,87},{84,87}},3,Accent);
            Line({{65,64},{65,52},{59,48},{65,42},{71,48},{65,52}},2,FLinearColor(.7f,.5f,1.f));
            for (int32 Ring=0;Ring<3;++Ring)
            {
                TArray<FVector2f> P;
                for (int32 I=0;I<=24;++I)
                {
                    const float A=-2.6f+I*2.05f/24.f, R=17.f+Ring*10.f;
                    P.Add(FVector2f(65+R*FMath::Cos(A),51+R*.7f*FMath::Sin(A)));
                }
                FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),P,ESlateDrawEffect::None,Accent,true,1.5f);
            }
            Text(96,55,TEXT("z"),16,Accent);
        }
        else if (Active == EPlayerWeapon::Bomb)
        {
            TArray<FVector2f> Circle;
            for (int32 I=0; I<=40; ++I)
            {
                const float Angle = I*UE_TWO_PI/40.f;
                Circle.Add(FVector2f(63+23*FMath::Cos(Angle),74+23*FMath::Sin(Angle)));
            }
            FSlateDrawElement::MakeLines(Out,Layer+1,G.ToPaintGeometry(),Circle,ESlateDrawEffect::None,Accent,true,3.f);
            Line({{64,51},{67,46},{76,49},{74,55}},3,Accent);
            Line({{73,47},{79,39},{87,38},{91,42},{97,40}},2,FLinearColor(1,.78f,.35f));
            Line({{97,36},{100,30}},2,Accent);
            Line({{101,40},{109,39}},2,Accent);
            Line({{98,44},{103,50}},2,Accent);
            Line({{48,68},{50,62},{57,59}},3,FLinearColor(1,.7f,.4f));
        }
        else if (Active == EPlayerWeapon::BFG)
        {
            Line({{23,58},{37,48},{75,48},{86,55},{112,55},{112,75},{83,75},{72,83},{36,83},{23,73},{23,58}},2,Accent);
            Line({{35,83},{32,94},{44,94},{50,83}},3,Accent);
            Line({{82,55},{94,49},{113,54},{118,64},{113,77},{94,81},{83,75}},2,Accent);
            Line({{98,59},{111,59},{114,65},{110,72},{98,72}},3,Accent);
            Line({{43,44},{67,44}},3,FLinearColor(.4f,1,.8f));
            Line({{43,55},{61,55},{68,65},{61,75},{43,75},{37,65},{43,55}},2,FLinearColor(.8f,1,.65f));
        }
        else if (Active == EPlayerWeapon::Railgun)
        {
            Line({{20,63},{39,54},{69,54},{78,59},{115,59},{115,66},{78,66},{67,76},{35,76},{20,70},{20,63}},2,Accent);
            Line({{42,76},{37,92},{48,92},{56,76}},3,Accent);
            for(int I=0;I<4;++I) Line({{79.f+I*9,55},{79.f+I*9,70}},2,Accent);
            Line({{31,64},{67,64}},3,FLinearColor(.7f,.96f,1));
        }
        else
        {
            Line({{21,59},{38,63},{43,55},{77,55},{84,60},{115,60},{115,66},{82,66},{76,73},{40,73},{21,81},{21,59}},2,Accent);
            Line({{47,73},{42,91},{51,91},{59,73}},3,Accent);
            Line({{65,73},{62,87},{76,87},{77,73}},3,Accent);
            Line({{54,50},{69,50}},3,Accent);
        }
        const TCHAR* Names[] = {TEXT("BAŞLANGIÇ"),TEXT("RAILGUN"),TEXT("BFG 10K"),TEXT("BOMBA"),TEXT("UYKU SİNYALİ"),TEXT("ARK KAFESİ")};
        const TCHAR* Descriptions[] = {TEXT("Seri atış"),TEXT("Hassas enerji atışı"),TEXT("Patlayıcı plazma"),TEXT("3 sn fitil · 2× BFG"),TEXT("Yakına bırak · Alan uykusu"),TEXT("Basılı tut · Savur · Bırak")};
        Text(137,47,Names[int32(Active)],int32(Active)>=4 ? 16 : 19,Accent,true);
        const bool Captured=Active==EPlayerWeapon::ElectricTether && Pawn && Pawn->ElectricWeapon && Pawn->ElectricWeapon->HasTarget();
        Text(138,76,Captured?TEXT("HEDEF BAĞLI · SAVUR"):Descriptions[int32(Active)],10,FLinearColor(.72f,.8f,.83f));
        const TCHAR* Slots[] = {TEXT("1 TÜF"),TEXT("2 RAIL"),TEXT("3 BFG"),TEXT("4 BOM"),TEXT("5 UYK"),TEXT("6 ARK")};
        for (int32 I=0;I<6;++I)
        {
            const bool Owned = Pawn ? Pawn->HasWeapon(static_cast<EPlayerWeapon>(I)) : I==0;
            const bool Selected = int32(Active)==I;
            Box(16+I*50,108,46,24,Selected ? FLinearColor(Accent.R*.16f,Accent.G*.16f,Accent.B*.16f,.95f) : FLinearColor(.04f,.065f,.08f,.8f));
            Text(21+I*50,112,Slots[I],9,Owned ? Colors[I] : FLinearColor(.25f,.3f,.33f),Selected);
            if(Selected) Box(16+I*50,132,46,2,Accent);
        }
        return Layer+3;
    }
private:
    TAttribute<TWeakObjectPtr<AMyProject6Character>> Player;
};
