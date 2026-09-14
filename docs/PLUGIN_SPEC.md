# Blamcon Lightguns for Unreal — plugin specification

Draft 3, 2026-09-13. Target: UE 5.4+ on Windows, distributed on GitHub. Companion to the Unity package `com.blamcon.lightguns`.

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
| 1 | Windows opens mouse/keyboard HID collections **exclusively** for system use; game controllers (usage 0x04/0x05) are **shared**. | A gun can never receive HID output through its mouse or keyboard collection. Confirmed on hardware. **Feedback requires gamepad/joystick mode**, unless the gun's firmware adds a vendor-defined collection to mouse mode (§4.2, confirmed on hardware on Windows and macOS, over USB and Bluetooth). |
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
│   ├─ ILightgunFeedbackBackend  <-- THE SEAM. HID now; serial later for other brands
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
  other brands use). Also match the Blamcon vendor-defined collection (usage page `0xFF00`, usage
  `0x01`), which firmware with mouse-mode feedback adds next to the mouse and keyboard collections
  (§4.2).
* A gun exposes several collections, and Windows gives each its own path. Group them by PID: a player
  is usable if it has a controller collection (gamepad mode) or the vendor collection (mouse mode with
  feedback).
* **Mouse-mode detection is a required feature, not a nicety.** If the VID/PID is present only as
  mouse/keyboard collections, with no controller and no vendor collection (older firmware), log a
  specific, actionable warning:
  `"Blamcon lightgun found in mouse mode. Force feedback needs Gamepad mode or newer firmware — change
  the mode in Blamcon ARC."` Silent failure here will otherwise be the #1 support question. A gun in
  mouse mode that has the vendor collection gets no warning; the connect line says it is in mouse mode.
* Hot-plug: poll enumeration on a timer (~1 s, off the game thread). Broadcast
  `OnLightgunConnected` / `OnLightgunDisconnected` with the player index.

### 4.1 Device info and live state

The plugin learns about each gun from the gun itself, over HID and without serial commands: which
firmware and board it runs, how it is connected, and whether it can take force feedback. Two HID
feature reports carry this, read with `hid_get_feature_report()` on the already-open handle. It is
implemented on the firmware's `release-3.0` branch but not yet in a release; the firmware work is listed
at the end of this section.

This is the **minimal first version**. Every field is there because the plugin acts on it or it answers
a support question. Both reports are built to grow (see *Extending the reports*).

#### Common header

Both reports start with the same 5 bytes:

| Byte | Field | Value |
|---|---|---|
| 0 | Report ID | `0x50` or `0x51` |
| 1–2 | Signature | `0x42 0x4C` (`"BL"`) |
| 3 | Protocol version | `2`. Bumped only for an incompatible change to an existing field. `1` is skipped: it was the unsigned one-byte answer |
| 4 | Payload length | Number of bytes after this one |

