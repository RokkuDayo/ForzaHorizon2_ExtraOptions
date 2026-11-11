# EO Garage Engine Rev 1.0.3 — standalone source

This source package is independent of the Extra Options source tree. It includes
the plugin source, its API header, import definitions, INI and version resources.
No game files, compiler, prebuilt DLLs or other plugins are bundled.
Runtime code is unchanged from the released Garage Engine Rev 1.0.3.

## Build

Install Python 3.10+ and download Zig 0.15.2 from https://ziglang.org/download/.
From this extracted folder run:

```text
python build.py "C:/path/to/zig-x86_64-windows-0.15.2/zig.exe"
```

The release DLL is written to `dist/EO_GarageEngineRev.dll`. Build intermediates
stay in `build/`. No prebuilt Extra Options import library or full source checkout
is required. The same Zig cross-compilation command supports Linux hosts.

`--test` builds a separate DLL with fixture exports, for development only.
The original internal regression harness requires the private native-game test
environment and is not included here; the recorded results are in docs/release.json.

## Install

Extra Options and Azimuth are required at runtime. Close the game, then place
the DLL and INI in `mods/Forza Horizon 2/Garage Engine Rev/plugins/` and enable the mod in
`mods/Forza Horizon 2/loadorder.txt`. Do not also load the older Garage Rev Test plugin.
The implementation currently accepts the Azimuth adapter only.

Logging defaults to Off. See docs/README.txt for usage and audio behavior.
The antivirus review records previous results, not a guarantee for new builds.
