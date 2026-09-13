# hidapi

[hidapi](https://github.com/libusb/hidapi) gives the plugin raw HID output reports on Windows, macOS and
Linux. Unreal has no API for writing HID output reports (see `docs/PLUGIN_SPEC.md`, constraint 3).

**Licence:** hidapi offers GPL v3, BSD-3-Clause or its original licence. This plugin uses it under
**BSD-3-Clause only**.

## Getting the sources

The sources are not committed yet. From the repo root:

```bash
Scripts/fetch-hidapi.sh
```

This copies `hidapi/hidapi.h`, the `windows/`, `mac/` and `linux/` (hidraw) backends and the licence files
here. `LightgunHidApi.Build.cs` stops the build with a clear message until they are present.

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
