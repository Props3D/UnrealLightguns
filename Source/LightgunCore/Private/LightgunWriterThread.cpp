// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunWriterThread.h"

#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTLS.h"
#include "HAL/PlatformTime.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "ILightgunFeedbackBackend.h"
#include "LightgunCoreLog.h"
#include "LightgunReport.h"

#include <atomic>

/** Per-device state shared by the producer thread and the writer thread. */
struct FLightgunWriterThread::FDeviceStatus
{
	/** ELightgunWriterDeviceState. Leaves Active at most once, by compare-exchange. */
	std::atomic<uint8> State{ static_cast<uint8>(ELightgunWriterDeviceState::Active) };

	/** FPlatformTime::Cycles64() when the in-progress write started, 0 when idle. */
	std::atomic<uint64> WriteStartCycles{ 0 };

	/** Written by the writer thread before it sets State to Failed; read by the producer after. */
	FString FailureReason;

	ELightgunWriterDeviceState GetState() const
	{
		return static_cast<ELightgunWriterDeviceState>(State.load());
	}

	/** Move from Active to NewState. Returns false if the device had already left Active. */
	bool LeaveActive(ELightgunWriterDeviceState NewState)
	{
		uint8 Expected = static_cast<uint8>(ELightgunWriterDeviceState::Active);
		return State.compare_exchange_strong(Expected, static_cast<uint8>(NewState));
	}
};

/**
 * Everything the writer thread touches. Heap-allocated separately from FLightgunWriterThread so it can
 * be abandoned (leaked) if a write hangs during shutdown, instead of being freed under a running thread.
 */
class FLightgunWriterThread::FWorker final : public FRunnable
{
public:
	enum class ECommandType : uint8
	{
		Add,
		Report,
		Remove,
	};

	struct FCommand
	{
		ECommandType Type = ECommandType::Report;
		int32 HandleId = INDEX_NONE;
		FLightgunReport Report;
		TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> Connection;
		TSharedPtr<FDeviceStatus, ESPMode::ThreadSafe> Status;
		FString Description;
	};

	FWorker()
		: WakeEvent(FPlatformProcess::GetSynchEventFromPool(false))
		, ExitedEvent(FPlatformProcess::GetSynchEventFromPool(true))
	{
	}

	virtual ~FWorker() override
	{
		FPlatformProcess::ReturnSynchEventToPool(WakeEvent);
		FPlatformProcess::ReturnSynchEventToPool(ExitedEvent);
	}

	/** Producer thread only. */
	void Enqueue(FCommand&& Command)
	{
		Commands.Enqueue(MoveTemp(Command));
		WakeEvent->Trigger();
	}

	/** Producer thread only. Returns true if the thread exited within the timeout. */
	bool RequestExitAndWait(double TimeoutSeconds)
	{
		bExitRequested.store(true);
		WakeEvent->Trigger();
		return ExitedEvent->Wait(FTimespan::FromSeconds(TimeoutSeconds));
	}

	//~ Begin FRunnable
	virtual uint32 Run() override
	{
		for (;;)
		{
			// Sample the exit flag before draining, so everything queued before Shutdown is still sent.
			const bool bExiting = bExitRequested.load();

			WriteAllPending();
			ApplyRemovals();

			if (bExiting)
			{
				break;
			}
			WakeEvent->Wait();
		}

		for (TPair<int32, FDevice>& Pair : Devices)
		{
			Pair.Value.Connection->Close();
		}
		Devices.Empty();

		ExitedEvent->Trigger();
		return 0;
	}

	virtual void Stop() override
	{
		bExitRequested.store(true);
		WakeEvent->Trigger();
	}
	//~ End FRunnable

private:
	struct FDevice
	{
		TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> Connection;
		TSharedPtr<FDeviceStatus, ESPMode::ThreadSafe> Status;
		FString Description;
		FLightgunReport Pending;
		bool bRemoveRequested = false;
	};

	void DrainCommands()
	{
		FCommand Command;
		while (Commands.Dequeue(Command))
		{
			switch (Command.Type)
			{
			case ECommandType::Add:
			{
				FDevice& Device = Devices.Add(Command.HandleId);
				Device.Connection = MoveTemp(Command.Connection);
				Device.Status = MoveTemp(Command.Status);
				Device.Description = MoveTemp(Command.Description);
				break;
			}
			case ECommandType::Report:
				if (FDevice* Device = Devices.Find(Command.HandleId))
				{
					// Coalesce: fold into whatever is still waiting for this device.
					Device->Pending.Merge(Command.Report);
				}
				break;
			case ECommandType::Remove:
				if (FDevice* Device = Devices.Find(Command.HandleId))
				{
					Device->bRemoveRequested = true;
				}
				break;
			}
		}
	}

