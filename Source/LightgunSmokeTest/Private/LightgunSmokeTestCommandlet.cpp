// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunSmokeTestCommandlet.h"

#include "BlamconHidBackend.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LightgunCoreLog.h"
#include "LightgunReport.h"
#include "LightgunWriterThread.h"
#include "Misc/Parse.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LightgunSmokeTestCommandlet)

namespace LightgunSmokeTest
{
	/** Commandlets have no engine tick, so run the writer's watchdog by hand. */
	void Pump(FLightgunWriterThread& Writer, double Seconds, const bool& bLost)
	{
		const double EndTime = FPlatformTime::Seconds() + Seconds;
		while (!bLost && FPlatformTime::Seconds() < EndTime)
		{
			Writer.Tick();
			FPlatformProcess::Sleep(0.02f);
		}
		Writer.Tick();
	}
}

ULightgunSmokeTestCommandlet::ULightgunSmokeTestCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 ULightgunSmokeTestCommandlet::Main(const FString& Params)
{
	int32 PlayerNumber = 1;
	FParse::Value(*Params, TEXT("player="), PlayerNumber);
	int32 Pulses = 1;
	FParse::Value(*Params, TEXT("pulses="), Pulses);
	Pulses = FMath::Clamp(Pulses, 1, 10);

	if (PlayerNumber < 1 || PlayerNumber > 4)
	{
		UE_LOG(LogLightgun, Error, TEXT("-player must be 1-4, got %d."), PlayerNumber);
		return 2;
	}

	FBlamconHidBackend Backend;
	if (!Backend.IsAvailable())
	{
		return 1;
	}

	FLightgunEnumeration Enumeration;
	Backend.Enumerate(Enumeration);
	for (const FString& Warning : Enumeration.Warnings)
	{
		UE_LOG(LogLightgun, Warning, TEXT("%s"), *Warning);
	}
	for (const FLightgunDeviceId& Found : Enumeration.Devices)
	{
		UE_LOG(LogLightgun, Display, TEXT("Found %s"), *Found.ToString());
	}

	const FLightgunDeviceId* Device = Enumeration.Devices.FindByPredicate(
		[PlayerNumber](const FLightgunDeviceId& Candidate) { return Candidate.PlayerIndex == PlayerNumber - 1; });
	if (!Device)
	{
		UE_LOG(LogLightgun, Error, TEXT("No Blamcon lightgun in Gamepad or Joystick mode for player %d (%d usable gun(s) found)."),
			PlayerNumber, Enumeration.Devices.Num());
		return 1;
	}

	FString OpenError;
	TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> Connection = Backend.Open(*Device, OpenError);
	if (!Connection)
	{
		UE_LOG(LogLightgun, Error, TEXT("Could not open %s: %s"), *Device->ToString(), *OpenError);
		return 1;
	}

	FLightgunWriterThread Writer;
	if (!Writer.Start())
	{
		return 1;
	}

	bool bLost = false;
	FString LostReason;
	Writer.OnDeviceLost.AddLambda([&bLost, &LostReason](FLightgunWriterHandle, const FLightgunDeviceId&, const FString& Reason)
	{
		bLost = true;
		LostReason = Reason;
	});

	const FLightgunWriterHandle Handle = Writer.AddDevice(*Device, Connection.ToSharedRef());
	// The writer now owns the connection. Holding on to it here could close the handle under a hung write.
	Connection.Reset();

	// Take control and fire in the same report: the gun services one report at a time.
	UE_LOG(LogLightgun, Display, TEXT("Taking recoil control and firing %d pulse(s) on player %d."), Pulses, PlayerNumber);
	Writer.Send(Handle, FLightgunReport().TakeControl(true, false, false, false).Recoil(Pulses));
	LightgunSmokeTest::Pump(Writer, 1.0 + 0.25 * Pulses, bLost);

	if (!bLost)
	{
		UE_LOG(LogLightgun, Display, TEXT("Releasing recoil control."));
		Writer.Send(Handle, FLightgunReport().ReleaseControl(true, false, false, false));
		LightgunSmokeTest::Pump(Writer, 0.5, bLost);
	}

	Writer.RemoveDevice(Handle);
	const bool bStopped = Writer.Shutdown();

	if (bLost)
	{
		UE_LOG(LogLightgun, Error, TEXT("Smoke test failed, device lost: %s"), *LostReason);
		return 1;
	}
	if (!bStopped)
	{
		UE_LOG(LogLightgun, Error, TEXT("Smoke test failed: the writer thread did not shut down."));
		return 1;
	}

	UE_LOG(LogLightgun, Display, TEXT("Smoke test passed: reports written to %s. Confirm the recoil fired, and that the trigger fires recoil by itself again."),
		*Device->ToString());
	return 0;
}
