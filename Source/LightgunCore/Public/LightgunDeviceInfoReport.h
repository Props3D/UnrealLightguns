// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

// Engine-free on purpose, like LightgunReport.h: compiled by Tests/host with a plain C++11 compiler.

#include <cstddef>
#include <cstdint>

/**
 * What a gun reports about itself in feature reports 0x50 (device info) and 0x51 (live state).
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

	/**
	 * The gun model its owner chose in ARC: a LightgunDeviceInfoReport::Model* value, kept raw so a model
	 * added after this plugin can still be shown as a number. ModelNone (0) means there is nothing to
	 * show, either because the owner never chose one or because the firmware is older than the field.
	 * Unlike the rest of this struct it is live on the gun, but the plugin reads 0x50 once per connection,
	 * so it is the value from connect time.
	 */
	uint8_t Model;

	/**
	 * LightgunDeviceInfoReport::ConfigCap* bits: what the gun's configuration interface (output report
	 * 0x30) can do on this build. Non-zero means the gun has that interface at all - firmware never
	 * reports 0 on a build that has it - so 0 covers both a build without it and firmware older than
	 * the field. Nothing in this plugin drives 0x30 yet; this is read so the connect line can say.
	 */
	uint8_t ConfigCapabilities;

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
		, Model(0)
		, ConfigCapabilities(0)
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
 *          [13] gun model and [14] config capabilities, appended in firmware 2.2.1 / 3.0.0
 *   0x51:  [5] host control bits
 *
 * Fields are only ever appended, so a longer payload parses; only the known fields are read. An
 * appended field is optional: a gun reports it only if its declared payload length covers it.
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

	/** The 0x50 fields every firmware with the report answers with, so the least a valid reply can carry. */
	static const size_t DeviceInfoPayloadSize = 8;
	static const size_t LiveStatePayloadSize = 1;

	/** 0-based positions within the 0x50 payload of the fields firmware appended later. */
	static const size_t ModelPayloadIndex = 8;
	static const size_t ConfigCapabilitiesPayloadIndex = 9;

	/**
	 * Buffer for reading any of these feature reports, including the report id. Sized to what the protocol
	 * allows, not to the report this plugin happens to know: firmware appends fields (0x50 already declares
	 * a payload longer than DeviceInfoPayloadSize), and Windows sizes a feature read to the collection's
	 * largest feature report and fails a buffer smaller than that. Firmware keeps every feature report
	 * within FEATURE_REPORT_MAX_LEN (31) bytes after the report id, so 32 covers all of them; the parsers
	 * ignore whatever trails the fields they know.
	 */
	static const size_t FeatureReportMaxSize = 32;

	static const uint8_t BoardUnknown = 0;
	static const uint8_t BoardRP2040 = 1;
	static const uint8_t BoardRP2350 = 2;

	static const uint8_t ModeMouse = 0;
	static const uint8_t ModeGamepad = 1;
	static const uint8_t ModeBluetoothMouse = 2;
	static const uint8_t ModeBluetoothGamepad = 3;

	/**
	 * Gun models (firmware MODEL_TYPE_*, the same values as ARC's own enum, which only ever appends).
	 * ZY.15 accepts up to ModelMax so ARC can add a model without a firmware update, which is why an
	 * unrecognised value is shown rather than refused.
	 */
	static const uint8_t ModelNone = 0;
	static const uint8_t ModelVyper = 1;
	static const uint8_t ModelAuto9 = 2;
	static const uint8_t ModelAceOfSpades = 3;
	static const uint8_t ModelPhazer = 4;
	static const uint8_t ModelLawgiver = 5;
	static const uint8_t ModelBullPump = 6;
	static const uint8_t ModelGuncon45 = 7;
	static const uint8_t ModelMax = 63;

	/** Firmware CONFIG_CAP_* bits, as answered in 0x50 and in feature report 0x52. */
	static const uint8_t ConfigCapTransientMode = 0x01; // the emulation mode can change without a reboot
	static const uint8_t ConfigCapPersist = 0x02; // a request can ask for the change to be stored

	static const uint8_t HostControlRecoil = 0x01;
	static const uint8_t HostControlRumble = 0x02;
	static const uint8_t HostControlLed = 0x04;
	static const uint8_t HostControlAmmo = 0x08;

	/**
	 * Size is the byte count hid_get_feature_report returned, including the report id. A reply longer than
	 * HeaderSize + MinPayloadSize is valid: the extra bytes are fields this plugin doesn't know, and the
	 * declared payload length (Data[4]) may be larger than MinPayloadSize for the same reason. Only a
	 * reply too short to hold the known fields is rejected.
	 */
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

	/**
	 * Whether an appended payload byte actually arrived: the gun's declared payload length has to cover
	 * it and so does what hid_get_feature_report returned. PayloadIndex is 0-based within the payload.
	 * Call it only after HasValidHeader, which is what establishes that Size and Data are usable.
	 */
	inline bool HasPayloadByte(const uint8_t* Data, int Size, size_t PayloadIndex)
	{
		return static_cast<size_t>(Size) > HeaderSize + PayloadIndex
			&& static_cast<size_t>(Data[4]) > PayloadIndex;
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
		// Appended fields: zero when this gun's firmware doesn't report them, so a struct parsed twice
		// never keeps a value from the previous gun.
		OutInfo.Model = HasPayloadByte(Data, Size, ModelPayloadIndex)
			? Data[HeaderSize + ModelPayloadIndex] : ModelNone;
		OutInfo.ConfigCapabilities = HasPayloadByte(Data, Size, ConfigCapabilitiesPayloadIndex)
			? Data[HeaderSize + ConfigCapabilitiesPayloadIndex] : 0;
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

	/**
	 * Display name, or null for ModelNone and for a model this plugin doesn't know - 8 and up, which ARC
	 * can add at any time. Callers show the raw number for those, as they do for an unknown board or mode.
	 * The spellings follow the firmware's MODEL_TYPE_* names; ARC's own labels may differ in wording.
	 */
	inline const char* GetModelName(uint8_t Model)
	{
		switch (Model)
		{
		case ModelVyper: return "Vyper";
		case ModelAuto9: return "Auto 9";
		case ModelAceOfSpades: return "Ace of Spades";
		case ModelPhazer: return "Phazer";
		case ModelLawgiver: return "Lawgiver";
		case ModelBullPump: return "Bull Pump";
		case ModelGuncon45: return "Guncon 45";
		default: return 0;
		}
	}

	/**
	 * Whether the gun has the configuration interface (output report 0x30 and feature report 0x52).
	 * Firmware never answers 0 on a build that has it, so the capability byte alone settles it.
	 */
	inline bool HasConfigInterface(uint8_t ConfigCapabilities)
	{
		return ConfigCapabilities != 0;
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
