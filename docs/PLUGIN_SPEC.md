# Blamcon Lightguns for Unreal — plugin specification

Draft 1, 2026-09-12. Target: UE 5.4+. Companion to the Unity package `com.blamcon.lightguns`.

---

## 1. What this plugin is for

Give an Unreal developer the same thing the Unity package gives them: a lightgun that aims and
shoots out of the box, and force feedback (recoil, rumble, RGB, ammo display) behind a few calls —
without the developer learning anything about HID.

Secondary goal, which shapes the architecture: the same plugin should be able to drive **other**
lightgun brands later, and should not fall over for players using a mouse.

## 2. Constraints that drove the design (all verified)

| # | Constraint | Consequence |
|---|---|---|
| 1 | Windows opens mouse/keyboard HID collections **exclusively** for system use; game controllers (usage 0x04/0x05) are **shared**. | A gun in mouse mode can never receive HID output. **Feedback requires gamepad/joystick mode.** Confirmed on hardware. |
| 2 | `FWindowsApplication` supports **XInput only**; there is no DirectInput path. | A gamepad-mode lightgun produces **zero input** in UE on Windows. If we require gamepad mode for feedback (see 1), we must supply input too. |
| 3 | UE has **no API to write an arbitrary HID output report**. `IInputDevice` is an extension point; the `RawInput` plugin is Windows-only and read-only; the GameInput wrapper exposes only the input half. | Bundle **hidapi** and own the transport. |
| 4 | UE's `RawInput` plugin is Experimental and has a reported UE 5.6 regression where axes stop working. | Do not build the input path on it. |
| 5 | Firmware services **one output report at a time**. | Combine effects into one report; coalesce queued reports. |
| 6 | HID writes block (Windows overlapped-but-waits, macOS `IOHIDDeviceSetReport` can hang, Linux `write()` blocks). | Writes never happen on the game thread. |
| 7 | Blamcon's largest output report is 40 bytes, and the firmware accepts the single-component reports only at their exact declared size. | Use **report 0x10 only**, always 40 bytes. |
| 8 | Current 3.0 firmware handles output reports over USB **and Bluetooth Classic**. | No transport-specific code; hidapi covers both. |

## 3. Architecture

```
BlamconLightguns.uplugin
├─ ThirdParty/hidapi/            ModuleType.External (or hid.c compiled into LightgunCore)
├─ LightgunCore/                 [no UObjects; Win64 now, Mac + Linux later — §9]
│   ├─ FLightgunDeviceId         VID/PID/path/serial/player index
│   ├─ FLightgunReport           the 40-byte POD; 1:1 with BlamconHIDOutputReport.cs
│   ├─ ILightgunFeedbackBackend  <-- THE SEAM. HID now; serial later for mouse-mode guns
│   │    └─ FBlamconHidBackend   hidapi: enumerate, open, hid_write
│   ├─ FLightgunWriterThread     FRunnable + SPSC queue + coalescing + watchdog
│   └─ FLightgunHotplug          arrival/removal polling
└─ Lightguns/                    [engine-facing layer; IInputDeviceModule]
    ├─ FLightgunInputDevice : IInputDevice     owns the guns: input, feedback, control, hot-plug events
    ├─ FLightgunKeys             Lightgun_Aim (2D), Trigger, A, B, Y, Start, Select, D-pad
    ├─ ULightgunSubsystem : UGameInstanceSubsystem   session start/end; relays connect + warning events
    └─ ULightgunLibrary : UBlueprintFunctionLibrary  recoil, ammo, LED, take/release control
```

**Let the engine own what it already owns.** The engine creates, ticks and destroys the input device,
maps each gun to a player (`IPlatformInputDeviceMapper`), broadcasts connection changes, and routes
force feedback and light colour to it. The plugin adds only what Unreal has no equivalent for: the HID
transport, recoil, ammo, take/release control and mouse-mode detection. There is no separate manager
object, ticker or settings class.

**Naming:** ship as *Blamcon Lightguns for Unreal* to match the Unity package, but keep every
internal type generic (`FLightgunReport`, `ILightgunFeedbackBackend`). Adding GUN4IR/OpenFIRE later
must not mean renaming the public API.

## 4. Device discovery and modes

* Enumerate with `hid_enumerate()`. Match **VID 0x3673, PID 0x0100–0x0103**; player index =
  `PID - 0x0100`. Accept usage `0x05` (Blamcon gamepad mode) **and** `0x04` (joystick mode, which
  other brands use).
* **Mouse-mode detection is a required feature, not a nicety.** If the VID/PID is present but only
  as a mouse/keyboard collection, log a specific, actionable warning:
  `"Blamcon lightgun found in mouse mode. Force feedback requires Gamepad mode — change it in
  Blamcon ARC."` Silent failure here will otherwise be the #1 support question.
