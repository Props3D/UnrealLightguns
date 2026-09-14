# Contributing

The design, constraints and roadmap are in [docs/PLUGIN_SPEC.md](docs/PLUGIN_SPEC.md). Read it before
changing the report format, threading or control rules.

## Status

| Milestone | State |
|---|---|
| 1. Transport: `LightgunCore`, writer thread, smoke test | Builds on UE 5.6. Feedback works on hardware through the input device; smoke test commandlet not run yet |
| 2. Input device and Blueprint surface | Builds on UE 5.6. LED exit test passed on hardware; aim, other buttons, rumble, ammo, alt-tab and the sample generator not tested yet |
| Device info reports and mouse-mode feedback (spec 4.1, 4.2) | Builds on UE 5.6. Mouse-mode feedback and control handoff work on hardware; device info connect line not confirmed yet |
| 3. Serial backend (other brands) | Not started |
| Future: macOS, Linux | Deferred |

## Hardware results

Only what was run is listed; everything else is untested.

**2026-09-14:** Windows 11, Unreal Engine 5.6.0, Visual Studio 2022 (MSVC 14.38), AMD Ryzen 7 5800H with
integrated graphics, one RP2350 gun on firmware `release-3.0`, USB. Test project: First Person template (C++).

| Check | Gamepad mode | Mouse mode |
|---|---|---|
| Plugin builds and loads; Lightgun keys appear in Blueprint | Pass | Pass |
| Gun detected (`Lightgun connected` logged) | Pass | Pass |
| Flash Led from Blueprint | Pass | Pass (on Left Mouse Button) |
| Play Recoil from Blueprint | Pass | Not tested |
| Trigger input reaches Unreal | Pass (`Lightgun Trigger` key event) | Pass (as Left Mouse Button) |
| Recoil control taken during play: trigger doesn't fire recoil by itself | Pass | Pass |
| Control released when play stops: trigger fires recoil by itself | Pass | Pass |

Notes:
- **Build:** the first Windows build found a duplicate hidapi symbol (`LNK2005`), fixed in `41bda77`.
  Everything else compiled first time.
- **Editor crash, not the plugin:** Unreal 5.6's DirectX 12 renderer failed to start on this integrated AMD
  GPU (`GetClockCalibration ... E_FAIL`, driver 25.5.1). The editor ran with `-dx11`.
- **Aim:** not testable, no IR LEDs. The gun sends button reports without tracking (checked in `joy.cpl`).
- **Not tested:** the connect line's device info fields, alt-tab, other buttons, rumble, ammo, aim,
  unplug and replug, Bluetooth, two guns, the smoke test commandlet, the sample input script.

## Layout

```
BlamconLightguns.uplugin
Source/
  ThirdParty/hidapi/     LightgunHidApi external module (hidapi, BSD-3-Clause)
  LightgunCore/          transport: FLightgunReport, ILightgunFeedbackBackend, FBlamconHidBackend,
                         FLightgunWriterThread, FLightgunHotplug, device info reports. No UObjects.
  Lightguns/             engine-facing: FLightgunInputDevice (IInputDevice), FLightgunKeys,
                         ULightgunSubsystem, ULightgunLibrary, Lightgun Mouse Aim modifier and trigger
  LightgunSmokeTest/     editor-only commandlet that fires recoil (milestone 1 exit test)
Tests/host/              engine-free tests, plain C++11
Scripts/
  fetch-hidapi.sh        updates the committed hidapi sources in Source/ThirdParty/hidapi
  create_sample_input.py generates the sample Input Mapping Context in the editor
docs/reference/          C++ reference implementation the report builder was ported from
```

`LightgunReport.h`, `LightgunDeviceMatch.h`, `LightgunInputReport.h` and `LightgunDeviceInfoReport.h` are deliberately engine-free, so
the wire formats can be tested without Unreal. Keep engine headers out of them.

## Host tests

Engine-free tests for the report byte layout, merge rules, input report parsing, device info report parsing and mouse-mode detection.
Any C++11 compiler, no Unreal needed. From the repo root:

```bash
c++ -std=c++11 -Wall -Wextra -Werror -ISource/LightgunCore/Public Tests/host/HostTests.cpp Tests/host/ReportTests.cpp Tests/host/DeviceMatchTests.cpp Tests/host/InputReportTests.cpp Tests/host/DeviceInfoReportTests.cpp -o Tests/host/host_tests && Tests/host/host_tests
```

## Hardware check without Unreal

For the device info reports (`0x50`/`0x51`) and the mouse-mode vendor collection, use `tools/hidprobe` in
the firmware repo (`blamcon-lightguns`); its README has Windows and macOS steps.

`EnumerateLightguns` is a smaller check: it lists Blamcon HID collections, reports guns in mouse mode, and
with `--recoil` fires one recoil pulse on player 1. Needs hidapi; macOS with Homebrew hidapi shown:

```bash
c++ -std=c++11 -ISource/LightgunCore/Public -I/opt/homebrew/include/hidapi Tests/host/EnumerateLightguns.cpp -L/opt/homebrew/lib -lhidapi -o Tests/host/enumerate_lightguns && Tests/host/enumerate_lightguns
```

## In-engine smoke test

With the plugin in a project and the gun in Gamepad mode:

```bash
UnrealEditor-Cmd MyProject.uproject -run=LightgunSmokeTest -player=1 -pulses=1
```

It takes recoil control, fires, and releases control through the writer thread. Exit code 0 means the
reports were written; confirm the recoil by feel, and that the trigger fires recoil by itself afterwards.
