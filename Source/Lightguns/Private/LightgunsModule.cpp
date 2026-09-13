// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunsModule.h"

#include "Features/IModularFeatures.h"
#include "LightgunInputDevice.h"
#include "LightgunKeys.h"

IMPLEMENT_MODULE(FLightgunsModule, Lightguns)

FLightgunsModule* FLightgunsModule::Get()
{
	return FModuleManager::GetModulePtr<FLightgunsModule>(TEXT("Lightguns"));
}

FLightgunInputDevice* FLightgunsModule::GetInputDevice() const
{
	// The application holds the strong reference for as long as the device exists.
	return InputDevice.Pin().Get();
}

void FLightgunsModule::BeginSession()
{
	if (SessionCount++ == 0)
	{
		if (FLightgunInputDevice* const Device = GetInputDevice())
		{
			Device->OnSessionStarted();
		}
	}
}

void FLightgunsModule::EndSession()
{
	if (SessionCount > 0 && --SessionCount == 0)
	{
		if (FLightgunInputDevice* const Device = GetInputDevice())
		{
			Device->OnSessionEnded();
		}
	}
}

void FLightgunsModule::StartupModule()
{
	// Registers this module as an input device provider.
	IInputDeviceModule::StartupModule();
	FLightgunKeys::Register();
}

void FLightgunsModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

TSharedPtr<IInputDevice> FLightgunsModule::CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	TSharedRef<FLightgunInputDevice> Device = MakeShared<FLightgunInputDevice>(InMessageHandler);
	InputDevice = Device;
	return Device;
}
