// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT
//
// Byte layout tests for FLightgunReport. Expected offsets are written as literal numbers, not the
// header's offset names, so the test checks the layout independently of the code it tests.

#include "TestHarness.h"

#include "LightgunReport.h"

#include <cstring>

namespace
{
	struct FExpected
	{
		unsigned char Bytes[40];

		FExpected()
		{
			std::memset(Bytes, 0, sizeof(Bytes));
			Bytes[0] = 0x10;
		}

		FExpected& Set(int Offset, int Value)
		{
			Bytes[Offset] = static_cast<unsigned char>(Value);
			return *this;
		}
	};

	void ExpectReport(const char* Name, const FLightgunReport& Report, const FExpected& Expected)
	{
		++GChecks;
		bool bMatch = Report.GetSize() == 40 && std::memcmp(Report.GetData(), Expected.Bytes, 40) == 0;
		if (!bMatch)
		{
			++GFailures;
			std::printf("  FAIL %s: size %d\n    got     ", Name, static_cast<int>(Report.GetSize()));
			for (int Index = 0; Index < 40; ++Index) { std::printf("%02x ", Report.GetData()[Index]); }
			std::printf("\n    expected");
			for (int Index = 0; Index < 40; ++Index) { std::printf(" %02x", Expected.Bytes[Index]); }
			std::printf("\n");
		}
	}

	void TestEmpty()
	{
		FLightgunReport Report;
		CHECK_EQ(Report.GetSize(), 40);
		CHECK(Report.IsEmpty());
		ExpectReport("empty", Report, FExpected());
	}

	void TestControl()
	{
		ExpectReport("take all", FLightgunReport().TakeControl(true, true, true, true),
			FExpected().Set(1, 1).Set(2, 3).Set(3, 1).Set(4, 3).Set(5, 1).Set(6, 3).Set(7, 1).Set(8, 3));

		ExpectReport("release all", FLightgunReport().ReleaseControl(true, true, true, true),
			FExpected().Set(1, 1).Set(2, 2).Set(3, 1).Set(4, 2).Set(5, 1).Set(6, 2).Set(7, 1).Set(8, 2));

		ExpectReport("take recoil only", FLightgunReport().TakeControl(true, false, false, false),
			FExpected().Set(5, 1).Set(6, 3));

		// Taking ammo control zeroes the display, so the starting count rides in the same report.
		ExpectReport("take ammo + count", FLightgunReport().TakeControl(false, false, false, true).Ammo(99),
			FExpected().Set(7, 1).Set(8, 3).Set(32, 99));

		CHECK(!FLightgunReport().TakeControl(true, false, false, false).IsEmpty());
	}

	void TestRecoil()
	{
		ExpectReport("recoil x1", FLightgunReport().Recoil(1), FExpected().Set(5, 1).Set(29, 1));
		ExpectReport("recoil timed", FLightgunReport().Recoil(3, 40, 60), FExpected().Set(5, 1).Set(29, 3).Set(30, 40).Set(31, 60));
		ExpectReport("recoil clamp low", FLightgunReport().Recoil(-4, 1, 1), FExpected().Set(5, 1).Set(29, 0).Set(30, 15).Set(31, 45));
		ExpectReport("recoil clamp high", FLightgunReport().Recoil(1000, 900, 900), FExpected().Set(5, 1).Set(29, 255).Set(30, 200).Set(31, 200));
	}

	void TestRumble()
	{
		// Firmware sample 1 from handleDefaultOutputReport, minus its control byte of 1 (<= 1 means unchanged).
		ExpectReport("rumble x1", FLightgunReport().Rumble(1), FExpected().Set(1, 1).Set(15, 1));

		// 300 = 0x012C, 2000 = 0x07D0, little-endian.
		ExpectReport("rumble timed", FLightgunReport().Rumble(2, 300, 2000),
			FExpected().Set(1, 1).Set(15, 2).Set(16, 0x2C).Set(17, 0x01).Set(18, 0xD0).Set(19, 0x07));

		// Clamped to 100 (0x0064) and 2400 (0x0960).
		ExpectReport("rumble clamp", FLightgunReport().Rumble(1, 10, 99999),
			FExpected().Set(1, 1).Set(15, 1).Set(16, 0x64).Set(17, 0x00).Set(18, 0x60).Set(19, 0x09));

		// Multiples of 256 are nudged by 1 ms for firmware <= 3.0.0: 256 -> 257 (0x0101), 512 -> 513 (0x0201).
		ExpectReport("rumble 256 nudge", FLightgunReport().Rumble(1, 256, 512),
			FExpected().Set(1, 1).Set(15, 1).Set(16, 0x01).Set(17, 0x01).Set(18, 0x01).Set(19, 0x02));
	}

