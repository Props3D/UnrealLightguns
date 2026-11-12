// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "TestHarness.h"

#include "LightgunInputReport.h"

#include <cstring>
#include <initializer_list>

using namespace LightgunInputReport;

namespace
{
	struct FRawReport
	{
		uint8_t Bytes[22];

		FRawReport(uint8_t ReportId, uint32_t Buttons, uint8_t Hat, int32_t X, int32_t Y)
		{
			std::memset(Bytes, 0, sizeof(Bytes));
			Bytes[0] = ReportId;
			Put(1, static_cast<int32_t>(Buttons));
			Bytes[5] = Hat;
			Put(6, X);
			Put(10, Y);
		}

		void Put(int Offset, int32_t Value)
		{
			const uint32_t Bits = static_cast<uint32_t>(Value);
			for (int Index = 0; Index < 4; ++Index)
			{
				Bytes[Offset + Index] = static_cast<uint8_t>((Bits >> (8 * Index)) & 0xFF);
			}
		}
	};

	bool Near(float A, float B)
	{
		return A - B < 0.0001f && B - A < 0.0001f;
	}

	void TestAimEdgesAndCentre()
	{
		FLightgunInputState State;

		CHECK(ParseBlamconReport(FRawReport(1, 0, 0, 0, 0).Bytes, 22, State));
		CHECK(Near(State.AimX, 0.0f));
		CHECK(Near(State.AimY, 1.0f)); // firmware top -> Unreal 1

		CHECK(ParseBlamconReport(FRawReport(1, 0, 0, 32767, 32767).Bytes, 22, State));
		CHECK(Near(State.AimX, 1.0f));
		CHECK(Near(State.AimY, 0.0f));

		// No deadzone: values a step either side of centre stay distinct, not snapped to 0.5.
		CHECK(ParseBlamconReport(FRawReport(1, 0, 0, 16384, 16384).Bytes, 22, State));
		const float JustRight = State.AimX;
		const float JustBelow = State.AimY;
		CHECK(ParseBlamconReport(FRawReport(1, 0, 0, 16383, 16383).Bytes, 22, State));
		CHECK(Near(JustRight, 16384.0f / 32767.0f));
		CHECK(Near(JustBelow, 1.0f - 16384.0f / 32767.0f));
		CHECK(JustRight > State.AimX);
		CHECK(JustBelow < State.AimY);

		// X and Y are read from their own offsets.
		CHECK(ParseBlamconReport(FRawReport(1, 0, 0, 8000, 24000).Bytes, 22, State));
		CHECK(Near(State.AimX, 8000.0f / 32767.0f));
		CHECK(Near(State.AimY, 1.0f - 24000.0f / 32767.0f));

		// Out-of-range values clamp.
		CHECK(ParseBlamconReport(FRawReport(1, 0, 0, -5, 90000).Bytes, 22, State));
		CHECK(Near(State.AimX, 0.0f));
		CHECK(Near(State.AimY, 0.0f));
	}

	void TestButtons()
	{
		FLightgunInputState State;

		CHECK(ParseBlamconReport(FRawReport(1, (1u << 0), 0, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons, FLightgunInputState::ButtonTrigger);

		CHECK(ParseBlamconReport(FRawReport(1, (1u << 1) | (1u << 2) | (1u << 8) | (1u << 9), 0, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons, FLightgunInputState::ButtonA | FLightgunInputState::ButtonB | FLightgunInputState::ButtonSelect | FLightgunInputState::ButtonStart);

		// Y is bit 3 (North).
		CHECK(ParseBlamconReport(FRawReport(1, (1u << 3), 0, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons, FLightgunInputState::ButtonY);

		// Buttons without a key (bumpers, triggers, sticks) and the high bits are dropped, so they
		// can't masquerade as d-pad bits.
		CHECK(ParseBlamconReport(FRawReport(1, 0xFFFFFFFFu & ~0x30Fu, 0, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons, 0);
	}

	void TestHat()
	{
		FLightgunInputState State;
		const uint32_t DPad = FLightgunInputState::DPadUp | FLightgunInputState::DPadDown | FLightgunInputState::DPadLeft | FLightgunInputState::DPadRight;

		CHECK(ParseBlamconReport(FRawReport(1, 0, 1, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons & DPad, FLightgunInputState::DPadUp);
		CHECK(ParseBlamconReport(FRawReport(1, 0, 3, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons & DPad, FLightgunInputState::DPadRight);
		CHECK(ParseBlamconReport(FRawReport(1, 0, 5, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons & DPad, FLightgunInputState::DPadDown);
		CHECK(ParseBlamconReport(FRawReport(1, 0, 7, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons & DPad, FLightgunInputState::DPadLeft);

		// Diagonals and neutral map to nothing; the high nibble is ignored.
		for (int Hat : { 0, 2, 4, 6, 8, 9, 15 })
		{
			CHECK(ParseBlamconReport(FRawReport(1, 0, static_cast<uint8_t>(Hat), 0, 0).Bytes, 22, State));
			CHECK_EQ(State.Buttons & DPad, 0);
		}
		CHECK(ParseBlamconReport(FRawReport(1, 0, 0xF1, 0, 0).Bytes, 22, State));
		CHECK_EQ(State.Buttons & DPad, FLightgunInputState::DPadUp);
	}

	void TestRejects()
	{
		FLightgunInputState State;
		State.Buttons = 123;

		CHECK(!ParseBlamconReport(FRawReport(0, 1, 0, 0, 0).Bytes, 22, State));
		CHECK(!ParseBlamconReport(FRawReport(6, 1, 0, 0, 0).Bytes, 22, State));
		CHECK(!ParseBlamconReport(FRawReport(1, 1, 0, 0, 0).Bytes, 21, State));
		CHECK(!ParseBlamconReport(0, 22, State));
		CHECK_EQ(State.Buttons, 123); // untouched

		for (int ReportId : { 1, 2, 3, 4, 5 })
		{
			CHECK(ParseBlamconReport(FRawReport(static_cast<uint8_t>(ReportId), 0, 0, 0, 0).Bytes, 22, State));
		}
		// Longer buffers are fine.
		uint8_t Long[64] = { 0 };
		std::memcpy(Long, FRawReport(3, 1, 0, 0, 0).Bytes, 22);
		CHECK(ParseBlamconReport(Long, sizeof(Long), State));
	}
}

void RunInputReportTests()
{
	TestAimEdgesAndCentre();
	TestButtons();
	TestHat();
	TestRejects();
}
