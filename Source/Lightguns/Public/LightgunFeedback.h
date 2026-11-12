// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

#include "LightgunFeedback.generated.h"

/**
 * One gun's feedback, as data: what the recoil, rumble, LED and ammo display should do.
 *
 * Play Lightgun Feedback sends the whole struct as a single report, so everything in it happens together.
 * Separate calls in one frame are usually merged into one report too, but only if the writer hasn't sent
 * the earlier one yet, which makes the result depend on timing. A struct removes that: a colour and a
 * flash can't race, because only one LED setting exists to send.
 *
 * Each component has a switch. A component whose switch is off is left alone, which is different from
 * setting it to zero: Recoil off means "don't touch the solenoid", while Recoil on with 0 pulses stops it.
 *
 * Store one per weapon to keep its feel in one place, and play it when the weapon fires.
 */
USTRUCT(BlueprintType)
struct LIGHTGUNS_API FLightgunFeedback
{
	GENERATED_BODY()

	/** Drive the recoil solenoid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil")
	bool bRecoil = false;

	/** Number of pulses. 0 stops recoil that is still cycling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (EditCondition = "bRecoil", ClampMin = "0", ClampMax = "255"))
	int32 RecoilPulses = 1;

	/** Milliseconds the solenoid is driven, 15-200. 0 uses the gun's own setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (EditCondition = "bRecoil", ClampMin = "0", ClampMax = "200"))
	int32 RecoilOnMs = 0;

	/** Milliseconds between pulses, 45-200. 0 uses the gun's own setting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recoil", meta = (EditCondition = "bRecoil", ClampMin = "0", ClampMax = "200"))
	int32 RecoilOffMs = 0;

	/** Drive the rumble motor. Its strength is a setting on the gun, not something a game can change. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble")
	bool bRumble = false;

	/** Number of pulses. 0 stops rumble that is still running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble", meta = (EditCondition = "bRumble", ClampMin = "0", ClampMax = "255"))
	int32 RumblePulses = 1;

	/** Milliseconds the motor runs per pulse, 100-2400. 0 uses the firmware default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble", meta = (EditCondition = "bRumble", ClampMin = "0", ClampMax = "2400"))
	int32 RumbleOnMs = 0;

	/** Milliseconds between pulses, 100-2400. 0 uses the firmware default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rumble", meta = (EditCondition = "bRumble", ClampMin = "0", ClampMax = "2400"))
	int32 RumbleOffMs = 0;

	/** Drive the RGB LED. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LED")
	bool bLed = false;

	/** Black turns the LED off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LED", meta = (EditCondition = "bLed"))
	FLinearColor LedColor = FLinearColor::Red;

	/** 0 holds the colour until something changes it; 1 or more flashes that many times, ending dark. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LED", meta = (EditCondition = "bLed", ClampMin = "0", ClampMax = "255"))
	int32 LedFlashes = 0;

	/** Milliseconds lit per flash, 20-5000. 0 uses the firmware default. Ignored when not flashing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LED", meta = (EditCondition = "bLed", ClampMin = "0", ClampMax = "5000"))
	int32 LedLitMs = 0;

	/** Milliseconds dark between flashes, 20-5000. 0 uses the firmware default. Ignored when not flashing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LED", meta = (EditCondition = "bLed", ClampMin = "0", ClampMax = "5000"))
	int32 LedDarkMs = 0;

	/** Set the ammo display. The gun ignores this until the game has taken ammo control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo")
	bool bAmmo = false;

	/** Rounds left to show. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ammo", meta = (EditCondition = "bAmmo", ClampMin = "0", ClampMax = "255"))
	int32 AmmoRemaining = 0;

	/** True when no component is switched on, so there is nothing to send. */
	bool IsEmpty() const { return !bRecoil && !bRumble && !bLed && !bAmmo; }
};
