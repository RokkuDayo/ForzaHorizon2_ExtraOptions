# Building and testing

Use Python 3.10 or later and Zig 0.15.2. Run these commands from this source
folder. Builds target x86_64 Windows PE DLLs and use only Zig's bundled Windows
headers and Kernel32 import library. No Microsoft C runtime, Xbox SDK, original
helper DLL, or translation-layer source is required.

## Release builds

```text
python build.py /absolute/path/to/zig.exe
python build.py /absolute/path/to/zig.exe --azimuth
```

The outputs are `../xg.dll` for Mezcal and `../azimuth-tls.dll` for Azimuth.
`--output PATH` chooses another destination. `--build-dir PATH` keeps the
intermediate files in that directory; otherwise they use a temporary directory.
The script lists common modules explicitly and selects one layer adapter.
Azimuth also includes its guarded host, livery and save compatibility modules.
All modules compile with warnings treated as errors and x64 unwind tables.

On Linux, use the portable Linux Zig distribution and the same commands with
`zig` as the compiler filename. The target remains a Windows DLL. Cross-build
support is retained; Linux/Wine execution has not been tested.

The public exports are `Fh2TweaksStatus` on both adapters and the additional
`azimuth_tls_index` / `azimuth_tls_attached` data exports on Azimuth. These names
are retained for compatibility. Azimuth has a 336-byte zero TLS template aligned
to 16 bytes and no TLS callbacks. The release binaries import Kernel32 only.
The gamerpic feature lazily loads the system Windows codec on a later UI call.
Autodrive loads User32 lazily on a native GPS update to read F2 and focus.
Controller buttons and telemetry visibility are read from native game state;
the cinematic transition calls the game's existing white-flash routine.
Keyboard & mouse (off by default) loads User32 and GDI32 lazily when enabled and
reads Raw Input on its own thread through a message-only window; while its menu
pointer is shown it sets the game window's class cursor.

Both adapters initialize synchronously in `DllMain`. Startup performs simple
configuration I/O, hashing, mapped-image validation, and memory patching.
Callback resource work and PNG decoding happen on normal game call paths.
There is no worker thread or startup wait. Callback installation pins the DLL
before any callback pointer is written.

## Offline tests

Build with `--test` (adds the `Fh2Test*` exports), then run the checks; they
work in a temporary folder and never start the game:

```text
python build.py /absolute/path/to/zig.exe --azimuth --test --output out/winmm-test.dll
python tools/test-keyboard-mouse.py out/winmm-test.dll
python tools/test-controller-pause.py out/winmm-test.dll
python tools/test-keyboard-mouse-page.py out/winmm-test.dll /path/to/media/UI.zip
```

The last one builds the Settings hub and the KEYBOARD & MOUSE page from the
game's own `A_PauseOptions.xaml` and `A_VideoOptions.xaml` (read from the
game's `media/UI.zip`) and round-trips options and key bindings through
`[KeyboardMouse]`.

## Patch data

Edit `patches.json`, then regenerate the checked-in C tables:

```text
python generate_data.py
```

The generator validates non-overlapping file/image ranges, supported groups,
and equal-length byte variants. It emits `patches_generated.c` and
`patches_generated.h`. You can format these with clang-format afterward.
It needs no game executable. Runtime patches use RVAs; file verification uses
raw offsets. The fifteen runtime callback descriptors live in the feature
modules and are described in `runtime_hooks` in the manifest.
