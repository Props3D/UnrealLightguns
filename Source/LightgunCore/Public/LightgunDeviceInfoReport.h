// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

// Engine-free on purpose, like LightgunReport.h: compiled by Tests/host with a plain C++11 compiler.

#include <cstddef>
#include <cstdint>

/**
 * What a gun reports about itself in feature reports 0x50 (device info) and 0x51 (live state).
 * See docs/PLUGIN_SPEC.md section 4.1.
 */
struct FLightgunDeviceInfo
{
	/** False for legacy firmware: no answer, a short answer, or no signature. The other fields are then zero. */
	bool bKnown;

	/** major * 10000 + minor * 100 + patch: 3.0.0 = 30000. */
	uint32_t FirmwareVersion;

	/** LightgunDeviceInfoReport::Board* value, kept raw so unknown values can be logged. */
	uint8_t Board;

	/** LightgunDeviceInfoReport::Mode* value, kept raw so unknown values can be logged. */
	uint8_t Mode;

	/** The gun processes output report 0x10 in its current mode and on its current connection. */
	bool bFeedbackAvailable;

	/** 1-4, from the gun's own settings. */
	uint8_t PlayerNumber;

	/** Whether report 0x51 was read. */
	bool bHasLiveState;

	/** LightgunDeviceInfoReport::HostControl* bits: components a host held control of when 0x51 was read. */
	uint8_t HostControl;

	FLightgunDeviceInfo()
		: bKnown(false)
		, FirmwareVersion(0)
		, Board(0)
		, Mode(0)
		, bFeedbackAvailable(false)
		, PlayerNumber(0)
		, bHasLiveState(false)
		, HostControl(0)
	{
	}

	uint32_t GetMajor() const { return FirmwareVersion / 10000; }
	uint32_t GetMinor() const { return (FirmwareVersion / 100) % 100; }
	uint32_t GetPatch() const { return FirmwareVersion % 100; }
};

/**
 * Parsers for the Blamcon feature reports, matching firmware/devices/hid/hid_feature_report.h.
 *
 *   [0]    report id: 0x50 device info, 0x51 live state
 *   [1-2]  signature 0x42 0x4C ("BL")
 *   [3]    protocol version, 2 or later
 *   [4]    payload length: bytes after this one
 *   0x50:  [5-8] firmware version (uint32 LE)  [9] board  [10] mode  [11] feedback  [12] player
 *   0x51:  [5] host control bits
 *
 * Fields are only ever appended, so a longer payload parses; only the known fields are read.
 */
namespace LightgunDeviceInfoReport
{
	static const uint8_t DeviceInfoReportId = 0x50;
	static const uint8_t LiveStateReportId = 0x51;

	static const uint8_t Signature0 = 0x42; // 'B'
	static const uint8_t Signature1 = 0x4C; // 'L'

	/** 1 was an unsigned one-byte answer that can't be told apart from stale bytes. */
	static const uint8_t MinProtocolVersion = 2;

	/** Report id, signature, protocol version, payload length. */
	static const size_t HeaderSize = 5;
	static const size_t DeviceInfoPayloadSize = 8;
	static const size_t LiveStatePayloadSize = 1;

	/**
	 * Buffer for reading either report, including the report id. Windows sizes feature reads to the
	 * collection's largest feature report, which is 0x50 at 13 bytes, so 0x51 is read with the same buffer.
	 */
	static const size_t ReadBufferSize = HeaderSize + DeviceInfoPayloadSize;

	static const uint8_t BoardUnknown = 0;
	static const uint8_t BoardRP2040 = 1;
	static const uint8_t BoardRP2350 = 2;

	static const uint8_t ModeMouse = 0;
	static const uint8_t ModeGamepad = 1;
	static const uint8_t ModeBluetoothMouse = 2;
	static const uint8_t ModeBluetoothGamepad = 3;

	static const uint8_t HostControlRecoil = 0x01;
	static const uint8_t HostControlRumble = 0x02;
	static const uint8_t HostControlLed = 0x04;
	static const uint8_t HostControlAmmo = 0x08;

	/** Size is the byte count hid_get_feature_report returned, including the report id. */
	inline bool HasValidHeader(const uint8_t* Data, int Size, uint8_t ReportId, size_t MinPayloadSize)
	{
		return Data
			&& Size >= 0
			&& static_cast<size_t>(Size) >= HeaderSize + MinPayloadSize
			&& Data[0] == ReportId
			&& Data[1] == Signature0
			&& Data[2] == Signature1
			&& Data[3] >= MinProtocolVersion
			&& Data[4] >= MinPayloadSize;
	}

	/** Returns false, leaving OutInfo untouched, for anything that isn't a valid 0x50 answer. */
	inline bool ParseDeviceInfo(const uint8_t* Data, int Size, FLightgunDeviceInfo& OutInfo)
	{
		if (!HasValidHeader(Data, Size, DeviceInfoReportId, DeviceInfoPayloadSize))
		{
			return false;
		}

		OutInfo.bKnown = true;
		OutInfo.FirmwareVersion = static_cast<uint32_t>(Data[5])
			| (static_cast<uint32_t>(Data[6]) << 8)
			| (static_cast<uint32_t>(Data[7]) << 16)
			| (static_cast<uint32_t>(Data[8]) << 24);
		OutInfo.Board = Data[9];
		OutInfo.Mode = Data[10];
		OutInfo.bFeedbackAvailable = Data[11] != 0;
		OutInfo.PlayerNumber = Data[12];
		return true;
	}

	/** Returns false, leaving OutInfo untouched, for anything that isn't a valid 0x51 answer. */
	inline bool ParseLiveState(const uint8_t* Data, int Size, FLightgunDeviceInfo& OutInfo)
	{
		if (!HasValidHeader(Data, Size, LiveStateReportId, LiveStatePayloadSize))
		{
			return false;
		}

		OutInfo.bHasLiveState = true;
		OutInfo.HostControl = static_cast<uint8_t>(Data[5] & (HostControlRecoil | HostControlRumble | HostControlLed | HostControlAmmo));
		return true;
	}

	/** Display name, or null for a value this plugin doesn't know. */
	inline const char* GetBoardName(uint8_t Board)
	{
		switch (Board)
		{
		case BoardRP2040: return "RP2040";
		case BoardRP2350: return "RP2350";
		default: return 0;
		}
	}

	/** Display name, or null for a value this plugin doesn't know. */
	inline const char* GetModeName(uint8_t Mode)
	{
		switch (Mode)
		{
		case ModeMouse: return "mouse";
		case ModeGamepad: return "gamepad";
		case ModeBluetoothMouse: return "Bluetooth mouse";
		case ModeBluetoothGamepad: return "Bluetooth gamepad";
		default: return 0;
		}
	}

	inline bool IsBluetoothMode(uint8_t Mode)
	{
		return Mode == ModeBluetoothMouse || Mode == ModeBluetoothGamepad;
	}

	inline bool IsMouseMode(uint8_t Mode)
	{
		return Mode == ModeMouse || Mode == ModeBluetoothMouse;
	}
}
