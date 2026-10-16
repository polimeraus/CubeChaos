// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnergyHUDWidget.h"
#include "MyProject6PlayerController.h"
#include "SWeaponHUD.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

UEnergyHUDWidget::UEnergyHUDWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryButtonStyle = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Button"));
	PrimaryButtonStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.10f, 0.82f, 0.74f), 8.0f));
	PrimaryButtonStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.25f, 1.0f, 0.90f), 8.0f));
	PrimaryButtonStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.05f, 0.55f, 0.50f), 8.0f));
	PrimaryButtonStyle.SetNormalForeground(FLinearColor(0.01f, 0.035f, 0.05f));
	PrimaryButtonStyle.SetHoveredForeground(FLinearColor(0.01f, 0.035f, 0.05f));
	PrimaryButtonStyle.SetPressedForeground(FLinearColor::White);
	SecondaryButtonStyle = PrimaryButtonStyle;
	SecondaryButtonStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.045f, 0.065f, 0.10f), 8.0f));
	SecondaryButtonStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.085f, 0.13f, 0.18f), 8.0f));
	SecondaryButtonStyle.SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.025f, 0.045f, 0.075f), 8.0f));
	SecondaryButtonStyle.SetNormalForeground(FLinearColor(0.85f, 0.91f, 0.96f));
	SecondaryButtonStyle.SetHoveredForeground(FLinearColor::White);
	MenuCardBrush = FSlateRoundedBoxBrush(FLinearColor(0.012f, 0.020f, 0.038f), 16.0f,
		FLinearColor(0.10f, 0.17f, 0.22f), 1.0f);
}

TSharedRef<SWidget> UEnergyHUDWidget::RebuildWidget()
{
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 18);
	const FSlateFontInfo ValueFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15);
	const FSlateFontInfo GameOverFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 34);
	const FSlateFontInfo CaptionFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12);

	TSharedRef<SWidget> RootWidget =
		SNew(SOverlay)

		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("+")))
			.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 16))
			.ColorAndOpacity(FLinearColor(.65f, .95f, 1.f, .85f))
			.Visibility(EVisibility::HitTestInvisible)
		]

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

		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(24)
		[
			SNew(SWeaponHUD).Player_Lambda([this]() { return TWeakObjectPtr<AMyProject6Character>(Cast<AMyProject6Character>(GetOwningPlayerPawn())); })
		]

		+ SOverlay::Slot()
		[
			SAssignNew(GameOverPanel, SBorder)
			.Visibility(bGameOverVisible ? EVisibility::Visible : EVisibility::Collapsed)
			.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
			.BorderBackgroundColor(FLinearColor(0.003f, 0.005f, 0.015f, 0.78f))
			.Padding(24.0f)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SScaleBox)
				.Stretch(EStretch::ScaleToFit)
				.StretchDirection(EStretchDirection::DownOnly)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(440.0f)
					[
						SNew(SBorder)
						.BorderImage(&MenuCardBrush)
						.BorderBackgroundColor(FLinearColor::White)
						.Padding(FMargin(32.0f, 28.0f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("EnergyHUD", "GameOver", "OYUN BİTTİ"))
								.Font(GameOverFont)
								.ColorAndOpacity(FLinearColor(1.0f, 0.17f, 0.30f))
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 22)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("EnergyHUD", "EnergyDepletedSentence", "Enerjin tükendi."))
								.Font(ValueFont)
								.ColorAndOpacity(FLinearColor(0.65f, 0.73f, 0.82f))
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SSeparator).ColorAndOpacity(FLinearColor(0.10f, 0.17f, 0.22f))
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 20)
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
								[
									SNew(STextBlock).Text(NSLOCTEXT("EnergyHUD", "SurvivalTitle", "HAYATTA KALMA SÜRESİ"))
									.Font(CaptionFont).ColorAndOpacity(FLinearColor(0.65f, 0.73f, 0.82f))
								]
								+ SHorizontalBox::Slot().AutoWidth()
								[
									SNew(STextBlock).Text_UObject(this, &UEnergyHUDWidget::GetSurvivalTimeText)
									.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 24))
									.ColorAndOpacity(FLinearColor(0.25f, 1.0f, 0.90f))
								]
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SAssignNew(RestartButton, SButton)
								.ButtonStyle(&PrimaryButtonStyle).HAlign(HAlign_Center).ContentPadding(FMargin(16, 13))
								.OnClicked_UObject(this, &UEnergyHUDWidget::RestartClicked)
								[
									SNew(STextBlock).Text(NSLOCTEXT("EnergyHUD", "Restart", "Yeniden Başla"))
									.Font(TitleFont).ColorAndOpacity(FSlateColor::UseForeground())
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
							[
								SNew(SButton)
								.ButtonStyle(&SecondaryButtonStyle).HAlign(HAlign_Center).ContentPadding(FMargin(16, 11))
								.OnClicked_UObject(this, &UEnergyHUDWidget::ControlsClicked)
								[
									SNew(STextBlock).Text_UObject(this, &UEnergyHUDWidget::GetControlsButtonText)
									.Font(ValueFont).ColorAndOpacity(FSlateColor::UseForeground())
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
							[
								SAssignNew(ControlsPanel, SBorder)
								.Visibility(bControlsVisible ? EVisibility::Visible : EVisibility::Collapsed)
								.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
								.BorderBackgroundColor(FLinearColor(0.025f, 0.04f, 0.065f))
								.Padding(14)
								[
									SNew(STextBlock)
									.Text(NSLOCTEXT("EnergyHUD", "ControlsHelp", "W A S D   ·   Hareket\nFare   ·   Etrafa bak\nBoşluk   ·   Zıpla\nSol tık   ·   Ateş et\nFare tekerleği   ·   Silah değiştir\n1 / 2 / 3   ·   Silah seç\nOrta fare   ·   Railgun ateşi\nBackspace   ·   Havadayken platform oluştur"))
									.Font(CaptionFont).ColorAndOpacity(FLinearColor(0.75f, 0.83f, 0.91f))
									.AutoWrapText(true)
								]
							]
							+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
							[
								SNew(SButton)
								.ButtonStyle(&SecondaryButtonStyle).HAlign(HAlign_Center).ContentPadding(FMargin(16, 11))
								.OnClicked_UObject(this, &UEnergyHUDWidget::QuitClicked)
								[
									SNew(STextBlock).Text(NSLOCTEXT("EnergyHUD", "Quit", "Çık"))
									.Font(ValueFont).ColorAndOpacity(FSlateColor::UseForeground())
								]
							]
							+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 18, 0, 0)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("EnergyHUD", "MenuNavigation", "Tab / Ok tuşları: Seç    ·    Enter: Onayla"))
								.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
								.ColorAndOpacity(FLinearColor(0.42f, 0.52f, 0.63f))
							]
						]
					]
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

