// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LightgunDeviceId.h"

class FLightgunReport;
struct FLightgunInputState;

enum class ELightgunReadResult : uint8
{
	/** No report waiting. */
	NoData,
	/** OutState holds the next report. */
	Report,
	/** The device is gone or unreadable. */
	Error,
};

/**
 * An open lightgun: feedback reports out, input reports in.
 *
 * Threading: Write and Close are called on the writer thread only (see FLightgunWriterThread), never on
 * the game thread, because they block. ReadInput is non-blocking and called from the game thread; it
 * must stop before the connection is handed to the writer for closing.
 */
class ILightgunConnection
{
public:
	virtual ~ILightgunConnection() = default;

	/** Send one report. Blocks until the platform accepts it. Returns false and fills OutError on failure. */
	virtual bool Write(const FLightgunReport& Report, FString& OutError) = 0;

	/** Read the next input report without blocking. A backend with no input (e.g. serial) returns NoData. */
	virtual ELightgunReadResult ReadInput(FLightgunInputState& OutState) = 0;

	/** Release the platform handle. Safe to call more than once. */
	virtual void Close() = 0;
};

/** Result of one enumeration pass. */
struct FLightgunEnumeration
{
	/** Guns that can receive feedback. */
	TArray<FLightgunDeviceId> Devices;

	/**
	 * Guns that are present but cannot receive feedback, as actionable messages for the log or the UI.
	 * The important case is a Blamcon gun left in mouse mode.
	 */
	TArray<FString> Warnings;
};

/**
 * The seam between lightgun feedback and its transport. HID today (FBlamconHidBackend); a serial
 * backend for guns in mouse mode, and other brands, can be added behind it without touching callers.
 *
 * Reports use the FLightgunReport layout. A backend for another transport translates its fields.
 *
 * Threading: Enumerate and Open may block, so call them off the game thread where it matters. A
 * backend must allow them to be called from any one thread at a time.
 */
class ILightgunFeedbackBackend
{
public:
	virtual ~ILightgunFeedbackBackend() = default;

	virtual FName GetBackendName() const = 0;

	/** Find connected guns. Finding none is not an error. */
	virtual void Enumerate(FLightgunEnumeration& OutEnumeration) = 0;

	/** Open a device returned by Enumerate. Returns null and fills OutError on failure. */
	virtual TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> Open(const FLightgunDeviceId& Device, FString& OutError) = 0;
};
