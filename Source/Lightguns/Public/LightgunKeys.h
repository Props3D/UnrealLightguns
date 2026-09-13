// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "InputCoreTypes.h"

/**
 * Input keys for lightguns, under the "Lightgun" category in key pickers and Input Mapping Contexts.
 *
 * Aim is an absolute pointer, normalised 0..1 with Y = 0 at the bottom (Unreal's convention). Multiply by
 * the viewport size for screen space. There is no deadzone.
 */
struct LIGHTGUNS_API FLightgunKeys
{
	static const FKey Aim;
	static const FKey AimX;
	static const FKey AimY;
	static const FKey Trigger;
	static const FKey ButtonA;
	static const FKey ButtonB;
	static const FKey ButtonY;
	static const FKey Start;
	static const FKey Select;
	static const FKey DPadUp;
	static const FKey DPadDown;
	static const FKey DPadLeft;
	static const FKey DPadRight;

	/** Register the keys with EKeys. Called at module startup. */
	static void Register();
};
