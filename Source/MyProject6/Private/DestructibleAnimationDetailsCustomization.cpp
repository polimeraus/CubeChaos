// Copyright Epic Games, Inc. All Rights Reserved.

#include "DestructibleAnimationDetailsCustomization.h"

#if WITH_EDITOR

#include "ChunkedDestructibleFloor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DestructibleAnimationDetailsCustomization"

namespace
{
	struct FDestructibleCategoryMapping
	{
		const TCHAR* ClassName;
		const TCHAR* CategoryName;
	};

	// These names mirror the existing top-level categories used by each native actor.
	constexpr FDestructibleCategoryMapping DestructibleCategoryMappings[] =
	{
		{ TEXT("SimpleHouseActorDestructable"), TEXT("Simple House Destructable") },
		{ TEXT("ChunkedDestructibleHalloweenPumpkin5"), TEXT("Destructible Halloween Pumpkin 5") },
		{ TEXT("ChunkedDestructibleHalloweenPumpkin4"), TEXT("Destructible Halloween Pumpkin 4") },
		{ TEXT("ChunkedDestructibleHalloweenPumpkin3"), TEXT("Destructible Halloween Pumpkin 3") },
		{ TEXT("ChunkedDestructibleHalloweenPumpkin2"), TEXT("Destructible Halloween Pumpkin 2") },
		{ TEXT("ChunkedDestructibleHalloweenPumpkin"), TEXT("Destructible Halloween Pumpkin") },
		{ TEXT("ChunkedDestructibleConeStructure"), TEXT("Destructible Cone Structure") },
		{ TEXT("ChunkedDestructiblePyramid"), TEXT("Destructible Pyramid") },
		{ TEXT("ChunkedDestructibleRoundedBlob"), TEXT("Destructible Rounded Blob") },
		{ TEXT("ChunkedDestructibleTriangle"), TEXT("Destructible Triangle") },
		{ TEXT("ChunkedDestructibleHouse"), TEXT("Destructible House") },
		{ TEXT("ChunkedDestructibleDecorativeFloor"), TEXT("Chunked Floor") },
		{ TEXT("ChunkedFloor"), TEXT("Chunked Floor") },
		{ TEXT("ChunkedDestructibleFloor"), TEXT("Chunked Floor") },
	};

	FName ResolveDestructibleCategory(const UClass* ObjectClass)
	{
		for (const UClass* Class = ObjectClass; Class; Class = Class->GetSuperClass())
		{
			for (const FDestructibleCategoryMapping& Mapping : DestructibleCategoryMappings)
			{
				if (Class->GetName() == Mapping.ClassName)
				{
					return FName(Mapping.CategoryName);
				}
			}
		}

		return TEXT("Destructible Actor");
	}

	UClass* GetCustomizedObjectClass(IDetailLayoutBuilder& DetailBuilder, FName& OutCategoryName)
	{
		TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
		DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);

		UClass* ObjectClass = nullptr;
		for (const TWeakObjectPtr<UObject>& WeakObject : CustomizedObjects)
		{
			const UObject* Object = WeakObject.Get();
			if (!Object)
			{
				continue;
			}

			const FName ObjectCategory = ResolveDestructibleCategory(Object->GetClass());
			if (!ObjectClass)
			{
				ObjectClass = Object->GetClass();
				OutCategoryName = ObjectCategory;
			}
			else if (OutCategoryName != ObjectCategory)
			{
				// A mixed multi-selection has no single actor-specific settings category.
				OutCategoryName = TEXT("Destructible Actors");
			}
		}

		if (!ObjectClass)
		{
			ObjectClass = DetailBuilder.GetBaseClass();
			OutCategoryName = ResolveDestructibleCategory(ObjectClass);
		}

