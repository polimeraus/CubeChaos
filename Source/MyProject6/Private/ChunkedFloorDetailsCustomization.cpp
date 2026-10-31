// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChunkedFloorDetailsCustomization.h"

#if WITH_EDITOR

#include "ChunkedDestructibleDecorativeFloor.h"
#include "ParametricPlatform.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"

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
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	const bool bPlatform = Objects.Num() > 0 && Objects[0].IsValid() && Objects[0]->IsA<AParametricPlatform>();
	if (!bPlatform)
	{
		const auto Shape = DetailBuilder.GetProperty(TEXT("OuterShape"), AChunkedDestructibleDecorativeFloor::StaticClass());
		const auto Restriction = MakeShared<FPropertyRestriction>(LOCTEXT("PlatformShapesOnly", "Available on Parametric Platform."));
		const UEnum* Shapes = StaticEnum<EChunkedDecorativeFloorShape>();
		for (int32 Value = int32(EChunkedDecorativeFloorShape::LShape); Value <= int32(EChunkedDecorativeFloorShape::ParametricShape); ++Value)
			Restriction->AddHiddenValue(Shapes->GetNameStringByValue(Value));
		Shape->AddRestriction(Restriction);
	}
	if (bPlatform)
	{
		// Present the inherited settings once, under the unified actor's vocabulary.
		for (TFieldIterator<FProperty> It(AChunkedDestructibleDecorativeFloor::StaticClass()); It; ++It)
		{
			const FName Name = It->GetFName();
			const FString Category = It->GetMetaData(TEXT("Category"));
			const auto Handle = DetailBuilder.GetProperty(Name, It->GetOwnerClass());
			if (Name == TEXT("ShapeSize"))
				Handle->SetInstanceMetaData(TEXT("EditCondition"), TEXT("OuterShape != EChunkedDecorativeFloorShape::ParametricShape"));
			if (Name == TEXT("BorderWidth") || Name == TEXT("BorderMaterialIndex"))
				Handle->SetInstanceMetaData(TEXT("EditCondition"), TEXT("BorderStyle != EPlatformBorder::None"));
			if (Name == TEXT("AreaSize") || Name == TEXT("CellDimensions") || Name == TEXT("bUseBrightBorder"))
			{
				DetailBuilder.HideProperty(Handle);
				continue;
			}
			if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_EditConst) ||
				!Category.StartsWith(TEXT("Chunked Floor|")) ||
				Name == TEXT("FloorPattern") || Name == TEXT("SpiralSettings") ||
				Name == TEXT("HeartMotifSettings") || Name == TEXT("StarMotifSettings") || Name == TEXT("FlowerMotifSettings")) continue;
			FString NewCategory = Category.Replace(TEXT("Chunked Floor|"), TEXT("Platform|"));
			NewCategory.ReplaceInline(TEXT("Bright Border"), TEXT("Edge"));
			NewCategory.ReplaceInline(TEXT("Generation"), TEXT("Cells"));
			DetailBuilder.HideProperty(Handle);
			DetailBuilder.EditCategory(FName(*NewCategory)).AddProperty(Handle);
		}
		DetailBuilder.EditCategory(TEXT("Platform|Mode")).SetSortOrder(0);
		DetailBuilder.EditCategory(TEXT("Platform|Shape")).SetSortOrder(1);
		DetailBuilder.EditCategory(TEXT("Platform|Dimensions")).SetSortOrder(2);
		DetailBuilder.EditCategory(TEXT("Platform|Materials")).SetSortOrder(3);
		DetailBuilder.EditCategory(TEXT("Platform|Edge")).SetSortOrder(4);
	}
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
	const TSharedPtr<IPropertyHandle> FlowerSettingsHandle = DetailBuilder.GetProperty(
		TEXT("FlowerMotifSettings"),
		AChunkedDestructibleDecorativeFloor::StaticClass());

	if (!FloorPatternHandle.IsValid() ||
		!SpiralSettingsHandle.IsValid() ||
		!HeartSettingsHandle.IsValid() ||
		!StarSettingsHandle.IsValid() ||
		!FlowerSettingsHandle.IsValid())
	{
		return;
	}

	// The struct properties remain the serialized source of truth, but their native top-level
	// rows are replaced by true child groups beneath the Floor Pattern property row.
	DetailBuilder.HideProperty(FloorPatternHandle);
	DetailBuilder.HideProperty(SpiralSettingsHandle);
	DetailBuilder.HideProperty(HeartSettingsHandle);
	DetailBuilder.HideProperty(StarSettingsHandle);
	DetailBuilder.HideProperty(FlowerSettingsHandle);

	IDetailCategoryBuilder& MaterialsCategory = DetailBuilder.EditCategory(
		bPlatform ? TEXT("Platform|Materials") : TEXT("Chunked Floor"));
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

	IDetailGroup& FlowerGroup = FloorPatternGroup.AddGroup(
		TEXT("FlowerMotifChildren"),
		LOCTEXT("FlowerMotifGroup", "Flower"),
		true);
	ConfigureMotifGroup(
		FlowerGroup,
		FlowerSettingsHandle);
	if (bPlatform)
	{
		const auto Settings = DetailBuilder.GetProperty(TEXT("PadPathSettings"), AParametricPlatform::StaticClass());
		const auto Warning = DetailBuilder.GetProperty(TEXT("PadPathWarning"), AParametricPlatform::StaticClass());
		DetailBuilder.HideProperty(Settings);
		DetailBuilder.HideProperty(Warning);
		auto& Group = FloorPatternGroup.AddGroup(TEXT("CirclesAndPathsChildren"), LOCTEXT("CirclesAndPathsGroup", "Circles & Paths"), true);
		ConfigureMotifGroup(Group, Settings);
		Group.AddPropertyRow(Warning);
	}
}

#undef LOCTEXT_NAMESPACE

#endif
