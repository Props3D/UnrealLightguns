// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

// Engine-free on purpose: this header must compile with a plain C++11 compiler so the byte layout
// can be tested without Unreal (see Tests/host). Do not include engine headers here.

#include <cstddef>
#include <cstdint>
#include <cstring>

/** The feedback components a lightgun report can drive, in wire order (update/control byte pairs). */
enum class ELightgunComponent : uint8_t
{
	Rumble = 0,
	Led = 1,
	Recoil = 2,
	Ammo = 3,
};

/**
 * Builds the 40-byte Blamcon feedback output report, id 0x10.
 *
 * Only report 0x10 is used, deliberately. It is exactly OutputReportByteLength (40) on Windows, so
 * nothing pads or trims it, and the firmware accepts the single-component reports (0x20-0x23) only
 * at their exact declared size. See docs/PLUGIN_SPEC.md, constraint 7.
 *
 * Calls are chainable, so one report can drive several components at once:
 *     FLightgunReport().TakeControl(true, false, false, true).Ammo(30)
 * That matters: the firmware services one output report at a time.
 *
 * Byte layout, matching handleDefaultOutputReport() in the firmware:
 *   [0]     report id, 0x10
 *   [1]/[2] rumble update / control     [3]/[4] LED update / control
 *   [5]/[6] recoil update / control     [7]/[8] ammo update / control
 *           update > 0 applies the component; control 2 = release, 3 = take, 0 = unchanged
 *   [9-14]  padding
 *   [15]    rumble pulses               [16-17] rumble on ms (u16 LE)   [18-19] rumble off ms
 *   [20-22] LED RGB                     [23] LED index (ignored by firmware 3.0)
 *   [24]    LED flashes                 [25-26] LED dark ms (u16 LE)    [27-28] LED lit ms
 *   [29]    recoil pulses               [30] recoil on ms               [31] recoil off ms
 *   [32]    ammo remaining              [33] ammo used (tick)           [34] ammo max
 *   [35-39] padding
 *
 * Besides the wire bytes, the report records which components had a control change and which had
 * an effect set, so two reports can be merged (see Merge) without one clobbering the other.
 */
class FLightgunReport
{
public:
	enum : uint8_t { ReportId = 0x10 };
	enum : size_t { ReportSize = 40 };

	enum EOffset : size_t
	{
		OffsetReportId = 0,
		OffsetRumbleUpdate = 1,
		OffsetRumbleControl = 2,
		OffsetLedUpdate = 3,
		OffsetLedControl = 4,
		OffsetRecoilUpdate = 5,
		OffsetRecoilControl = 6,
		OffsetAmmoUpdate = 7,
		OffsetAmmoControl = 8,
		OffsetRumblePulses = 15,
		OffsetRumbleOn = 16,
		OffsetRumbleOff = 18,
		OffsetLedRed = 20,
		OffsetLedGreen = 21,
		OffsetLedBlue = 22,
		OffsetLedIndex = 23,
		OffsetLedFlashes = 24,
		// The firmware's own comments label bytes 25 and 27 the other way round, but the LED stays
		// dark for byte 25's period and lit for byte 27's. These names reflect the actual behaviour.
		OffsetLedDark = 25,
		OffsetLedLit = 27,
		OffsetRecoilPulses = 29,
		OffsetRecoilOn = 30,
		OffsetRecoilOff = 31,
		OffsetAmmoRemaining = 32,
		OffsetAmmoUsed = 33,
		OffsetAmmoMax = 34,
	};

	enum : uint8_t
	{
		ControlUnchanged = 0,
		ControlRelease = 2,
		ControlTake = 3,
	};

	// Firmware clamps, applied here too so values behave predictably.
	enum : int
	{
		RecoilOnMinMs = 15, RecoilOnMaxMs = 200,
		RecoilOffMinMs = 45, RecoilOffMaxMs = 200,
		RumbleMinMs = 100, RumbleMaxMs = 2400,
		LedMinMs = 20, LedMaxMs = 5000,
	};

	FLightgunReport()
	{
		Reset();
	}

	void Reset()
	{
		std::memset(Bytes, 0, sizeof(Bytes));
		Bytes[OffsetReportId] = ReportId;
		ControlMask = 0;
		EffectMask = 0;
	}

	/**
	 * Take components away from the gun's own logic: recoil stops firing on the trigger pull, and so on.
	 * Ammo is ignored by the firmware until ammo control is taken, and taking it zeroes the display,
	 * so send the starting count in the same report.
	 */
	FLightgunReport& TakeControl(bool bRecoil, bool bRumble, bool bLed, bool bAmmo)
	{
		return SetControl(ControlTake, bRecoil, bRumble, bLed, bAmmo);
	}

