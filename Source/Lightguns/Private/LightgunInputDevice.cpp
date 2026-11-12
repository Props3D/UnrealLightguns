// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunInputDevice.h"

#include "BlamconHidBackend.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformTime.h"
#include "LightgunCoreLog.h"
#include "LightgunHotplug.h"
#include "LightgunKeys.h"
#include "LightgunReport.h"
#include "LightgunsModule.h"

namespace LightgunInputDevice
{
	/** Force feedback channel levels that count as "on". */
	constexpr float RumbleThreshold = 0.1f;
	constexpr float RecoilThreshold = 0.5f;

	/** While a rumble channel stays on, pulse the motor this often. */
	constexpr int32 RumbleOnMs = 150;
	constexpr int32 RumbleOffMs = 100;
	constexpr double RumbleRepeatSeconds = (RumbleOnMs + RumbleOffMs) / 1000.0;

	/** Cap on input reports drained per gun per frame (reports arrive at about 1 kHz). */
	constexpr int32 MaxReportsPerFrame = 64;

	/** A gun that reported it can't take feedback gets none; legacy firmware can't tell, so it does. */
	bool CanTakeFeedback(const FLightgunDeviceId& Id)
	{
		return !Id.Info.bKnown || Id.Info.bFeedbackAvailable;
	}

	/** "recoil, LED" for host control bits. */
	FString DescribeHostControl(uint8 HostControl)
	{
		TArray<FString> Names;
		if (HostControl & LightgunDeviceInfoReport::HostControlRecoil) { Names.Add(TEXT("recoil")); }
		if (HostControl & LightgunDeviceInfoReport::HostControlRumble) { Names.Add(TEXT("rumble")); }
		if (HostControl & LightgunDeviceInfoReport::HostControlLed) { Names.Add(TEXT("LED")); }
		if (HostControl & LightgunDeviceInfoReport::HostControlAmmo) { Names.Add(TEXT("ammo")); }
		return FString::Join(Names, TEXT(", "));
	}

	struct FButtonKey
	{
		uint32 Bit;
		const FKey* Key;
	};

	const FButtonKey ButtonKeys[] =
	{
		{ FLightgunInputState::ButtonTrigger, &FLightgunKeys::Trigger },
		{ FLightgunInputState::ButtonA, &FLightgunKeys::ButtonA },
		{ FLightgunInputState::ButtonB, &FLightgunKeys::ButtonB },
		{ FLightgunInputState::ButtonY, &FLightgunKeys::ButtonY },
		{ FLightgunInputState::ButtonStart, &FLightgunKeys::Start },
		{ FLightgunInputState::ButtonSelect, &FLightgunKeys::Select },
		{ FLightgunInputState::DPadUp, &FLightgunKeys::DPadUp },
		{ FLightgunInputState::DPadDown, &FLightgunKeys::DPadDown },
		{ FLightgunInputState::DPadLeft, &FLightgunKeys::DPadLeft },
		{ FLightgunInputState::DPadRight, &FLightgunKeys::DPadRight },
	};
}

FLightgunInputDevice::FLightgunInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	: MessageHandler(InMessageHandler)
{
	TSharedRef<FBlamconHidBackend, ESPMode::ThreadSafe> Backend = MakeShared<FBlamconHidBackend, ESPMode::ThreadSafe>();
	if (!Backend->IsAvailable() || !Writer.Start())
	{
		UE_LOG(LogLightgun, Error, TEXT("Lightgun support is disabled for this session."));
		return;
	}

	Writer.OnDeviceLost.AddLambda([this](FLightgunWriterHandle, const FLightgunDeviceId& DeviceId, const FString& Reason)
	{
		RemoveGun(DeviceId, Reason);
	});

	TArray<TSharedRef<ILightgunFeedbackBackend, ESPMode::ThreadSafe>> Backends;
	Backends.Add(Backend);
	Hotplug = MakeUnique<FLightgunHotplug>(MoveTemp(Backends));
	Hotplug->Start();

	if (FSlateApplication::IsInitialized())
	{
		bApplicationActive = FSlateApplication::Get().IsActive();
		ActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddRaw(this, &FLightgunInputDevice::HandleApplicationActivationChanged);
	}
}

