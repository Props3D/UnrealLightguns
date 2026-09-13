// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#include "LightgunKeys.h"

#define LOCTEXT_NAMESPACE "LightgunKeys"

const FKey FLightgunKeys::Aim(TEXT("Lightgun_Aim"));
const FKey FLightgunKeys::AimX(TEXT("Lightgun_AimX"));
const FKey FLightgunKeys::AimY(TEXT("Lightgun_AimY"));
const FKey FLightgunKeys::Trigger(TEXT("Lightgun_Trigger"));
const FKey FLightgunKeys::ButtonA(TEXT("Lightgun_ButtonA"));
const FKey FLightgunKeys::ButtonB(TEXT("Lightgun_ButtonB"));
const FKey FLightgunKeys::ButtonY(TEXT("Lightgun_ButtonY"));
const FKey FLightgunKeys::Start(TEXT("Lightgun_Start"));
const FKey FLightgunKeys::Select(TEXT("Lightgun_Select"));
const FKey FLightgunKeys::DPadUp(TEXT("Lightgun_DPadUp"));
const FKey FLightgunKeys::DPadDown(TEXT("Lightgun_DPadDown"));
const FKey FLightgunKeys::DPadLeft(TEXT("Lightgun_DPadLeft"));
const FKey FLightgunKeys::DPadRight(TEXT("Lightgun_DPadRight"));

void FLightgunKeys::Register()
{
	static const FName Category(TEXT("Lightgun"));
	EKeys::AddMenuCategoryDisplayInfo(Category, LOCTEXT("Category", "Lightgun"), TEXT("GraphEditor.PadEvent_16x"));

	const uint32 Button = FKeyDetails::GamepadKey;
	const uint32 Axis = FKeyDetails::GamepadKey | FKeyDetails::Axis1D;

	EKeys::AddKey(FKeyDetails(AimX, LOCTEXT("AimX", "Lightgun Aim X"), Axis, Category));
	EKeys::AddKey(FKeyDetails(AimY, LOCTEXT("AimY", "Lightgun Aim Y"), Axis, Category));
	EKeys::AddPairedKey(FKeyDetails(Aim, LOCTEXT("Aim", "Lightgun Aim"), FKeyDetails::GamepadKey | FKeyDetails::Axis2D, Category), AimX, AimY);

	EKeys::AddKey(FKeyDetails(Trigger, LOCTEXT("Trigger", "Lightgun Trigger"), Button, Category));
	EKeys::AddKey(FKeyDetails(ButtonA, LOCTEXT("ButtonA", "Lightgun A"), Button, Category));
	EKeys::AddKey(FKeyDetails(ButtonB, LOCTEXT("ButtonB", "Lightgun B"), Button, Category));
	EKeys::AddKey(FKeyDetails(ButtonY, LOCTEXT("ButtonY", "Lightgun Y"), Button, Category));
	EKeys::AddKey(FKeyDetails(Start, LOCTEXT("Start", "Lightgun Start"), Button, Category));
	EKeys::AddKey(FKeyDetails(Select, LOCTEXT("Select", "Lightgun Select"), Button, Category));
	EKeys::AddKey(FKeyDetails(DPadUp, LOCTEXT("DPadUp", "Lightgun D-pad Up"), Button, Category));
	EKeys::AddKey(FKeyDetails(DPadDown, LOCTEXT("DPadDown", "Lightgun D-pad Down"), Button, Category));
	EKeys::AddKey(FKeyDetails(DPadLeft, LOCTEXT("DPadLeft", "Lightgun D-pad Left"), Button, Category));
	EKeys::AddKey(FKeyDetails(DPadRight, LOCTEXT("DPadRight", "Lightgun D-pad Right"), Button, Category));
}

#undef LOCTEXT_NAMESPACE
