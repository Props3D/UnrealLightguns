// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunLibrary.h"

#include "Interfaces/IPluginManager.h"
#include "LightgunDeviceInfoReport.h"
#include "LightgunInputDevice.h"
#include "LightgunReport.h"
#include "LightgunsModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LightgunLibrary)

namespace LightgunLibrary
{
	FLightgunInputDevice* GetDevice()
	{
		const FLightgunsModule* const Module = FLightgunsModule::Get();
		return Module ? Module->GetInputDevice() : nullptr;
	}

	bool Send(int32 PlayerIndex, const FLightgunReport& Report)
	{
		FLightgunInputDevice* const Device = GetDevice();
		return Device && Device->SendFeedback(PlayerIndex, Report);
	}

	ELightgunControl ToControl(bool bRecoil, bool bRumble, bool bLed, bool bAmmo)
	{
		ELightgunControl Control = ELightgunControl::None;
		if (bRecoil) { Control |= ELightgunControl::Recoil; }
		if (bRumble) { Control |= ELightgunControl::Rumble; }
		if (bLed) { Control |= ELightgunControl::Led; }
		if (bAmmo) { Control |= ELightgunControl::Ammo; }
		return Control;
	}

	FLightgunReport MakeLed(FLinearColor Color)
	{
		// The colour picker works in linear space; the LED takes 8-bit sRGB, which matches what the picker shows.
		const FColor Srgb = Color.ToFColor(true);
		return FLightgunReport().Led(Srgb.R, Srgb.G, Srgb.B);
	}
}

bool ULightgunLibrary::PlayRecoil(int32 PlayerIndex, int32 Pulses)
{
	return LightgunLibrary::Send(PlayerIndex, FLightgunReport().Recoil(Pulses));
}

bool ULightgunLibrary::PlayRecoilTimed(int32 PlayerIndex, int32 Pulses, int32 OnMs, int32 OffMs)
{
	return LightgunLibrary::Send(PlayerIndex, FLightgunReport().Recoil(Pulses, OnMs, OffMs));
}

bool ULightgunLibrary::PlayRumble(int32 PlayerIndex, int32 Pulses)
{
	return LightgunLibrary::Send(PlayerIndex, FLightgunReport().Rumble(Pulses));
}

bool ULightgunLibrary::PlayRumbleTimed(int32 PlayerIndex, int32 Pulses, int32 OnMs, int32 OffMs)
{
	return LightgunLibrary::Send(PlayerIndex, FLightgunReport().Rumble(Pulses, OnMs, OffMs));
}

bool ULightgunLibrary::SetLedColor(int32 PlayerIndex, FLinearColor Color)
{
	return LightgunLibrary::Send(PlayerIndex, LightgunLibrary::MakeLed(Color));
}

bool ULightgunLibrary::FlashLed(int32 PlayerIndex, FLinearColor Color, int32 Flashes, int32 LitMs, int32 DarkMs)
{
	const FColor Srgb = Color.ToFColor(true);
	FLightgunReport Report;
	if (LitMs == 0 && DarkMs == 0)
	{
		Report.Led(Srgb.R, Srgb.G, Srgb.B, Flashes);
	}
	else
	{
		// Both periods or neither: the firmware can misread the first if only the second is given.
		Report.Led(Srgb.R, Srgb.G, Srgb.B, Flashes, LitMs, DarkMs);
	}
	return LightgunLibrary::Send(PlayerIndex, Report);
}

bool ULightgunLibrary::SetAmmoCount(int32 PlayerIndex, int32 Remaining)
{
	return LightgunLibrary::Send(PlayerIndex, FLightgunReport().Ammo(Remaining));
}

bool ULightgunLibrary::TakeFeedbackControl(int32 PlayerIndex, bool bRecoil, bool bRumble, bool bLed, bool bAmmo, int32 StartingAmmo)
{
	FLightgunInputDevice* const Device = LightgunLibrary::GetDevice();
	return Device && Device->TakeControl(PlayerIndex, LightgunLibrary::ToControl(bRecoil, bRumble, bLed, bAmmo), StartingAmmo);
}