FLightgunInputDevice::~FLightgunInputDevice()
{
	if (ActivationHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
	}

	if (Hotplug)
	{
		Hotplug->Shutdown();
	}

	// Never leave a gun under app control. The writer sends these before it stops.
	for (FGun& Gun : Guns)
	{
		SetControl(Gun, Gun.HeldControl, false, 0);
		Writer.RemoveDevice(Gun.WriterHandle);
	}
	Guns.Empty();
	Writer.Shutdown();
}

bool FLightgunInputDevice::SendFeedback(int32 PlayerIndex, const FLightgunReport& Report)
{
	bool bSent = false;
	for (FGun& Gun : Guns)
	{
		if ((PlayerIndex == AllPlayers || Gun.Id.PlayerIndex == PlayerIndex) && LightgunInputDevice::CanTakeFeedback(Gun.Id))
		{
			bSent |= Writer.Send(Gun.WriterHandle, Report);
		}
	}
	return bSent;
}

bool FLightgunInputDevice::TakeControl(int32 PlayerIndex, ELightgunControl Components, int32 StartingAmmo)
{
	bool bFound = false;
	for (FGun& Gun : Guns)
	{
		if (PlayerIndex == AllPlayers || Gun.Id.PlayerIndex == PlayerIndex)
		{
			bFound |= SetControl(Gun, Components, true, StartingAmmo);
		}
	}
	return bFound;
}

bool FLightgunInputDevice::ReleaseControl(int32 PlayerIndex, ELightgunControl Components)
{
	bool bFound = false;
	for (FGun& Gun : Guns)
	{
		if (PlayerIndex == AllPlayers || Gun.Id.PlayerIndex == PlayerIndex)
		{
			bFound |= SetControl(Gun, Components, false, 0);
		}
	}
	return bFound;
}

bool FLightgunInputDevice::IsConnected(int32 PlayerIndex) const
{
	return Guns.ContainsByPredicate([PlayerIndex](const FGun& Gun) { return Gun.Id.PlayerIndex == PlayerIndex; });
}

bool FLightgunInputDevice::HasGunInput(int32 PlayerIndex) const
{
	return Guns.ContainsByPredicate([PlayerIndex](const FGun& Gun) { return Gun.Id.PlayerIndex == PlayerIndex && Gun.Id.bHasGunInput; });
}

const FLightgunDeviceId* FLightgunInputDevice::FindDeviceId(int32 PlayerIndex) const
{
	const FGun* const Gun = Guns.FindByPredicate([PlayerIndex](const FGun& Candidate) { return Candidate.Id.PlayerIndex == PlayerIndex; });
	return Gun ? &Gun->Id : nullptr;
}

bool FLightgunInputDevice::IsGamepadAttached() const
{
	// Mouse-mode guns aren't gamepads to the engine: their input arrives as the mouse.
	return Guns.ContainsByPredicate([](const FGun& Gun) { return Gun.Id.bHasGunInput; });
}

TArray<int32> FLightgunInputDevice::GetConnectedPlayers() const
{
	TArray<int32> Players;
	for (const FGun& Gun : Guns)
	{
		Players.Add(Gun.Id.PlayerIndex);
	}
	Players.Sort();
	return Players;
}

int32 FLightgunInputDevice::GetPlayerIndex(FInputDeviceId InputDeviceId) const
{
	for (const TPair<int32, FInputDeviceId>& Pair : InputDeviceIds)
	{
		if (Pair.Value == InputDeviceId)
		{
			return Pair.Key;
		}
	}
	return INDEX_NONE;
}

