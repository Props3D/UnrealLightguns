// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "ILightgunFeedbackBackend.h"

/**
 * Blamcon lightguns over HID, via hidapi.
 *
 * Matches VID 0x3673, PID 0x0100-0x0103 (player index = PID - 0x0100), and opens one collection per player:
 * - gamepad (0x05) or joystick (0x04): input and feedback;
 * - otherwise the Blamcon vendor-defined collection (0xFF00 / 0x01) that newer firmware adds in mouse
 *   mode: feedback only, while the OS uses the gun as a mouse.
 * Windows opens mouse and keyboard collections exclusively, so a gun in mouse mode with neither can't
 * receive output reports. Enumerate reports that case as a warning rather than failing silently.
 *
 * Open reads feature reports 0x50 and 0x51 (docs/PLUGIN_SPEC.md section 4.1).
 */
class LIGHTGUNCORE_API FBlamconHidBackend : public ILightgunFeedbackBackend
{
public:
	static const FName BackendName;

	FBlamconHidBackend();
	virtual ~FBlamconHidBackend() override;

	FBlamconHidBackend(const FBlamconHidBackend&) = delete;
	FBlamconHidBackend& operator=(const FBlamconHidBackend&) = delete;

	/** False if hidapi failed to initialise; Enumerate then finds nothing and Open fails. */
	bool IsAvailable() const { return bAvailable; }

	//~ Begin ILightgunFeedbackBackend
	virtual FName GetBackendName() const override { return BackendName; }
	virtual void Enumerate(FLightgunEnumeration& OutEnumeration) override;
	virtual TSharedPtr<ILightgunConnection, ESPMode::ThreadSafe> Open(const FLightgunDeviceId& Device, FString& OutError) override;
	//~ End ILightgunFeedbackBackend

private:
	bool bAvailable = false;
};
