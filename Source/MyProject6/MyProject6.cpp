// Copyright Epic Games, Inc. All Rights Reserved.

#include "MyProject6.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "ChunkedDestructibleDecorativeFloor.h"
#include "ChunkedFloorDetailsCustomization.h"
#include "PropertyEditorModule.h"
#endif

class FMyProject6Module final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();

#if WITH_EDITOR
		FPropertyEditorModule& PropertyEditorModule =
			FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyEditorModule.RegisterCustomClassLayout(
			AChunkedDestructibleDecorativeFloor::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(
				&FChunkedFloorDetailsCustomization::MakeInstance));
		PropertyEditorModule.NotifyCustomizationModuleChanged();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		if (FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
		{
			FPropertyEditorModule& PropertyEditorModule =
				FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyEditorModule.UnregisterCustomClassLayout(
				AChunkedDestructibleDecorativeFloor::StaticClass()->GetFName());
			PropertyEditorModule.NotifyCustomizationModuleChanged();
		}
#endif

		FDefaultGameModuleImpl::ShutdownModule();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMyProject6Module, MyProject6, "MyProject6");

DEFINE_LOG_CATEGORY(LogMyProject6)