* Hot-plug: poll enumeration on a timer (~1 s, off the game thread). Broadcast
  `OnLightgunConnected` / `OnLightgunDisconnected` with the player index.

## 5. Input

Read input reports over the **same hidapi handle** used for writing. Non-blocking
(`hid_set_nonblocking(1)`) and drained from `IInputDevice::SendControllerEvents()`, so no reader
thread is needed — input reports are 22 bytes and arrive at ~1 kHz.

Wire format (firmware `GamepadReport`, 22 bytes):

| Offset | Field |
|---|---|
| 0 | report id (1 = P1, 3 = P2, 4 = P3, 5 = P4) |
| 1–4 | 32 button bits |
| 5 | hat, low nibble |
| 6–9 / 10–13 | X / Y, int32 LE, range 0–32767, **Y = 0 is top of screen** |
| 14–17 / 18–21 | Rx / Ry — always zero in current firmware; ignore |

Buttons: bit 0 = trigger (West/X), 1 = South/A, 2 = East/B, 3 = North/Y, 4 = LB, 5 = RB, 6 = LT,
7 = RT, 8 = Select, 9 = Start, 10 = L3, 11 = R3.

Hat: 0 = neutral, 1 = up, 2 = up/right … 8 = up/left. **Map cardinals only**, matching the Unity
package, where ignoring diagonals is deliberate.

Exposed as:

* `Lightgun_Aim` — 2D axis, **normalised 0..1**, Y flipped to UE's bottom-up convention. Multiply by
  viewport size for screen space. No deadzone, ever — this is an absolute pointer.
* `Lightgun_Trigger`, `Lightgun_ButtonA/B/Y`, `Lightgun_Start`, `Lightgun_Select`, `Lightgun_DPad*`.
* Per-device `FInputDeviceId` so two guns are distinct players.

**Mouse parity is a deliverable, not an afterthought.** Ship a sample Input Mapping Context that
binds one `Aim` action to both `Lightgun_Aim` and mouse position, and one `Fire` action to trigger
and left mouse. A developer should be able to build and test the whole game on a mouse and have
lightguns work on the cabinet without touching bindings.