**Why the signature:** the descriptor has declared feature report `0x50` for several releases, but until
`c5d859d` (#225, 2026-09-11) its handler was a TODO. It returned the requested length without writing
the buffer, so older firmware answers with stale bytes from TinyUSB's control buffer, which output
reports also use. A lone version byte of `1` is indistinguishable from leftover data. The one-byte form
(`BLAMCON_HID_PROTOCOL_VERSION = 1`) is already replaced on `release-3.0`, before it reaches a release.

#### Report `0x50`: device info

Fixed while the gun stays connected. Read once per connection. 13 bytes including the report ID
(descriptor `HID_REPORT_COUNT(12)`), payload length `8`.

| Byte | Field | Values |
|---|---|---|
| 5–8 | Firmware version | `uint32` little-endian, `major × 10000 + minor × 100 + patch`: 3.0.0 = `30000`, 3.1.12 = `30112` |
| 9 | Board | `0` unknown, `1` RP2040, `2` RP2350 |
| 10 | Mode | `0` mouse, `1` gamepad, `2` Bluetooth mouse, `3` Bluetooth gamepad (firmware `*_EMULATION_MODE`) |
| 11 | Feedback over HID output | `1` if this gun, in its current mode and on its current connection, processes output report `0x10`; otherwise `0` |
| 12 | Player number | `1`–`4`, from the gun's settings (`getPlayerId()`) |

The firmware works out byte 11 itself from its mode and build, so the host never guesses from version
numbers. In mouse mode the host reads both reports through the vendor-defined collection (§4.2), since
Windows opens the mouse and keyboard collections exclusively (constraint 1). Mouse-mode firmware
without that collection can't be read at all. With it, mode is `0` over USB and `2` over Bluetooth,
and byte 11 is `1` (seen on hardware on macOS and Windows, over USB and Bluetooth).

#### Report `0x51`: live state

Can change while connected. 6 bytes including the report ID (descriptor `HID_REPORT_COUNT(5)`),
payload length `1`.

| Byte | Field | Values |
|---|---|---|
| 5 | Host control | Bit 0 recoil, bit 1 rumble, bit 2 LED, bit 3 ammo: set while a host holds control of that component (`serialRecoil`, `serialRumble`, `serialLed`, `serialAmmo`). Bits 4–7 are 0 |

#### Plugin behaviour

Reads happen on the hot-plug thread straight after opening the gun, because a GET_REPORT blocks and
over Bluetooth can take tens of milliseconds. Never every frame. Both reports are read with a 13-byte
buffer, the size of `0x50`: Windows sizes feature reads to the collection's largest feature report. The
connection keeps the result (`ILightgunConnection::GetDeviceInfo`), and the hot-plug arrival event copies
it into `FLightgunDeviceId::Info`.

| Result | Plugin does |
|---|---|
| Valid `0x50` | Logs it in the connect line, e.g. `Lightgun connected: P1 firmware 3.0.0, RP2350, Bluetooth gamepad, feedback yes` |
| Feedback over HID output = `0` | Warns once ("this gun can't take force feedback in its current mode or connection; update firmware or use USB"), sends no feedback, input keeps working |
| Player number differs from the PID's player | Warns once with both values. The PID still decides the player index. This is the hardware check for the Bluetooth / Blamcon Buddy open question |
| Valid `0x51` with control bits set on connect | Logs which components another host holds. **Never releases them:** the plugin only releases control it took itself in the running process, and can't tell another program's control (a game, Blamcon ARC, a second Unreal process) from a crashed one's. After a crash the user reconnects the gun. A game session still takes its own components as usual |
| Unknown board or mode value | Logs the number, treats the field as unknown, carries on |
| Request fails, answer too short, or no signature | **Legacy firmware:** assumes feedback works over USB (the 3.0 baseline). Over Bluetooth it warns once that feedback may need newer firmware, and still sends it |

Games get this later through a `GetLightgunInfo(PlayerIndex)` node (firmware version, board, mode,
feedback availability). That node is not part of milestone 2.

#### Build identification during enumeration

Separately from the reports, the firmware version also goes in the device version, which hidapi returns
from enumeration (`hid_device_info::release_number`) with no extra I/O, before the gun is opened. It is
only logged, never used to decide behaviour.

* USB: `bcdDevice`, via `TinyUSBDevice.setDeviceVersion()`. Set on `release-3.0`; released builds
  report TinyUSB's default `0x0100`.
* Bluetooth Classic: the version field of the Device ID SDP record (`device_id_create_sdp_record` in
  `easybt.cpp`). Set on `release-3.0`; released builds send `1`. hidapi on macOS returns this field
  (`0x0001` seen on hardware with a pre-change build). **Windows doesn't pass it through:** with
  `release-3.0` over Bluetooth, hidapi on Windows 11 returned `release_number` `0x0000` and an empty
  product string. Over Bluetooth on Windows, only report `0x50` identifies the build.
* Encoding is USB BCD `0xJJMN` (`0x0301` = 3.0.1), so minor and patch are one digit each; report `0x50`
  carries the full version.
* BLE is not a supported transport and is not planned.

#### Extending the reports

* Fields are only appended, and the payload length grows with them. A host reads the fields it knows
  and ignores the rest, so older plugins keep working with newer firmware.
* A field is never removed, reordered or given a new meaning without bumping the protocol version.
* Unused bits in bit fields are sent as 0 and ignored by hosts.
* Keep each report under 32 bytes, comfortable for USB control transfers and Bluetooth.
* Firmware constraint: the GET_REPORT handler can run in an interrupt, so it only reads cached values and
  settings, never calls into feedback components. Every field above is already a stored value.

#### Future additions (not in the first version)

| Candidate | Report | Why |
|---|---|---|
| Recoil on/off timing of the active preset | `0x50` | Pace Play Recoil to the gun's real cycle instead of dropping pulses |
| Recoil toggle switch state | `0x51` | Explains "Play Recoil does nothing" when recoil is switched off on the gun |
| Rumble mode | `0x51` | Same for rumble disabled in settings |
| Aspect ratio setting | `0x51` | Aim maps differently for 4:3 and widescreen; a game can warn on a mismatch |
| Model type, selected calibration profile | `0x50` / `0x51` | Support and diagnostics |

#### Firmware work (in `blamcon-lightguns`, not this repo)

Status (2026-09-14): items 1–3 and 5 are done on `release-3.0`, not yet in a release. Item 4 holds as
long as that branch ships as a whole.

1. Replace the one-byte `0x50` answer with the device info report, and change its descriptor entry to
   `HID_REPORT_COUNT(12)` in `TUD_HID_REPORT_DESC_ABS_GAMEPADV2`.
2. Add feature report `0x51` (live state) to the same descriptor with `HID_REPORT_COUNT(5)`, and answer
   it in `handleGetReport`.
3. Call `TinyUSBDevice.setDeviceVersion()` with the BCD version, and pass the same version to the
   Bluetooth Device ID record.
4. Ship 1 and 2 together, before any release includes the one-byte form.
5. Mouse mode: add the vendor-defined collection to the USB and Bluetooth Classic mouse descriptors,
   and register the report callbacks for the Bluetooth mouse (§4.2). Release notes must tell Bluetooth
   users to re-pair.

### 4.2 Mouse mode: vendor-defined collection

In mouse mode the gun's descriptor has a mouse and a keyboard collection, which Windows opens
exclusively (constraint 1). Firmware on `release-3.0` (not yet in a release) adds a third top-level
collection to the mouse-mode descriptor, over USB and Bluetooth Classic. `-DMOUSE_VENDOR_COLLECTION=0`
builds the old descriptor. The host opens that collection for feedback and device
info, while the OS keeps using the gun as a mouse.

| Collection | Usage page / usage | Reports |
|---|---|---|
| Mouse | `0x01` / `0x02` | Input. Report ID 1, 3, 4 or 5 over USB (by player), 1 over Bluetooth |
| Keyboard | `0x01` / `0x06` | Input, and output (keyboard LEDs). Mouse report ID + 1 |
| Vendor (new) | `0xFF00` / `0x01` | Output `0x10` (40 bytes), feature `0x50` (13 bytes) and `0x51` (6 bytes), sizes including the ID |

* **Layouts are unchanged:** report `0x10` is the same 40-byte report as in gamepad mode, and
  `0x50`/`0x51` follow §4.1.
* **Report IDs can't clash:** the mouse and keyboard use IDs 1–6.
* **The vendor collection comes last,** so Windows keeps the mouse as `Col01` and the keyboard as
  `Col02`, and the vendor collection becomes `Col03`.
* **Size:** the mouse-mode descriptor grows from 147 to 186 bytes, well inside TinyUSB's limits and
  BTstack's 1300-byte SDP record buffer.

**Status**

| | USB | Bluetooth Classic |
|---|---|---|
| Windows | Tested on hardware: hidapi and Unity | Tested on hardware: hidapi and Unity |
| macOS | Tested on hardware | Tested on hardware |
| Linux | Not tested | Not tested |

**Why it works on Windows**
* HIDClass creates a separate device for each top-level collection
  ([Top-Level Collections](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/top-level-collections)).
* Only system usages such as mouse, keyboard, pen and touch are listed as opened exclusively
  ([HID clients supported in Windows](https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/hid-architecture)).
  Vendor-defined pages aren't listed, so nothing in Windows claims them.
* Windows accepts only report IDs that belong to the opened collection, which is why `0x10`, `0x50` and
  `0x51` are declared inside the vendor collection.
* Bluetooth HID devices go through the same class driver, so the split is the same over Bluetooth.

**Tested on Windows 11** (2026-09-14, RP2350 gun on `release-3.0`, hidapi 0.14 via the Python probe).
* **USB:**
  * The gun enumerated as three collections on interface 2: `MI_02&Col01` mouse, `Col02` keyboard,
    `Col03` vendor.
  * `Col03` opened, with no "Access denied".
  * `0x50` decoded as firmware 3.0.0, RP2350, mode `0`, feedback `1`, player 1. `0x51` decoded too.
  * Every `hid_write` of `0x10` returned 40, and the `0x51` control bits followed take and release for
    recoil and the LED.
  * Trigger clicks kept reaching Windows while the collection was held open.
* **Bluetooth:**
  * The same three collections appeared on the Bluetooth HID path,
    `HID#{00001124-0000-1000-8000-00805f9b34fb}_VID&00013673_PID&0100&Col01`–`Col03`, with
    `interface_number` `-1`.
  * hidapi still reported VID `0x3673` and PID `0x0100`, so grouping by PID works over Bluetooth.
  * `Col03` opened, and `0x50` decoded as mode `2` (Bluetooth mouse), feedback `1`.
  * Every `hid_write` of `0x10` returned 40, with the `0x51` control bits following.
  * Trigger clicks kept reaching Windows while the collection was held open.
  * `release_number` was `0x0000` and the product string was empty, so the plugin must not rely on
    either over Bluetooth.
* **Unity** also worked over USB and Bluetooth (see *Unity package* below).

**Tested on macOS** (2026-09-13, RP2350 gun, USB and Bluetooth, hidapi 0.14). This was the prototype
build, which has the same descriptor as `release-3.0`.
* The vendor collection opens.
* `0x50` and `0x51` decode correctly.
* Report `0x10` takes recoil control, fires one pulse and releases control, and does the same for the
  LED. Both effects were seen on the gun, and the `0x51` control bits followed each step.
* Mouse clicks from the gun kept reaching the OS while the collection was held open.
* Not tested: rumble, ammo, and cursor aiming (no IR markers).

**Plugin behaviour**
* **Device matching.** `LightgunDeviceMatch` gets a collection kind for the Blamcon vendor collection,
  and `FPlayerScan` records its index.
  * `IsUsable()` is true for a controller collection or the vendor collection.
  * `IsMouseModeOnly()` means mouse/keyboard collections with neither, and is what triggers the §4
    warning.
* **Backend.** For a mouse-mode gun, `FBlamconHidBackend` opens the vendor collection's path.
  * It uses that path for `hid_write` of `0x10` and for the §4.1 reads.
  * It never reads input from it; the collection has no input reports.
* **Input comes from the OS mouse,** through the mouse mapping in §5 (mouse parity).
  * Unreal can't tell mice apart, so all guns in mouse mode drive one cursor, and aim is effectively
    single-player.
  * Feedback can still target each gun by its PID.
  * The gun is still registered with the engine's device mapper, so connect and disconnect events fire,
    but it sends no input events and doesn't count as an attached gamepad. The Lightgun Mouse Aim trigger
    keeps mouse aim on for its player.
* **Connect line.** A vendor-capable gun in mouse mode gets no warning; the connect line shows the mode,
  e.g. `P1 firmware 3.1.0, RP2350, mouse, feedback yes`.
* **Warning for mouse mode without the collection.** Over Bluetooth it also says to remove and re-pair
  the gun, because a gun paired before the firmware update looks exactly like older firmware.

**Caveats**
* **Bluetooth pairing.** Hosts cache the HID descriptor when the gun pairs. After a firmware update that
  adds the collection, the gun must be removed and paired again. This was needed on macOS. On Windows
  every test was run after removing and re-pairing the gun, so whether Windows would keep the old
  descriptor without it wasn't checked. Release notes should tell users to re-pair.
* **macOS (deferred platform).**
  * macOS makes one HID device per USB interface or Bluetooth link, so the vendor collection shares a
    device with the keyboard collection.
  * Opening it therefore needs **Input Monitoring**. Without it, `hid_open_path` fails with
    `kIOReturnNotPermitted`. Confirmed on hardware.
  * Gamepad mode opened without the permission.
  * hidapi seizes devices on macOS by default, which takes the gun's mouse input away from the OS, so
    the backend must call `hid_darwin_set_open_exclusive(0)`.
  * For a game, the Input Monitoring prompt says the game wants to monitor keyboard input, which is a
    poor experience for mouse mode.
  * Putting the vendor collection on its own USB HID interface might avoid the prompt over USB, but not
    over Bluetooth (§11).
* **Linux (deferred platform).** Not tested. hidraw works per interface and doesn't stop the kernel's
  input handling; permissions come from the udev rule in §8.

**Unity package (companion).**
* **Works on Windows over USB and Bluetooth (tested on hardware, 2026-09-14).** The gun was an RP2350 on
  `release-3.0`, in USB mouse mode and then Bluetooth mouse mode (re-paired). The details below are from
  the USB run; Bluetooth gave the same results.
  * Unity's native backend reports the vendor collection as its own HID device: usage page 65280, usage
    1, `outputReportSize` 40, `featureReportSize` 13, `inputReportSize` 0, device version 768 (3.0.0).
  * A test layout matching usage page `0xFF00`, usage `0x01` and VID `0x3673` picked it up. A layout
    with its own matcher bypasses `HIDSupport.supportedHIDUsages`.
  * The package's existing `BlamconHIDOutputReport` sent as `HIDO` returned `1` for every command. Recoil
    (take control, fire, release) and LED (take control, flash, release) both worked on the gun.
* **Package change needed:** a feedback-only device class with that matcher, and the existing `0x10`
  commands sent to it when the gun has no gamepad device. Aim and fire come through Unity's normal Mouse.
* **Stale devices after a firmware update.** Unity kept devices from the gun's previous firmware
  listed after a reflash, until the Editor restarted. `HIDO` to those returns `-1`. The package should
  send to the most recently added matching device, not the first in the list.
* **Unity's parsed elements** list every byte of `0x10` and `0x50`/`0x51` with `reportOffsetInBits` 8.
  This looks harmless because the package sends raw reports, but don't build a layout from those
  elements.
* **Not tested:** rumble and ammo through `0x10`.
* **No feature reports.** Unity's Input System has no command to read them, so `0x50`/`0x51` would need
  native code.
* **macOS is unlikely,** because the shared device reports the mouse as its main usage.
* **Linux isn't possible:** Unity doesn't support HID devices other than gamepads and joysticks there.

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
and `ULightgunMouseAimTrigger` (ticks every frame; silent while that player has a lightgun connected in
gamepad mode, so a mouse on the cabinet can't fight the gun — when two mappings drive one action, the
larger value wins). A gun in mouse mode (§4.2) sends no gun input, so mouse aim stays on for its player.
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
| Permission | none for game controllers or vendor-defined collections | **Input Monitoring** (TCC) for guns in mouse mode, whose vendor collection shares a device with the keyboard (§4.2); gamepad mode opened without it on hardware. Check `IOHIDCheckAccess`, prompt via `IOHIDRequestAccess` | udev rule shipped with plugin |
| Known issue | HidHide users must whitelist the app | `IOHIDDeviceSetReport` hang reports → watchdog. hidapi seizes devices by default: call `hid_darwin_set_open_exclusive(0)` | `uaccess` rule must sort before `73-seat-late.rules` |

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
   makes other brands viable. For Blamcon guns it only matters for firmware without the mouse-mode
   vendor collection: §4.2 works on Windows over USB and Bluetooth, so mouse-mode feedback goes over HID,
   and this milestone is about other brands.

### Future milestones (deferred)

* **macOS**, incl. Input Monitoring (TCC) prompt handling for mouse-mode guns, non-exclusive opening,
  and a hardware check of the watchdog against `IOHIDDeviceSetReport` hangs.
* **Linux**, incl. udev rule, packaging notes, and confirming UE's Linux toolchain provides `libudev.h`.

## 10. Acceptance tests

* Recoil, rumble, LED and ammo each fire on hardware, over USB **and** Bluetooth.
* Two guns: correct player index, feedback goes to the right gun, aim is independent.
* Aim reaches the screen edges (0 and 1 on both axes) and has no deadzone near centre.
* Gun in mouse mode on firmware without the vendor collection → clear actionable log line, no crash,
  no silent failure.
* Gun in mouse mode with the vendor collection (§4.2), over USB **and** Bluetooth → no warning; the
  connect line shows mouse mode; recoil, rumble, LED and ammo fire; the gun still aims and fires as the
  system mouse while the plugin holds the collection open.
* Unplug mid-session → disconnect event, no hang, no leaked thread; replug re-acquires.
* PIE stop and app exit both release feedback control (verify recoil returns to firing on trigger).
* Mouse-only play works end to end with the shipped mapping context.
* Device info and live state (once the section 4.1 firmware changes exist): the connect log shows
  firmware version, board, mode and feedback availability; a gun reporting feedback unavailable gets a
  warning and no feedback while input keeps working; a gun already under another host's control on
  connect is logged and not released; a gun whose `0x50` answer has no signature is treated as Legacy and
  still gets USB feedback.

## 11. Open questions

Decided (2026-09-13):

* **Plugin distribution: GitHub**, like the Unity package. Source-only; testers build the plugin in
  their own project. No Fab listing for now.
* **UE version floor: 5.4.** It has `UInputDeviceSubsystem` and device properties, and avoids the 5.6
  RawInput regression.
* **Firmware info without serial commands:** signed HID feature reports for device info (`0x50`) and
  live state (`0x51`), plus the device version at enumeration. Minimal first version in section 4.1;
  the firmware changes listed there are on `release-3.0`, not yet in a release.
* **Mouse-mode feedback over HID** (2026-09-14): the vendor-defined collection (§4.2) works on Windows
  over USB and Bluetooth with both hidapi and Unity. So mouse-mode Blamcon guns get feedback
  over HID, the §4 warning is only for older firmware, and milestone 3 is about other brands. To
  re-check a gun, use `tools/hidprobe` in `blamcon-lightguns`.

Still open:

* **Emulators and lightgun front ends on Windows with the extra collection.** They should ignore a
  vendor-defined device, since Raw Input and DirectInput only pick up mice and game controllers, but
  that isn't checked.
* **Vendor collection on its own USB interface** (deferred). It could avoid the macOS Input Monitoring
  prompt over USB, but not over Bluetooth. Only relevant for the macOS milestone.
* **hidapi source in the repo.** Currently fetched by `Scripts/fetch-hidapi.sh` (needs Git Bash).
  Committing it would remove an install step for testers.
* **Blamcon Buddy / multi-gun over Bluetooth** — does enumeration and player indexing still hold when
  guns arrive over BT rather than USB? Needs hardware confirmation.
* **Does UE's bundled SDL2 export `SDL_hid_*`?** If so, Linux/macOS could skip vendoring hidapi.
  Only relevant once the deferred platform milestones start. ~30 minutes to check.
* **Which firmware build is 3.0.0?** There is no `3.0.0` tag in `blamcon-lightguns`, so it is unclear
  whether shipped 3.0.0 includes `c5d859d` (the one-byte `0x50` answer) or the older TODO handler. The
  plugin's Legacy classification covers both, but the answer decides how urgent firmware item 4 is.
