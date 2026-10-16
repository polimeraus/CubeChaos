#pragma once

#include "MyProject6Character.h"
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
        const FLinearColor Colors[] = {FLinearColor(1,.62f,.18f), FLinearColor(.15f,.83f,1), FLinearColor(.36f,1,.2f)};
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
        Text(213,10,TEXT("TEKERLEK  /  1 2 3"),9,FLinearColor(.48f,.59f,.65f));
        if (Active == EPlayerWeapon::BFG)
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
        const TCHAR* Names[] = {TEXT("BAŞLANGIÇ"),TEXT("RAILGUN"),TEXT("BFG 10K")};
        const TCHAR* Descriptions[] = {TEXT("Seri atış"),TEXT("Hassas enerji atışı"),TEXT("Patlayıcı plazma")};
        Text(137,47,Names[int32(Active)],19,Accent,true);
        Text(138,76,Descriptions[int32(Active)],10,FLinearColor(.72f,.8f,.83f));
        const TCHAR* Slots[] = {TEXT("1  TÜFEK"),TEXT("2  RAIL"),TEXT("3  BFG")};
        for (int32 I=0;I<3;++I)
        {
            const bool Owned = Pawn ? Pawn->HasWeapon(static_cast<EPlayerWeapon>(I)) : I==0;
            const bool Selected = int32(Active)==I;
            Box(16+I*101,108,96,24,Selected ? FLinearColor(Accent.R*.16f,Accent.G*.16f,Accent.B*.16f,.95f) : FLinearColor(.04f,.065f,.08f,.8f));
            Text(24+I*101,112,Slots[I],10,Owned ? Colors[I] : FLinearColor(.25f,.3f,.33f),Selected);
            if(Selected) Box(16+I*101,132,96,2,Accent);
        }
        return Layer+3;
    }
private:
    TAttribute<TWeakObjectPtr<AMyProject6Character>> Player;
};
