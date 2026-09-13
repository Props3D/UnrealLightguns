// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT
//
// Host-side hardware check using the same engine-free matching and report code as the plugin.
// Lists Blamcon HID collections, reports mouse-mode guns, and with --recoil fires one recoil pulse
// on player 1. Build and run from the repo root (hidapi from Homebrew on macOS):
//   c++ -std=c++11 -Wall -Wextra -ISource/LightgunCore/Public -I/opt/homebrew/include/hidapi Tests/host/EnumerateLightguns.cpp -L/opt/homebrew/lib -lhidapi -o Tests/host/enumerate_lightguns && Tests/host/enumerate_lightguns

#include "LightgunDeviceMatch.h"
#include "LightgunReport.h"

#include <hidapi.h>
#ifdef __APPLE__
#include <hidapi_darwin.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace LightgunDeviceMatch;

namespace
{
	std::string Narrow(const wchar_t* Wide)
	{
		std::string Out;
		for (const wchar_t* Char = Wide; Char && *Char; ++Char)
		{
			Out += (*Char > 0 && *Char < 127) ? static_cast<char>(*Char) : '?';
		}
		return Out;
	}

	bool Send(hid_device* Device, const FLightgunReport& Report)
	{
		const int Written = hid_write(Device, Report.GetData(), Report.GetSize());
		if (Written < 0)
		{
			std::printf("  hid_write failed: %s\n", Narrow(hid_error(Device)).c_str());
			return false;
		}
		std::printf("  hid_write ok (%d bytes)\n", Written);
		return true;
	}
}

int main(int ArgCount, char** Args)
{
	const bool bFireRecoil = ArgCount > 1 && std::strcmp(Args[1], "--recoil") == 0;

	if (hid_init() != 0)
	{
		std::printf("hid_init failed\n");
		return 1;
	}
#ifdef __APPLE__
	hid_darwin_set_open_exclusive(0);
#endif

	std::vector<FHidCollection> Collections;
	std::vector<std::string> Paths;
	hid_device_info* List = hid_enumerate(BlamconVendorId, 0);
	for (hid_device_info* Info = List; Info; Info = Info->next)
	{
		FHidCollection Collection = { Info->vendor_id, Info->product_id, Info->usage_page, Info->usage };
		Collections.push_back(Collection);
		Paths.push_back(Info->path ? Info->path : "");
		std::printf("found %04x:%04x usage %02x:%02x bus %d \"%s\" %s\n", Info->vendor_id, Info->product_id,
			Info->usage_page, Info->usage, static_cast<int>(Info->bus_type), Narrow(Info->product_string).c_str(), Paths.back().c_str());
	}
	hid_free_enumeration(List);

	FPlayerScan Players[BlamconMaxPlayers];
	ScanBlamconCollections(Collections.empty() ? 0 : &Collections[0], Collections.size(), Players);

	int Usable = 0;
	for (int Player = 0; Player < BlamconMaxPlayers; ++Player)
	{
		if (Players[Player].IsUsable())
		{
			++Usable;
			std::printf("P%d: usable, %s\n", Player + 1, Paths[Players[Player].ControllerIndex].c_str());
		}
		else if (Players[Player].IsMouseModeOnly())
		{
			std::printf("P%d: Blamcon lightgun found in mouse mode. Force feedback requires Gamepad mode - change it in Blamcon ARC.\n", Player + 1);
		}
	}
	std::printf("%d Blamcon collection(s), %d usable gun(s)\n", static_cast<int>(Collections.size()), Usable);

	int Result = 0;
	if (bFireRecoil)
	{
		if (!Players[0].IsUsable())
		{
			std::printf("--recoil: no usable player 1\n");
			Result = 1;
		}
		else if (hid_device* Device = hid_open_path(Paths[Players[0].ControllerIndex].c_str()))
		{
			std::printf("--recoil: take recoil control + fire 1 pulse\n");
			bool bOk = Send(Device, FLightgunReport().TakeControl(true, false, false, false).Recoil(1));
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			std::printf("--recoil: release recoil control\n");
			bOk = Send(Device, FLightgunReport().ReleaseControl(true, false, false, false)) && bOk;
			hid_close(Device);
			Result = bOk ? 0 : 1;
		}
		else
		{
			std::printf("--recoil: hid_open_path failed: %s\n", Narrow(hid_error(0)).c_str());
			Result = 1;
		}
	}

	hid_exit();
	return Result;
}
