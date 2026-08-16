// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MyProject6 : ModuleRules
{
	public MyProject6(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Several actor implementations intentionally reuse translation-unit-local helper names.
		// Keep them as separate translation units so clean builds do not merge those names together.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"GeometryCore",
			"GeometryFramework",
			"GeometryScriptingCore",
			"Text3D"
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"Slate",
			"SlateCore"
		});

		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("PropertyEditor");
		}

		PublicIncludePaths.AddRange(new string[] {
			"MyProject6",
			"MyProject6/Variant_Horror",
			"MyProject6/Variant_Horror/UI",
			"MyProject6/Variant_Shooter",
			"MyProject6/Variant_Shooter/AI",
			"MyProject6/Variant_Shooter/UI",
			"MyProject6/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}