Enhanced Input has no absolute mouse-position key (`Mouse2D` is movement, and only reports while the
mouse moves), so the plugin adds two small Enhanced Input classes for the mouse mapping:
`ULightgunMouseAimModifier` (value = cursor position in the viewport, normalised like `Lightgun_Aim`)
and `ULightgunMouseAimTrigger` (ticks every frame; silent while that player has a lightgun connected, so
a mouse on the cabinet can't fight the gun — when two mappings drive one action, the larger value wins).
Assets can only be written by the editor, so `Scripts/create_sample_input.py` generates
`IA_LightgunAim`, `IA_LightgunFire` and `IMC_Lightgun` in the plugin's `Content/Samples`.

## 6. Feedback

Public surface (Blueprint + C++), per device. `Target` is a 0-based player index; `-1` means every
connected gun.

```
PlayRecoil(Target, Pulses = 1)
PlayRecoilTimed(Target, Pulses, OnMs, OffMs)
PlayRumble(Target, Pulses = 1)      PlayRumbleTimed(...)
SetLedColor(Target, LinearColor)    FlashLed(Target, Color, Flashes[, LitMs, DarkMs])
SetAmmoCount(Target, Remaining)
TakeFeedbackControl(Target, Recoil, Rumble, Led, Ammo, StartingAmmo = 0)
ReleaseFeedbackControl(Target, ...)
IsLightgunConnected(Target)         GetConnectedLightguns()
```

`ULightgunSubsystem` broadcasts `OnLightgunConnected` / `OnLightgunDisconnected` (relayed from the
engine's device mapper, which Blueprint can't bind to) and `OnLightgunWarning` (e.g. mouse mode).

Rules baked into the implementation:

* **Take control on session start, release on teardown.** Until the app takes control the gun drives
  these itself (recoil on trigger pull). Release on `Deinitialize`, PIE stop, and application focus
  loss — a gun left under app control after a crash is a support call. A session is a live game
  instance; sessions are counted, so several PIE instances share guns safely. Sessions take recoil,
  rumble and LED; ammo is left to the game.
* Ammo is ignored by firmware until ammo control is taken, and taking it zeroes the display, so send
  the starting count in the same report (`TakeFeedbackControl`'s `StartingAmmo`). After a focus-loss
  release and retake, the display reads 0 until the game next sets the count.
* The gun ignores a new recoil while earlier pulses are still cycling: pace calls like a fire rate.
* Firmware clamps: rumble 100–2400 ms, LED 20–5000 ms, recoil on 15–200 / off 45–200 ms. Clamp in the
  plugin so Blueprint values behave predictably.
* Batch within a frame: if two calls land before the writer thread drains, merge them into one 0x10
  report. This is required, not an optimisation (constraint 5).
* Also engine-native routing, so existing content works:
  * `IInputDevice::SetChannelValues` → map UE force feedback channels onto rumble/recoil, so
    `UForceFeedbackEffect` assets and `ClientPlayForceFeedback` just work. Large motors pulse the
    rumble motor while on; small motors fire one recoil pulse each time they turn on.
  * `IInputDevice::SetLightColor` / `SetDeviceProperty` → `UColorInputDeviceProperty` drives the RGB.

## 7. Threading

* One `FRunnable` writer thread (shared, per-device queue), fed by `TQueue<FLightgunReport, Spsc>`.
* Game thread enqueues and returns. Never `Async(TaskGraph)` — that would park worker threads on
  blocking I/O.
* Coalesce: a pending report for the same device is merged rather than queued behind.
* Watchdog: if a write exceeds ~250 ms, mark the device suspect, stop queueing, and surface a
  disconnect event. Protects against the documented macOS `IOHIDDeviceSetReport` hangs.

## 8. Platforms

**Windows is the only target for milestones 1–3.** macOS and Linux are deferred to future
milestones (§9); their columns are kept as the design notes for that work. Code should stay portable
(hidapi, no Windows-only APIs outside the backend), but the plugin ships Win64-only until then.

| | Windows | macOS | Linux |
|---|---|---|---|
| Transport | hidapi (loads `hid.dll`/`cfgmgr32.dll`; **no import libs**) | hidapi + `IOKit`, `CoreFoundation` frameworks | hidapi hidraw + `libudev` |
| Permission | none for game controllers | **Input Monitoring** (TCC); check `IOHIDCheckAccess`, prompt via `IOHIDRequestAccess` | udev rule shipped with plugin |
| Known issue | HidHide users must whitelist the app | `IOHIDDeviceSetReport` hang reports → watchdog | `uaccess` rule must sort before `73-seat-late.rules` |

udev rule: `KERNEL=="hidraw*", ATTRS{idVendor}=="3673", ATTRS{idProduct}=="010[0-3]", TAG+="uaccess"`

**hidapi is tri-licensed — select BSD-3-Clause explicitly** in the plugin's LICENSE and `.uplugin`.

## 9. Milestones

1. **Transport.** `LightgunCore` + writer thread + a C++ smoke test that fires recoil on Windows.
   Proves the 40-byte report end to end. *Exit: recoil fires from a UE commandlet.*
2. **Input device and Blueprint surface** (formerly two milestones, merged so the engine's input
   device owns the guns instead of a parallel manager). `IInputDevice`, aim axis, buttons, per-device
   ids, engine force feedback and light colour, subsystem lifecycle, function library, take/release
   control, sample Input Mapping Context with mouse parity.
   *Exit: a designer can flash the LED from a Blueprint with no C++; the gun aims and fires in a blank
   project, and the mouse does too, with the same bindings.*
3. **Second backend** (serial) behind `ILightgunFeedbackBackend`, proving the seam — this is what
   makes other brands viable.

### Future milestones (deferred)

* **macOS**, incl. Input Monitoring (TCC) prompt handling and a hardware check of the watchdog against
  `IOHIDDeviceSetReport` hangs.
* **Linux**, incl. udev rule, packaging notes, and confirming UE's Linux toolchain provides `libudev.h`.

## 10. Acceptance tests

* Recoil, rumble, LED and ammo each fire on hardware, over USB **and** Bluetooth.
* Two guns: correct player index, feedback goes to the right gun, aim is independent.
* Aim reaches the screen edges (0 and 1 on both axes) and has no deadzone near centre.
* Gun in mouse mode → clear actionable log line, no crash, no silent failure.
* Unplug mid-session → disconnect event, no hang, no leaked thread; replug re-acquires.
* PIE stop and app exit both release feedback control (verify recoil returns to firing on trigger).
* Mouse-only play works end to end with the shipped mapping context.

## 11. Open questions

* **Plugin distribution** — Fab/Marketplace listing, or GitHub like the Unity package? Affects the
  hidapi licence statement and whether prebuilt binaries are acceptable.
* **UE version floor.** 5.4 gets `UInputDeviceSubsystem` and device properties; 5.6 has the RawInput
  regression we avoid anyway. Is 5.4 low enough for the cabinet builders?
* **Does UE's bundled SDL2 export `SDL_hid_*`?** If so, Linux/macOS could skip vendoring hidapi.
  (Only relevant once the deferred platform milestones start.)
  ~30 minutes to check; would simplify packaging.
* **Blamcon Buddy / multi-gun over Bluetooth** — does enumeration and player indexing still hold when
  guns arrive over BT rather than USB? Needs hardware confirmation.
* Should the plugin read the firmware version (feature report) to gate behaviour? Currently there is
  no way for a host to tell 3.0.0 from a current build, which matters for Bluetooth feedback.
