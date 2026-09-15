# Blamcon Lightguns for Unreal

Blamcon lightgun support for Unreal Engine. Guns aim and shoot through Enhanced Input like any other
controller, and your game can drive their force feedback: recoil, rumble, the RGB LED and the ammo display.

Companion to the Unity package [com.blamcon.lightguns](https://github.com/Props3D/UnityLightguns).

> **Early preview.** The plugin builds on Unreal Engine 5.6 and the basics work on hardware over USB (one
> gun, gamepad and mouse mode), including buttons and rumble. Several guns, the ammo display and aim haven't
> been tested yet, so expect rough edges and please report them. Guns connect by USB only for now. [docs/TESTING.md](docs/TESTING.md) explains what to try and
> how to report.

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
- A Blamcon lightgun on firmware 2.1.0 or later, connected by USB:
  - **Gamepad mode:** aim, buttons and force feedback through the plugin. Needed for gun aim with
    several players.
  - **Mouse mode:** force feedback only needs firmware with mouse-mode feedback (the firmware's
    `release-3.0` branch, not yet released). The gun aims and fires as the system mouse.
  - Bluetooth support is not available in any current releases. This is planned for a future firmware release.

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
   `LogLightgun: Lightgun connected: P1 3673:0100 ...`. Released firmware shows `firmware unknown`; builds
   with the device info reports show details such as `firmware 3.0.0, RP2350, gamepad, feedback yes`.
2. In your character or player controller Blueprint, add the **Lightgun Trigger** key event, and connect
   **Pressed** to **Play Recoil** with Player Index 0.
3. Press **Play** and pull the trigger: the gun recoils.
4. Stop play and pull the trigger again: the gun recoils by itself, because control went back to the gun.

Player Index is 0-based: player 1 is 0. Pass -1 to send to every connected gun.

Force feedback nodes are in the **Blamcon > Lightguns** category, and the keys are grouped under
**Blamcon Lightgun**. Connection and warning events are on the
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

## Using the plugin from C++

`BlamconLightguns` is the plugin's name, not a module. The module to depend on is `Lightguns`. In your
game's `.Build.cs`:

```csharp
PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Lightguns" });
```

Keep one copy of the plugin, in `YourProject/Plugins/BlamconLightguns`. If your project is Blueprint-only,
add any C++ class first (**Tools > New C++ Class**), which creates the `Source` folder and the `.Build.cs`.
After adding the dependency, close the editor, delete the project's `Binaries` and `Intermediate` folders,
regenerate the Visual Studio project files (right-click the `.uproject` > **Show more options**), and build
the **Development Editor** configuration with the editor closed.

Every Blueprint node is a static function on `ULightgunLibrary`, with the same parameters:

```cpp
#include "LightgunLibrary.h"

// When the weapon fires
ULightgunLibrary::PlayRecoil(PlayerIndex);
ULightgunLibrary::FlashLed(PlayerIndex, FLinearColor::Red, 3);
```

Recoil, rumble and the LED are already under the game's control while a game instance exists, so nothing
needs to be taken first. The ammo display is the exception: it stays with the gun until the game asks for
it, and taking it zeroes the display, so send the starting count in the same call:

```cpp
ULightgunLibrary::TakeFeedbackControl(PlayerIndex, false, false, false, true, 12);
ULightgunLibrary::SetAmmoCount(PlayerIndex, Ammo);
```

The gun's keys are on `FLightgunKeys`. Bind them on the pawn's input component, which is where the
equivalent Blueprint key event lives:

```cpp
#include "LightgunKeys.h"

void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindKey(FLightgunKeys::Trigger, IE_Pressed, this, &AMyCharacter::Fire);
	PlayerInputComponent->BindKey(FLightgunKeys::ButtonA, IE_Pressed, this, &AMyCharacter::Reload);
}
```

The pawn is where the equivalent Blueprint key event binds, and it is what has been tested. For Enhanced Input, map the same keys to your Input Actions with
`UInputMappingContext::MapKey`, or generate a ready-made context with `Scripts/create_sample_input.py`.

Feedback calls made in the same frame are merged into one report, because the gun services one output
report at a time. Per component the last call wins, so **Set Led Color** followed by **Flash Led** in the
same frame shows the flash and the colour is lost; the flash ends with the LED dark. Space them out if you
want both.

These keys only arrive in **Gamepad mode**. In mouse mode the gun is the system mouse, so bind
`EKeys::LeftMouseButton` for the trigger instead; feedback works in both modes.

### Enhanced Input with the shipped assets

The plugin ships Input Actions and a Mapping Context, so there is nothing to author: `IA_LightgunFire`,
`IA_LightgunAim`, `IA_LightgunReload` and `IMC_Lightgun`, in **Plugins > Blamcon Lightguns Content > Input**.
The context maps each action to the gun and to a keyboard or mouse equivalent, so the same actions work with
or without hardware: fire from the trigger or the left mouse button, reload from the gun's A button or the R
key, and aim from the gun or the mouse.

```cpp
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

void AMyCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (const APlayerController* const PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* const Input = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			Input->AddMappingContext(LoadObject<UInputMappingContext>(nullptr, TEXT("/BlamconLightguns/Input/IMC_Lightgun")), 0);
		}
	}
}

void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UInputAction* const FireAction = LoadObject<UInputAction>(nullptr, TEXT("/BlamconLightguns/Input/IA_LightgunFire"));
	Cast<UEnhancedInputComponent>(PlayerInputComponent)->BindAction(FireAction, ETriggerEvent::Started, this, &AMyCharacter::Fire);
}
```

Copy the assets into your own project's content if you want to keep changes when the plugin updates. In a
shipping game, reference them with `TObjectPtr<UInputAction>` properties set in a Blueprint subclass rather
than loading by path.

For connection and warning events, bind to `ULightgunSubsystem` (a Game Instance Subsystem). The handlers
must be `UFUNCTION`s:

```cpp
#include "LightgunSubsystem.h"

ULightgunSubsystem* Lightguns = GetGameInstance()->GetSubsystem<ULightgunSubsystem>();
Lightguns->OnLightgunConnected.AddDynamic(this, &AMyPlayerController::HandleLightgunConnected);
Lightguns->OnLightgunWarning.AddDynamic(this, &AMyPlayerController::HandleLightgunWarning);
```

Unreal's own force feedback works too: a Force Feedback Effect played on a player controller drives that
player's gun (large motors as rumble, small motors as recoil).

## Documentation

- [docs/TESTING.md](docs/TESTING.md): first test, test checklist, troubleshooting, known limitations, and
  how to report problems
- [CONTRIBUTING.md](CONTRIBUTING.md): repository layout and developer tests
- [docs/PLUGIN_SPEC.md](docs/PLUGIN_SPEC.md): design and roadmap

## License

MIT, except hidapi, which is used under its BSD-3-Clause option. See [LICENSE](LICENSE).
