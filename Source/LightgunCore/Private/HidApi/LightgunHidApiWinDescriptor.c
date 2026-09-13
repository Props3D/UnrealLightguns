// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT
//
// hidapi's Windows report-descriptor reconstruction, needed by windows/hid.c. Kept in its own
// compilation unit, as hidapi builds it. Empty on other platforms.

#if defined(_WIN32)
	#include "LightgunHidApiRename.h"

	#pragma warning(push, 0)
	#include "windows/hidapi_descriptor_reconstruct.c"
	#pragma warning(pop)
#else
	typedef int LightgunHidApiWinDescriptorUnused; // ISO C forbids an empty translation unit
#endif
