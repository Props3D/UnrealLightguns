// Copyright (c) 2026 Props3D. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "ILightgunFeedbackBackend.h"

/**
 * Blamcon lightguns over HID, via hidapi.
 *
 * Matches VID 0x3673, PID 0x0100-0x0103 (player index = PID - 0x0100), accepting gamepad (0x05) and
 * joystick (0x04) collections. Feedback needs one of those: Windows opens mouse and keyboard
 * collections exclusively, so a gun in mouse mode can never receive output reports. Enumerate reports
 * that case as a warning rather than failing silently.
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
