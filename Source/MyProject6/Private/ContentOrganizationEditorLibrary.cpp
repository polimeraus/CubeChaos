#include "ContentOrganizationEditorLibrary.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "CoreGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#endif

bool UContentOrganizationEditorLibrary::RenameOrganizedAssets(
    const TArray<UObject*>& Assets, const TArray<FString>& TargetPackages)
{
#if WITH_EDITOR
    if (!IsRunningCommandlet() || !FParse::Param(FCommandLine::Get(), TEXT("ContentOrganization"))
        || Assets.Num() != TargetPackages.Num())
    {
        return false;
    }
    TArray<FAssetRenameData> Moves;
    for (int32 Index=0; Index<Assets.Num(); ++Index)
    {
        UObject* Asset=Assets[Index];
        const FString& Target=TargetPackages[Index];
        if (!Asset || !Asset->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
            || !Target.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Target)
            || FPackageName::GetLongPackageAssetName(Target) != Asset->GetName())
        {
            return false;
        }
        Moves.Emplace(Asset,FPackageName::GetLongPackagePath(Target),Asset->GetName());
    }
    // This migration also rewrites and rebuilds native asset paths. Acknowledge
    // only that specific informational warning; unexpected prompts cancel.
    const auto PreviousDialog=FCoreDelegates::ModalMessageDialog;
    TGuardValue<bool> ScriptScope(GIsRunningUnattendedScript,false);
    FCoreDelegates::ModalMessageDialog.BindLambda(
        [](EAppMsgCategory, EAppMsgType::Type, const FText& Message, const FText&)
        {
            const FString Text=Message.ToString();
            if (Text.StartsWith(TEXT("Source code, config INI, and text files may need Find/Replace for:")))
            {
                UE_LOG(LogTemp,Display,TEXT("Content migration: native references scheduled for rewrite: %s"),*Text);
                return EAppReturnType::Ok;
            }
            UE_LOG(LogTemp,Error,TEXT("Content migration rejected unexpected prompt: %s"),*Text);
            return EAppReturnType::Cancel;
        });
    const bool Result=FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().RenameAssets(Moves);
    FCoreDelegates::ModalMessageDialog=PreviousDialog;
    return Result;
#else
    return false;
#endif
}
