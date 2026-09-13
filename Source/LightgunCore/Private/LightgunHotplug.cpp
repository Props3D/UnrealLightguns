// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunHotplug.h"

#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "LightgunCoreLog.h"

#include <atomic>

class FLightgunHotplug::FWorker final : public FRunnable
{
public:
	FWorker(TArray<TSharedRef<ILightgunFeedbackBackend, ESPMode::ThreadSafe>> InBackends, double InIntervalSeconds)
		: Backends(MoveTemp(InBackends))
		, IntervalSeconds(InIntervalSeconds)
		, WakeEvent(FPlatformProcess::GetSynchEventFromPool(false))
		, ExitedEvent(FPlatformProcess::GetSynchEventFromPool(true))
	{
	}

	virtual ~FWorker() override
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		FPlatformProcess::ReturnSynchEventToPool(ExitedEvent);
	}

	//~ Begin FRunnable
	virtual uint32 Run() override
	{
		while (!bExitRequested.load())
		{
			Scan();
			WakeEvent->Wait(FTimespan::FromSeconds(IntervalSeconds));
		}
		ExitedEvent->Trigger();
		return 0;
	}

	virtual void Stop() override
	{
		bExitRequested.store(true);
		WakeEvent->Trigger();
	}
	//~ End FRunnable

	bool RequestExitAndWait(double TimeoutSeconds)
	{
		Stop();
		return ExitedEvent->Wait(FTimespan::FromSeconds(TimeoutSeconds));
	}

	/** Single consumer: the game thread. */
	TQueue<FLightgunHotplugEvent, EQueueMode::Spsc> Events;

private:
	void Scan()
	{
		TSet<FLightgunDeviceId> Present;
		TSet<FString> Warnings;
		TMap<FLightgunDeviceId, ILightgunFeedbackBackend*> BackendFor;
		FLightgunEnumeration Enumeration;
		for (const TSharedRef<ILightgunFeedbackBackend, ESPMode::ThreadSafe>& Backend : Backends)
		{
			Backend->Enumerate(Enumeration);
			for (const FLightgunDeviceId& DeviceId : Enumeration.Devices)
			{
				Present.Add(DeviceId);
				BackendFor.Add(DeviceId, &Backend.Get());
			}
			for (const FString& Warning : Enumeration.Warnings)
			{
				Warnings.Add(Warning);
			}
		}

		// Arrivals.
		for (const FLightgunDeviceId& DeviceId : Present)
		{
			if (Known.Contains(DeviceId))
			{
				continue;
			}

			FString Error;
			TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> Connection = BackendFor.FindChecked(DeviceId)->Open(DeviceId, Error);
			if (!Connection)
			{
				// Retried every scan; logged once until the gun goes away.
				bool bAlreadyReported = false;
				FailedOpens.Add(DeviceId, &bAlreadyReported);
				if (!bAlreadyReported)
				{
					UE_LOG(LogLightgun, Warning, TEXT("Could not open lightgun %s: %s"), *DeviceId.ToString(), *Error);
				}
				continue;
			}

			FailedOpens.Remove(DeviceId);
			Known.Add(DeviceId);

			FLightgunHotplugEvent Event;
			Event.Type = FLightgunHotplugEvent::EType::Arrived;
			Event.DeviceId = DeviceId;
			Event.Connection = MoveTemp(Connection);
			Events.Enqueue(MoveTemp(Event));
		}

		// Removals, and forgetting state for guns that went away so a replug starts fresh.
		for (auto It = Known.CreateIterator(); It; ++It)
		{
			if (!Present.Contains(*It))
			{
				FLightgunHotplugEvent Event;
				Event.Type = FLightgunHotplugEvent::EType::Removed;
				Event.DeviceId = *It;
				Events.Enqueue(MoveTemp(Event));
				It.RemoveCurrent();
			}
		}
		for (auto It = FailedOpens.CreateIterator(); It; ++It)
		{
			if (!Present.Contains(*It))
			{
				It.RemoveCurrent();
			}
		}

		// Warnings: once each while the condition lasts.
		for (const FString& Warning : Warnings)
		{
			if (!ActiveWarnings.Contains(Warning))
			{
				FLightgunHotplugEvent Event;
				Event.Type = FLightgunHotplugEvent::EType::Warning;
				Event.Message = Warning;
				Events.Enqueue(MoveTemp(Event));
			}
		}
		ActiveWarnings = MoveTemp(Warnings);
	}

	TArray<TSharedRef<ILightgunFeedbackBackend, ESPMode::ThreadSafe>> Backends;
	double IntervalSeconds;
	FEvent* WakeEvent;
	FEvent* ExitedEvent;
	std::atomic<bool> bExitRequested{ false };

	/** Hot-plug thread only. */
	TSet<FLightgunDeviceId> Known;
	TSet<FLightgunDeviceId> FailedOpens;
	TSet<FString> ActiveWarnings;
};

FLightgunHotplug::FLightgunHotplug(TArray<TSharedRef<ILightgunFeedbackBackend, ESPMode::ThreadSafe>> InBackends, double InIntervalSeconds)
	: Backends(MoveTemp(InBackends))
	, IntervalSeconds(InIntervalSeconds)
{
}

FLightgunHotplug::~FLightgunHotplug()
{
	if (IsRunning())
	{
		Shutdown();
	}
}

bool FLightgunHotplug::Start()
{
	if (IsRunning())
	{
		return true;
	}
	if (!FPlatformProcess::SupportsMultithreading())
	{
		UE_LOG(LogLightgun, Error, TEXT("Lightgun hot-plug thread not started: multithreading is unavailable."));
		return false;
	}

	Worker = new FWorker(Backends, IntervalSeconds);
	Thread = FRunnableThread::Create(Worker, TEXT("LightgunHotplug"), 0, TPri_BelowNormal);
	if (!Thread)
	{
		UE_LOG(LogLightgun, Error, TEXT("Lightgun hot-plug thread could not be created."));
		delete Worker;
		Worker = nullptr;
		return false;
	}
	return true;
}

bool FLightgunHotplug::Shutdown(double TimeoutSeconds)
{
	if (!IsRunning())
	{
		return true;
	}

	const bool bExited = Worker->RequestExitAndWait(TimeoutSeconds);
	if (bExited)
	{
		Thread->WaitForCompletion();
		delete Thread;

		// Close connections for arrivals nobody collected.
		FLightgunHotplugEvent Event;
		while (Worker->Events.Dequeue(Event))
		{
			if (Event.Connection)
			{
				Event.Connection->Close();
			}
		}
		delete Worker;
	}
	else
	{
		// A backend call is hung. Deleting the thread would block on it and freeing the worker would
		// crash when the call returns, so both are abandoned on purpose.
		UE_LOG(LogLightgun, Error, TEXT("Lightgun hot-plug thread did not stop within %.0f ms; abandoning it."), TimeoutSeconds * 1000.0);
	}

	Thread = nullptr;
	Worker = nullptr;
	return bExited;
}

bool FLightgunHotplug::PollEvent(FLightgunHotplugEvent& OutEvent)
{
	return Worker && Worker->Events.Dequeue(OutEvent);
}
