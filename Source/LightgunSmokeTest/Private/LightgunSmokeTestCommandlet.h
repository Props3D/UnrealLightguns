// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "Commandlets/Commandlet.h"

#include "LightgunSmokeTestCommandlet.generated.h"

/**
 * Milestone 1 exit test: proves the 40-byte report reaches a gun end to end.
 *
 * Opens a Blamcon lightgun, takes recoil control and fires recoil in one report, then releases control
 * through the writer thread. Run with the gun in Gamepad mode:
 *
 *   UnrealEditor-Cmd <Project>.uproject -run=LightgunSmokeTest [-player=1] [-pulses=1]
 *
 * -player is 1-based (1-4). Exit code 0 means the reports were written; confirm the recoil by feel,
 * and that the trigger fires recoil on its own again afterwards (control was released).
 */
UCLASS()
class ULightgunSmokeTestCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	ULightgunSmokeTestCommandlet();

	virtual int32 Main(const FString& Params) override;
};