void FLightgunInputDevice::OnSessionStarted()
{
	for (FGun& Gun : Guns)
	{
		if (bApplicationActive)
		{
			SetControl(Gun, ELightgunControl::Session, true, 0);
		}
		else
		{
			Gun.SuspendedControl |= ELightgunControl::Session;
		}
	}
}

void FLightgunInputDevice::OnSessionEnded()
{
	for (FGun& Gun : Guns)
	{
		SetControl(Gun, Gun.HeldControl, false, 0);
		Gun.SuspendedControl = ELightgunControl::None;
	}
}

void FLightgunInputDevice::Tick(float DeltaTime)
{
	if (Hotplug)
	{
		FLightgunHotplugEvent Event;
		while (Hotplug->PollEvent(Event))
		{
			switch (Event.Type)
			{
			case FLightgunHotplugEvent::EType::Arrived:
				AddGun(Event.DeviceId, Event.Connection.ToSharedRef());
				break;
			case FLightgunHotplugEvent::EType::Removed:
				RemoveGun(Event.DeviceId, TEXT("unplugged"));
				break;
			case FLightgunHotplugEvent::EType::Warning:
				Warn(Event.Message);
				break;
			}
		}
	}

	// Watchdog: may call RemoveGun through OnDeviceLost.
	Writer.Tick();
}

void FLightgunInputDevice::SendControllerEvents()
{
	for (int32 Index = Guns.Num() - 1; Index >= 0; --Index)
	{
		FGun& Gun = Guns[Index];
		if (!Gun.Id.bHasGunInput)
		{
			// Mouse mode: the vendor collection has no input reports, and reading one isn't meaningful.
			continue;
		}
		const bool bFirstReport = !Gun.bHasInput;
		const float OldAimX = Gun.Input.AimX;
		const float OldAimY = Gun.Input.AimY;

		bool bReadError = false;
		bool bGotReport = false;
		FLightgunInputState State;
		for (int32 Count = 0; Count < LightgunInputDevice::MaxReportsPerFrame; ++Count)
		{
			const ELightgunReadResult Result = Gun.Connection->ReadInput(State);
			if (Result != ELightgunReadResult::Report)
			{
				bReadError = Result == ELightgunReadResult::Error;
				break;
			}
			// Button edges from every report, so a tap shorter than a frame still registers.
			SendButtonEvents(Gun, Gun.bHasInput ? Gun.Input.Buttons : 0, State.Buttons);
			Gun.Input = State;
			Gun.bHasInput = true;
			bGotReport = true;
		}

		// Aim only needs the latest sample.
		if (bGotReport && (bFirstReport || Gun.Input.AimX != OldAimX || Gun.Input.AimY != OldAimY))
		{
			MessageHandler->OnControllerAnalog(FLightgunKeys::AimX.GetFName(), Gun.UserId, Gun.InputDeviceId, Gun.Input.AimX);
			MessageHandler->OnControllerAnalog(FLightgunKeys::AimY.GetFName(), Gun.UserId, Gun.InputDeviceId, Gun.Input.AimY);
		}

		if (bReadError)
		{
			const FLightgunDeviceId DeviceId = Gun.Id; // RemoveGun frees Gun
			RemoveGun(DeviceId, TEXT("input read failed; replug the gun to reconnect"));
		}
	}
}

void FLightgunInputDevice::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	MessageHandler = InMessageHandler;
}

void FLightgunInputDevice::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
{
	if (FGun* const Gun = FindGun(ControllerId))
	{
		switch (ChannelType)
		{
		case FForceFeedbackChannelType::LEFT_LARGE: Gun->ForceFeedback.LeftLarge = Value; break;
		case FForceFeedbackChannelType::LEFT_SMALL: Gun->ForceFeedback.LeftSmall = Value; break;
		case FForceFeedbackChannelType::RIGHT_LARGE: Gun->ForceFeedback.RightLarge = Value; break;
		case FForceFeedbackChannelType::RIGHT_SMALL: Gun->ForceFeedback.RightSmall = Value; break;
		}
		ApplyForceFeedback(*Gun);
	}
}