	/** Write one merged report per device, draining the queue before each write so late reports coalesce. */
	void WriteAllPending()
	{
		for (;;)
		{
			DrainCommands();

			TArray<int32, TInlineAllocator<4>> PendingHandles;
			for (const TPair<int32, FDevice>& Pair : Devices)
			{
				if (!Pair.Value.Pending.IsEmpty())
				{
					PendingHandles.Add(Pair.Key);
				}
			}
			if (PendingHandles.IsEmpty())
			{
				return;
			}

			for (int32 HandleId : PendingHandles)
			{
				DrainCommands();
				if (FDevice* Device = Devices.Find(HandleId))
				{
					WriteOne(*Device);
				}
			}
		}
	}

	void WriteOne(FDevice& Device)
	{
		const FLightgunReport Report = Device.Pending;
		Device.Pending.Reset();

		if (Device.Status->GetState() != ELightgunWriterDeviceState::Active)
		{
			return; // Failed or stalled: drop it.
		}

		// Never store 0, which means idle.
		Device.Status->WriteStartCycles.store(FMath::Max<uint64>(FPlatformTime::Cycles64(), 1));
		FString Error;
		const bool bWritten = Device.Connection->Write(Report, Error);
		Device.Status->WriteStartCycles.store(0);

		if (!bWritten)
		{
			Device.Status->FailureReason = Error;
			if (Device.Status->LeaveActive(ELightgunWriterDeviceState::Failed))
			{
				UE_LOG(LogLightgun, Warning, TEXT("Lightgun write failed, device lost: %s: %s"), *Device.Description, *Error);
			}
		}
	}

	void ApplyRemovals()
	{
		for (auto It = Devices.CreateIterator(); It; ++It)
		{
			if (It.Value().bRemoveRequested)
			{
				It.Value().Connection->Close();
				It.RemoveCurrent();
			}
		}
	}

	TQueue<FCommand, EQueueMode::Spsc> Commands;
	FEvent* WakeEvent;
	FEvent* ExitedEvent;
	std::atomic<bool> bExitRequested{ false };

	/** Writer thread only. */
	TMap<int32, FDevice> Devices;
};

FLightgunWriterThread::FLightgunWriterThread(double InStallTimeoutSeconds)
	: StallTimeoutSeconds(InStallTimeoutSeconds)
{
}

FLightgunWriterThread::~FLightgunWriterThread()
{
	if (IsRunning())
	{
		Shutdown();
	}
}

bool FLightgunWriterThread::Start()
{
	CheckProducerThread();
	if (IsRunning())
	{
		return true;
	}

	if (!FPlatformProcess::SupportsMultithreading())
	{
		UE_LOG(LogLightgun, Error, TEXT("Lightgun writer thread not started: multithreading is unavailable, and HID writes must not run on the game thread."));
		return false;
	}

	Worker = new FWorker();
	Thread = FRunnableThread::Create(Worker, TEXT("LightgunWriter"), 0, TPri_Normal);
	if (!Thread)
	{
		UE_LOG(LogLightgun, Error, TEXT("Lightgun writer thread could not be created."));
		delete Worker;
		Worker = nullptr;
		return false;
	}
	return true;
}

bool FLightgunWriterThread::Shutdown(double TimeoutSeconds)
{
	CheckProducerThread();
	if (!IsRunning())
	{
		return true;
	}

	bool bExited = Worker->RequestExitAndWait(TimeoutSeconds);
	if (bExited)
	{
		Thread->WaitForCompletion();
		delete Thread;
		delete Worker;
	}
	else
	{
		// A write is hung inside the platform HID stack. Deleting the thread would block on it, and
		// freeing the worker would crash when the write returns, so both are abandoned on purpose.
		UE_LOG(LogLightgun, Error, TEXT("Lightgun writer thread did not stop within %.0f ms (a HID write is hung); abandoning it."), TimeoutSeconds * 1000.0);
	}

	Thread = nullptr;
	Worker = nullptr;
	Devices.Empty();
	return bExited;
}

