// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT
//
// Compiles hidapi's platform backend into LightgunCore (hidapi is used under BSD-3-Clause; see LICENSE).
// UBT builds .c files as C, without the module PCH. Third-party warnings are silenced here rather than
// by loosening the module's settings.

#include "LightgunHidApiRename.h"

#if defined(_MSC_VER)
	#pragma warning(push, 0)
#elif defined(__clang__)
	#pragma clang diagnostic push
	#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
	#pragma GCC diagnostic push
	#pragma GCC diagnostic ignored "-Wall"
	#pragma GCC diagnostic ignored "-Wextra"
#endif

#if defined(_WIN32)
	#include "windows/hid.c" // hidapi_descriptor_reconstruct.c is a separate unit: LightgunHidApiWinDescriptor.c
#elif defined(__APPLE__)
	#include "mac/hid.c"
#elif defined(__linux__)
	#include "linux/hid.c"
#endif

#if defined(_MSC_VER)
	#pragma warning(pop)
#elif defined(__clang__)
	#pragma clang diagnostic pop
#elif defined(__GNUC__)
	#pragma GCC diagnostic pop
#endif
