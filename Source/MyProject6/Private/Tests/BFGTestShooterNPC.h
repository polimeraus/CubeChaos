#pragma once

#include "CoreMinimal.h"
#include "ShooterNPC.h"
#include "BFGTestShooterNPC.generated.h"

/** Concrete damage-test instance of the template's abstract shooter NPC. */
UCLASS(Transient, NotPlaceable)
class ABFGTestShooterNPC : public AShooterNPC
{
    GENERATED_BODY()
protected:
    // The damage test does not need a loadout or AI controller.
    virtual void BeginPlay() override { ACharacter::BeginPlay(); }
public:
    bool IsDeadForTest() const { return bIsDead; }
};
