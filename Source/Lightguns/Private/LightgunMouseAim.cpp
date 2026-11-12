// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunMouseAim.h"

#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "LightgunInputDevice.h"
#include "LightgunsModule.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LightgunMouseAim)

FInputActionValue ULightgunMouseAimModifier::ModifyRaw_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue CurrentValue, float DeltaTime)
{
	const APlayerController* const PlayerController = PlayerInput ? PlayerInput->GetOuterAPlayerController() : nullptr;

	double MouseX = 0.0;
	double MouseY = 0.0;
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	if (PlayerController && PlayerController->GetMousePosition(MouseX, MouseY))
	{
		PlayerController->GetViewportSize(ViewportWidth, ViewportHeight);
		if (ViewportWidth > 0 && ViewportHeight > 0)
		{
			// Viewport pixels have Y = 0 at the top; Lightgun Aim has Y = 0 at the bottom.
			LastPosition.X = FMath::Clamp(MouseX / ViewportWidth, 0.0, 1.0);
			LastPosition.Y = FMath::Clamp(1.0 - MouseY / ViewportHeight, 0.0, 1.0);
		}
	}

	return FInputActionValue(EInputActionValueType::Axis2D, LastPosition);
}

ULightgunMouseAimTrigger::ULightgunMouseAimTrigger()
{
	// Evaluate every frame, not only when the mouse moves.
	bShouldAlwaysTick = true;
}

ETriggerState ULightgunMouseAimTrigger::UpdateState_Implementation(const UEnhancedPlayerInput* PlayerInput, FInputActionValue ModifiedValue, float DeltaTime)
{
	const APlayerController* const PlayerController = PlayerInput ? PlayerInput->GetOuterAPlayerController() : nullptr;
	const ULocalPlayer* const LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (!LocalPlayer)
	{
		return ETriggerState::None;
	}

	// Lightguns are mapped to players by controller id (see FLightgunInputDevice). A gun in mouse mode is the
	// mouse, so only a gun that sends its own aim turns mouse aim off.
	const FLightgunsModule* const Module = FLightgunsModule::Get();
	const FLightgunInputDevice* const Device = Module ? Module->GetInputDevice() : nullptr;
	const bool bGunAims = Device && Device->HasGunInput(LocalPlayer->GetControllerId());

	return bGunAims ? ETriggerState::None : ETriggerState::Triggered;
}
