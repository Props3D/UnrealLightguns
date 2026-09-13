// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "LightgunDeviceId.h"

class FLightgunReport;
class FRunnableThread;
class ILightgunConnection;

/** Handle to a device registered with an FLightgunWriterThread. */
struct FLightgunWriterHandle
{
	int32 Id = INDEX_NONE;

	bool IsValid() const { return Id != INDEX_NONE; }
	bool operator==(const FLightgunWriterHandle& Other) const { return Id == Other.Id; }
	friend uint32 GetTypeHash(const FLightgunWriterHandle& Handle) { return GetTypeHash(Handle.Id); }
};

enum class ELightgunWriterDeviceState : uint8
{
	/** Not registered with this writer. */
	Unknown,
	/** Accepting reports. */
	Active,
	/** A write failed, usually because the device was unplugged. Reports are rejected. */
	Failed,
	/** A write exceeded the watchdog timeout. The device is treated as gone; reports are rejected. */
	Stalled,
};

/**
 * Sends feedback reports to lightguns from one background thread, so HID writes never block the game
 * thread (they can block for a long time: see docs/PLUGIN_SPEC.md, constraint 6).
 *
 * - One thread serves every registered device, fed by a single-producer single-consumer queue.
 * - Coalescing: reports that arrive for a device while the thread is busy are merged into one report
 *   (FLightgunReport::Merge) rather than queued behind each other. The firmware services one output
 *   report at a time, so this is required, not an optimisation.
 * - Watchdog: Tick() marks a device Stalled if a write to it has been in progress longer than the stall
 *   timeout (default 250 ms), stops accepting reports for it and broadcasts OnDeviceLost. This guards
 *   against the documented macOS IOHIDDeviceSetReport hangs. Because the thread is shared, a hung write
 *   also delays the other devices until it returns.
 *
 * Threading: every public method must be called from one thread, normally the game thread.
 */
class LIGHTGUNCORE_API FLightgunWriterThread
{
public:
	static constexpr double DefaultStallTimeoutSeconds = 0.25;
	static constexpr double DefaultShutdownTimeoutSeconds = 1.0;

	explicit FLightgunWriterThread(double InStallTimeoutSeconds = DefaultStallTimeoutSeconds);

	/** Calls Shutdown if the thread is still running. */
	~FLightgunWriterThread();

	FLightgunWriterThread(const FLightgunWriterThread&) = delete;
	FLightgunWriterThread& operator=(const FLightgunWriterThread&) = delete;

	/** Start the thread. Returns false if it could not be created; Send then rejects every report. */
	bool Start();

	bool IsRunning() const { return Worker != nullptr; }

	/**
	 * Send everything still queued (e.g. a final release-control report), close every connection and stop
	 * the thread. Returns false if the thread did not finish within TimeoutSeconds because a write is
	 * hung; the thread and its connections are then abandoned deliberately rather than risking a hang
	 * on exit.
	 */
	bool Shutdown(double TimeoutSeconds = DefaultShutdownTimeoutSeconds);

	/**
	 * Register an open connection. The writer thread owns it from here and closes it when the device is
	 * removed or on Shutdown. Drop your own reference: if a write hangs and the thread is abandoned, a
	 * reference released elsewhere would close the handle under the hung write.
	 */
	FLightgunWriterHandle AddDevice(const FLightgunDeviceId& DeviceId, const TSharedRef<ILightgunConnection, ESPMode::ThreadSafe>& Connection);

	/** Send anything still pending for the device, then close its connection. */
	void RemoveDevice(FLightgunWriterHandle Handle);

	/** Queue a report. Returns false if the thread is not running or the device is not Active. */
	bool Send(FLightgunWriterHandle Handle, const FLightgunReport& Report);

	/** Run the watchdog and broadcast OnDeviceLost for newly failed or stalled devices. Call every frame. */
	void Tick();

	ELightgunWriterDeviceState GetDeviceState(FLightgunWriterHandle Handle) const;

	/**
	 * A device failed or stalled. Broadcast from Tick(), once per device. The device stays registered and
	 * rejects reports until RemoveDevice is called.
	 */
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnDeviceLost, FLightgunWriterHandle /*Handle*/, const FLightgunDeviceId& /*DeviceId*/, const FString& /*Reason*/);
	FOnDeviceLost OnDeviceLost;

private:
	class FWorker;
	struct FDeviceStatus;

	struct FRegisteredDevice
	{
		FLightgunDeviceId DeviceId;
		TSharedRef<FDeviceStatus, ESPMode::ThreadSafe> Status;
		bool bLossReported = false;
	};

	void CheckProducerThread() const;

	double StallTimeoutSeconds;
	FWorker* Worker = nullptr;
	FRunnableThread* Thread = nullptr;
	TMap<int32, FRegisteredDevice> Devices;
	int32 NextHandleId = 0;
	mutable uint32 ProducerThreadId = 0;
};
