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

	/** The Blamcon vendor-defined collection that firmware with mouse-mode feedback adds (spec section 4.2). */
	static const uint16_t UsagePageBlamconVendor = 0xFF00;
	static const uint16_t UsageBlamconVendor = 0x01;

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
		/**
		 * Blamcon vendor-defined collection, next to the mouse and keyboard in mouse mode. Nothing in Windows
		 * claims it, so it takes output report 0x10 and feature reports 0x50/0x51. It has no input reports.
		 */
		Vendor,
		Other,
	};

	inline ECollectionKind ClassifyCollection(uint16_t UsagePage, uint16_t Usage)
	{
		if (UsagePage == UsagePageBlamconVendor && Usage == UsageBlamconVendor)
		{
			return ECollectionKind::Vendor;
		}
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
		/** Index of the first vendor collection, or -1. */
		int VendorIndex;
		/** Vendor collections seen for this player id. */
		int VendorCount;
		bool bHasMouseOrKeyboard;

		/**
		 * The gun is present but cannot receive feedback: in mouse mode on firmware without the vendor
		 * collection, or paired over Bluetooth before a firmware update added it.
		 */
		bool IsMouseModeOnly() const { return ControllerCount == 0 && VendorCount == 0 && bHasMouseOrKeyboard; }

		bool IsUsable() const { return GetUsableIndex() >= 0; }

		/** The collection to open: the controller (gamepad mode), else the vendor collection (mouse mode), else -1. */
		int GetUsableIndex() const { return ControllerIndex >= 0 ? ControllerIndex : VendorIndex; }

		/** Guns sharing this player id, counted in the collection kind that is used. */
		int GetUsableCount() const { return ControllerIndex >= 0 ? ControllerCount : VendorCount; }
	};

	/**
	 * Group collections by Blamcon player id. A gun exposes several collections (in mouse mode a mouse, a
	 * keyboard and, on newer firmware, the vendor collection), so a gun is usable if any of its
	 * collections is a controller or the vendor collection.
	 *
	 * Grouping is by product id because that is what identifies a Blamcon player: Windows gives each
	 * collection of one gun a different path, and over Bluetooth it reports no interface number but keeps
	 * the product id.
	 */
	inline void ScanBlamconCollections(const FHidCollection* Collections, size_t Count, FPlayerScan OutPlayers[BlamconMaxPlayers])
	{
		for (int Player = 0; Player < BlamconMaxPlayers; ++Player)
		{
			OutPlayers[Player].ControllerIndex = -1;
			OutPlayers[Player].ControllerCount = 0;
			OutPlayers[Player].VendorIndex = -1;
			OutPlayers[Player].VendorCount = 0;
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
			case ECollectionKind::Vendor:
				if (Player.VendorIndex < 0)
				{
					Player.VendorIndex = static_cast<int>(Index);
				}
				++Player.VendorCount;
				break;
			case ECollectionKind::Other:
				break;
			}
		}
	}
}
