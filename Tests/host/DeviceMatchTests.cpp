// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "TestHarness.h"

#include "LightgunDeviceMatch.h"

using namespace LightgunDeviceMatch;

namespace
{
	FHidCollection Collection(uint16_t VendorId, uint16_t ProductId, uint16_t UsagePage, uint16_t Usage)
	{
		FHidCollection Result = { VendorId, ProductId, UsagePage, Usage };
		return Result;
	}

	void TestProductIds()
	{
		CHECK(IsBlamconProduct(0x3673, 0x0100));
		CHECK(IsBlamconProduct(0x3673, 0x0103));
		CHECK(!IsBlamconProduct(0x3673, 0x00FF));
		CHECK(!IsBlamconProduct(0x3673, 0x0104));
		CHECK(!IsBlamconProduct(0x045E, 0x0100));

		CHECK_EQ(GetBlamconPlayerIndex(0x0100), 0);
		CHECK_EQ(GetBlamconPlayerIndex(0x0103), 3);
		CHECK_EQ(GetBlamconPlayerIndex(0x0104), -1);
	}

	void TestClassify()
	{
		CHECK(ClassifyCollection(0x01, 0x05) == ECollectionKind::Controller);
		CHECK(ClassifyCollection(0x01, 0x04) == ECollectionKind::Controller);
		CHECK(ClassifyCollection(0x01, 0x02) == ECollectionKind::MouseOrKeyboard);
		CHECK(ClassifyCollection(0x01, 0x06) == ECollectionKind::MouseOrKeyboard);
		CHECK(ClassifyCollection(0x0C, 0x01) == ECollectionKind::Other);   // consumer control
		CHECK(ClassifyCollection(0xFF00, 0x05) == ECollectionKind::Other); // vendor page, not generic desktop
		CHECK(ClassifyCollection(0x00, 0x00) == ECollectionKind::Other);
	}

	void TestNoDevices()
	{
		FPlayerScan Players[BlamconMaxPlayers];
		ScanBlamconCollections(0, 0, Players);
		for (int Player = 0; Player < BlamconMaxPlayers; ++Player)
		{
			CHECK(!Players[Player].IsUsable());
			CHECK(!Players[Player].IsMouseModeOnly());
		}
	}

	void TestGamepadMode()
	{
		// P1 in gamepad mode with an extra keyboard collection, plus an unrelated mouse.
		const FHidCollection Collections[] =
		{
			Collection(0x046D, 0xC077, 0x01, 0x02),
			Collection(0x3673, 0x0100, 0x01, 0x06),
			Collection(0x3673, 0x0100, 0x01, 0x05),
		};
		FPlayerScan Players[BlamconMaxPlayers];
		ScanBlamconCollections(Collections, 3, Players);

		CHECK(Players[0].IsUsable());
		CHECK_EQ(Players[0].ControllerIndex, 2);
		CHECK_EQ(Players[0].ControllerCount, 1);
		CHECK(!Players[0].IsMouseModeOnly());
		CHECK(!Players[1].IsUsable());
		CHECK(!Players[1].IsMouseModeOnly());
	}

	void TestMouseMode()
	{
		// P2 is only a mouse + keyboard: present, but cannot receive feedback.
		const FHidCollection Collections[] =
		{
			Collection(0x3673, 0x0101, 0x01, 0x02),
			Collection(0x3673, 0x0101, 0x01, 0x06),
			Collection(0x3673, 0x0102, 0x01, 0x04), // P3 in joystick mode
		};
		FPlayerScan Players[BlamconMaxPlayers];
		ScanBlamconCollections(Collections, 3, Players);

		CHECK(!Players[1].IsUsable());
		CHECK(Players[1].IsMouseModeOnly());
		CHECK(Players[2].IsUsable());
		CHECK_EQ(Players[2].ControllerIndex, 2);
		CHECK(!Players[0].IsMouseModeOnly());
	}

	void TestDuplicatePlayerId()
	{
		const FHidCollection Collections[] =
		{
			Collection(0x3673, 0x0100, 0x01, 0x05),
			Collection(0x3673, 0x0100, 0x01, 0x05),
		};
		FPlayerScan Players[BlamconMaxPlayers];
		ScanBlamconCollections(Collections, 2, Players);

		CHECK_EQ(Players[0].ControllerIndex, 0);
		CHECK_EQ(Players[0].ControllerCount, 2);
	}
}

void RunDeviceMatchTests()
{
	TestProductIds();
	TestClassify();
	TestNoDevices();
	TestGamepadMode();
	TestMouseMode();
	TestDuplicatePlayerId();
}
