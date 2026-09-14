// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LightgunDeviceInfoReport.h"

enum class ELightgunTransport : uint8
{
	Unknown,
	Usb,
	Bluetooth,
};

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
	/** Empty over Bluetooth on Windows. */
	FString ProductName;

	/** 0-based player index, or INDEX_NONE if the backend cannot tell. */
	int32 PlayerIndex = INDEX_NONE;

	/** From enumeration. Superseded by the mode in Info when the gun reports one. */
	ELightgunTransport Transport = ELightgunTransport::Unknown;

	/** Device version from enumeration (USB bcdDevice), or 0 if not reported, as over Bluetooth on Windows. For logs only. */
	uint16 DeviceVersion = 0;

	/**
	 * Whether this collection sends aim and buttons. False for a gun in mouse mode, whose input goes to the
	 * OS as a mouse while the plugin uses the vendor collection for feedback only.
	 */
	bool bHasGunInput = true;

	/** What the gun reported about itself when it was opened (feature reports 0x50/0x51). */
	FLightgunDeviceInfo Info;

	bool operator==(const FLightgunDeviceId& Other) const
	{
		return BackendName == Other.BackendName && Path == Other.Path;
	}

	friend uint32 GetTypeHash(const FLightgunDeviceId& Id)
	{
		return HashCombine(GetTypeHash(Id.BackendName), GetTypeHash(Id.Path));
	}

	/** Transport, preferring what the gun reported over what enumeration saw. */
	ELightgunTransport GetEffectiveTransport() const
	{
		if (Info.bKnown && LightgunDeviceInfoReport::GetModeName(Info.Mode))
		{
			return LightgunDeviceInfoReport::IsBluetoothMode(Info.Mode) ? ELightgunTransport::Bluetooth : ELightgunTransport::Usb;
		}
		return Transport;
	}

	/** Short identity for log lines: player, VID:PID, product name if any, path. */
	FString ToString() const
	{
		const FString Name = ProductName.IsEmpty() ? FString() : FString::Printf(TEXT(" \"%s\""), *ProductName);
		return FString::Printf(TEXT("P%d %04x:%04x%s (%s)"), PlayerIndex + 1, VendorId, ProductId, *Name, *Path);
	}

	/**
	 * What the gun is, for the connect line: "firmware 3.0.0, RP2350, Bluetooth mouse, feedback yes", or for
	 * legacy firmware "firmware unknown, USB, device version 1.00".
	 */
	FString Describe() const
	{
		using namespace LightgunDeviceInfoReport;

		if (!Info.bKnown)
		{
			FString Result = TEXT("firmware unknown");
			switch (Transport)
			{
			case ELightgunTransport::Usb: Result += TEXT(", USB"); break;
			case ELightgunTransport::Bluetooth: Result += TEXT(", Bluetooth"); break;
			default: break;
			}
			if (DeviceVersion != 0)
			{
				// BCD: 0x0301 = 3.01.
				Result += FString::Printf(TEXT(", device version %x.%02x"), static_cast<uint32>(DeviceVersion >> 8), static_cast<uint32>(DeviceVersion & 0xFF));
			}
			return Result;
		}

		const char* const BoardName = GetBoardName(Info.Board);
		const char* const ModeName = GetModeName(Info.Mode);
		return FString::Printf(TEXT("firmware %u.%u.%u, %s, %s, feedback %s"),
			Info.GetMajor(), Info.GetMinor(), Info.GetPatch(),
			BoardName ? *FString(UTF8_TO_TCHAR(BoardName)) : *FString::Printf(TEXT("board %u"), Info.Board),
			ModeName ? *FString(UTF8_TO_TCHAR(ModeName)) : *FString::Printf(TEXT("mode %u"), Info.Mode),
			Info.bFeedbackAvailable ? TEXT("yes") : TEXT("no"));
	}
};
