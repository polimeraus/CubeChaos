#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "NPCHealthBarWidget.generated.h"

class SProgressBar;
class SBorder;

/** Small rectangular health gauge with a matching outline and fill. */
UCLASS()
class MYPROJECT6_API UNPCHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UNPCHealthBarWidget(const FObjectInitializer& ObjectInitializer);
	void SetHealthPercent(float InPercent);
	float GetHealthPercent() const { return HealthPercent; }
	void SetBarColor(FLinearColor InColor);
	FLinearColor GetBarColor() const { return BarColor; }
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	FProgressBarStyle ProgressStyle;
	TSharedPtr<SProgressBar> ProgressBar;
	TSharedPtr<SBorder> Outline;
	FLinearColor BarColor = FLinearColor(0.04f, 0.9f, 0.08f, 1.0f);
	float HealthPercent = 1.0f;
};
