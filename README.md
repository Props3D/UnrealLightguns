# Blamcon Lightguns for Unreal

Blamcon lightgun support for Unreal Engine. Guns aim and shoot through Enhanced Input like any other
controller, and your game can drive their force feedback: recoil, rumble, the RGB LED and the ammo display.

Companion to the Unity package [com.blamcon.lightguns](https://github.com/Props3D/UnityLightguns).

> **Early preview: not yet built or tested in Unreal Engine.** The protocol code has automated tests, but
> the plugin itself has not been compiled or run on hardware. Expect build errors and rough edges, and
> please report them. [docs/TESTING.md](docs/TESTING.md) explains what to try and how to report.

## Features

- **Input:** Lightgun Aim (absolute, 0–1), Trigger, A, B, Y, Start, Select and D-pad keys. Each gun is
  its own player, up to four.
- **Force feedback from Blueprint or C++:** Play Recoil, Play Rumble, Set Led Color, Flash Led, Set Ammo
  Count.
- **Works with Unreal's own features:** Force Feedback Effects drive rumble and recoil, and the Input
  Device Subsystem's light colour drives the LED.
- **Safe by default:** the game takes control of recoil, rumble and LED while it runs, and hands them back
  to the gun when play stops or the window loses focus.
- **Mouse parity:** aim and fire with a mouse through the same Input Actions, so you can develop without a
  gun.
- **Gamepad or mouse mode:** in Gamepad mode the gun is its own controller. In mouse mode it stays the
  system mouse and still gets force feedback, on firmware that supports it.
- **Clear diagnostics:** logs each gun's firmware, board, mode and connection, and tells you when a gun
  can't take feedback or two guns share a player number.

## Requirements

- Unreal Engine 5.4 or later, on Windows 10 or 11 (Windows only for now)
- Visual Studio 2022 with the **Game development with C++** workload (the plugin ships as source)
- A Blamcon lightgun on firmware 3.0 or later, connected by USB or Bluetooth:
  - **Gamepad mode:** aim, buttons and force feedback through the plugin. Needed for gun aim with
    several players.
  - **Mouse mode:** force feedback only needs firmware with mouse-mode feedback (the firmware's
    `release-3.0` branch, not yet released). The gun aims and fires as the system mouse.
  - Bluetooth feedback needs a current firmware build. After updating the firmware, remove the gun from
    Bluetooth settings and pair it again.

## Install

1. Clone the plugin into your project's `Plugins` folder. The folder must be named `BlamconLightguns`:

   ```bash
   git clone https://github.com/Props3D/UnrealLightguns.git Plugins/BlamconLightguns
   ```

2. If your project is Blueprint-only, add any C++ class first (**Tools > New C++ Class**) so Unreal can
   build the plugin.
3. Open the project. When Unreal offers to rebuild the missing modules, choose **Yes**.
4. Check **Edit > Plugins > Blamcon Lightguns for Unreal** is enabled, and restart if prompted.

## Quick start

1. Plug in a gun in Gamepad mode. **Window > Output Log** should show a line like
   `LogLightgun: Lightgun connected: P1 3673:0100 ... firmware 3.0.0, RP2350, gamepad, feedback yes`.
2. In your character or player controller Blueprint, add the **Lightgun Trigger** key event, and connect
   **Pressed** to **Play Recoil** with Player Index 0.
3. Press **Play** and pull the trigger: the gun recoils.
4. Stop play and pull the trigger again: the gun recoils by itself, because control went back to the gun.

Player Index is 0-based: player 1 is 0. Pass -1 to send to every connected gun.

Force feedback nodes are in the **Lightguns** category. Connection and warning events are on the
**Lightgun Subsystem** (Get Game Instance Subsystem).

### Aiming with Enhanced Input

Map **Lightgun Aim** to an Axis2D Input Action. Its value is where the gun points: X and Y from 0 to 1,
with Y = 0 at the bottom of the screen. Multiply by the viewport size for screen coordinates.

To aim with the mouse through the same action, add a **Mouse XY 2D-Axis** mapping to it, with the
**Lightgun Mouse Aim** modifier and the **Lightgun Mouse Aim** trigger. Mouse aim switches off for a player
while their gun is connected in Gamepad mode. A gun in mouse mode *is* the mouse, so mouse aim stays on;
all guns in mouse mode share one cursor.

`Scripts/create_sample_input.py` can generate a ready-made Input Mapping Context with aim and fire already
set up for gun and mouse. See [docs/TESTING.md](docs/TESTING.md#optional-sample-input-mapping-context).

### Ammo display

The gun ignores ammo counts until the game takes ammo control, and taking it clears the display. Take it
with the starting count in one call: **Take Feedback Control** with **Ammo** ticked and **Starting Ammo** set.
After that, call **Set Ammo Count** whenever the count changes.

## Documentation

- [docs/TESTING.md](docs/TESTING.md): first test, test checklist, troubleshooting, known limitations, and
  how to report problems
- [CONTRIBUTING.md](CONTRIBUTING.md): repository layout and developer tests
- [docs/PLUGIN_SPEC.md](docs/PLUGIN_SPEC.md): design and roadmap

## License

MIT, except hidapi, which is used under its BSD-3-Clause option. See [LICENSE](LICENSE).
