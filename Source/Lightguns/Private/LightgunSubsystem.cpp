// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunSubsystem.h"

#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "LightgunInputDevice.h"
#include "LightgunsModule.h"
#include "Misc/CoreMisc.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LightgunSubsystem)

bool ULightgunSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Guns are local hardware: a dedicated server has none.
	return !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void ULightgunSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (FLightgunsModule* const Module = FLightgunsModule::Get())
	{
		Module->BeginSession();
		WarningHandle = Module->OnWarning.AddUObject(this, &ULightgunSubsystem::HandleWarning);
	}
	ConnectionHandle = IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().AddUObject(this, &ULightgunSubsystem::HandleConnectionChange);
}

void ULightgunSubsystem::Deinitialize()
{
	IPlatformInputDeviceMapper::Get().GetOnInputDeviceConnectionChange().Remove(ConnectionHandle);
	if (FLightgunsModule* const Module = FLightgunsModule::Get())
	{
		Module->OnWarning.Remove(WarningHandle);
		Module->EndSession();
	}

	Super::Deinitialize();
}

void ULightgunSubsystem::HandleConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId InputDeviceId)
{
	const FLightgunsModule* const Module = FLightgunsModule::Get();
	const FLightgunInputDevice* const Device = Module ? Module->GetInputDevice() : nullptr;
	const int32 PlayerIndex = Device ? Device->GetPlayerIndex(InputDeviceId) : INDEX_NONE;
	if (PlayerIndex == INDEX_NONE)
	{
		return; // Not a lightgun.
	}

	if (NewState == EInputDeviceConnectionState::Connected)
	{
		OnLightgunConnected.Broadcast(PlayerIndex);
	}
	else if (NewState == EInputDeviceConnectionState::Disconnected)
	{
		OnLightgunDisconnected.Broadcast(PlayerIndex);
	}
}

void ULightgunSubsystem::HandleWarning(const FString& Message)
{
	OnLightgunWarning.Broadcast(Message);
}