void FLightgunInputDevice::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values)
{
	if (FGun* const Gun = FindGun(ControllerId))
	{
		Gun->ForceFeedback = Values;
		ApplyForceFeedback(*Gun);
	}
}

void FLightgunInputDevice::SetLightColor(int32 ControllerId, FColor Color)
{
	SendFeedback(ControllerId, FLightgunReport().Led(Color.R, Color.G, Color.B));
}

void FLightgunInputDevice::ResetLightColor(int32 ControllerId)
{
	SendFeedback(ControllerId, FLightgunReport().LedOff());
}

void FLightgunInputDevice::SetDeviceProperty(int32 ControllerId, const FInputDeviceProperty* Property)
{
	// UColorInputDeviceProperty (Input Device Subsystem) arrives here.
	if (Property && Property->Name == FInputDeviceLightColorProperty::PropertyName())
	{
		const FInputDeviceLightColorProperty* const Light = static_cast<const FInputDeviceLightColorProperty*>(Property);
		if (Light->bEnable)
		{
			SetLightColor(ControllerId, Light->Color);
		}
		else
		{
			ResetLightColor(ControllerId);
		}
	}
}

FLightgunInputDevice::FGun* FLightgunInputDevice::FindGun(int32 PlayerIndex)
{
	return Guns.FindByPredicate([PlayerIndex](const FGun& Gun) { return Gun.Id.PlayerIndex == PlayerIndex; });
}

void FLightgunInputDevice::AddGun(const FLightgunDeviceId& DeviceId, const TSharedRef<ILightgunConnection, ESPMode::ThreadSafe>& Connection)
{
	if (FindGun(DeviceId.PlayerIndex))
	{
		UE_LOG(LogLightgun, Warning, TEXT("Ignoring %s: player %d already has a lightgun."), *DeviceId.ToString(), DeviceId.PlayerIndex + 1);
		Connection->Close();
		return;
	}

	const FLightgunWriterHandle WriterHandle = Writer.AddDevice(DeviceId, Connection);
	if (!WriterHandle.IsValid())
	{
		Connection->Close();
		return;
	}

	IPlatformInputDeviceMapper& Mapper = IPlatformInputDeviceMapper::Get();
	FInputDeviceId* InputDeviceId = InputDeviceIds.Find(DeviceId.PlayerIndex);
	if (!InputDeviceId)
	{
		InputDeviceId = &InputDeviceIds.Add(DeviceId.PlayerIndex, Mapper.AllocateNewInputDeviceId());
	}

	FGun& Gun = Guns.AddDefaulted_GetRef();
	Gun.Id = DeviceId;
	Gun.Connection = Connection;
	Gun.WriterHandle = WriterHandle;
	Gun.InputDeviceId = *InputDeviceId;
	Gun.UserId = FGenericPlatformMisc::GetPlatformUserForUserIndex(DeviceId.PlayerIndex);

	UE_LOG(LogLightgun, Log, TEXT("Lightgun connected: %s, %s"), *DeviceId.ToString(), *DeviceId.Describe());
	HandleDeviceInfo(Gun);

	if (ShouldHoldControl())
	{
		SetControl(Gun, ELightgunControl::Session, true, 0);
	}
	else if (FLightgunsModule* const Module = FLightgunsModule::Get(); Module && Module->IsSessionActive())
	{
		Gun.SuspendedControl = ELightgunControl::Session;
	}

	// Broadcasts the engine's input device connection event.
	Mapper.Internal_MapInputDeviceToUser(Gun.InputDeviceId, Gun.UserId, EInputDeviceConnectionState::Connected);
}

