// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

#include "LightgunInfo.generated.h"

/** The board in the gun, as it reports itself. */
UENUM(BlueprintType)
enum class ELightgunBoard : uint8
{
	Unknown,
	RP2040,
	RP2350,
};

/** How the gun presents itself to the system. Gun keys only arrive in Gamepad mode. */
UENUM(BlueprintType)
enum class ELightgunMode : uint8
{
	Unknown,
	Mouse,
	Gamepad,
};

/** How the gun is connected. */
UENUM(BlueprintType)
enum class ELightgunConnection : uint8
{
	Unknown,
	Usb,
	Bluetooth,
};

/**
 * What the plugin knows about one player's gun: use it to show hardware state in a settings screen, and to
 * decide what to offer (a gun in mouse mode has no gun keys; older firmware can't report its own details).
 *
 * Game preferences such as the chosen LED colour or whether rumble is on belong in your own settings, not
 * here: this describes the device.
 */
USTRUCT(BlueprintType)
struct LIGHTGUNS_API FLightgunInfo
{
	GENERATED_BODY()

	/** A gun is connected for this player index. Every other field is default when false. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	bool bConnected = false;

	/** 0-based, as passed to the other nodes: player 1 is 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	int32 PlayerIndex = -1;

	/** The gun sends aim and buttons. False in mouse mode, where it is the system mouse. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	bool bHasGunInput = false;

	/** The gun accepts force feedback in its current mode and connection. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	bool bFeedbackAvailable = false;

	/**
	 * The gun answered with its own details. Firmware before the device info reports says nothing, so
	 * Firmware Version, Board, Mode and Player Number stay empty or Unknown while the gun still works.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	bool bDetailsKnown = false;

	/** "3.0.0", or empty on firmware that doesn't report it. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	FString FirmwareVersion;

	/** major * 10000 + minor * 100 + patch, so 3.0.0 is 30000 and versions compare with >=. 0 if unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	int32 FirmwareVersionNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	ELightgunBoard Board = ELightgunBoard::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	ELightgunMode Mode = ELightgunMode::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	ELightgunConnection Connection = ELightgunConnection::Unknown;

	/** The player number set on the gun itself, 1-4. 0 if unknown. Player Index is this minus one. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	int32 PlayerNumberOnGun = 0;

	/** The gun's USB product name, for showing which device this is. */
	UPROPERTY(BlueprintReadOnly, Category = "Lightgun")
	FString ProductName;
};
