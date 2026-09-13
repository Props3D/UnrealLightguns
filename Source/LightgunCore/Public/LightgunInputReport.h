// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

// Engine-free on purpose, like LightgunReport.h: compiled by Tests/host with a plain C++11 compiler.

#include <cstddef>
#include <cstdint>

/** One lightgun input sample, independent of the wire format. */
struct FLightgunInputState
{
	enum EButton : uint32_t
	{
		ButtonTrigger = 1u << 0,
		ButtonA = 1u << 1,
		ButtonB = 1u << 2,
		ButtonY = 1u << 3,
		ButtonSelect = 1u << 8,
		ButtonStart = 1u << 9,
		DPadUp = 1u << 28,
		DPadDown = 1u << 29,
		DPadLeft = 1u << 30,
		DPadRight = 1u << 31,
	};

	/** EButton bits. The d-pad occupies bits the firmware never sets. */
	uint32_t Buttons;

	/** Aim, normalised 0..1. X = 0 is the left edge; Y = 0 is the bottom edge (Unreal's convention). */
	float AimX;
	float AimY;

	FLightgunInputState()
		: Buttons(0)
		, AimX(0.5f)
		, AimY(0.5f)
	{
	}
};

/**
 * Parses the Blamcon gamepad input report (firmware GamepadReport, 22 bytes). See docs/PLUGIN_SPEC.md section 5.
 *
 *   [0]      report id: 1 = P1, 3 = P2, 4 = P3, 5 = P4 (2 is also accepted, as the Unity package does)
 *   [1-4]    32 button bits: 0 trigger, 1 A, 2 B, 3 Y, 4 LB, 5 RB, 6 LT, 7 RT, 8 Select, 9 Start, 10 L3, 11 R3
 *   [5]      hat, low nibble: 0 neutral, 1 up, 2 up/right ... 8 up/left
 *   [6-9]    X, int32 LE, 0-32767
 *   [10-13]  Y, int32 LE, 0-32767, 0 = top of screen
 *   [14-21]  Rx / Ry, always zero in current firmware, ignored
 */
namespace LightgunInputReport
{
	static const size_t BlamconReportSize = 22;
	static const int32_t BlamconAxisMax = 32767;

	inline int32_t ReadInt32(const uint8_t* Data)
	{
		const uint32_t Value = static_cast<uint32_t>(Data[0])
			| (static_cast<uint32_t>(Data[1]) << 8)
			| (static_cast<uint32_t>(Data[2]) << 16)
			| (static_cast<uint32_t>(Data[3]) << 24);
		return static_cast<int32_t>(Value);
	}

	inline float NormaliseAxis(int32_t Value)
	{
		const int32_t Clamped = Value < 0 ? 0 : (Value > BlamconAxisMax ? BlamconAxisMax : Value);
		return static_cast<float>(Clamped) / static_cast<float>(BlamconAxisMax);
	}

	/** Returns false, leaving OutState untouched, for anything that isn't a gamepad report. */
	inline bool ParseBlamconReport(const uint8_t* Data, size_t Size, FLightgunInputState& OutState)
	{
		if (!Data || Size < BlamconReportSize || Data[0] < 1 || Data[0] > 5)
		{
			return false;
		}

		const uint32_t RawButtons = static_cast<uint32_t>(ReadInt32(Data + 1));
		uint32_t Buttons = RawButtons & (FLightgunInputState::ButtonTrigger | FLightgunInputState::ButtonA
			| FLightgunInputState::ButtonB | FLightgunInputState::ButtonY | FLightgunInputState::ButtonSelect | FLightgunInputState::ButtonStart);

		// Cardinal directions only, matching the Unity package: diagonals map to no direction on purpose.
		switch (Data[5] & 0x0F)
		{
		case 1: Buttons |= FLightgunInputState::DPadUp; break;
		case 3: Buttons |= FLightgunInputState::DPadRight; break;
		case 5: Buttons |= FLightgunInputState::DPadDown; break;
		case 7: Buttons |= FLightgunInputState::DPadLeft; break;
		default: break;
		}

		OutState.Buttons = Buttons;
		OutState.AimX = NormaliseAxis(ReadInt32(Data + 6));
		// The firmware's Y = 0 is the top of the screen; flip to Unreal's bottom-up convention.
		OutState.AimY = 1.0f - NormaliseAxis(ReadInt32(Data + 10));
		return true;
	}
}