void FLightgunInputDevice::RemoveGun(const FLightgunDeviceId& DeviceId, const FString& Reason)
{
	const int32 Index = Guns.IndexOfByPredicate([&DeviceId](const FGun& Gun) { return Gun.Id == DeviceId; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	// Copy the entry: it is removed before the engine is told, and DeviceId may refer into it.
	const FGun Gun = Guns[Index];
	const FLightgunDeviceId RemovedId = Gun.Id;
	Guns.RemoveAt(Index);

	// Stop reading before the writer closes the connection.
	Writer.RemoveDevice(Gun.WriterHandle);

	// Don't leave buttons held down.
	if (Gun.bHasInput)
	{
		SendButtonEvents(Gun, Gun.Input.Buttons, 0);
	}

	UE_LOG(LogLightgun, Log, TEXT("Lightgun disconnected (%s): %s"), *Reason, *RemovedId.ToString());
	IPlatformInputDeviceMapper::Get().Internal_SetInputDeviceConnectionState(Gun.InputDeviceId, EInputDeviceConnectionState::Disconnected);
}

bool FLightgunInputDevice::SetControl(FGun& Gun, ELightgunControl Components, bool bTake, int32 StartingAmmo)
{
	if (!LightgunInputDevice::CanTakeFeedback(Gun.Id))
	{
		return false;
	}
	if (Components == ELightgunControl::None)
	{
		return true;
	}

	const bool bRecoil = EnumHasAnyFlags(Components, ELightgunControl::Recoil);
	const bool bRumble = EnumHasAnyFlags(Components, ELightgunControl::Rumble);
	const bool bLed = EnumHasAnyFlags(Components, ELightgunControl::Led);
	const bool bAmmo = EnumHasAnyFlags(Components, ELightgunControl::Ammo);

	FLightgunReport Report;
	if (bTake)
	{
		Report.TakeControl(bRecoil, bRumble, bLed, bAmmo);
		if (bAmmo)
		{
			// Taking ammo control zeroes the display, so the count must ride in the same report.
			Report.Ammo(StartingAmmo);
		}
		Gun.HeldControl |= Components;
	}
	else
	{
		Report.ReleaseControl(bRecoil, bRumble, bLed, bAmmo);
		Gun.HeldControl &= ~Components;
	}
	Writer.Send(Gun.WriterHandle, Report);
	return true;
}

void FLightgunInputDevice::HandleDeviceInfo(FGun& Gun)
{
	const FLightgunDeviceId& Id = Gun.Id;
	const FLightgunDeviceInfo& Info = Id.Info;

	if (!Info.bKnown)
	{
		// Legacy firmware says nothing about itself. Feedback over USB is the 3.0 baseline; over Bluetooth it
		// may not be supported, so say so rather than failing silently. Feedback is still sent.
		if (Id.Transport == ELightgunTransport::Bluetooth)
		{
			Warn(FString::Printf(TEXT("Blamcon lightgun (player %d) is on Bluetooth with firmware that doesn't report its version. Force feedback over Bluetooth may need a firmware update; use USB if feedback doesn't work."),
				Id.PlayerIndex + 1));
		}
		return;
	}

	if (!Info.bFeedbackAvailable)
	{
		Warn(FString::Printf(TEXT("Blamcon lightgun (player %d) can't take force feedback in its current mode or connection (%s). Update the gun's firmware or use USB. Input still works."),
			Id.PlayerIndex + 1, *Id.Describe()));
	}

	// The product id decides the player index; the gun's own setting should agree with it.
	if (Info.PlayerNumber != Id.PlayerIndex + 1)
	{
		Warn(FString::Printf(TEXT("Blamcon lightgun connected as player %d reports player number %d. Using player %d."),
			Id.PlayerIndex + 1, Info.PlayerNumber, Id.PlayerIndex + 1));
	}

	// Control already held on connect belongs to someone else: another program, or a process that crashed.
	// The plugin only ever releases control it took itself in this process, so it just says so. It explains
	// why the gun's own recoil doesn't fire on the trigger; replugging the gun clears it after a crash.
	if (Info.bHasLiveState && (Info.HostControl & (LightgunDeviceInfoReport::HostControlRecoil | LightgunDeviceInfoReport::HostControlRumble
		| LightgunDeviceInfoReport::HostControlLed | LightgunDeviceInfoReport::HostControlAmmo)) != 0)
	{
		UE_LOG(LogLightgun, Log, TEXT("Lightgun player %d is already under another host's control (%s): another program, or one that closed without releasing it. Not released; reconnect the gun to clear it."),
			Id.PlayerIndex + 1, *LightgunInputDevice::DescribeHostControl(Info.HostControl));
	}
}

void FLightgunInputDevice::Warn(const FString& Message)
{
	UE_LOG(LogLightgun, Warning, TEXT("%s"), *Message);
	if (FLightgunsModule* const Module = FLightgunsModule::Get())
	{
		Module->OnWarning.Broadcast(Message);
	}
}

void FLightgunInputDevice::ApplyForceFeedback(FGun& Gun)
{
	if (!LightgunInputDevice::CanTakeFeedback(Gun.Id))
	{
		return;
	}

	const FForceFeedbackValues& Values = Gun.ForceFeedback;

	// Large motors: the rumble motor only pulses, so keep pulsing while the channel stays on.
	if (FMath::Max(Values.LeftLarge, Values.RightLarge) >= LightgunInputDevice::RumbleThreshold)
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= Gun.NextRumbleTime)
		{
			Writer.Send(Gun.WriterHandle, FLightgunReport().Rumble(1, LightgunInputDevice::RumbleOnMs, LightgunInputDevice::RumbleOffMs));
			Gun.NextRumbleTime = Now + LightgunInputDevice::RumbleRepeatSeconds;
		}
	}
	else
	{
		Gun.NextRumbleTime = 0.0;
	}

	// Small motors: one recoil pulse each time the channel turns on.
	const bool bRecoilOn = FMath::Max(Values.LeftSmall, Values.RightSmall) >= LightgunInputDevice::RecoilThreshold;
	if (bRecoilOn && !Gun.bRecoilChannelOn)
	{
		Writer.Send(Gun.WriterHandle, FLightgunReport().Recoil(1));
	}
	Gun.bRecoilChannelOn = bRecoilOn;
}

