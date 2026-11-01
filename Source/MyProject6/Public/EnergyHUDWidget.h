// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "EnergyHUDWidget.generated.h"

class SBorder;
class SProgressBar;
class STextBlock;
class SWidget;
class SButton;
class SPlayerDamageFeedback;

/** Native, asset-free energy HUD displayed in the top-left corner. */
UCLASS()
class MYPROJECT6_API UEnergyHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UEnergyHUDWidget(const FObjectInitializer& ObjectInitializer);
	void SetEnergy(float CurrentEnergy, float MaxEnergy);
	void NotifyEnergyDamage(float DamageFraction);
	void ClearDamageFeedback();
	void ShowGameOver(float SurvivalSeconds = 0.0f);
	TSharedPtr<SWidget> GetGameOverFocusTarget() const;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	friend class FPlayerDamageFeedbackTest;
	friend class FPlayerDamageFeedbackRenderTest;
	void RefreshEnergyDisplay();
	FReply RestartClicked();
	FReply QuitClicked();
	FReply ControlsClicked();
	FText GetSurvivalTimeText() const;
	FText GetControlsButtonText() const;

	float DisplayedEnergy = 100.0f;
	float DisplayedMaxEnergy = 100.0f;
	float SurvivalTime = 0.0f;
	bool bGameOverVisible = false;
	bool bControlsVisible = false;
	FButtonStyle PrimaryButtonStyle;
	FButtonStyle SecondaryButtonStyle;
	FSlateBrush MenuCardBrush;

	TSharedPtr<SProgressBar> EnergyProgressBar;
	TSharedPtr<STextBlock> EnergyValueText;
	TSharedPtr<SBorder> GameOverPanel;
	TSharedPtr<SBorder> ControlsPanel;
	TSharedPtr<SButton> RestartButton;
	TSharedPtr<SPlayerDamageFeedback> DamageFeedback;
};
