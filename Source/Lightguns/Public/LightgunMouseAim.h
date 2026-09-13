// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

#include "LightgunMouseAim.generated.h"

/**
 * Mouse parity for lightgun aim. Enhanced Input has no key for the absolute mouse position, only
 * movement (Mouse2D), so add both of these to a mouse mapping on the same action as Lightgun Aim:
 *
 *   IA_LightgunAim (Axis2D)  <-  Lightgun Aim
 *                            <-  Mouse XY 2D-Axis   [Modifier: Lightgun Mouse Aim] [Trigger: Lightgun Mouse Aim]
 *
 * The action then gets the same value from either device: the pointer position, 0..1, Y = 0 at the bottom.
 * Scripts/create_sample_input.py builds exactly this as IMC_Lightgun.
 */

/** Replaces the mapping's value with the cursor position in the player's viewport, normalised like Lightgun Aim. */
UCLASS(NotBlueprintable, meta = (DisplayName = "Lightgun Mouse Aim"))
class ULightgunMouseAimModifier : public UInputModifier
{
	GENERATED_BODY()

protected:
	virtual FInputActionValue ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime) override;

private:
	/** Kept while the cursor is outside the viewport. */
	FVector LastPosition = FVector(0.5, 0.5, 0.0);
};

/**
 * Keeps mouse aim current while the mouse is still (movement keys report nothing then), and hands aim to
 * the lightgun: the mapping stays silent while this player has a lightgun connected, so a mouse on the
 * cabinet can't fight the gun.
 */
UCLASS(NotBlueprintable, meta = (DisplayName = "Lightgun Mouse Aim"))
class ULightgunMouseAimTrigger : public UInputTrigger
{
	GENERATED_BODY()

public:
	ULightgunMouseAimTrigger();

protected:
	virtual ETriggerState UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime) override;
};
