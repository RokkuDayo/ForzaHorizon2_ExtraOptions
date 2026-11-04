# Building and testing

Use Python 3.10 or later and Zig 0.15.2. Run these commands from this source
folder. Builds target x86_64 Windows PE DLLs and use only Zig's bundled Windows
headers and Kernel32 import library. No Microsoft C runtime, Xbox SDK, original
helper DLL, or translation-layer source is required.

## Source layout

- Top level: files shared by every build: `dllmain.c`, `support.*`,
  `sha256.*`, `extra_options.h`, the Azimuth adapter in `adapters/`, the
  import definitions and `build.py`.
- `fh2/`: Forza Horizon 2 only (runtime, settings, patch engine, `features/`,
  `patches.json` and its generator, the Mezcal adapter, FH2 plugins and
  tools).
- `fh3/`: Forza Horizon 3 only (`fh3.c`, the mod loader, `fh3_plugin.h`,
  the example plugin and `tools/fh3_zip.py`).

`build.py` compiles with `-I . -I fh2 -I fh3`, so shared headers resolve from
either title folder.

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

## Continuous builds

`.github/workflows/build.yml` builds all three variants (FH2 Mezcal, FH2
Azimuth, FH3 Azimuth) with Zig 0.15.2 on Ubuntu. It runs when a GitHub
release is published, attaching `ExtraOptions-<version>-<variant>.zip` to the
release, or manually from the Actions tab (Run workflow), uploading the same
zips as a workflow artifact. The zips hold only what players install, plus
`README.md` and `LICENSE`; plugin headers and tools stay in the repository.
The FH3 zip also carries `FH3_ExtraOptions.ini`
(`fh3/FH3_ExtraOptions.ini`, the defaults listed below) and the NoPause and Groove Music
Radio plugins under `mods\<Name>\plugins\`, built with llvm-mingw 20261020 by
each plugin's `build.sh`.

## Forza Horizon 3 build

```text
python build.py /absolute/path/to/zig.exe --azimuth --fh3
```

This is a separate Azimuth-only DLL for Forza Horizon 3 (title `4CE1DFAB`,
`forza_durango_release_final.exe` 1.0.125.0, SHA-256
`e6a362a2fde118577ab3018c8e042e62a3bb42f83a63ace41145c0864527a661`). The
default output is `../FH3/winmm.dll`, so it never overwrites the FH2 build. It
links only the adapter, support, SHA-256 and `fh3/`; none of the FH2
runtime, settings, plugins or save compatibility modules are included. Any other
title loaded through Azimuth is left untouched.

`fh3/fh3.c` owns its own verification, `FH3_ExtraOptions.ini` and
`FH3_ExtraOptions.log`. Every enabled site is checked against its original
bytes before any write; on mismatch nothing is patched. Options:

```ini
[Global]
Enabled=1
[Settings]
; Add the Extra Options rows to Settings: HUD and Gameplay (FOV, minimap, drift camera) and Video (liveries).
InGameMenu=1
[Frontend]
; Skip the T10/Microsoft Studios startup video (legal screen stays).
SkipIntro=1
; In My Cars, the Autoshow and other car lists, View (or Backspace) opens a grid of manufacturers; pick one to jump to its tab.
ManufacturerJump=1
; Keep the Social, Clubs, Rivals and Marketplace pause menu tabs enabled offline.
OnlineTabs=1
[Content]
; VIP license refresh and Main/IsVip reads report VIP.
OfflineVIP=1
; Unlock the Treasure Map (barn finds and boards on the map) in each world you visit.
UnlockTreasureMap=0
; Every car hidden from the Autoshow can be bought there, including Forzathon prizes, except Horizon Editions.
UnlockAllCars=1
; Horizon Edition (Forzathon) cars can also be bought in the Autoshow (0: only won in Wheelspins).
UnlockForzathonHorizonEditionCarsInAutoshow=0
; Forzathon horns (Duck, Cuckoo, Train...) are unlocked in Car Horns.
UnlockForzathonHorns=1
; Wheelspins' Horizon Edition prizes can award any of the 33 Horizon Edition cars (the game: 17 of them).
HorizonEditionWheelspins=1
[Vehicle]
; AI drives the car along the GPS route. Throttle, brake or steering cancels.
Autodrive=1
; Keyboard toggle: F1-F12, a letter or digit, or a virtual-key code such as 0x71.
AutodriveKey=F2
; Controller toggle, buttons joined by +: A B X Y LB RB L3 R3 View Menu Up Down Left Right.
AutodriveButtons=L3+R3
; Also list "Autodrive" in the ANNA menu (D-pad Down).
AutodriveInAnna=1
; ANNA "Car" submenu: open and close the roof, hood, trunk and doors the car animates (as in ForzaVista), or all at once. Parts open only while stationary; the throttle closes them.
CarPartsInAnna=1
; Switch to slow orbiting cinematic shots (HUD hidden) after 3 s of Autodrive; camera and HUD return when you take over.
AutodriveCinematic=1
[HUD]
; Turn the minimap (and its N marker) with the right-stick look camera.
MinimapFollowsCamera=1
; FPS readout in the top-right corner of the HUD and of menus with the player bar,
; e.g. "72 GPU:71%  13.9 ms". GPU usage is the total 3D-engine load. Also in Settings > HUD and Gameplay.
ShowFPS=0
ShowGPUUsage=1
ShowFrameTime=1
; Draw the readout on a dark box (0 shows the text alone). With GPU usage and frame time off it reads "60 FPS".
ShowFPSBackground=1
[Camera]
; Field of view in degrees (10-150); 0 keeps the game value.
ChaseNearFOV=0
ChaseFarFOV=0
HoodFOV=0
BumperFOV=0
CockpitFOV=0
; Chase cameras swing toward the direction of travel while the car slides.
DriftCamera=1
; Drift camera tuning in percent: how far it follows the slide, how fast, and the maximum swing.
DriftSensitivity=35
DriftSpeed=70
DriftRange=80
[Livery]
; Render car liveries at 4096x4096 (game: 1024 or 2048 by quality).
Resolution4K=1
; Livery anti-aliasing samples: 2 (game) or 4.
MSAA=4
; Render the livery at twice the resolution again before downsampling (heavier).
Supersampling=0
[PhotoMode]
; Remove the photo camera boundary and height limits.
Unrestricted=1
; Also let the camera pass through the ground, walls and car (altitude keeps working).
NoCollision=0
[Files]
; Loose files in Mount\mediaoverride and Mount\mediapc override game media.
MediaOverrides=1
[Mods]
; Load mods from the mods folder next to azimuth.exe (mods\<ModName>\). Disable one with <ModName>=0.
Enabled=1
[Compatibility]
; Accept edited or hand-copied Azimuth saves (rebuilds missing commit files).
DisableAzimuthSaveChecksums=1
```

The file is optional; missing keys use the defaults shown. There is no DLC
option: car packs are rebuilt from store licenses on every boot, and Azimuth
already reports them as owned. Expansions need their own packages installed.

### FH3 mods

Each folder in `mods\` next to `azimuth.exe` (and `FH3_ExtraOptions.ini`) is a
mod, as with FH2; mods load in name order.

- `media\` is mounted over `game:\media\` with the native loose-file volume,
  above `mediapc` and below `mediaoverride`. On conflicts the first mod name
  wins. Replacement archives (for example `UI.zip`) must use the game's zip
  layout: every central directory entry carries extra field `0x1123` with the
  absolute data offset. `python fh3/tools/fh3_zip.py repack SOURCE.zip OUT.zip DIR`
  writes such an archive, replacing or adding the files found under `DIR`.
  Azimuth only lets the game read real folders inside its own install (no
  `..`, junctions or symbolic links), so on every start each enabled mod's
  `media\` is mirrored to `<game>\Mount\mods\<ModName>\media\` with hard links
  (copies when a hard link isn't possible) and mounted from there. The mirror
  is rebuilt each time; edit the mods next to `azimuth.exe`, not the mirror.
- `plugins\*.dll` are loaded once, in name order, on the game thread right
  after the native file system is initialized (never inside `DllMain`). The
  API is `fh3_plugin.h`: optional exports `FH3PluginInitialize(context)` and
  `FH3PluginUpdate(frame)` (per frame while driving, with the player car).
  The context provides logging to `FH3_ExtraOptions.log`, verified byte
  patching, rel32 call hooks (jump stubs are allocated within reach of the
  image), pointer/vtable hooks, and the player car. `fh3/plugins/example` is a
  minimal plugin.

### Groove Music Radio (FH3 plugin)

`fh3/plugins/GrooveMusicRadio` plays your music on the radio's Groove station.
Build it with `sh fh3/plugins/GrooveMusicRadio/build.sh` (llvm-mingw clang) and
install the DLL as `mods\GrooveMusicRadio\plugins\GrooveMusicRadio.dll` next to
`azimuth.exe`. Its settings are in `mods\GrooveMusicRadio\GrooveMusicRadio.ini`
(shipped with an empty `api_key`; recreated with the defaults if missing). Choose what plays in Audio Options > Groove Music
Playlist:

- each folder in your Windows Music folder (MP3, FLAC, WAV);
- Spotify, through Spotify Soloist: put `soloist.exe` in the mod folder and set
  `api_key=` (Spotify Premium); play on the device "FH3 Radio";
- YouTube Music (music.youtube.com in a browser), NetEase Cloud Music and QQ
  Music: start the app and play something. Its own sound is turned down while
  the radio plays it and put back afterwards.

The plugin answers the shut-down Groove service's calls itself (sign-in,
playlists, tracks, song URLs) and gives the game's Groove player its own Media
Foundation source reader, since Azimuth's only opens the game's files.

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

Both adapters initialize synchronously in `DllMain`. Startup performs simple
configuration I/O, hashing, mapped-image validation, and memory patching.
Callback resource work and PNG decoding happen on normal game call paths.
There is no worker thread or startup wait. Callback installation pins the DLL
before any callback pointer is written.

## Patch data

Edit `fh2/patches.json`, then regenerate the checked-in C tables:

```text
python fh2/generate_data.py
```

The generator validates non-overlapping file/image ranges, supported groups,
and equal-length byte variants. It emits `patches_generated.c` and
`patches_generated.h`. You can format these with clang-format afterward.
It needs no game executable. Runtime patches use RVAs; file verification uses
raw offsets. The fifteen runtime callback descriptors live in the feature
modules and are described in `runtime_hooks` in the manifest.
