// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnergyHUDWidget.h"

#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UEnergyHUDWidget::RebuildWidget()
{
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18);
	const FSlateFontInfo ValueFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15);
	const FSlateFontInfo GameOverFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 40);

	TSharedRef<SWidget> RootWidget =
		SNew(SOverlay)

		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(24.0f))
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			.HeightOverride(96.0f)
			[
				SNew(SBorder)
				.Padding(FMargin(14.0f, 10.0f))
				.BorderBackgroundColor(FLinearColor(0.01f, 0.015f, 0.035f, 0.92f))
				[
					SNew(SVerticalBox)

					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("EnergyHUD", "EnergyTitle", "ENERJİ"))
						.Font(TitleFont)
						.ColorAndOpacity(FLinearColor(0.25f, 0.95f, 1.0f, 1.0f))
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(FMargin(0.0f, 7.0f, 0.0f, 5.0f))
					[
						SNew(SBox)
						.HeightOverride(24.0f)
						[
							SAssignNew(EnergyProgressBar, SProgressBar)
							.Percent(1.0f)
							.FillColorAndOpacity(FLinearColor(0.05f, 0.95f, 0.75f, 1.0f))
						]
					]

					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Right)
					[
						SAssignNew(EnergyValueText, STextBlock)
						.Font(ValueFont)
						.ColorAndOpacity(FLinearColor::White)
					]
				]
			]
		]

		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SAssignNew(GameOverPanel, SBorder)
			.Visibility(EVisibility::Collapsed)
			.Padding(FMargin(42.0f, 28.0f))
			.BorderBackgroundColor(FLinearColor(0.035f, 0.0f, 0.015f, 0.94f))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("EnergyHUD", "GameOver", "OYUN BİTTİ"))
					.Font(GameOverFont)
					.ColorAndOpacity(FLinearColor(1.0f, 0.05f, 0.2f, 1.0f))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				.Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("EnergyHUD", "EnergyDepleted", "ENERJİN TÜKENDİ"))
					.Font(TitleFont)
					.ColorAndOpacity(FLinearColor::White)
				]
			]
		];

	RefreshEnergyDisplay();
	return RootWidget;
}

void UEnergyHUDWidget::SetEnergy(const float CurrentEnergy, const float MaxEnergy)
{
	DisplayedMaxEnergy = FMath::Max(1.0f, MaxEnergy);
	DisplayedEnergy = FMath::Clamp(CurrentEnergy, 0.0f, DisplayedMaxEnergy);
	RefreshEnergyDisplay();
}

void UEnergyHUDWidget::ShowGameOver()
{
	if (GameOverPanel.IsValid())
	{
		GameOverPanel->SetVisibility(EVisibility::Visible);
	}
}

void UEnergyHUDWidget::RefreshEnergyDisplay()
{
	const float EnergyFraction = FMath::Clamp(DisplayedEnergy / DisplayedMaxEnergy, 0.0f, 1.0f);
	if (EnergyProgressBar.IsValid())
	{
		EnergyProgressBar->SetPercent(EnergyFraction);
		const FLinearColor FillColor = EnergyFraction > 0.5f
			? FLinearColor(0.05f, 0.95f, 0.75f, 1.0f)
			: EnergyFraction > 0.2f
				? FLinearColor(1.0f, 0.65f, 0.02f, 1.0f)
				: FLinearColor(1.0f, 0.02f, 0.12f, 1.0f);
		EnergyProgressBar->SetFillColorAndOpacity(FillColor);
	}

	if (EnergyValueText.IsValid())
	{
		EnergyValueText->SetText(FText::Format(
			NSLOCTEXT("EnergyHUD", "EnergyValue", "{0} / {1}"),
			FText::AsNumber(FMath::CeilToInt(DisplayedEnergy)),
			FText::AsNumber(FMath::CeilToInt(DisplayedMaxEnergy))));
	}
}

void UEnergyHUDWidget::NativeDestruct()
{
	EnergyProgressBar.Reset();
	EnergyValueText.Reset();
	GameOverPanel.Reset();
	Super::NativeDestruct();
}