void UEnergyHUDWidget::ShowGameOver(float SurvivalSeconds)
{
	bGameOverVisible = true;
	SurvivalTime = FMath::Max(0.0f, SurvivalSeconds);
	// Also handles game over before the first viewport paint.
	TakeWidget();
	if (GameOverPanel.IsValid())
	{
		GameOverPanel->SetVisibility(EVisibility::Visible);
	}
}

TSharedPtr<SWidget> UEnergyHUDWidget::GetGameOverFocusTarget() const
{
	return RestartButton;
}

FText UEnergyHUDWidget::GetSurvivalTimeText() const
{
	const int32 Seconds = FMath::FloorToInt(SurvivalTime);
	return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60));
}

FText UEnergyHUDWidget::GetControlsButtonText() const
{
	return bControlsVisible ? NSLOCTEXT("EnergyHUD", "HideControls", "Kontrolleri Gizle")
		: NSLOCTEXT("EnergyHUD", "Controls", "Kontroller");
}

FReply UEnergyHUDWidget::ControlsClicked()
{
	bControlsVisible = !bControlsVisible;
	if (ControlsPanel) { ControlsPanel->SetVisibility(bControlsVisible ? EVisibility::Visible : EVisibility::Collapsed); }
	return FReply::Handled();
}

FReply UEnergyHUDWidget::RestartClicked()
{
	if (AMyProject6PlayerController* Controller = Cast<AMyProject6PlayerController>(GetOwningPlayer()))
	{
		Controller->RestartAfterGameOver();
	}
	return FReply::Handled();
}

FReply UEnergyHUDWidget::QuitClicked()
{
	if (AMyProject6PlayerController* Controller = Cast<AMyProject6PlayerController>(GetOwningPlayer()))
	{
		Controller->QuitAfterGameOver();
	}
	return FReply::Handled();
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

void UEnergyHUDWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	EnergyProgressBar.Reset();
	EnergyValueText.Reset();
	GameOverPanel.Reset();
	ControlsPanel.Reset();
	RestartButton.Reset();
}
