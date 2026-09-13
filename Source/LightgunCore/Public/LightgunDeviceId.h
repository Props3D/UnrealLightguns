// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/** Identifies one lightgun found by a feedback backend. */
struct FLightgunDeviceId
{
	/** Backend that found the device, e.g. "BlamconHid". */
	FName BackendName;

	/** Platform device path. Pass back to the backend to open the device. */
	FString Path;

	uint16 VendorId = 0;
	uint16 ProductId = 0;
	FString SerialNumber;
	FString ProductName;

	/** 0-based player index, or INDEX_NONE if the backend cannot tell. */
	int32 PlayerIndex = INDEX_NONE;

	bool operator==(const FLightgunDeviceId& Other) const
	{
		return BackendName == Other.BackendName && Path == Other.Path;
	}

	friend uint32 GetTypeHash(const FLightgunDeviceId& Id)
	{
		return HashCombine(GetTypeHash(Id.BackendName), GetTypeHash(Id.Path));
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("P%d %04x:%04x \"%s\" (%s)"), PlayerIndex + 1, VendorId, ProductId, *ProductName, *Path);
	}
};
