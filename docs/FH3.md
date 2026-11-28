# Forza Horizon 3 on Azimuth

The FH3 profile is based on [PR #134](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/pull/134).
Build with `--azimuth --fh3`. This can be combined with `--fm5`.
The adapter reads the launched package's Title ID. Only `4CE1DFAB` reaches
FH3 initialization, which verifies the FH3 1.0.125.0 executable and patch
signatures before enabling any hooks or loading packages.

The Extra Options settings, ANNA car commands and menu prompts follow the
game's language, including the translations from
[PR #137](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/pull/137).
The minimap can follow the camera during gameplay while the pause menu's
World Map tile keeps its native orientation.

Place `FH3_ExtraOptions.ini` beside `winmm.dll`. FH3 writes to its own
`FH3_ExtraOptions.log`. Missing Azimuth save commits can be generated using
the shared compatibility code. Azimuth still validates saves.

Place packages in `mods/Forza Horizon 3/<ModName>/` next to Azimuth. Each
package can contain a loose `media` tree and a `plugins` directory.
`mods/Forza Horizon 3/loadorder.txt` works as in FH2 and FM5: the loader
creates it on the first run, lists one folder name per line (UTF-8, 64 KiB at
most) and appends new folders alphabetically on every start; entries whose
folder is gone are removed. Packages load in file order and later entries win.
Put `#` in front of a name to disable that package without deleting it; the
log then says `<ModName> disabled in loadorder.txt`. A duplicate or invalid
entry disables package loading for that run, and the log says why.
`<ModName>=0` in the INI's `[Mods]` section also keeps a package off.

Native mount priority is `mediaoverride`, FH3 mod media, `mediapc`, then
original game media. FH3 stages loose mod media in the game's
`Mount/__eo_fh3_media__` directory using hard links where possible and copies
otherwise. Edit the original package files. The mirror folder uses the mod
name with every character other than letters, digits, `.`, `_` and `-`
replaced by `_` (a space in a `game:` path makes the game's zip manifest loader
crash), and folders of mods that are gone or disabled are removed from the
mirror on the next start.

Packages can also carry FH2's `mod.json`, `database.json` and `strings.json`.
A car pack's `mod.json` can name an FH3 engine sound for each add-on car
(`"fh3_engine_audio": {"<car>": "<FH3 car>", "*": "<default>"}`); see
`FH3-car-conversion.md`. Without it an add-on car is silent.
The database rows go into the in-memory copy of `gamedbRC.slt` that the game
opens (the file on disk never changes), one savepoint per package, followed by
SQLite's integrity check; `dependencies` must load first. Strings are added to
the game's string tables as they load. Packages made for FH2 are translated:
their `_&` name references point at FH3's string tables, rows FH3 already has
(makes, shared lists) keep FH3's data, a clashing row of the package's own car
gets a free ID, FH3-only columns take FH3's most common value, and
`update`/`delete` operations are left out because they name FH2's rows. Set
`"game": "FH3"` in `database.json`'s `target` to skip the translation.
FH3 re-exported every car (Bundle v1.1 models and textures, its own carbin,
lights and library shaders), so FH2 and FM5 car models are not drawn yet: such
a car is listed and priced, and the game shows its fallback model.

The FH3 plugin SDK is `source/fh3/fh3_plugin.h`. API version 2 adds
`set_hud_hidden` (hide the HUD, optionally behind a letterbox) and
`set_hud_prompt` (a button prompt at the bottom of the HUD); version 3 adds
`set_anna_status` (ANNA's ring lit and a tag above her) and `set_anna_ready`
(her button shows D-pad Down where the game has her unavailable). The NoPause
and example plugin sources are optional projects
in `source/fh3/plugins`. The main DLL build does not compile those plugins.

ANNA's menu (D-pad Down while driving) is the
[FH3 ANNA](https://github.com/qiangqiang101/FH3-ANNA) plugin,
`mods/Forza Horizon 3/ANNA/plugins/ANNA.dll`: her Car list (Auto Drive, Lights,
Doors), Auto Drive with its cinematic camera, the headlight, hazard and indicator
overrides (with the D-pad hold) and the car's doors. Its settings are in
`mods/Forza Horizon 3/ANNA/ANNA.ini`, written on first start with the values
`FH3_ExtraOptions.ini`'s former `[Vehicle]` section had. Other mods add entries
through its `fh3anna.h`. [Groove Music Radio](https://github.com/qiangqiang101/FH3-GrooveMusic)
brings back the radio's Groove station with your own music.
The FH3-specific ZIP repacker is in `source/fh3/tools/fh3_zip.py`.