FLightgunWriterHandle FLightgunWriterThread::AddDevice(const FLightgunDeviceId& DeviceId, const TSharedRef<ILightgunConnection, ESPMode::ThreadSafe>& Connection)
{
	CheckProducerThread();
	if (!IsRunning())
	{
		return FLightgunWriterHandle();
	}

	FLightgunWriterHandle Handle;
	Handle.Id = NextHandleId++;

	TSharedRef<FDeviceStatus, ESPMode::ThreadSafe> Status = MakeShared<FDeviceStatus, ESPMode::ThreadSafe>();
	Devices.Add(Handle.Id, FRegisteredDevice{ DeviceId, Status, false });

	FWorker::FCommand Command;
	Command.Type = FWorker::ECommandType::Add;
	Command.HandleId = Handle.Id;
	Command.Connection = Connection;
	Command.Status = Status;
	Command.Description = DeviceId.ToString();
	Worker->Enqueue(MoveTemp(Command));
	return Handle;
}

void FLightgunWriterThread::RemoveDevice(FLightgunWriterHandle Handle)
{
	CheckProducerThread();
	if (!IsRunning() || Devices.Remove(Handle.Id) == 0)
	{
		return;
	}

	FWorker::FCommand Command;
	Command.Type = FWorker::ECommandType::Remove;
	Command.HandleId = Handle.Id;
	Worker->Enqueue(MoveTemp(Command));
}

bool FLightgunWriterThread::Send(FLightgunWriterHandle Handle, const FLightgunReport& Report)
{
	CheckProducerThread();
	const FRegisteredDevice* Device = Devices.Find(Handle.Id);
	if (!IsRunning() || !Device || Device->Status->GetState() != ELightgunWriterDeviceState::Active)
	{
		return false;
	}
	if (Report.IsEmpty())
	{
		return true;
	}

	FWorker::FCommand Command;
	Command.Type = FWorker::ECommandType::Report;
	Command.HandleId = Handle.Id;
	Command.Report = Report;
	Worker->Enqueue(MoveTemp(Command));
	return true;
}

void FLightgunWriterThread::Tick()
{
	CheckProducerThread();

	struct FLoss
	{
		FLightgunWriterHandle Handle;
		FLightgunDeviceId DeviceId;
		FString Reason;
	};
	TArray<FLoss, TInlineAllocator<4>> Losses;

	const uint64 NowCycles = FPlatformTime::Cycles64();
	for (TPair<int32, FRegisteredDevice>& Pair : Devices)
	{
		FRegisteredDevice& Device = Pair.Value;
		if (Device.bLossReported)
		{
			continue;
		}

		const uint64 StartCycles = Device.Status->WriteStartCycles.load();
		if (StartCycles != 0 && NowCycles > StartCycles
			&& FPlatformTime::ToSeconds64(NowCycles - StartCycles) > StallTimeoutSeconds)
		{
			if (Device.Status->LeaveActive(ELightgunWriterDeviceState::Stalled))
			{
				UE_LOG(LogLightgun, Warning, TEXT("Lightgun write stalled for over %.0f ms, treating device as disconnected: %s"),
					StallTimeoutSeconds * 1000.0, *Device.DeviceId.ToString());
			}
		}

		const ELightgunWriterDeviceState State = Device.Status->GetState();
		if (State == ELightgunWriterDeviceState::Active)
		{
			continue;
		}

		Device.bLossReported = true;
		FLoss& Loss = Losses.AddDefaulted_GetRef();
		Loss.Handle.Id = Pair.Key;
		Loss.DeviceId = Device.DeviceId;
		Loss.Reason = State == ELightgunWriterDeviceState::Stalled
			? FString::Printf(TEXT("write did not complete within %.0f ms"), StallTimeoutSeconds * 1000.0)
			: Device.Status->FailureReason;
	}

	// Broadcast after iterating: handlers are expected to call RemoveDevice.
	for (const FLoss& Loss : Losses)
	{
		OnDeviceLost.Broadcast(Loss.Handle, Loss.DeviceId, Loss.Reason);
	}
}

ELightgunWriterDeviceState FLightgunWriterThread::GetDeviceState(FLightgunWriterHandle Handle) const
{
	CheckProducerThread();
	const FRegisteredDevice* Device = Devices.Find(Handle.Id);
	return Device ? Device->Status->GetState() : ELightgunWriterDeviceState::Unknown;
}

void FLightgunWriterThread::CheckProducerThread() const
{
#if DO_CHECK
	// The report queue is single-producer: every public method must come from the same thread.
	const uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
	if (ProducerThreadId == 0)
	{
		ProducerThreadId = CurrentThreadId;
	}
	checkf(ProducerThreadId == CurrentThreadId, TEXT("FLightgunWriterThread must be used from a single thread."));
#endif
}
