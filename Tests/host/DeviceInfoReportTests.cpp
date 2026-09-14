// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "TestHarness.h"

#include "LightgunDeviceInfoReport.h"

#include <cstring>

using namespace LightgunDeviceInfoReport;

namespace
{
	// Byte-for-byte what firmware/devices/hid/hid_feature_report.h builds, with the report id prepended
	// as hidapi returns it: 3.1.12, RP2350, Bluetooth mouse, feedback on, player 2.
	const uint8_t DeviceInfo[] = { 0x50, 0x42, 0x4C, 0x02, 0x08, 0xA0, 0x75, 0x00, 0x00, 0x02, 0x02, 0x01, 0x02 };

	// Recoil and LED held.
	const uint8_t LiveState[] = { 0x51, 0x42, 0x4C, 0x02, 0x01, 0x05 };

	void TestDeviceInfo()
	{
		FLightgunDeviceInfo Info;
		CHECK(!Info.bKnown);

		CHECK(ParseDeviceInfo(DeviceInfo, sizeof(DeviceInfo), Info));
		CHECK(Info.bKnown);
		CHECK_EQ(Info.FirmwareVersion, 30112);
		CHECK_EQ(Info.GetMajor(), 3);
		CHECK_EQ(Info.GetMinor(), 1);
		CHECK_EQ(Info.GetPatch(), 12);
		CHECK_EQ(Info.Board, BoardRP2350);
		CHECK_EQ(Info.Mode, ModeBluetoothMouse);
		CHECK(Info.bFeedbackAvailable);
		CHECK_EQ(Info.PlayerNumber, 2);
		CHECK(!Info.bHasLiveState);

		// 3.0.0 as reported by release-3.0 on hardware.
		uint8_t Release300[sizeof(DeviceInfo)];
		std::memcpy(Release300, DeviceInfo, sizeof(DeviceInfo));
		Release300[5] = 0x30;
		Release300[6] = 0x75;
		Release300[11] = 0x00;
		CHECK(ParseDeviceInfo(Release300, sizeof(Release300), Info));
		CHECK_EQ(Info.FirmwareVersion, 30000);
		CHECK(!Info.bFeedbackAvailable);
	}

	void TestLiveState()
	{
		FLightgunDeviceInfo Info;
		CHECK(ParseLiveState(LiveState, sizeof(LiveState), Info));
		CHECK(Info.bHasLiveState);
		CHECK_EQ(Info.HostControl, HostControlRecoil | HostControlLed);
		CHECK(!Info.bKnown); // 0x51 alone doesn't make the gun known

		// Unknown bits are dropped.
		const uint8_t HighBits[] = { 0x51, 0x42, 0x4C, 0x02, 0x01, 0xF2 };
		CHECK(ParseLiveState(HighBits, sizeof(HighBits), Info));
		CHECK_EQ(Info.HostControl, HostControlRumble);

		// Windows reads 0x51 into the 13-byte buffer and returns its full length.
		uint8_t Padded[ReadBufferSize] = { 0 };
		std::memcpy(Padded, LiveState, sizeof(LiveState));
		CHECK(ParseLiveState(Padded, sizeof(Padded), Info));
		CHECK_EQ(Info.HostControl, HostControlRecoil | HostControlLed);
	}

	void TestLongerFuturePayload()
	{
		// A later firmware appends fields: a larger payload length, truncated to our buffer.
		uint8_t Future[ReadBufferSize];
		std::memcpy(Future, DeviceInfo, sizeof(DeviceInfo));
		Future[3] = 2;
		Future[4] = 20;
		FLightgunDeviceInfo Info;
		CHECK(ParseDeviceInfo(Future, sizeof(Future), Info));
		CHECK_EQ(Info.FirmwareVersion, 30112);

		// A later protocol version is accepted too.
		Future[3] = 7;
		CHECK(ParseDeviceInfo(Future, sizeof(Future), Info));
	}

	void TestLegacyAnswers()
	{
		FLightgunDeviceInfo Info;
		Info.FirmwareVersion = 123;

		// Failed read.
		CHECK(!ParseDeviceInfo(DeviceInfo, -1, Info));
		CHECK(!ParseDeviceInfo(0, 13, Info));

		// The old one-byte form: id + protocol version 1.
		const uint8_t OneByte[] = { 0x50, 0x01 };
		CHECK(!ParseDeviceInfo(OneByte, sizeof(OneByte), Info));

		// Stale control-buffer bytes from firmware before c5d859d, e.g. the tail of a 0x10 report.
		const uint8_t Stale[] = { 0x50, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
		CHECK(!ParseDeviceInfo(Stale, sizeof(Stale), Info));

		// Short answer.
		CHECK(!ParseDeviceInfo(DeviceInfo, 12, Info));

		// Right shape, wrong pieces.
		uint8_t Bad[sizeof(DeviceInfo)];
		std::memcpy(Bad, DeviceInfo, sizeof(Bad));
		Bad[1] = 0x41;
		CHECK(!ParseDeviceInfo(Bad, sizeof(Bad), Info));
		std::memcpy(Bad, DeviceInfo, sizeof(Bad));
		Bad[2] = 0x4D;
		CHECK(!ParseDeviceInfo(Bad, sizeof(Bad), Info));
		std::memcpy(Bad, DeviceInfo, sizeof(Bad));
		Bad[3] = 1;
		CHECK(!ParseDeviceInfo(Bad, sizeof(Bad), Info));
		std::memcpy(Bad, DeviceInfo, sizeof(Bad));
		Bad[4] = 7;
		CHECK(!ParseDeviceInfo(Bad, sizeof(Bad), Info));
		std::memcpy(Bad, DeviceInfo, sizeof(Bad));
		Bad[0] = LiveStateReportId;
		CHECK(!ParseDeviceInfo(Bad, sizeof(Bad), Info));
		CHECK(!ParseLiveState(DeviceInfo, sizeof(DeviceInfo), Info));

		CHECK(!Info.bKnown);
		CHECK_EQ(Info.FirmwareVersion, 123); // untouched
	}

	void TestNames()
	{
		CHECK(std::strcmp(GetBoardName(BoardRP2040), "RP2040") == 0);
		CHECK(std::strcmp(GetBoardName(BoardRP2350), "RP2350") == 0);
		CHECK(GetBoardName(BoardUnknown) == 0);
		CHECK(GetBoardName(9) == 0);

		CHECK(std::strcmp(GetModeName(ModeMouse), "mouse") == 0);
		CHECK(std::strcmp(GetModeName(ModeBluetoothGamepad), "Bluetooth gamepad") == 0);
		CHECK(GetModeName(4) == 0);

		CHECK(IsBluetoothMode(ModeBluetoothMouse));
		CHECK(IsBluetoothMode(ModeBluetoothGamepad));
		CHECK(!IsBluetoothMode(ModeGamepad));
		CHECK(IsMouseMode(ModeMouse));
		CHECK(IsMouseMode(ModeBluetoothMouse));
		CHECK(!IsMouseMode(ModeGamepad));
		CHECK(!IsMouseMode(4));
	}
}

void RunDeviceInfoReportTests()
{
	TestDeviceInfo();
	TestLiveState();
	TestLongerFuturePayload();
	TestLegacyAnswers();
	TestNames();
}
