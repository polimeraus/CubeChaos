// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EnergyHUDWidget.generated.h"

class SBorder;
class SProgressBar;
class STextBlock;
class SWidget;

/** Native, asset-free energy HUD displayed in the top-left corner. */
UCLASS()
class MYPROJECT6_API UEnergyHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetEnergy(float CurrentEnergy, float MaxEnergy);
	void ShowGameOver();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeDestruct() override;

private:
	void RefreshEnergyDisplay();

	float DisplayedEnergy = 100.0f;
	float DisplayedMaxEnergy = 100.0f;

	TSharedPtr<SProgressBar> EnergyProgressBar;
	TSharedPtr<STextBlock> EnergyValueText;
	TSharedPtr<SBorder> GameOverPanel;
};
