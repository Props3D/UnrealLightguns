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
		if (PlayerIndex == AllPlayers || Gun.Id.PlayerIndex == PlayerIndex)
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
			SetControl(Gun, Components, true, StartingAmmo);
			bFound = true;
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
			SetControl(Gun, Components, false, 0);
			bFound = true;
		}
	}
	return bFound;
}

bool FLightgunInputDevice::IsConnected(int32 PlayerIndex) const
{
	return Guns.ContainsByPredicate([PlayerIndex](const FGun& Gun) { return Gun.Id.PlayerIndex == PlayerIndex; });
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
				UE_LOG(LogLightgun, Warning, TEXT("%s"), *Event.Message);
				if (FLightgunsModule* const Module = FLightgunsModule::Get())
				{
					Module->OnWarning.Broadcast(Event.Message);
				}
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

	UE_LOG(LogLightgun, Log, TEXT("Lightgun connected: %s"), *DeviceId.ToString());

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

void FLightgunInputDevice::SetControl(FGun& Gun, ELightgunControl Components, bool bTake, int32 StartingAmmo)
{
	if (Components == ELightgunControl::None)
	{
		return;
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
}

void FLightgunInputDevice::ApplyForceFeedback(FGun& Gun)
{
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