	/** Hand components back to the gun. Must happen on exit and focus loss. */
	FLightgunReport& ReleaseControl(bool bRecoil, bool bRumble, bool bLed, bool bAmmo)
	{
		return SetControl(ControlRelease, bRecoil, bRumble, bLed, bAmmo);
	}

	/** Recoil solenoid. 0 pulses stops it; the gun's configured timing is used. */
	FLightgunReport& Recoil(int Pulses)
	{
		MarkEffect(ELightgunComponent::Recoil);
		Bytes[OffsetRecoilPulses] = ToByte(Pulses);
		return *this;
	}

	/** Recoil solenoid with explicit timing. On is clamped to 15-200 ms, off to 45-200 ms. */
	FLightgunReport& Recoil(int Pulses, int OnMs, int OffMs)
	{
		Recoil(Pulses);
		// Send both periods or neither: the firmware can read an uninitialised value for the first
		// if only the second is supplied.
		Bytes[OffsetRecoilOn] = static_cast<uint8_t>(Clamp(OnMs, RecoilOnMinMs, RecoilOnMaxMs));
		Bytes[OffsetRecoilOff] = static_cast<uint8_t>(Clamp(OffMs, RecoilOffMinMs, RecoilOffMaxMs));
		return *this;
	}

	/** Rumble motor. 0 pulses stops it; the firmware's default timing is used. */
	FLightgunReport& Rumble(int Pulses)
	{
		MarkEffect(ELightgunComponent::Rumble);
		Bytes[OffsetRumblePulses] = ToByte(Pulses);
		return *this;
	}

	/** Rumble motor with explicit timing, both clamped to 100-2400 ms. */
	FLightgunReport& Rumble(int Pulses, int OnMs, int OffMs)
	{
		Rumble(Pulses);
		PutU16(OffsetRumbleOn, SafePeriod(OnMs, RumbleMinMs, RumbleMaxMs));
		PutU16(OffsetRumbleOff, SafePeriod(OffMs, RumbleMinMs, RumbleMaxMs));
		return *this;
	}

	/** Solid colour. Black turns the LED off. */
	FLightgunReport& Led(uint8_t R, uint8_t G, uint8_t B)
	{
		MarkEffect(ELightgunComponent::Led);
		Bytes[OffsetLedRed] = R;
		Bytes[OffsetLedGreen] = G;
		Bytes[OffsetLedBlue] = B;
		return *this;
	}

	/** Flash a colour with the firmware's default timing. */
	FLightgunReport& Led(uint8_t R, uint8_t G, uint8_t B, int Flashes)
	{
		Led(R, G, B);
		Bytes[OffsetLedFlashes] = ToByte(Flashes);
		return *this;
	}

	/** Flash a colour. Lit and dark periods are clamped to 20-5000 ms. */
	FLightgunReport& Led(uint8_t R, uint8_t G, uint8_t B, int Flashes, int LitMs, int DarkMs)
	{
		Led(R, G, B, Flashes);
		PutU16(OffsetLedLit, SafePeriod(LitMs, LedMinMs, LedMaxMs));
		PutU16(OffsetLedDark, SafePeriod(DarkMs, LedMinMs, LedMaxMs));
		return *this;
	}

	/** All-zero RGB with no flashes tells the firmware to disable the LED. */
	FLightgunReport& LedOff()
	{
		Led(0, 0, 0);
		Bytes[OffsetLedFlashes] = 0;
		return *this;
	}

	/** Show this count on the ammo display. Requires ammo control. */
	FLightgunReport& Ammo(int Remaining)
	{
		MarkEffect(ELightgunComponent::Ammo);
		Bytes[OffsetAmmoRemaining] = ToByte(Remaining);
		Bytes[OffsetAmmoUsed] = 0;
		Bytes[OffsetAmmoMax] = 0;
		return *this;
	}

	/** Initialise the ammo counter to a maximum. Requires ammo control. */
	FLightgunReport& AmmoMax(int Max)
	{
		MarkEffect(ELightgunComponent::Ammo);
		Bytes[OffsetAmmoMax] = ToByte(Max);
		return *this;
	}

	/**
	 * Decrement the ammo counter by one. Requires ammo control. Two ticks merged into one report
	 * count once, so prefer Ammo(Remaining) when reports may be coalesced.
	 */
	FLightgunReport& AmmoTick()
	{
		MarkEffect(ELightgunComponent::Ammo);
		Bytes[OffsetAmmoUsed] = 1;
		return *this;
	}

