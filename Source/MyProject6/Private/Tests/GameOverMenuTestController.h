#pragma once

#include "CoreMinimal.h"
#include "MyProject6PlayerController.h"
#include "GameOverMenuTestController.generated.h"

/** Exercises the real menu actions while capturing quit instead of closing the test runner. */
UCLASS(Transient, NotPlaceable)
class AGameOverMenuTestController : public AMyProject6PlayerController
{
	GENERATED_BODY()
public:
	void InitializeTestMenu();
	UEnergyHUDWidget* GetTestMenu() const { return EnergyHUDWidget; }
	virtual FString ConsoleCommand(const FString& Command, bool bWriteToLog = true) override;
	FString LastConsoleCommand;
	int32 ConsoleCommandCount = 0;
protected:
	virtual void BeginPlay() override;
};
