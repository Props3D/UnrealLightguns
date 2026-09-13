// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "BlamconHidBackend.h"

#include "LightgunCoreLog.h"
#include "LightgunDeviceMatch.h"
#include "LightgunInputReport.h"
#include "LightgunReport.h"
#include "Misc/ScopeLock.h"

THIRD_PARTY_INCLUDES_START
#include "HidApi/LightgunHidApiRename.h"
#include <hidapi.h>
#if PLATFORM_MAC
#include <hidapi_darwin.h>
#endif
THIRD_PARTY_INCLUDES_END

const FName FBlamconHidBackend::BackendName(TEXT("BlamconHid"));

namespace BlamconHid
{
	/**
	 * hidapi's global state is shared by every backend instance and every open device, so hid_init and
	 * hid_exit are reference counted. An open connection holds a reference too: hid_exit must not run
	 * while a device is still open, including one abandoned by a hung writer thread.
	 */
	FCriticalSection LifetimeLock;
	int32 LifetimeRefs = 0;

	bool AcquireHidApi()
	{
		FScopeLock Lock(&LifetimeLock);
		if (LifetimeRefs == 0)
		{
			if (hid_init() != 0)
			{
				UE_LOG(LogLightgun, Error, TEXT("hidapi failed to initialise; lightgun feedback is unavailable."));
				return false;
			}
#if PLATFORM_MAC
			// hidapi opens devices exclusively on macOS by default, which would hide the gun from the OS.
			hid_darwin_set_open_exclusive(0);
#endif
		}
		++LifetimeRefs;
		return true;
	}

	void ReleaseHidApi()
	{
		FScopeLock Lock(&LifetimeLock);
		check(LifetimeRefs > 0);
		if (--LifetimeRefs == 0)
		{
			hid_exit();
		}
	}

	/** hidapi strings are wchar_t, which is UTF-32 on macOS and Linux. Serials and product names are ASCII. */
	FString FromWide(const wchar_t* Wide)
	{
		FString Out;
		for (const wchar_t* Char = Wide; Char && *Char; ++Char)
		{
			Out.AppendChar((*Char > 0 && *Char < 127) ? static_cast<TCHAR>(*Char) : TEXT('?'));
		}
		return Out;
	}

	class FConnection final : public ILightgunConnection
	{
	public:
		/** Takes ownership of Handle and of one hidapi reference. */
		explicit FConnection(hid_device* InHandle)
			: Handle(InHandle)
		{
		}

		virtual ~FConnection() override
		{
			Close();
			ReleaseHidApi();
		}

		virtual bool Write(const FLightgunReport& Report, FString& OutError) override
		{
			if (!Handle)
			{
				OutError = TEXT("device is closed");
				return false;
			}

			// hid_write, not hid_send_output_report: both reach current firmware, but the latter is a
			// control transfer, which Microsoft warns can wedge some devices, and older firmware
			// misroutes it entirely. On Windows hid_write uses the interrupt OUT endpoint.
			const int Written = hid_write(Handle, Report.GetData(), Report.GetSize());
			if (Written < 0)
			{
				OutError = FromWide(hid_error(Handle));
				if (OutError.IsEmpty())
				{
					OutError = TEXT("hid_write failed");
				}
				return false;
			}
			return true;
		}

		virtual ELightgunReadResult ReadInput(FLightgunInputState& OutState) override
		{
			if (!Handle)
			{
				return ELightgunReadResult::Error;
			}

			// Anything that isn't a gamepad report (e.g. a keyboard collection's report) is skipped.
			uint8_t Buffer[64];
			for (;;)
			{
				const int Read = hid_read(Handle, Buffer, sizeof(Buffer));
				if (Read < 0)
				{
					return ELightgunReadResult::Error;
				}
				if (Read == 0)
				{
					return ELightgunReadResult::NoData;
				}
				if (LightgunInputReport::ParseBlamconReport(Buffer, static_cast<size_t>(Read), OutState))
				{
					return ELightgunReadResult::Report;
				}
			}
		}

		virtual void Close() override
		{
			if (Handle)
			{
				hid_close(Handle);
				Handle = nullptr;
			}
		}

