// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedFloorDetailsCustomization.h"

#if WITH_EDITOR

#include "ChunkedDestructibleDecorativeFloor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "ChunkedFloorDetailsCustomization"

namespace
{
	void ConfigureMotifGroup(
		IDetailGroup& Group,
		const TSharedPtr<IPropertyHandle>& SettingsHandle)
	{
		uint32 ChildCount = 0;
		if (!SettingsHandle.IsValid() || SettingsHandle->GetNumChildren(ChildCount) != FPropertyAccess::Success)
		{
			return;
		}

		// Using the actual struct property as the group header preserves its native
		// EditConditionHides evaluation and makes that visibility apply to the entire subtree.
		Group.HeaderProperty(SettingsHandle.ToSharedRef());

		for (uint32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
		{
			const TSharedPtr<IPropertyHandle> ChildHandle = SettingsHandle->GetChildHandle(ChildIndex);
			if (ChildHandle.IsValid())
			{
				Group.AddPropertyRow(ChildHandle.ToSharedRef());
			}
		}
	}
}

TSharedRef<IDetailCustomization> FChunkedFloorDetailsCustomization::MakeInstance()
{
	return MakeShared<FChunkedFloorDetailsCustomization>();
}

void FChunkedFloorDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	const TSharedPtr<IPropertyHandle> FloorPatternHandle = DetailBuilder.GetProperty(
		TEXT("FloorPattern"),
		AChunkedDestructibleFloor::StaticClass());
	const TSharedPtr<IPropertyHandle> SpiralSettingsHandle = DetailBuilder.GetProperty(
		TEXT("SpiralSettings"),
		AChunkedDestructibleFloor::StaticClass());
	const TSharedPtr<IPropertyHandle> HeartSettingsHandle = DetailBuilder.GetProperty(
		TEXT("HeartMotifSettings"),
		AChunkedDestructibleDecorativeFloor::StaticClass());
	const TSharedPtr<IPropertyHandle> StarSettingsHandle = DetailBuilder.GetProperty(
		TEXT("StarMotifSettings"),
		AChunkedDestructibleDecorativeFloor::StaticClass());

	if (!FloorPatternHandle.IsValid() ||
		!SpiralSettingsHandle.IsValid() ||
		!HeartSettingsHandle.IsValid() ||
		!StarSettingsHandle.IsValid())
	{
		return;
	}

	// The struct properties remain the serialized source of truth, but their native top-level
	// rows are replaced by true child groups beneath the Floor Pattern property row.
	DetailBuilder.HideProperty(SpiralSettingsHandle);
	DetailBuilder.HideProperty(HeartSettingsHandle);
	DetailBuilder.HideProperty(StarSettingsHandle);

	IDetailCategoryBuilder& MaterialsCategory = DetailBuilder.EditCategory(
		TEXT("Chunked Floor|Materials"));
	IDetailGroup& FloorPatternGroup = MaterialsCategory.AddGroup(
		TEXT("FloorPatternWithMotifs"),
		LOCTEXT("FloorPatternGroup", "Floor Pattern"),
		false,
		true);
	FloorPatternGroup.HeaderProperty(FloorPatternHandle.ToSharedRef());

	IDetailGroup& SpiralGroup = FloorPatternGroup.AddGroup(
		TEXT("SpiralChildren"),
		LOCTEXT("SpiralGroup", "Spiral"),
		true);
	ConfigureMotifGroup(
		SpiralGroup,
		SpiralSettingsHandle);

	IDetailGroup& HeartGroup = FloorPatternGroup.AddGroup(
		TEXT("HeartMotifChildren"),
		LOCTEXT("HeartMotifGroup", "Heart"),
		true);
	ConfigureMotifGroup(
		HeartGroup,
		HeartSettingsHandle);

	IDetailGroup& StarGroup = FloorPatternGroup.AddGroup(
		TEXT("StarMotifChildren"),
		LOCTEXT("StarMotifGroup", "Star"),
		true);
	ConfigureMotifGroup(
		StarGroup,
		StarSettingsHandle);
}

#undef LOCTEXT_NAMESPACE

#endif