	/**
	 * Fold a newer report into this one, so a single write carries both.
	 *
	 * Per component, a control change in Newer replaces this report's control change, and an effect
	 * in Newer replaces this report's effect. The two are independent: "take control" followed by
	 * "recoil once" merges into one report that does both, in either order.
	 */
	void Merge(const FLightgunReport& Newer)
	{
		for (int Index = 0; Index < ComponentCount; ++Index)
		{
			const ELightgunComponent Component = static_cast<ELightgunComponent>(Index);
			if (Newer.HasControlChange(Component))
			{
				ControlMask |= Bit(Component);
				Bytes[ControlOffset(Component)] = Newer.Bytes[ControlOffset(Component)];
			}
			if (Newer.HasEffect(Component))
			{
				EffectMask |= Bit(Component);
				const FRange Range = EffectRange(Component);
				std::memcpy(Bytes + Range.Begin, Newer.Bytes + Range.Begin, Range.End - Range.Begin);
			}
			if ((ControlMask | EffectMask) & Bit(Component))
			{
				Bytes[UpdateOffset(Component)] = 1;
			}
		}
	}

	bool IsEmpty() const { return ControlMask == 0 && EffectMask == 0; }
	bool HasControlChange(ELightgunComponent Component) const { return (ControlMask & Bit(Component)) != 0; }
	bool HasEffect(ELightgunComponent Component) const { return (EffectMask & Bit(Component)) != 0; }

	const uint8_t* GetData() const { return Bytes; }
	size_t GetSize() const { return ReportSize; }

private:
	enum : int { ComponentCount = 4 };

	struct FRange
	{
		size_t Begin;
		size_t End;
	};

	static uint8_t Bit(ELightgunComponent Component) { return static_cast<uint8_t>(1u << static_cast<unsigned>(Component)); }
	static size_t UpdateOffset(ELightgunComponent Component) { return OffsetRumbleUpdate + 2 * static_cast<size_t>(Component); }
	static size_t ControlOffset(ELightgunComponent Component) { return OffsetRumbleControl + 2 * static_cast<size_t>(Component); }

	static FRange EffectRange(ELightgunComponent Component)
	{
		switch (Component)
		{
		case ELightgunComponent::Rumble: { FRange Range = { OffsetRumblePulses, OffsetLedRed }; return Range; }
		case ELightgunComponent::Led: { FRange Range = { OffsetLedRed, OffsetRecoilPulses }; return Range; }
		case ELightgunComponent::Recoil: { FRange Range = { OffsetRecoilPulses, OffsetAmmoRemaining }; return Range; }
		case ELightgunComponent::Ammo:
		default: { FRange Range = { OffsetAmmoRemaining, OffsetAmmoMax + 1 }; return Range; }
		}
	}

	static int Clamp(int Value, int Min, int Max) { return Value < Min ? Min : (Value > Max ? Max : Value); }
	static uint8_t ToByte(int Value) { return static_cast<uint8_t>(Clamp(Value, 0, 255)); }

	// Firmware up to and including 3.0.0 tested 16-bit periods by their low byte only, so exact
	// multiples of 256 ms were silently dropped. The 1 ms nudge keeps older guns working.
	static uint16_t SafePeriod(int Ms, int Min, int Max)
	{
		int Value = Clamp(Ms, Min, Max);
		if ((Value % 256) == 0)
		{
			Value = (Value + 1 <= Max) ? Value + 1 : Value - 1;
		}
		return static_cast<uint16_t>(Value);
	}

	void PutU16(size_t Offset, uint16_t Value)
	{
		Bytes[Offset] = static_cast<uint8_t>(Value & 0xFF);
		Bytes[Offset + 1] = static_cast<uint8_t>((Value >> 8) & 0xFF);
	}

	FLightgunReport& SetControl(uint8_t Control, bool bRecoil, bool bRumble, bool bLed, bool bAmmo)
	{
		if (bRecoil) { MarkControl(ELightgunComponent::Recoil, Control); }
		if (bRumble) { MarkControl(ELightgunComponent::Rumble, Control); }
		if (bLed) { MarkControl(ELightgunComponent::Led, Control); }
		if (bAmmo) { MarkControl(ELightgunComponent::Ammo, Control); }
		return *this;
	}

	void MarkControl(ELightgunComponent Component, uint8_t Control)
	{
		ControlMask |= Bit(Component);
		Bytes[UpdateOffset(Component)] = 1;
		Bytes[ControlOffset(Component)] = Control;
	}

	void MarkEffect(ELightgunComponent Component)
	{
		EffectMask |= Bit(Component);
		Bytes[UpdateOffset(Component)] = 1;
	}

	uint8_t Bytes[ReportSize];
	uint8_t ControlMask;
	uint8_t EffectMask;
};
