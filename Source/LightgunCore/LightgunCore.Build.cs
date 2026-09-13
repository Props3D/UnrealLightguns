// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

using UnrealBuildTool;

// Transport layer: HID enumeration, the 40-byte feedback report and the writer thread. No UObjects.
public class LightgunCore : ModuleRules
{
	public LightgunCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core" });

		// Private/HidApi/LightgunHidApi.c compiles the platform hidapi backend into this module.
		PrivateDependencyModuleNames.AddRange(new string[] { "LightgunHidApi" });
	}
}
