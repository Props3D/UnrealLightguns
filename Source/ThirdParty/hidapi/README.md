# hidapi

[hidapi](https://github.com/libusb/hidapi) gives the plugin raw HID output reports on Windows, macOS and
Linux. Unreal has no API for writing HID output reports (see `docs/PLUGIN_SPEC.md`, constraint 3).

**Licence:** hidapi offers GPL v3, BSD-3-Clause or its original licence. This plugin uses it under
**BSD-3-Clause only**.

## Sources

hidapi **0.15.0** (see `VERSION` and `TAG`) is committed here: `hidapi/hidapi.h`, the `windows/`, `mac/`
and `linux/` (hidraw) backends, and the licence files. Nothing needs fetching to build the plugin.

To update to another release, from the repo root on macOS or Linux (or Git Bash on Windows):

```bash
Scripts/fetch-hidapi.sh hidapi-0.15.0
```

Then check that every public function is still renamed (see below) and commit the result.

## How it is built

This is the `External` module `LightgunHidApi` (named so it cannot clash with another plugin's hidapi module): it provides include paths and platform link dependencies only. The
backend `.c` files are compiled into `LightgunCore` by `Private/HidApi/LightgunHidApi.c`, and no prebuilt
binaries are shipped.

| Platform | Backend | Links |
|---|---|---|
| Windows | `windows/hid.c` | nothing: hidapi loads `hid.dll` and `cfgmgr32.dll` at runtime |
| macOS | `mac/hid.c` | `IOKit`, `CoreFoundation` frameworks (deferred platform) |
| Linux | `linux/hid.c` (hidraw) | `libudev` (deferred platform: must confirm UE's Linux toolchain provides `libudev.h`) |

Every public hidapi function is renamed with a `LightgunHid_` prefix
(`Private/HidApi/LightgunHidApiRename.h`), so another plugin bundling hidapi cannot collide with this
one in a monolithic build.