void FLightgunInputDevice::SendButtonEvents(const FGun& Gun, uint32 OldButtons, uint32 NewButtons)
{
	const uint32 Changed = OldButtons ^ NewButtons;
	if (Changed == 0)
	{
		return;
	}

	for (const LightgunInputDevice::FButtonKey& Button : LightgunInputDevice::ButtonKeys)
	{
		if ((Changed & Button.Bit) == 0)
		{
			continue;
		}
		if ((NewButtons & Button.Bit) != 0)
		{
			MessageHandler->OnControllerButtonPressed(Button.Key->GetFName(), Gun.UserId, Gun.InputDeviceId, false);
		}
		else
		{
			MessageHandler->OnControllerButtonReleased(Button.Key->GetFName(), Gun.UserId, Gun.InputDeviceId, false);
		}
	}
}

void FLightgunInputDevice::HandleApplicationActivationChanged(bool bIsActive)
{
	if (bIsActive == bApplicationActive)
	{
		return;
	}
	bApplicationActive = bIsActive;

	// Give guns back while the game is in the background, so they work normally in other apps.
	for (FGun& Gun : Guns)
	{
		if (!bIsActive)
		{
			Gun.SuspendedControl |= Gun.HeldControl;
			SetControl(Gun, Gun.HeldControl, false, 0);
		}
		else
		{
			// The ammo display comes back at 0 until the game next sets the count.
			SetControl(Gun, Gun.SuspendedControl, true, 0);
			Gun.SuspendedControl = ELightgunControl::None;
		}
	}
}

bool FLightgunInputDevice::ShouldHoldControl() const
{
	const FLightgunsModule* const Module = FLightgunsModule::Get();
	return bApplicationActive && Module && Module->IsSessionActive();
}
