// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

// Engine-free on purpose, like LightgunReport.h: compiled by Tests/host with a plain C++11 compiler.

#include <cstddef>
#include <cstdint>

namespace LightgunDeviceMatch
{
	// Plain constants rather than anonymous enums: arithmetic between two different enums is
	// deprecated in C++20, which Unreal compiles with.
	static const uint16_t BlamconVendorId = 0x3673;
	static const uint16_t BlamconProductIdP1 = 0x0100; // P2 = 0x0101, P3 = 0x0102, P4 = 0x0103
	static const int BlamconMaxPlayers = 4;

	static const uint16_t UsagePageGenericDesktop = 0x01;
	static const uint16_t UsageMouse = 0x02;
	static const uint16_t UsageJoystick = 0x04;
	static const uint16_t UsageGamepad = 0x05;
	static const uint16_t UsageKeyboard = 0x06;

	inline bool IsBlamconProduct(uint16_t VendorId, uint16_t ProductId)
	{
		return VendorId == BlamconVendorId
			&& ProductId >= BlamconProductIdP1
			&& ProductId < BlamconProductIdP1 + BlamconMaxPlayers;
	}

	/** 0-based player index for a Blamcon product id, or -1. */
	inline int GetBlamconPlayerIndex(uint16_t ProductId)
	{
		if (ProductId < BlamconProductIdP1 || ProductId >= BlamconProductIdP1 + BlamconMaxPlayers)
		{
			return -1;
		}
		return static_cast<int>(ProductId - BlamconProductIdP1);
	}

	enum class ECollectionKind : uint8_t
	{
		/** Joystick (0x04, other brands) or gamepad (0x05, Blamcon). Opened shared, so output reports reach it. */
		Controller,
		/** Opened exclusively by the OS on Windows: output reports can never reach it. */
		MouseOrKeyboard,
		Other,
	};

	inline ECollectionKind ClassifyCollection(uint16_t UsagePage, uint16_t Usage)
	{
		if (UsagePage != UsagePageGenericDesktop)
		{
			return ECollectionKind::Other;
		}
		if (Usage == UsageJoystick || Usage == UsageGamepad)
		{
			return ECollectionKind::Controller;
		}
		if (Usage == UsageMouse || Usage == UsageKeyboard)
		{
			return ECollectionKind::MouseOrKeyboard;
		}
		return ECollectionKind::Other;
	}

	/** One top-level HID collection, as hid_enumerate() reports it. */
	struct FHidCollection
	{
		uint16_t VendorId;
		uint16_t ProductId;
		uint16_t UsagePage;
		uint16_t Usage;
	};

	/** What was found for one Blamcon player id. */
	struct FPlayerScan
	{
		/** Index into the scanned collections of the first controller collection, or -1. */
		int ControllerIndex;
		/** Controller collections seen for this player id. More than one means two guns share a player id. */
		int ControllerCount;
		bool bHasMouseOrKeyboard;

		/** The gun is present but cannot receive feedback: it is in mouse mode. */
		bool IsMouseModeOnly() const { return ControllerCount == 0 && bHasMouseOrKeyboard; }
		bool IsUsable() const { return ControllerIndex >= 0; }
	};

	/**
	 * Group collections by Blamcon player id. A gun can expose several collections (in gamepad mode it
	 * may also have a keyboard collection), so a gun is usable if any of its collections is a
	 * controller, and in mouse mode only if it has mouse/keyboard collections and no controller.
	 *
	 * Grouping is by product id because that is what identifies a Blamcon player, and Windows gives
	 * each collection of one gun a different path.
	 */
	inline void ScanBlamconCollections(const FHidCollection* Collections, size_t Count, FPlayerScan OutPlayers[BlamconMaxPlayers])
	{
		for (int Player = 0; Player < BlamconMaxPlayers; ++Player)
		{
			OutPlayers[Player].ControllerIndex = -1;
			OutPlayers[Player].ControllerCount = 0;
			OutPlayers[Player].bHasMouseOrKeyboard = false;
		}

		for (size_t Index = 0; Index < Count; ++Index)
		{
			const FHidCollection& Collection = Collections[Index];
			if (!IsBlamconProduct(Collection.VendorId, Collection.ProductId))
			{
				continue;
			}

			FPlayerScan& Player = OutPlayers[GetBlamconPlayerIndex(Collection.ProductId)];
			switch (ClassifyCollection(Collection.UsagePage, Collection.Usage))
			{
			case ECollectionKind::Controller:
				if (Player.ControllerIndex < 0)
				{
					Player.ControllerIndex = static_cast<int>(Index);
				}
				++Player.ControllerCount;
				break;
			case ECollectionKind::MouseOrKeyboard:
				Player.bHasMouseOrKeyboard = true;
				break;
			case ECollectionKind::Other:
				break;
			}
		}
	}
}
