#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "NPCHealthBarComponent.generated.h"

/** Screen-facing gauge that follows the character's head. */
UCLASS()
class MYPROJECT6_API UNPCHealthBarComponent : public UWidgetComponent
{
	GENERATED_BODY()

public:
	UNPCHealthBarComponent();

	/** World-space gap from the head bone, including headgear. */
	UPROPERTY(EditAnywhere, Category = "Health Bar", meta = (ClampMin = "0.0", Units = "cm"))
	float HeadClearance = 26.0f;

	/** Color shared by the outline and remaining health; defaults to the original green. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetBarColor, Category = "Health Bar")
	FLinearColor BarColor = FLinearColor(0.04f, 0.9f, 0.08f, 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Health Bar")
	void SetBarColor(FLinearColor InColor);

	void UpdateHealth(float CurrentHealth, float MaxHealth);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateHeadPosition();
	void UpdateVisibility();
	bool CanSeeCharacter() const;
	FName HeadBone;
	bool bHasHealth = true;
};
