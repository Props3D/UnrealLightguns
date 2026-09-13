// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "LightgunSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLightgunConnectionChangedSignature, int32, PlayerIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLightgunWarningSignature, const FString&, Message);

/**
 * Marks a game session for the lightguns: while a game instance exists, guns are under the game's control
 * (recoil, rumble and LED); when it shuts down (PIE stop or exit) control goes back to the guns.
 *
 * Also relays lightgun connection changes, which the engine only exposes to C++, and actionable warnings
 * such as a gun left in mouse mode.
 */
UCLASS()
class LIGHTGUNS_API ULightgunSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** A gun connected. PlayerIndex is 0-based. Guns already connected at startup don't fire this: use Get Connected Lightguns. */
	UPROPERTY(BlueprintAssignable, Category = "Lightguns")
	FLightgunConnectionChangedSignature OnLightgunConnected;

	/** A gun was unplugged or stopped responding. PlayerIndex is 0-based. */
	UPROPERTY(BlueprintAssignable, Category = "Lightguns")
	FLightgunConnectionChangedSignature OnLightgunDisconnected;

	/** A gun is present but can't be used, e.g. it is in mouse mode. The message says how to fix it. */
	UPROPERTY(BlueprintAssignable, Category = "Lightguns")
	FLightgunWarningSignature OnLightgunWarning;

private:
	void HandleConnectionChange(EInputDeviceConnectionState NewState, FPlatformUserId UserId, FInputDeviceId InputDeviceId);
	void HandleWarning(const FString& Message);

	FDelegateHandle ConnectionHandle;
	FDelegateHandle WarningHandle;
};
