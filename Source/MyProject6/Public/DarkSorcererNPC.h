#pragma once

#include "CoreMinimal.h"
#include "ChasingNPC.h"
#include "DarkSorcererNPC.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UBlendSpace;

/** Dark Sorcerer presentation on the project's existing enemy gameplay. */
UCLASS(Blueprintable)
class MYPROJECT6_API ADarkSorcererNPC : public AChasingNPC
{

	GENERATED_BODY()
public:
	ADarkSorcererNPC();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dark Sorcerer")
	TObjectPtr<UStaticMeshComponent> Staff;

	/** Asset authoring helper. It has no work in packaged builds or during play. */
	UFUNCTION(BlueprintCallable, Category="Dark Sorcerer|Editor")
	static int32 ConfigureLocomotion(UObject* AnimationBlueprint, UBlendSpace* Locomotion);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void PrepareStaffDeathFade();
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> StaffDeathMaterial;
};
