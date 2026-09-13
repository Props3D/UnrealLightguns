// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/IInputInterface.h"
#include "IInputDevice.h"
#include "LightgunDeviceId.h"
#include "LightgunInputReport.h"
#include "LightgunWriterThread.h"

class FLightgunHotplug;
class FLightgunReport;
class ILightgunConnection;

/** Feedback components the game can take control of. */
enum class ELightgunControl : uint8
{
	None = 0,
	Recoil = 1 << 0,
	Rumble = 1 << 1,
	Led = 1 << 2,
	Ammo = 1 << 3,
	/** What a game session takes automatically. Ammo is left to the game, since taking it clears the display. */
	Session = Recoil | Rumble | Led,
};
ENUM_CLASS_FLAGS(ELightgunControl);

/**
 * The engine's view of every connected lightgun. Created and ticked by the platform application like any
 * other input device.
 *
 * - Input: aim and buttons become FLightgunKeys events, each gun with its own FInputDeviceId mapped to the
 *   platform user for its player index, so two guns are two players.
 * - Feedback: engine force feedback drives rumble (large motors) and recoil (small motors), the engine's
 *   light colour drives the LED, and ULightgunLibrary covers the rest.
 * - Control: takes control of each gun while a game session is active and the application has focus,
 *   and releases it otherwise.
 *
 * Game thread only.
 */
class FLightgunInputDevice : public IInputDevice
{
public:
	/** Player index meaning "every connected gun". */
	static constexpr int32 AllPlayers = INDEX_NONE;

	explicit FLightgunInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
	virtual ~FLightgunInputDevice() override;

	/** Send a report to one gun, or every gun with AllPlayers. Returns true if any gun was sent it. */
	bool SendFeedback(int32 PlayerIndex, const FLightgunReport& Report);

	/** Take or release control of components. StartingAmmo is shown in the same report when taking ammo control. */
	bool TakeControl(int32 PlayerIndex, ELightgunControl Components, int32 StartingAmmo = 0);
	bool ReleaseControl(int32 PlayerIndex, ELightgunControl Components);

	bool IsConnected(int32 PlayerIndex) const;
	TArray<int32> GetConnectedPlayers() const;

	/** The player index a lightgun input device id belongs to, connected or not, or INDEX_NONE. */
	int32 GetPlayerIndex(FInputDeviceId InputDeviceId) const;

	void OnSessionStarted();
	void OnSessionEnded();

	//~ Begin IInputDevice
	virtual void Tick(float DeltaTime) override;
	virtual void SendControllerEvents() override;
	virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override { return false; }
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values) override;
	virtual void SetLightColor(int32 ControllerId, FColor Color) override;
	virtual void ResetLightColor(int32 ControllerId) override;
	virtual void SetDeviceProperty(int32 ControllerId, const FInputDeviceProperty* Property) override;
	virtual bool IsGamepadAttached() const override { return !Guns.IsEmpty(); }
	//~ End IInputDevice

private:
	struct FGun
	{
		FLightgunDeviceId Id;
		TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> Connection;
		FLightgunWriterHandle WriterHandle;
		FInputDeviceId InputDeviceId;
		FPlatformUserId UserId;

		FLightgunInputState Input;
		bool bHasInput = false;

		ELightgunControl HeldControl = ELightgunControl::None;
		/** Control given back while the application is inactive, to retake on return. */
		ELightgunControl SuspendedControl = ELightgunControl::None;

		FForceFeedbackValues ForceFeedback;
		bool bRecoilChannelOn = false;
		double NextRumbleTime = 0.0;
	};

	FGun* FindGun(int32 PlayerIndex);
	void AddGun(const FLightgunDeviceId& DeviceId, const TSharedRef<ILightgunConnection, ESPMode::ThreadSafe>& Connection);
	void RemoveGun(const FLightgunDeviceId& DeviceId, const FString& Reason);
	void SetControl(FGun& Gun, ELightgunControl Components, bool bTake, int32 StartingAmmo);
	void ApplyForceFeedback(FGun& Gun);
	void SendButtonEvents(const FGun& Gun, uint32 OldButtons, uint32 NewButtons);
	void HandleApplicationActivationChanged(bool bIsActive);
	bool ShouldHoldControl() const;

	TSharedRef<FGenericApplicationMessageHandler> MessageHandler;
	FLightgunWriterThread Writer;
	TUniquePtr<FLightgunHotplug> Hotplug;
	TArray<FGun> Guns;

	/** Stable per player, so a replugged gun is the same input device. */
	TMap<int32, FInputDeviceId> InputDeviceIds;

	bool bApplicationActive = true;
	FDelegateHandle ActivationHandle;
};