	void TestLed()
	{
		ExpectReport("led solid", FLightgunReport().Led(255, 128, 1),
			FExpected().Set(3, 1).Set(20, 255).Set(21, 128).Set(22, 1));

		ExpectReport("led flash default timing", FLightgunReport().Led(0, 0, 255, 3),
			FExpected().Set(3, 1).Set(22, 255).Set(24, 3));

		// Lit 1000 ms (0x03E8) goes to bytes 27-28; dark 300 ms (0x012C) goes to bytes 25-26.
		ExpectReport("led flash timed", FLightgunReport().Led(10, 20, 30, 5, 1000, 300),
			FExpected().Set(3, 1).Set(20, 10).Set(21, 20).Set(22, 30).Set(24, 5)
				.Set(25, 0x2C).Set(26, 0x01).Set(27, 0xE8).Set(28, 0x03));

		// Clamped to 20 (0x0014) and 5000 (0x1388).
		ExpectReport("led flash clamp", FLightgunReport().Led(1, 1, 1, 1, 1, 60000),
			FExpected().Set(3, 1).Set(20, 1).Set(21, 1).Set(22, 1).Set(24, 1)
				.Set(25, 0x88).Set(26, 0x13).Set(27, 0x14).Set(28, 0x00));

		FLightgunReport Off;
		Off.Led(255, 255, 255, 4).LedOff();
		ExpectReport("led off", Off, FExpected().Set(3, 1));
	}

	void TestAmmo()
	{
		ExpectReport("ammo remaining", FLightgunReport().Ammo(30), FExpected().Set(7, 1).Set(32, 30));
		ExpectReport("ammo clamp", FLightgunReport().Ammo(1000), FExpected().Set(7, 1).Set(32, 255));
		ExpectReport("ammo max", FLightgunReport().AmmoMax(50), FExpected().Set(7, 1).Set(34, 50));
		ExpectReport("ammo tick", FLightgunReport().AmmoTick(), FExpected().Set(7, 1).Set(33, 1));
	}

	void TestPaddingStaysZero()
	{
		FLightgunReport Everything;
		Everything.TakeControl(true, true, true, true)
			.Rumble(9, 777, 888)
			.Led(1, 2, 3, 4, 555, 666)
			.Recoil(7, 50, 60)
			.Ammo(12);

		const unsigned char* Data = Everything.GetData();
		for (int Index = 9; Index <= 14; ++Index) { CHECK_EQ(Data[Index], 0); }
		CHECK_EQ(Data[23], 0); // LED index: ignored by firmware 3.0, always sent as 0
		for (int Index = 35; Index <= 39; ++Index) { CHECK_EQ(Data[Index], 0); }
		CHECK_EQ(Data[0], 0x10);
	}

	void TestMerge()
	{
		// Control and effect merge independently, in either order.
		FLightgunReport RecoilThenTake;
		RecoilThenTake.Recoil(1);
		RecoilThenTake.Merge(FLightgunReport().TakeControl(true, false, false, false));

		FLightgunReport TakeThenRecoil;
		TakeThenRecoil.TakeControl(true, false, false, false);
		TakeThenRecoil.Merge(FLightgunReport().Recoil(1));

		const FExpected TakeAndFire = FExpected().Set(5, 1).Set(6, 3).Set(29, 1);
		ExpectReport("merge recoil then take", RecoilThenTake, TakeAndFire);
		ExpectReport("merge take then recoil", TakeThenRecoil, TakeAndFire);

		// A newer effect replaces the older one's whole field range, periods included.
		FLightgunReport Replaced;
		Replaced.Recoil(1, 100, 100);
		Replaced.Merge(FLightgunReport().Recoil(2));
		ExpectReport("merge newer effect wins", Replaced, FExpected().Set(5, 1).Set(29, 2));

		// Components the newer report doesn't touch are left alone.
		FLightgunReport Mixed;
		Mixed.Rumble(1, 300, 300).Ammo(8);
		Mixed.Merge(FLightgunReport().Led(9, 8, 7));
		ExpectReport("merge keeps untouched components", Mixed,
			FExpected().Set(1, 1).Set(15, 1).Set(16, 0x2C).Set(17, 0x01).Set(18, 0x2C).Set(19, 0x01)
				.Set(3, 1).Set(20, 9).Set(21, 8).Set(22, 7)
				.Set(7, 1).Set(32, 8));

		// A later release overrides an earlier take.
		FLightgunReport TakeThenRelease;
		TakeThenRelease.TakeControl(true, true, true, true);
		TakeThenRelease.Merge(FLightgunReport().ReleaseControl(true, true, true, true));
		ExpectReport("merge release wins", TakeThenRelease,
			FExpected().Set(1, 1).Set(2, 2).Set(3, 1).Set(4, 2).Set(5, 1).Set(6, 2).Set(7, 1).Set(8, 2));

		// Merging an empty report changes nothing.
		FLightgunReport Unchanged;
		Unchanged.Recoil(1);
		Unchanged.Merge(FLightgunReport());
		ExpectReport("merge empty", Unchanged, FExpected().Set(5, 1).Set(29, 1));

		FLightgunReport Empty;
		Empty.Merge(FLightgunReport());
		CHECK(Empty.IsEmpty());
		CHECK(Unchanged.HasEffect(ELightgunComponent::Recoil));
		CHECK(!Unchanged.HasControlChange(ELightgunComponent::Recoil));
		CHECK(!Unchanged.HasEffect(ELightgunComponent::Rumble));

		// Reset clears bytes and masks.
		Unchanged.Reset();
		CHECK(Unchanged.IsEmpty());
		ExpectReport("reset", Unchanged, FExpected());
	}
}

void RunReportTests()
{
	TestEmpty();
	TestControl();
	TestRecoil();
	TestRumble();
	TestLed();
	TestAmmo();
	TestPaddingStaysZero();
	TestMerge();
}
