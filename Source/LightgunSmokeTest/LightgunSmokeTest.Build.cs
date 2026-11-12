// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

using UnrealBuildTool;

// Editor-only milestone 1 smoke test: a commandlet that fires recoil through LightgunCore.
public class LightgunSmokeTest : ModuleRules
{
	public LightgunSmokeTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "LightgunCore" });
	}
}
