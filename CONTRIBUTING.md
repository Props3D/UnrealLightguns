# Contributing

The design, constraints and roadmap are in [docs/PLUGIN_SPEC.md](docs/PLUGIN_SPEC.md). Read it before
changing the report format, threading or control rules.

## Status

| Milestone | State |
|---|---|
| 1. Transport: `LightgunCore`, writer thread, smoke test | Code complete. Host tests pass; UE build and hardware run still to do |
| 2. Input device and Blueprint surface | Code complete: input device, keys, subsystem, library, mouse parity and sample generator. UE build, asset generation and hardware run still to do |
| Device info reports and mouse-mode feedback (spec 4.1, 4.2) | Code complete. Host tests pass; UE build and hardware run still to do |
| 3. Serial backend (other brands) | Not started |
| Future: macOS, Linux | Deferred |

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
