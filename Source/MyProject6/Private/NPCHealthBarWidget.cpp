#include "NPCHealthBarWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SProgressBar.h"

UNPCHealthBarWidget::UNPCHealthBarWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FSlateBrush Background = *FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	Background.TintColor = FLinearColor(0.008f, 0.025f, 0.008f, 0.95f);
	ProgressStyle.SetBackgroundImage(Background);
	ProgressStyle.SetFillImage(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
	SetIsFocusable(false);
}

TSharedRef<SWidget> UNPCHealthBarWidget::RebuildWidget()
{
	return SAssignNew(Outline, SBorder)
		.Visibility(EVisibility::HitTestInvisible)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor(BarColor)
		.Padding(1.0f)
		[
			SAssignNew(ProgressBar, SProgressBar)
			.Style(&ProgressStyle)
			.Percent(HealthPercent)
			.BarFillType(EProgressBarFillType::LeftToRight)
			.BarFillStyle(EProgressBarFillStyle::Scale)
			.FillColorAndOpacity(BarColor)
			.BorderPadding(FVector2D::ZeroVector)
		];
}

void UNPCHealthBarWidget::SetHealthPercent(float InPercent)
{
	HealthPercent = FMath::Clamp(InPercent, 0.0f, 1.0f);
	if (ProgressBar) { ProgressBar->SetPercent(HealthPercent); }
}

void UNPCHealthBarWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	ProgressBar.Reset();
	Outline.Reset();
}

void UNPCHealthBarWidget::SetBarColor(FLinearColor InColor)
{
	if (BarColor == InColor) { return; }
	BarColor = InColor;
	if (Outline) { Outline->SetBorderBackgroundColor(BarColor); }
	if (ProgressBar) { ProgressBar->SetFillColorAndOpacity(BarColor); }
}
