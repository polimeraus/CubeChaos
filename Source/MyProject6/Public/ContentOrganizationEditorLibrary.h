#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ContentOrganizationEditorLibrary.generated.h"

/** Commandlet-only helpers for the backed-up Content organization migration. */
UCLASS()
class MYPROJECT6_API UContentOrganizationEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Editor|Content Organization", meta=(DevelopmentOnly))
    static bool RenameOrganizedAssets(const TArray<UObject*>& Assets, const TArray<FString>& TargetPackages);
};
