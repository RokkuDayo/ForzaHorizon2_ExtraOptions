# Building and testing

Use Python 3.10+ and Zig 0.15.2 from the source folder. Only the bundled Windows
headers and import-library tools are needed. No layer source, Xbox SDK, CRT,
original helper or game assets are required to compile the mod.

## Release builds

```text
python build.py /absolute/path/to/zig.exe --azimuth
python build.py /absolute/path/to/zig.exe
```

Azimuth builds write `../azimuth/winmm.dll`; test DLLs and an explicitly requested
Mezcal build write under `../.build`. Use `--output` to select another destination.
The current development build includes FH2, FM5 and FH3.

```text
python source/build.py toolchain/zig-x86_64-windows-0.15.2/zig.exe --azimuth --fm5 --fh3
```

Run this command from the workspace root. Copy the INIs from `source/config`
to `azimuth` when preparing a fresh installation. Keep users' existing INIs and DAT
files when updating. Omit `--fm5` and `--fh3` for an FH2-only release. Build Mezcal only when
explicitly requested. No ZIP is produced by the build script.

See [FH3 support](FH3.md) for the supported executable, mod layout and plugin SDK.

Unchanged objects and import libraries are reused from ../.build/azimuth or
../.build/mezcal, with separate caches for test builds. --build-dir changes the
cache directory, --jobs controls parallel compilation (up to eight jobs by
default), and --rebuild forces a clean compilation. Cache signatures include
source/header contents, compiler metadata and command-line arguments. Linking
always runs, and object order is fixed for reproducibility. Intermediates stay
outside source and need not be distributed. Production source files
are explicitly listed; development files are not compiled into a release.
The build targets x64 PE, treats warnings as errors and emits unwind tables,
ASLR/NX flags, accurate VERSIONINFO resources and a deterministic /Brepro image.
The unsigned binaries import Kernel32 and ntdll's structured exception handler.
No executable packer, obfuscator or post-build patcher is used.

The same source builds with Linux's Zig distribution, targeting Windows. This
is cross-compilation support; Linux/Wine execution is still untested here.

Azimuth's proxy forwards Windows multimedia exports, including the unnamed
ordinal 2. Its startup work runs on the inspected launcher's timeBeginPeriod
call, outside DllMain. The host's native TLS reserve and game loader are preserved. See AZIMUTH_NOTES.md for the fingerprint, ABI, one-shot
game-mapping handoff and refusal paths. The old --diagnostic build is retired.

Mezcal retains its optional XG lookup path. Common callbacks are pinned before
installation. PNG decoding, resources and gameplay controls run on their native
call paths. Small same-process reads use structured exception handling rather
than ReadProcessMemory. There is no background polling thread.

## Patch and asset data

For a separate instrumented mod-loading DLL, see [boot diagnostics](BOOT_DIAGNOSTICS.md).
The `--boot-diagnostics` flag is for temporary performance investigation only.

```text
python generate_data.py
python development/tools/generate_stock_wheels.py /path/to/Mount/media
```

The first command regenerates patch tables from patches.json after validating
ranges and byte lengths. The second optionally regenerates donor wheel paths
from your own read-only media, retaining the two supplied AO path corrections. Neither is needed for an unchanged source build.
Wheel paths are normalized into shared four-corner layouts and a sorted list
of wheel names, donor aliases and layout IDs. The generator verifies that every
template expands to its exact authored path and derives the output-buffer size
from the longest path. No donor car definitions are needed at build time or
parsed by the wheel hook at runtime.
The generator scripts at the source root are build inputs; development contains
only tests, optional tools, formatting configuration and investigation notes.

## Native regression tests

Windows x64 Python is required to execute fixtures. They map your own game/host
files privately and do not run launcher UI, graphics, the game entry point or
personal save access. Keep fixture files local; they can contain proprietary
bytes. --test adds fixture exports that must never be distributed in release DLLs.

```text
python build.py /path/to/zig.exe --azimuth --test --output /path/to/test.dll
python development/tests/run_tests.py /path/to/test.dll /path/to/game.exe /path/to/Media /path/to/new-scratch --azimuth-exe /path/to/azimuth.exe --release-adapter ../winmm.dll
```

--only NAME ... selects groups. With --azimuth-exe, the suite adds v1.0.1–v1.0.4
handoff, save and marker checks and validates its graphics presets. The optional
--release-adapter adds production Windows export forwarding.
The runtime group defaults to 16,384 configurations; its standalone script also
accepts a final integer selecting a representative subset, including endpoints.

The handoff tests use private PE mappings, the real call/thunk bytes and a mapper
stand-in. They check actual-package title gating, all four native arguments,
full file fingerprints, changed code/padding, permission-failure rollback and
restoration of host bytes/protections. They do not execute the host's TLS reserve
or game entry point. Save tests execute the native rejection decisions with
instrumented private exits. Production forwarding is compared with system winmm.
These fixtures do not replace a full v1.0.1/v1.0.2 launch/gameplay test.

Other groups cover memory faults, camera/drift/FOV, in-game Video/HUD settings,
stock-rim paths/localization, UI suppression, vehicle gestures/roof locks,
patch-page transactions, override mounts, PNG decoding, collectible ownership,
parental checks, garage/road transitions, XML changes and host compatibility.
The memory benchmark compares guarded reads with the former process API on
Windows only; its speedup is not a game FPS or Wine performance measurement.
The stock_wheel_paths group compares the compiled path builder with all 347
authored wheel sets using original, lower and upper-case names. It also checks
exact buffer limits, unknown names and deterministic generation. The rims group
separately runs the game's actual model-replacement routine and texture fallbacks.

The retired TLS-replacement fixture scripts are retained under history/tls-loader
for reference. They are not tests of the current adapter.

## Release verification

Build twice to different intermediate/output folders with the same DLL filename
and compare their SHA-256 values. Validate metadata and exports, run applicable
fixtures, and verify the original game, host and original helper hashes remain
unchanged. Production DLLs must not export Fh2Test symbols or contain a TLS
replacement. Signing requires a genuine publisher certificate; if signing is
performed, regenerate release checksums afterward. See ANTIVIRUS_REVIEW.md in
this documentation folder for the historical malware-detection investigation.
