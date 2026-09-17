# Testing Blamcon Lightguns for Unreal

Thanks for trying the plugin. This is an early preview. It has been built and run on one setup (Unreal
Engine 5.6, Windows 11, one gun over USB); see [what has been tested](../CONTRIBUTING.md#hardware-results).
Everything else is untested, so reports of anything that goes wrong are just as useful as confirmations
that something works.

Install the plugin first: see [Install in the README](../README.md#install).

## Before you start

- **Gun mode:** set the gun to **Gamepad mode** in Blamcon ARC for most tests. In mouse mode the gun stays
  the system mouse, and gets force feedback only on firmware with mouse-mode feedback (the firmware's
  `release-3.0` branch).
- **Firmware:** 2.1.0 or later. Mouse-mode feedback and the device info in the connect line need the
  firmware's `release-3.0` branch.
- **Connection:** USB only. Bluetooth isn't available in any current firmware release; it's planned for a
  future one.
- **Logs:** most results show up in **Window > Output Log**. Type `LogLightgun` in the search box to see
  only the plugin's messages. The full log is in `Saved/Logs/<YourProject>.log`.

## First test (5 minutes)

1. Open your project with the gun connected. The Output Log should show a line like:

   ```
   LogLightgun: Lightgun connected: P1 3673:0100 (...), firmware 3.0.0, RP2350, gamepad, feedback yes
   ```

   Released firmware (2.1.0) doesn't report device info, so it shows `firmware unknown, USB` instead.
   Include this line in any report.

2. In your character or player controller Blueprint, add the **Lightgun Trigger** key event, and connect
   **Pressed** to **Play Recoil** (Player Index 0).
3. Press **Play** and pull the trigger. The gun should recoil once per pull.
4. Stop play and pull the trigger. The gun should recoil by itself: control went back to the gun.

If that works, the core of the plugin is working. If it doesn't, see [Troubleshooting](#troubleshooting).

## Test checklist

Tick what you tried, and note anything that behaved differently. Unless a test says otherwise, use
Play-in-Editor with the gun in Gamepad mode.

### Connection

- [ ] Gun connected over **USB** shows `Lightgun connected` in the log.
- [ ] Unplug the gun during play: the log shows `Lightgun disconnected`, and nothing freezes or crashes.
- [ ] Plug it back in: it reconnects within a couple of seconds, and input and recoil work again.
- [ ] On `release-3.0` firmware, the connect line shows the firmware version, board, mode (`gamepad`) and
      `feedback yes`.
- [ ] **On Lightgun Connected**, **On Lightgun Disconnected** and **On Lightgun Warning** fire on the
      Lightgun Subsystem.

### Input

- [ ] **Lightgun Trigger**, **A**, **B**, **Y**, **Start** and **Select** each produce Pressed and Released
      events.
- [ ] **D-pad** up, down, left and right each work. Diagonals are ignored on purpose.
- [ ] **Lightgun Aim** reaches 0 and 1 at every screen edge. Y is 0 at the bottom.
- [ ] Aim is smooth near the centre of the screen: no dead zone or snapping.
- [ ] Quick trigger taps all register.

### Force feedback

- [ ] **Play Recoil**, and **Play Recoil Timed** with different on and off times.
- [ ] **Play Rumble** and **Play Rumble Timed**.
- [ ] **Set Led Color** with a few colours; black turns the LED off.
- [ ] **Flash Led**, with default timing and with Lit Ms and Dark Ms set.
- [ ] **Take Feedback Control** with Ammo ticked and Starting Ammo set: the display shows that count.
- [ ] **Set Ammo Count** updates the display.
- [ ] **Release Feedback Control**: the gun goes back to firing recoil on its own trigger pull.
- [ ] A **Force Feedback Effect** (Play Force Feedback / Client Play Force Feedback): large motor curves
      rumble, small motor curves fire recoil.
- [ ] **Input Device Subsystem** light colour property changes the LED.

### Control handoff

- [ ] During play, the trigger no longer fires recoil by itself.
- [ ] **Stop play:** the trigger fires recoil by itself again.
- [ ] **Alt-tab away during play:** the trigger fires recoil by itself; alt-tab back and it stops again.
- [ ] **Packaged build:** quit the game, and the trigger fires recoil by itself again.

### Two or more guns

- [ ] Each gun is a separate player (P1 is player index 0, P2 is 1, and so on).
- [ ] Feedback sent to one player index reaches only that gun.
- [ ] Two guns set to the **same player number** log a warning.
- [ ] On `release-3.0` firmware, no `reports player number` warning appears.

### Mouse mode

On firmware with mouse-mode feedback (`release-3.0`):

- [ ] The connect line shows `mouse, feedback yes`, with no mouse-mode warning.
- [ ] While play runs, the gun still moves the cursor and clicks as the system mouse.
- [ ] **Play Recoil**, **Play Rumble**, **Set Led Color** and **Set Ammo Count** (after Take Feedback
      Control with Ammo) each work. Rumble and ammo haven't been tested in mouse mode yet.
- [ ] Mouse aim through the Lightgun Mouse Aim mapping still works for that player.
- [ ] Stop play: the trigger fires recoil by itself again.

On released firmware (2.1.0):

- [ ] The log shows `found in mouse mode without feedback support`, and the gun works as a normal mouse.

### Device info

- [ ] Kill the editor or game from Task Manager during play, then start it again without playing: the log
      shows `already under another host's control`, and the plugin leaves the gun alone. Reconnect the gun,
      and the trigger fires recoil by itself again.
- [ ] Run a packaged build during play, then open the editor: the game keeps control of the gun.

### Mouse

- [ ] With no gun connected, the mouse aims and fires through the same Input Actions (see
      [Aiming with Enhanced Input](../README.md#aiming-with-enhanced-input)).
- [ ] Connect a gun in Gamepad mode for that player: the gun aims, and the mouse no longer moves the aim.

## Sample Input Mapping Context

The plugin ships Input Actions and an Input Mapping Context already wired up for gun and mouse. Find them
in the Content Browser, in the **Input** folder of the plugin's content (under **Plugins**). If there's no
Plugins folder, enable **Show Plugin Content** in the Content Browser settings.

The assets were saved by Unreal 5.6. Unreal can't open assets from a newer version than its own, so on 5.4
or 5.5 they won't load and you need to regenerate them. Regenerating is also how you get them back if you
change or delete them:

1. Enable **Edit > Plugins > Python Editor Script Plugin** and restart.
2. Run **Tools > Execute Python Script...** and choose `Plugins/BlamconLightguns/Scripts/create_sample_input.py`.

| Asset | Contents |
|---|---|
| `IA_LightgunAim` | Axis2D action |
| `IA_LightgunFire` | Digital action |
| `IA_LightgunReload` | Digital action |
| `IMC_Lightgun` | Aim from Lightgun Aim or the mouse; Fire from Lightgun Trigger or Left Mouse Button; Reload from Lightgun A or R |

Add `IMC_Lightgun` with **Add Mapping Context** on the Enhanced Input Local Player Subsystem, then bind
the three actions in your pawn or controller.

The assets are written inside the plugin folder, so copy them into your own project's content if you
want to keep them when you update the plugin.

## Troubleshooting

**The plugin doesn't build**
Send us the first error from the build output, with your Unreal and Visual Studio versions.

**No "Lightgun connected" message**
- Check the gun is in Gamepad mode in Blamcon ARC, or in mouse mode on firmware with mouse-mode feedback.
- Try another USB cable or port. Bluetooth isn't supported yet.
- If you use **HidHide**, add `UnrealEditor.exe` (and your packaged game) to its allow list.
- Look for `Could not open lightgun` or `Lightgun support is disabled` in the log, and send it to us.

**"found in mouse mode without feedback support"**
Windows reserves the gun's mouse and keyboard for itself, so a game can only send feedback through the extra
channel the firmware's `release-3.0` branch adds. Either use that firmware, or change the gun to Gamepad mode
in Blamcon ARC.

**"can't take force feedback in its current mode or connection"**
The gun says its firmware can't take feedback this way. Input still works. Connect by USB, and report it
with the connect line.

**"reports player number"**
The gun's player number in Blamcon ARC doesn't match the player it connected as. The plugin uses the
connected player. Please report this with the connect line.

**"lightguns are set to player N"**
Two guns share a player number. Give each gun its own player number in Blamcon ARC.

**Recoil fires on every trigger pull during play**
The game didn't take control. Check the log for errors, and that the game window has focus.

**The gun stays silent after a crash**
A crash can leave the gun under game control. Unplug and reconnect the gun, or turn it off and on. The plugin
doesn't release control it didn't take in the running game, because it can't tell a crashed game from
another program that is still using the gun; the log says `already under another host's control`.

**Aim is stuck, or the mouse doesn't aim**
Mouse aim needs the cursor free to move over the viewport. If your game locks the cursor to the centre, the
mouse reads a fixed position. Mouse aim is also off for a player whose gun is connected in Gamepad mode.

**A gun stops responding and doesn't come back**
If the log shows `write failed`, `write stalled` or `input read failed`, unplug and reconnect the gun.

## Known limitations

- Windows only. macOS and Linux are planned.
- USB only. Bluetooth needs a future firmware release.
- Up to four guns (player numbers 1–4).
- In mouse mode, input comes from the system mouse: guns in mouse mode share one cursor, so aim is
  effectively single-player. Use Gamepad mode for multiplayer.
- Mouse-mode feedback needs firmware that isn't released yet.
- The gun ignores a new recoil while earlier recoil pulses are still cycling. If you fire faster than that,
  some pulses are dropped.
- After alt-tabbing back, the ammo display reads 0 until the game next sets the count.
- A gun that has a read or write error needs to be reconnected.
- With several Play-in-Editor windows open at once, guns stay under game control until the last one stops.

## Reporting problems

Please open an issue at <https://github.com/Props3D/UnrealLightguns/issues> with:

- Unreal Engine version, Windows version, and Visual Studio version if it's a build problem
- The `Lightgun connected` line (firmware, board, mode and connection), and the gun's firmware version from
  Blamcon ARC
- How many guns were connected
- What you did, what you expected, and what happened
- The `LogLightgun` lines from the Output Log, or the whole `Saved/Logs/<YourProject>.log`

Successful results are welcome too: tell us which checklist items passed.