bool ULightgunLibrary::ReleaseFeedbackControl(int32 PlayerIndex, bool bRecoil, bool bRumble, bool bLed, bool bAmmo)
{
	FLightgunInputDevice* const Device = LightgunLibrary::GetDevice();
	return Device && Device->ReleaseControl(PlayerIndex, LightgunLibrary::ToControl(bRecoil, bRumble, bLed, bAmmo));
}

bool ULightgunLibrary::IsLightgunConnected(int32 PlayerIndex)
{
	const FLightgunInputDevice* const Device = LightgunLibrary::GetDevice();
	return Device && Device->IsConnected(PlayerIndex);
}

TArray<int32> ULightgunLibrary::GetConnectedLightguns()
{
	const FLightgunInputDevice* const Device = LightgunLibrary::GetDevice();
	return Device ? Device->GetConnectedPlayers() : TArray<int32>();
}

namespace LightgunLibrary
{
	ELightgunBoard ToBoard(uint8 Board)
	{
		switch (Board)
		{
		case LightgunDeviceInfoReport::BoardRP2040: return ELightgunBoard::RP2040;
		case LightgunDeviceInfoReport::BoardRP2350: return ELightgunBoard::RP2350;
		default: return ELightgunBoard::Unknown;
		}
	}

	/** The Bluetooth modes report as their wired equivalent: Connection says how the gun is attached. */
	ELightgunMode ToMode(uint8 Mode)
	{
		switch (Mode)
		{
		case LightgunDeviceInfoReport::ModeMouse:
		case LightgunDeviceInfoReport::ModeBluetoothMouse:
			return ELightgunMode::Mouse;
		case LightgunDeviceInfoReport::ModeGamepad:
		case LightgunDeviceInfoReport::ModeBluetoothGamepad:
			return ELightgunMode::Gamepad;
		default:
			return ELightgunMode::Unknown;
		}
	}

	ELightgunConnection ToConnection(ELightgunTransport Transport)
	{
		switch (Transport)
		{
		case ELightgunTransport::Usb: return ELightgunConnection::Usb;
		case ELightgunTransport::Bluetooth: return ELightgunConnection::Bluetooth;
		default: return ELightgunConnection::Unknown;
		}
	}
}

FLightgunInfo ULightgunLibrary::GetLightgunInfo(int32 PlayerIndex)
{
	FLightgunInfo Result;

	const FLightgunInputDevice* const Device = LightgunLibrary::GetDevice();
	const FLightgunDeviceId* const Id = Device ? Device->FindDeviceId(PlayerIndex) : nullptr;
	if (!Id)
	{
		return Result;
	}

	const FLightgunDeviceInfo& Info = Id->Info;
	Result.bConnected = true;
	Result.PlayerIndex = Id->PlayerIndex;
	Result.bHasGunInput = Id->bHasGunInput;
	// Legacy firmware can't say, and is given feedback anyway: report what the plugin will actually do.
	Result.bFeedbackAvailable = !Info.bKnown || Info.bFeedbackAvailable;
	Result.bDetailsKnown = Info.bKnown;
	Result.FirmwareVersionNumber = static_cast<int32>(Info.FirmwareVersion);
	if (Info.bKnown)
	{
		Result.FirmwareVersion = FString::Printf(TEXT("%u.%u.%u"), Info.GetMajor(), Info.GetMinor(), Info.GetPatch());
	}
	Result.Board = LightgunLibrary::ToBoard(Info.Board);
	Result.Mode = LightgunLibrary::ToMode(Info.Mode);
	Result.Connection = LightgunLibrary::ToConnection(Id->GetEffectiveTransport());
	Result.PlayerNumberOnGun = Info.PlayerNumber;
	Result.ProductName = Id->ProductName;
	return Result;
}

FString ULightgunLibrary::GetPluginVersion()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("BlamconLightguns"));
	return Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString();
}