	private:
		hid_device* Handle;
	};
}

FBlamconHidBackend::FBlamconHidBackend()
{
	bAvailable = BlamconHid::AcquireHidApi();
}

FBlamconHidBackend::~FBlamconHidBackend()
{
	if (bAvailable)
	{
		BlamconHid::ReleaseHidApi();
	}
}

void FBlamconHidBackend::Enumerate(FLightgunEnumeration& OutEnumeration)
{
	using namespace LightgunDeviceMatch;

	OutEnumeration.Devices.Reset();
	OutEnumeration.Warnings.Reset();
	if (!bAvailable)
	{
		return;
	}

	TArray<FHidCollection> Collections;
	TArray<FLightgunDeviceId> Ids;

	hid_device_info* const List = hid_enumerate(BlamconVendorId, 0x0000);
	for (hid_device_info* Info = List; Info; Info = Info->next)
	{
		if (!IsBlamconProduct(Info->vendor_id, Info->product_id))
		{
			continue;
		}

		FHidCollection Collection = { Info->vendor_id, Info->product_id, Info->usage_page, Info->usage };
		Collections.Add(Collection);

		FLightgunDeviceId& Id = Ids.AddDefaulted_GetRef();
		Id.BackendName = BackendName;
		Id.Path = Info->path ? FString(UTF8_TO_TCHAR(Info->path)) : FString();
		Id.VendorId = Info->vendor_id;
		Id.ProductId = Info->product_id;
		Id.SerialNumber = BlamconHid::FromWide(Info->serial_number);
		Id.ProductName = BlamconHid::FromWide(Info->product_string);
		Id.PlayerIndex = GetBlamconPlayerIndex(Info->product_id);
	}
	if (List)
	{
		hid_free_enumeration(List);
	}

	FPlayerScan Players[BlamconMaxPlayers];
	ScanBlamconCollections(Collections.GetData(), Collections.Num(), Players);

	for (int32 Player = 0; Player < BlamconMaxPlayers; ++Player)
	{
		const FPlayerScan& Scan = Players[Player];
		if (Scan.IsUsable())
		{
			OutEnumeration.Devices.Add(Ids[Scan.ControllerIndex]);
			if (Scan.ControllerCount > 1)
			{
				OutEnumeration.Warnings.Add(FString::Printf(
					TEXT("%d Blamcon lightguns are set to player %d; only the first gets feedback. Give each gun its own player number in Blamcon ARC."),
					Scan.ControllerCount, Player + 1));
			}
		}
		else if (Scan.IsMouseModeOnly())
		{
			OutEnumeration.Warnings.Add(FString::Printf(
				TEXT("Blamcon lightgun (player %d) found in mouse mode. Force feedback requires Gamepad mode - change it in Blamcon ARC."),
				Player + 1));
		}
	}
}

TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> FBlamconHidBackend::Open(const FLightgunDeviceId& Device, FString& OutError)
{
	if (!bAvailable)
	{
		OutError = TEXT("hidapi is not available");
		return nullptr;
	}
	if (Device.BackendName != BackendName || Device.Path.IsEmpty())
	{
		OutError = FString::Printf(TEXT("not a Blamcon HID device: %s"), *Device.ToString());
		return nullptr;
	}
	if (!BlamconHid::AcquireHidApi())
	{
		OutError = TEXT("hidapi failed to initialise");
		return nullptr;
	}

	hid_device* const Handle = hid_open_path(TCHAR_TO_UTF8(*Device.Path));
	if (!Handle)
	{
		OutError = BlamconHid::FromWide(hid_error(nullptr));
		if (OutError.IsEmpty())
		{
			OutError = FString::Printf(TEXT("hid_open_path failed for %s"), *Device.Path);
		}
		BlamconHid::ReleaseHidApi();
		return nullptr;
	}

	// Input is drained every frame from the game thread, so reads must never block.
	hid_set_nonblocking(Handle, 1);

	return MakeShared<BlamconHid::FConnection, ESPMode::ThreadSafe>(Handle);
}
