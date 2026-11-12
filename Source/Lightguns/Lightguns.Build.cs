// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

using UnrealBuildTool;

// The engine-facing layer: an IInputDevice that owns the guns, their input keys, the Blueprint
// subsystem and function library, and the Enhanced Input pieces for mouse parity.
public class Lightguns : ModuleRules
{
	public Lightguns(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"InputDevice",
			"ApplicationCore",
			"EnhancedInput",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"LightgunCore",
			// IPluginManager, for reporting the plugin's own version.
			"Projects",
			// FSlateApplication, for releasing feedback control when the application loses focus.
			"Slate",
			"SlateCore",
		});
	}
}
