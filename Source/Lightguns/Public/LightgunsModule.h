// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "IInputDeviceModule.h"

class FLightgunInputDevice;

/**
 * Registers the lightgun keys and gives the engine its lightgun input device. The engine creates the
 * device on the first frame and owns it; this module only keeps a weak reference for the Blueprint layer.
 */
class LIGHTGUNS_API FLightgunsModule : public IInputDeviceModule
{
public:
	/** The loaded module, or null during shutdown. */
	static FLightgunsModule* Get();

	/** The engine-owned input device, or null before the first frame and after shutdown. Game thread. */
	FLightgunInputDevice* GetInputDevice() const;

	/**
	 * A game session started or ended (see ULightgunSubsystem). While at least one session is active, guns
	 * are under the game's control; when the last one ends, control goes back to the guns.
	 */
	void BeginSession();
	void EndSession();
	bool IsSessionActive() const { return SessionCount > 0; }

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnWarning, const FString& /*Message*/);
	/** A gun is present but can't be used, e.g. it is in mouse mode. Already logged. */
	FOnWarning OnWarning;

	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	//~ Begin IInputDeviceModule
	virtual TSharedPtr<IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
	//~ End IInputDeviceModule

private:
	TWeakPtr<FLightgunInputDevice> InputDevice;
	int32 SessionCount = 0;
};
