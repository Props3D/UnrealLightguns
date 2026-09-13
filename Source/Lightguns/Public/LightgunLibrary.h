// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LightgunLibrary.generated.h"

/**
 * Lightgun feedback that Unreal has no built-in equivalent for.
 *
 * PlayerIndex is 0-based (player 1 is 0); -1 sends to every connected gun. Each function returns true if a
 * connected gun received it.
 *
 * While a game is running the plugin holds control of recoil, rumble and LED, so the gun no longer fires
 * recoil on its own trigger pull: call Play Recoil when your weapon fires. Engine force feedback (rumble)
 * and the Input Device Subsystem's light colour property also reach the gun.
 */
UCLASS()
class LIGHTGUNS_API ULightgunLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Fire the recoil solenoid with the gun's configured timing. The gun ignores a new recoil while earlier
	 * pulses are still cycling, so pace calls like a real fire rate.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool PlayRecoil(int32 PlayerIndex = 0, int32 Pulses = 1);

	/** Fire the recoil solenoid with explicit timing. On is clamped to 15-200 ms, off to 45-200 ms. */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool PlayRecoilTimed(int32 PlayerIndex = 0, int32 Pulses = 1, int32 OnMs = 40, int32 OffMs = 60);

	/** Pulse the rumble motor with the gun's default timing. 0 pulses stops it. */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool PlayRumble(int32 PlayerIndex = 0, int32 Pulses = 1);

	/** Pulse the rumble motor with explicit timing. Both periods are clamped to 100-2400 ms. */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool PlayRumbleTimed(int32 PlayerIndex = 0, int32 Pulses = 1, int32 OnMs = 200, int32 OffMs = 200);

	/** Set a steady LED colour. Black turns the LED off. */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool SetLedColor(int32 PlayerIndex, FLinearColor Color);

	/** Flash the LED. With Lit Ms and Dark Ms both 0 the gun's default timing is used; otherwise both are clamped to 20-5000 ms. */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool FlashLed(int32 PlayerIndex, FLinearColor Color, int32 Flashes = 3, int32 LitMs = 0, int32 DarkMs = 0);

	/** Show a count (0-255) on the ammo display. The gun ignores this until ammo control is taken: see Take Feedback Control. */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool SetAmmoCount(int32 PlayerIndex, int32 Remaining);

	/**
	 * Take components away from the gun's own logic. Recoil, rumble and LED are taken automatically while
	 * the game runs; take ammo yourself. Taking ammo control clears the display, so pass the count to show
	 * as Starting Ammo.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool TakeFeedbackControl(int32 PlayerIndex = 0, bool bRecoil = true, bool bRumble = true, bool bLed = true, bool bAmmo = false, int32 StartingAmmo = 0);

	/** Hand components back to the gun, e.g. so recoil fires on the trigger during a menu. */
	UFUNCTION(BlueprintCallable, Category = "Lightguns")
	static bool ReleaseFeedbackControl(int32 PlayerIndex = 0, bool bRecoil = true, bool bRumble = true, bool bLed = true, bool bAmmo = true);

	UFUNCTION(BlueprintPure, Category = "Lightguns")
	static bool IsLightgunConnected(int32 PlayerIndex = 0);

	/** 0-based player indices of the connected guns, in order. */
	UFUNCTION(BlueprintPure, Category = "Lightguns")
	static TArray<int32> GetConnectedLightguns();
};
