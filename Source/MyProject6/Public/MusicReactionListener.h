#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "MusicReactionListener.generated.h"

struct FMusicReactionEvent;

/** Native listeners share the world's music clock, event routing, seek and stop behavior. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UMusicReactionListener : public UInterface
{
	GENERATED_BODY()
};

class MYPROJECT6_API IMusicReactionListener
{
	GENERATED_BODY()
public:
	virtual bool IsMusicReactionEnabled() const = 0;
	virtual void ApplyMusicEvent(const FMusicReactionEvent& Event, double CurrentTime) = 0;
	virtual void ResetMusicPlayback() = 0;
};
