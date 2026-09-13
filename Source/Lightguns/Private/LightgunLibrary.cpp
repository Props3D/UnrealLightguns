// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunLibrary.h"

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
