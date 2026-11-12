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

		// Windows reads 0x51 into the whole feature-report buffer and returns its full length.
		uint8_t Padded[FeatureReportMaxSize] = { 0 };
		std::memcpy(Padded, LiveState, sizeof(LiveState));
		CHECK(ParseLiveState(Padded, sizeof(Padded), Info));
		CHECK_EQ(Info.HostControl, HostControlRecoil | HostControlLed);
	}

	void TestLongerFuturePayload()
	{
		// A later firmware appends fields: a larger payload length, truncated to our buffer.
		uint8_t Future[FeatureReportMaxSize];
		std::memset(Future, 0, sizeof(Future));
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

	// The payload length firmware already ships (DEVICE_INFO_PAYLOAD_LEN 10): the two fields appended
	// after the ones parsed here (gun model, config capabilities) must be ignored, not rejected.
	void TestCurrentFirmwarePayload()
	{
		const uint8_t Shipping[] = {
			0x50, 0x42, 0x4C, 0x02, 0x0A,
			0xA0, 0x75, 0x00, 0x00, // 3.1.12
			0x02,                   // RP2350
			0x01,                   // gamepad
			0x01,                   // feedback
			0x03,                   // player 3
			0x05,                   // model Lawgiver, appended in firmware 3.0.0 / 2.2.1
			0x03                    // config capabilities: transient mode and persist
		};
		CHECK_EQ(sizeof(Shipping), HeaderSize + 10);

		FLightgunDeviceInfo Info;
		CHECK(ParseDeviceInfo(Shipping, sizeof(Shipping), Info));
		CHECK(Info.bKnown);
		CHECK_EQ(Info.FirmwareVersion, 30112);
		CHECK_EQ(Info.Board, BoardRP2350);
		CHECK_EQ(Info.Mode, ModeGamepad);
		CHECK(Info.bFeedbackAvailable);
		CHECK_EQ(Info.PlayerNumber, 3);
		CHECK_EQ(Info.Model, ModelLawgiver);
		CHECK_EQ(Info.ConfigCapabilities, ConfigCapTransientMode | ConfigCapPersist);
		CHECK(HasConfigInterface(Info.ConfigCapabilities));
	}

	// The appended fields are optional: each arrives only if the declared payload length covers it.
	void TestAppendedFieldsAbsent()
	{
		// The 8-byte payload every firmware with the report answers with: no model, no capabilities.
		FLightgunDeviceInfo Info;
		CHECK_EQ(sizeof(DeviceInfo), HeaderSize + DeviceInfoPayloadSize);
		CHECK(ParseDeviceInfo(DeviceInfo, sizeof(DeviceInfo), Info));
		CHECK_EQ(Info.Model, ModelNone);
		CHECK_EQ(Info.ConfigCapabilities, 0);
		CHECK(!HasConfigInterface(Info.ConfigCapabilities));

		// Reusing the struct must not carry the previous gun's model over to one that can't report it.
		Info.Model = ModelPhazer;
		Info.ConfigCapabilities = ConfigCapPersist;
		CHECK(ParseDeviceInfo(DeviceInfo, sizeof(DeviceInfo), Info));
		CHECK_EQ(Info.Model, ModelNone);
		CHECK_EQ(Info.ConfigCapabilities, 0);

		// Payload 9: the model arrived, the capabilities didn't.
		uint8_t WithModel[HeaderSize + 9];
		std::memcpy(WithModel, DeviceInfo, sizeof(DeviceInfo));
		WithModel[4] = 9;
		WithModel[HeaderSize + ModelPayloadIndex] = ModelLawgiver;
		CHECK(ParseDeviceInfo(WithModel, sizeof(WithModel), Info));
		CHECK_EQ(Info.Model, ModelLawgiver);
		CHECK_EQ(Info.ConfigCapabilities, 0);

		// Bytes present in the buffer but outside the declared payload are not fields: a gun that
		// declares 8 is read as 8, whatever Windows left in the rest of the buffer.
		uint8_t Undeclared[FeatureReportMaxSize];
		std::memset(Undeclared, 0xFF, sizeof(Undeclared));
		std::memcpy(Undeclared, DeviceInfo, sizeof(DeviceInfo));
		CHECK(ParseDeviceInfo(Undeclared, sizeof(Undeclared), Info));
		CHECK_EQ(Info.Model, ModelNone);
		CHECK_EQ(Info.ConfigCapabilities, 0);

		// And the other way round: a gun declaring 10 whose answer reached us truncated to the 8 bytes
		// every firmware has. The declared length alone must not be trusted - the buffer past what the
		// read returned holds whatever was already there, and reading it would be out of bounds of the
		// answer. Filled with 0xFF so trusting the length shows up as a model of 255, not as a zero that
		// looks like the right result.
		uint8_t Truncated[FeatureReportMaxSize];
		std::memset(Truncated, 0xFF, sizeof(Truncated));
		std::memcpy(Truncated, DeviceInfo, sizeof(DeviceInfo));
		Truncated[4] = 10;
		CHECK(ParseDeviceInfo(Truncated, static_cast<int>(HeaderSize + DeviceInfoPayloadSize), Info));
		CHECK_EQ(Info.Model, ModelNone);
		CHECK_EQ(Info.ConfigCapabilities, 0);
	}

	void TestModelNames()
	{
		CHECK(std::strcmp(GetModelName(ModelVyper), "Vyper") == 0);
		CHECK(std::strcmp(GetModelName(ModelAceOfSpades), "Ace of Spades") == 0);
		CHECK(std::strcmp(GetModelName(ModelGuncon45), "Guncon 45") == 0);

		// Nothing to name, and a model ARC adds later: the caller shows the number instead.
		CHECK(GetModelName(ModelNone) == 0);
		CHECK(GetModelName(8) == 0);
		CHECK(GetModelName(ModelMax) == 0);

		// A model past the firmware's own limit is still only reported, never acted on, so it parses.
		uint8_t Future[HeaderSize + 10];
		std::memcpy(Future, DeviceInfo, sizeof(DeviceInfo));
		Future[4] = 10;
		Future[HeaderSize + ModelPayloadIndex] = 200;
		Future[HeaderSize + ConfigCapabilitiesPayloadIndex] = 0x80; // a capability bit this plugin doesn't know
		FLightgunDeviceInfo Info;
		CHECK(ParseDeviceInfo(Future, sizeof(Future), Info));
		CHECK_EQ(Info.Model, 200);
		CHECK(GetModelName(Info.Model) == 0);
		CHECK_EQ(Info.ConfigCapabilities, 0x80);
		CHECK(HasConfigInterface(Info.ConfigCapabilities));
	}

	// What the read buffer has to survive at both ends: a report filling it, and a short one.
	void TestReadBufferExtremes()
	{
		// FEATURE_REPORT_MAX_LEN (31) bytes after the report id is the most firmware can send, so a
		// reply that fills the buffer parses, with every trailing byte ignored.
		uint8_t Biggest[FeatureReportMaxSize];
		std::memset(Biggest, 0xFF, sizeof(Biggest));
		std::memcpy(Biggest, DeviceInfo, sizeof(DeviceInfo));
		Biggest[4] = static_cast<uint8_t>(sizeof(Biggest) - HeaderSize); // declares 27
		FLightgunDeviceInfo Info;
		CHECK(ParseDeviceInfo(Biggest, sizeof(Biggest), Info));
		CHECK_EQ(Info.FirmwareVersion, 30112);
		CHECK_EQ(Info.PlayerNumber, 2);

		// Too short for the known fields stays rejected, however long the payload claims to be,
		// and whatever junk follows in the buffer.
		for (int Short = 0; Short < static_cast<int>(HeaderSize + DeviceInfoPayloadSize); ++Short)
		{
			FLightgunDeviceInfo Rejected;
			CHECK(!ParseDeviceInfo(Biggest, Short, Rejected));
			CHECK(!Rejected.bKnown);
		}

		// Exactly enough is enough: the last byte of the known payload arriving is the boundary.
		FLightgunDeviceInfo Exact;
		CHECK(ParseDeviceInfo(Biggest, static_cast<int>(HeaderSize + DeviceInfoPayloadSize), Exact));
		CHECK(Exact.bKnown);

		// 0x51 read into the same buffer: a declared-longer payload parses, a truncated one doesn't.
		uint8_t Live[FeatureReportMaxSize];
		std::memset(Live, 0, sizeof(Live));
		std::memcpy(Live, LiveState, sizeof(LiveState));
		Live[4] = 6; // a later firmware's longer 0x51 payload
		FLightgunDeviceInfo LiveInfo;
		CHECK(ParseLiveState(Live, sizeof(Live), LiveInfo));
		CHECK_EQ(LiveInfo.HostControl, HostControlRecoil | HostControlLed);
		CHECK(!ParseLiveState(Live, static_cast<int>(HeaderSize), LiveInfo));
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
	TestCurrentFirmwarePayload();
	TestAppendedFieldsAbsent();
	TestModelNames();
	TestReadBufferExtremes();
	TestLegacyAnswers();
	TestNames();
}
