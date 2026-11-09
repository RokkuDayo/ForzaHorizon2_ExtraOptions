# Extra Options v0.4.4 — Forza Horizon 2

This release supports the inspected **Azimuth v1.0.1, v1.0.2, v1.0.3 and v1.0.4**
executables. It activates only for **Forza Horizon 2, title ID 1A96545B**.
FM5 support is excluded from this release. Other titles retain native behavior
and receive no Extra Options game or save changes.

## Install or update

1. Close Azimuth and the game.
2. Replace Extra Options' `winmm.dll` beside `azimuth.exe`.
3. Keep your existing `FH2_ExtraOptions.ini`, `FH2_ExtraOptions.dat` and mods
   in that folder. Use the supplied INI only for a new install.
4. Start FH2. The log heading is `Forza Horizon 2 Extra Options 0.4.4`; successful
   startup also records `AZIMUTH: 1.0.4 initialized for FH2 (1A96545B)`.

Leave Azimuth's own files in `data/bin` intact. The mod does not replace its TLS
helpers. An unrelated existing `winmm.dll` is not chained by this adapter.
The Mezcal adapter remains `xg.dll`, installed beside the game EXE in `Mount`.

## What changed

- Added inspected Azimuth v1.0.4 support, including the native loading handoff,
  optional missing save commit generation with native validation retained.
- This release is FH2-only. The unfinished FM5 profile, rim system, INI/log
  handling and FM5 save generation are not compiled into the DLL. FM5 source is
  retained separately for development; normal builds now exclude it.
- Routine builds remain Azimuth-only. Mezcal source support is retained.

- Added **Vignette Effect** above Speed Zone Vignette in HUD settings. On follows native rendering; Off suppresses the main world-image vignette without changing authored parameters. Defaults to On, including migration from older settings files.

- Added general shared database IDs with `policy.id_conflicts: "last_wins"`. Later successful mods supply duplicate rows while distinct cars and shared references remain intact. The diff wizard includes the policy.

- Manual headlights now bypass the native daylight cutoff for the player’s beam and glow presets. Default/Off restores normal activation; world night lighting is unchanged. This includes Photo Mode.

- Locked Hour now copies the native Photo Mode numeric row and slider templates. A normalized position drives the bar, while the readout remains in hours. The readout has room for five characters without shrinking the native font.

- Right-aligns the Locked Hour text within its value field, preserving vertical centering.

- Vertically centers the Locked Hour value in the same 22-pixel lane as its slider.

- Fixed Locked Hour photo row sizing: dependencies are declared before its style, and the row explicitly uses the native sliders' 72-pixel height and spacing.

- Photo Mode uses a temporary time-lock toggle and a 00:00–23:45 hour slider, in 15-minute steps, replacing fixed time presets. It restores the captured clock on exit.
- Native sampling resets when these controls change. Clock state and addon persistence remain separate.

- Changed the English pause prompt to `Return to garage`.
- Added isolation for the separate Weather Control options page so normal Video settings are not appended to it.
- Weather Control is an optional plugin; see [WEATHER_CONTROL.md](WEATHER_CONTROL.md).

- Added a separate fingerprinted Azimuth v1.0.3 profile; previous profiles remain supported.
- Both adapters now identify the author, adapter and intended purpose in VERSIONINFO.
- Repeatable unsigned builds and native compatibility fixtures passed. Antivirus clearance is not claimed.

- Removed the experimental aero startup popup. Its warning remains in the log.
- Garage revving and AllEngineSwaps audio behavior are unchanged in this release.

- Added a separately fingerprinted v1.0.2 startup/save profile; v1.0.1 support is retained.
- The native mapper, TLS setup and gameplay hooks retain their existing behavior.
- Checks the **loaded package's parsed title ID and executable**, independent of
  the launcher's saved selection. The game path comes from that loaded package.
- Missing save commits can be generated from actual blob sizes and hashes.
  Native save validation, parsing, cleanup and writing remain unchanged.
- Removed the minizip/hash asset override experiment, including `assets.json`
  discovery, texture hooks, extra asset mounts, thread-local aliases and per-file
  hash lookups. Those files are ignored by this release. Normal `media/` and ZIP
  mod loading, carbin/database/string patches and mod audio banks remain supported.
- Fingerprints the host once at startup and restores the temporary handoff before
  initializing Extra Options. No new per-frame worker, scan or polling was added.
- Removed “Azimuth winmm proxy” from the log heading.
- Consolidated documentation here in `source/docs`.

The current inspected Azimuth v1.0.2 executable is 9,715,712 bytes, SHA-256:
`42527964f48c90a537f2414200b8598ab9a6f92919bfbe046dbecccb36e7c597`.
v1.0.1 is also supported with its own exact fingerprint. Other host builds are
refused safely. See the adapter notes for both profiles.

## Settings and modding

Use the game's Video, HUD, Audio and Difficulty pages for in-game controls;
those choices are stored in `FH2_ExtraOptions.dat`. Startup options and bindings
remain in the INI. The experimental all-engine, all-transmission and aero tuning
options remain disabled by default under `[Experiments]`.

- [Addon loading and database/string JSON](MODS.md)
- [Carbin JSON patches](CARBIN.md)
- [DLL plugins](PLUGINS.md) and [plugin API](PLUGIN_API.md)
- [Diff tool and drag-and-drop options](MOD_DIFF.md)
- [Build and validation commands](BUILD.md)
- [Source layout](SOURCE_GUIDE.md)
- [Azimuth v1.0.1 handoff](AZIMUTH_NOTES.md) and [save compatibility](AZIMUTH_SAVE_NOTES.md)
- [Validation results and limits](VALIDATION.md)

Older investigation records and release history are retained in `history/`.
They describe the versions named in those records, not the current adapter.

Private native fixtures validate hooks and file loading without running the
launcher, GPU or game entry point. A full v1.0.3 gameplay launch is still needed
for visual, audio and long-session confirmation. No FPS improvement is claimed.
