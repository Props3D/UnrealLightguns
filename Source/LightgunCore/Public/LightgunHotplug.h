// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "ILightgunFeedbackBackend.h"
#include "LightgunDeviceId.h"

class FRunnableThread;

/** Something the hot-plug thread noticed. */
struct FLightgunHotplugEvent
{
	enum class EType : uint8
	{
		/** A gun appeared and was opened. Connection is set, and DeviceId.Info holds what the gun reported. */
		Arrived,
		/** A previously arrived gun is no longer enumerated. */
		Removed,
		/** A gun is present but unusable, e.g. in mouse mode. Message is set. Sent once while the condition lasts. */
		Warning,
	};

	EType Type = EType::Warning;
	FLightgunDeviceId DeviceId;
	TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> Connection;
	FString Message;
};

/**
 * Polls the feedback backends for guns arriving and leaving, on its own thread, so enumeration and
 * opening never block the game thread (docs/PLUGIN_SPEC.md section 4).
 *
 * Events are queued for the game thread to collect with PollEvent. A gun stays "known" after arriving
 * until it disappears from enumeration, so it is opened once: a gun dropped after an I/O error is
 * reopened when it is replugged.
 */
class LIGHTGUNCORE_API FLightgunHotplug
{
public:
	static constexpr double DefaultIntervalSeconds = 1.0;

	FLightgunHotplug(TArray<TSharedRef<ILightgunFeedbackBackend, ESPMode::ThreadSafe>> InBackends, double InIntervalSeconds = DefaultIntervalSeconds);

	/** Calls Shutdown if still running. */
	~FLightgunHotplug();

	FLightgunHotplug(const FLightgunHotplug&) = delete;
	FLightgunHotplug& operator=(const FLightgunHotplug&) = delete;

	/** Start polling. The first scan runs immediately. */
	bool Start();

	/**
	 * Stop polling. Returns false if the thread did not stop within TimeoutSeconds (a backend call is
	 * hung); it is then abandoned. Connections from arrivals not yet collected are closed.
	 */
	bool Shutdown(double TimeoutSeconds = 2.0);

	bool IsRunning() const { return Worker != nullptr; }

	/** Collect the next event. Call from one thread only, normally the game thread. */
	bool PollEvent(FLightgunHotplugEvent& OutEvent);

private:
	class FWorker;

	TArray<TSharedRef<ILightgunFeedbackBackend, ESPMode::ThreadSafe>> Backends;
	double IntervalSeconds;
	FWorker* Worker = nullptr;
	FRunnableThread* Thread = nullptr;
};