		return ObjectClass;
	}

	int32 MoveAnimationProperties(
		IDetailLayoutBuilder& DetailBuilder,
		const UClass* ObjectClass,
		const FString& CategoryPrefix,
		IDetailGroup& TargetGroup,
		const FString& GroupNamePrefix,
		bool bAddProperties = true)
	{
		if (!ObjectClass)
		{
			return 0;
		}

		int32 AddedPropertyCount = 0;
		TMap<FString, IDetailGroup*> ChildGroups;
		const FString ChildPrefix = CategoryPrefix + TEXT("|");

		for (TFieldIterator<FProperty> PropertyIt(ObjectClass, EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}

			const FString PropertyCategory = Property->GetMetaData(TEXT("Category"));
			if (PropertyCategory != CategoryPrefix && !PropertyCategory.StartsWith(ChildPrefix))
			{
				continue;
			}

			const TSharedRef<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(
				Property->GetFName(),
				Property->GetOwnerStruct());
			if (!PropertyHandle->IsValidHandle())
			{
				continue;
			}

			DetailBuilder.HideProperty(PropertyHandle);
			if (!bAddProperties)
			{
				continue;
			}

			IDetailGroup* DestinationGroup = &TargetGroup;
			if (PropertyCategory.Len() > ChildPrefix.Len())
			{
				const FString ChildCategory = PropertyCategory.RightChop(ChildPrefix.Len());
				IDetailGroup*& ChildGroup = ChildGroups.FindOrAdd(ChildCategory);
				if (!ChildGroup)
				{
					const FName ChildGroupName(*FString::Printf(TEXT("%s_%s"), *GroupNamePrefix, *ChildCategory));
					ChildGroup = &TargetGroup.AddGroup(
						ChildGroupName,
						FText::FromString(ChildCategory),
						true);
					ChildGroup->SetDisplayMode(EDetailGroupDisplayMode::Category);
				}
				DestinationGroup = ChildGroup;
			}

			DestinationGroup->AddPropertyRow(PropertyHandle);
			++AddedPropertyCount;
		}

		return AddedPropertyCount;
	}
}

TSharedRef<IDetailCustomization> FDestructibleAnimationDetailsCustomization::MakeInstance()
{
	return MakeShared<FDestructibleAnimationDetailsCustomization>();
}

void FDestructibleAnimationDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FName DestructibleCategoryName;
	UClass* ObjectClass = GetCustomizedObjectClass(DetailBuilder, DestructibleCategoryName);
	if (!ObjectClass || !ObjectClass->IsChildOf(AChunkedDestructibleFloor::StaticClass()))
	{
		return;
	}

	IDetailCategoryBuilder& DestructibleCategory = DetailBuilder.EditCategory(DestructibleCategoryName);
	IDetailGroup& AnimationGroup = DestructibleCategory.AddGroup(
		TEXT("DestructibleAnimation"),
		LOCTEXT("AnimationGroup", "Animation"),
		false,
		true);
	AnimationGroup.SetDisplayMode(EDetailGroupDisplayMode::Category);

	// Nested groups deliberately create a second indentation level in the Details panel.
	IDetailGroup& FixedGroup = AnimationGroup.AddGroup(
		TEXT("DestructibleFixedAnimation"),
		LOCTEXT("FixedGroup", "Fixed"),
		true);
	FixedGroup.SetDisplayMode(EDetailGroupDisplayMode::Category);
	const AChunkedDestructibleFloor* DestructibleDefaults =
		Cast<AChunkedDestructibleFloor>(ObjectClass->GetDefaultObject());
	const bool bSupportsFixedAnimation = DestructibleDefaults &&
		DestructibleDefaults->IsFixedAnimationSupported();

	const int32 FixedPropertyCount = MoveAnimationProperties(
		DetailBuilder,
		ObjectClass,
		TEXT("Animation|Fixed"),
		FixedGroup,
		TEXT("DestructibleFixedAnimation"),
		bSupportsFixedAnimation);
	if (FixedPropertyCount == 0)
	{
		FixedGroup.AddWidgetRow()
			.FilterString(LOCTEXT("NoFixedAnimationFilter", "Fixed Animation"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoFixedAnimation", "No fixed animation settings are available for this actor."))
				.AutoWrapText(true)
			];
	}

	IDetailGroup& MusicGroup = AnimationGroup.AddGroup(
		TEXT("DestructibleMusicAnimation"),
		LOCTEXT("MusicGroup", "Music"),
		true);
	MusicGroup.SetDisplayMode(EDetailGroupDisplayMode::Category);
	MoveAnimationProperties(
		DetailBuilder,
		ObjectClass,
		TEXT("Animation|Music"),
		MusicGroup,
		TEXT("DestructibleMusicAnimation"),
		true);
}

#undef LOCTEXT_NAMESPACE

#endif
