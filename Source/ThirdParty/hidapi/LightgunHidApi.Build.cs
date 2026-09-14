// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

using System.IO;
using UnrealBuildTool;

// hidapi is used under its BSD-3-Clause license option. See LICENSE at the plugin root.
//
// External module: include paths and platform link dependencies only. The platform backend .c file
// is compiled into LightgunCore (Private/HidApi/LightgunHidApi.c), so no binaries are shipped.
public class LightgunHidApi : ModuleRules
{
	public LightgunHidApi(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (!File.Exists(Path.Combine(ModuleDirectory, "hidapi", "hidapi.h")))
		{
			throw new BuildException(
				"hidapi sources are missing from {0}. They are part of the plugin: restore Source/ThirdParty/hidapi from the repository.",
				ModuleDirectory);
		}

		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "hidapi"));
		// The backend wrapper includes "windows/hid.c", "mac/hid.c" or "linux/hid.c" from here.
		PublicSystemIncludePaths.Add(ModuleDirectory);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "windows"));
			// No import libraries: hidapi loads hid.dll and cfgmgr32.dll itself at runtime.
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "mac"));
			PublicFrameworks.AddRange(new string[] { "IOKit", "CoreFoundation" });
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			PublicSystemLibraries.Add("udev");
		}
	}
}
