#pragma once

#include "CoreMinimal.h"
#include "PlayerWeapon.generated.h"

UENUM(BlueprintType)
enum class EPlayerWeapon : uint8
{
    Default UMETA(DisplayName="Default Rifle"),
    Railgun,
    BFG UMETA(DisplayName="BFG 10K"),
    Bomb UMETA(DisplayName="Fuse Bomb"),
    SleepSignal UMETA(DisplayName="Sleep Signal")
};
