## BUGS
### I personally encountered
- Add-on car decals draw their transparent part black (user report 2026-11-27, Honda #9; also the E21 on non-black paint)
  - **Fixed for mask decals (2026-11-27, verified on both sides of the Honda).** FH3 applies a decal's opacity mask
    only in its mask layout: `0xe159d67b` on, `0x7169ac81` off, the FM5 `_opac` texture kept in `0x66e53f62`, and the
    decal colour in `0xc0cb2820` (the paint colour parameter of FH3's library livery materials; without it the
    decals come out pale lavender). In the full-colour layout the game ignores the texture's alpha, so every
    rewrite I tried there (baked alpha in RGBA8 or BC7, other switch combinations) gave boxes. Now the converter default.
  - **Still open:** full-colour sheets with no mask, such as the E21's gold lettering. They keep black boxes on any
    paint but black.
  - **Imported FH2-native cars hang the game** (tried the Nissan 370Z G-Shock: blank showroom, freeze on purchase);
    cause not found, the test mod was removed.
- Honda #9 Target DW12: grey body with white blocks instead of the Target livery (user report 2026-11-27)
  - **Fixed (2026-11-27, verified by my screenshots: red body, white panels, Target bullseyes, sponsors, #9).**
    Three converter causes. The 32 px red and white textures of the livery were "colour tiles" that the
    converter turned into plain grey paint (rule now off). The sponsor logos are FM5 *mask* decals (an `_opac`
    BC4 mask tinted by the slot colour), a mode FH3's shader lacks, so they drew as solid white panels; they
    are now baked into real premultiplied RGBA textures (`MaskBake.cs`). Slots that kept FM5's `_opac` in the
    second texture slot get FH3's default gloss map instead. FH3 blends decals premultiplied, so a bake with
    the tint left in transparent texels drew solid panels (the red diagnostic run proved it).
  - Still grey: front wing endplates and the lower sidepods, which are paint zones mapped to FH3's generic
    carpaint. Mapping FM5 zone colours to FH3 colours is a separate change.
  - Hands: formula pose tuned on the E21; DW12s converted with `--hands-lower 0.09` as a first guess,
    not checked in cockpit view (the pad's View button opens the map, Y rewinds).
  - Third round: "the decals' transparent part is black" (front wing endplates black with logos, FM5 shows
    them white). The colour-slots diagnostic build showed every panel surface is paint zone and the livery
    is decals on top, so the fault was the paint under the decals: every non-dark zone had been mapped to
    FH3's generic grey carpaint. `CarColors.cs` now picks the FH3 manufacturer preset nearest the FM5 zone
    preview colour (Honda #9: `britered` body, `pearlwhitelfa` wings); FM5 solid-fill slots (solid switch on,
    own colour, no texture) get a 4x4 fill texture instead of turning into paintable carpaint. Verified by
    my screenshot against the FM5 reference: red body, white endplates, all decals.
  - Second round (same day): the nose and hood still showed white blocks. Those slots have the white 32 px
    tile as colour texture *and* an `_opac` mask; the bake now covers them too (tile colour from its first
    BC7 block). 87 decals baked on the Honda; nose dashes and sponsor logos verified by my screenshot.
- Crash at the main menu, `game+0x1a51399` reading -1, six sessions in a row (2026-11-27 01:18 to 01:37)
  - **Not Extra Options and not the car pack.** The same crash came with the previous car zips and with
    the approved E21 swapped back. It arrived with the user's update of the Underglow platform mod and
    went away in the first session without it. I first blamed FH3_AutovistaLayer (installed at the same
    time); that was wrong. Lesson: compare the per-session module list in `data\logs\azimuth.log`
    (`modifications:` line) and `fh3-mods\platform-mods.txt` before bisecting files.
- Add-on cars had no engine sound (user report 2026-11-26)
  - **Fixed (2026-11-26, heard by the user and measured).** FH3 picks a car's engine bank through
    `media\audio\cars\CarEngineEventMapping.xml` (`<Engine mediaName="<car>" engineName="<engine>" />`);
    a car that isn't listed gets `fallback` = `NUL_Car_00` = silence. FH2/FM5 banks are FMOD bank
    version 46, FH3's FMOD wants 78, and no converter exists. A copied FH3 bank under the add-on car's
    name does nothing either, because the mapping, not the file name, decides. Extra Options now writes
    the game's mapping plus one line per add-on car (donor from the mod's `mod.json`
    `"fh3_engine_audio"`) into `__eo_fh3_media__\_eo_manifest\media\audio\cars\` and mounts it above
    the mods; that alone is enough, `CarEventMapping.xml` is not needed. Verified with the title
    screen's Drive rev (menu peak 0.05 to 0.47 on Drive) with the game's own files untouched.
  - Test lesson: at the title screen START does nothing; A leaves the title, then A on Drive plays the
    rev. Pressing Play on the Azimuth launcher 8 s after start is too early; retry until the log exists.
- Crash at boot (game+0x18dda9a, null read) as soon as a mod whose folder name contains a space
  has `"fh3_cars": true` (e.g. converted zips placed directly in `ZS FM5 Formula Racing Pack`)
  - **Fixed (2026-11-26, tested in game).** The crash dump showed the game's zip manifest loader
    (0x18dd6d0) opening the first listed mod zip, `game:\media\cars\che_12_dw12_13.zip`, getting a
    null handle back and dereferencing it. The zip was served by the mod's media volume, mounted from
    `game:\__eo_fh3_media__\ZS FM5 Formula Racing Pack\media\`; the same files under a folder name
    without spaces loaded fine. Loose files through such a volume never showed the problem, archive
    opens do. The mirror folder is now the mod name with anything but letters, digits, `.`, `_`, `-`
    turned into `_` (`ZS_FM5_Formula_Racing_Pack`); mod folders keep their names. Stale mirror
    folders (renamed, removed or disabled mods) are deleted on the next start, with a log line each.
- Return to Garage during Races will crash the game after exiting from Garage
  - **Fixed.** Return to Garage entered the festival hub in the middle of an event, which the game can't
    come back from (it crashed on leaving the garage). In an event it now quits the event first, as the
    pause menu's Quit Event tile does (`0x1689f60(page, quit_event action 0x4e32678, NULL)`), waits until
    the player has been back in free roam for 4 s, then enters the garage. If the event hasn't ended within
    15 s (the quit was cancelled), it gives up.
  - **Tested in game:** in free roam Return to Garage works as before; in a race it quits the race, then
    opens the garage (log: `GARAGE: quitting the event first.`, then `GARAGE: entering the nearest
    festival site.`), with no crash on leaving the garage.
  - How it knows: the same state the HUD's mini leaderboard shows `InEvent` by: the local player
    (`CPlayerManager` [0x4d6fc00], `[[+0x38]]+0x10`) → its `CLobbyPlayer` (+0x9b8) → state byte +0x960.
    3, 4, 5 and 7 are in an event. Measured in game: 2 in free roam, 4 in a race.
  - The pause menu's X label is set once, when the pause menu is first built, so it can't change between
    free roam and a race; that's why the button does the right thing in both instead.
- While in garage upgrading car, the menu is a little stutter
  - **Gone in testing.** With Show FPS off it went away; afterwards it couldn't be reproduced with Show FPS
    on and Show GPU Usage off either. If it comes back, the suspects are the FPS readout's GPU usage
    (read from Windows' performance counters once a second) and its twice-a-second text refresh on
    every page that shows it.
  - **Fixed meanwhile:** with the readout off, pages were still told twice a second that its (empty)
    text changed. Now they're told once, when it's switched off.

### Other users reported
- Some users reported Exit Photo Mode in Races loading unexpectedly long (https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/issues/141)
  - **Livery theory ruled out (2026-11-25).** The reporter's second log (0.6.1) has no `LIVERY:` line at
    all, so the livery feature was off, and the FPS readout and photo camera options were off too. The
    one-minute loading still happened. Only in races; free roam photo mode is fine.
  - **Next suspect: the NoPause plugin.** Issue 144 (also race-only) was confirmed to be NoPause, and
    the 141 log shows NoPause switching to its unfocused XInput feed during the session
    (`NoPause: feeding XInput (1 pad) into unfocused readings.`), i.e. something other than the game
    window was in front at some point (recording tool, overlay, notification). The reporter also has
    RivaTuner (RTSSHooks64.dll) and Kaspersky (klhkum.dll) injected into the game.
  - **Reply to the reporter:** thanks, that rules the livery rendering out. One more test please: set
    `KeepRunning=0` in `mods\Forza Horizon 3\NoPause\NoPause.ini` and race again. The other race-only report (#144) turned out to be that
    plugin, and your log shows it took over the pad once because another window came in front. What was
    on screen then: the recording software, RivaTuner's overlay, a notification? If it still happens
    with NoPause off, set `Enabled=0` under `[Global]` with winmm.dll still in place, so we know whether
    Extra Options is involved at all.
- Intermittent complete controller input loss (https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/issues/144)
  - **Confirmed NoPause (2026-11-26):** the reporter set `KeepRunning=0` in `NoPause.ini` and finished
    three races without input loss. Workaround until fixed: keep it at 0.
  - **Where it can go wrong:** `hooked_reading` replaces the game's `IGamepad::GetCurrentReading` result
    with the merged XInput state of every pad whenever the real `GetForegroundWindow` isn't the game
    window. It overwrites even when the real reading already carries input and the XInput readings
    are neutral: a pad the game drives through Windows.Gaming.Input but XInput doesn't see (Steam
    Input, DS4Windows and some wheels present a different device to XInput), or XInput reporting an
    idle second device, gives all zeros for as long as some other window is "in front" (toasts,
    Game Bar, RTSS, Kaspersky popups), which in a race means a dead car.
  - **Proposed fix (not done):** only substitute when the real reading is neutral (no button, trigger
    or stick input) and the merged XInput state isn't; otherwise return the game's own reading. Log the
    first substitution with which pads XInput saw. Verify in game by alt-tabbing during a race with the
    pad connected over Bluetooth.
  - **Reply to the reporter:** thanks for confirming. Keep `KeepRunning=0` for now; the plugin's
    background-input feed will be made to only step in when the game's own pad reading is empty.

### Not bug, but somebody ask this and IDK how to answer
```
is there a very specific way addon cars need to be setup for fh3
i dont see a loadorder system anywhere
get messages like these in the log:
MODS: ZS FM5 Common media mounted.
MODS: ZS FM5 Formula Racing Pack media mounted.
MODS: ZS FM5 GTX Racing Pack media mounted.


but none of the modded cars ever show up in the autoshow
```

**Answer:**
**Load order (done 2026-11-26):** FH3 now has the same `loadorder.txt` as FH2 and FM5, in
`mods\Forza Horizon 3`. It is created on the first run with every folder listed, new folders are
appended, missing ones removed, `#` in front of a name disables it. Before this build FH3 only read
the file if the user made one, which is why "I don't see a loadorder system anywhere".

### Add-on cars (FH2 / FM5 packs) in FH3

#### A friend's log: "car zip not accepted by the game"

```
MODS: mod car zips use an installed content chunk.
MODS: the game's zip manifest is loaded; mounting mod car zips.
MODS: car zip not accepted by the game: game:\media\cars\CHE_12_DW12_13\
...
DIAG volume slot 0x0e8 cars\LOT_E21_13\ @01c55275
DIAG exists 0: game:\media\cars\LOT_E21_13\ f76494be
```

- `car zip not accepted by the game` is an old experiment still in that build: the pack zips are offered to
  the game's archive-mount call and refused. Harmless, not the load path; to be removed.
- `DIAG ...` lines are temporary file-system diagnostics (every access whose name contains `E21`).
  `exists 0` for `media\cars\LOT_E21_13\` is normal: the game asked for a loose folder, the car is a zip.
- Nothing in that log is a failure. The cars stay invisible because the DLL alone can't show them: the
  pack's zips are FM5/FH2-format files and must be converted, and converted zips must be listed in a zip
  manifest. Both live in a mod folder (test mod `zzEOCarTest`: converted car zips, converted tyre assets,
  `media\zipmanifest.xml`). Send that folder with the DLL and the ZS packs.

#### Converting an FH2 / FM5 car pack to FH3, in plain words

The idea: an FH2 car pack already has the right *data* (database, names). Only the car *files* (the zips)
are in the older game's format, so they go through a converter once. The converted copies go into a new
mod folder, next to a copy of the pack's data files. The original pack is never touched.

You need, installed once:
- Extra Options (the current build).
- ForzaTech Studio (free modding tool; the converter borrows its libraries). Default folder
  `D:\Games\FH2\ForzaTech.Studio`; anywhere else works with `--studio "<folder>"`.
- `fh3cars.exe` (single file, nothing else to install; built from `tools\fh3-cars\converter` with
  `dotnet publish`, output in `tools\fh3-cars\dist`).

Then, for a pack, one command in a command prompt (Win+R, `cmd`):

```
fh3cars pack "<pack folder>" "<mods\Forza Horizon 3\NewFolder>" --fh3 "<FH3 Mount folder>"
```

- `<pack folder>` is the FH2 pack (the folder with `mod.json`), it is only read.
- The new folder name must not contain spaces, e.g. `ZSFormulaFH3`. The tool creates it, copies the pack's
  data files and thumbnails, converts every car zip (about a minute each), converts FH3's own old tyre
  files, and writes the zip list the game needs (`media\zipmanifest.xml`).
- `<FH3 Mount folder>` is the FH3 install folder that contains `media` (`--fh3` can be left out when the
  game is at the default `D:\Games\FH2\Opus_...\Mount`). Add `--fh2 "<FH2 Mount folder>"` if FH2 is
  installed; it is used to copy materials FH3 doesn't have.

Start the game; the cars are in the Autoshow under their manufacturer.

Cars straight from a Forza Horizon 2 install (89 are missing in FH3) need no pack:

```
fh3cars fh2 "<mods\Forza Horizon 3\FH2Cars>" TOY_2000GT_69 MER_SLR_05 --fh2 "<FH2 Mount folder>" --fh3 "<FH3 Mount folder>"
```

(`--all-missing` instead of car names takes all 89; `--name "..."` sets the mod's display name.)

Several car mods at once are fine: Extra Options builds the game's zip list itself at start-up from every
mod whose `mod.json` says `"fh3_cars": true` (the tool writes that line; it marks the zips as converted,
unconverted FH2/FM5 zips would hang the game and are skipped with a note in the log). Single pieces:
`fh3cars car in.zip out.zip`, `fh3cars tyres "<mod>"`, `fh3cars manifest "<mod>"` (only needed with an
Extra Options build older than this one); `fh3cars help` lists everything.

What you get: the car drives, with livery, paint, wheels, working steering wheel and a driver. What is
still missing: engine sound, Autoshow thumbnails (unless the pack brings them), and the driver's hands sit
only roughly on the wheel. If a part shows up black, the converter log names the missing material; it gets
a replacement in `MaterialMap.cs` and a new build of the exe. Full details: docs/FH3-car-conversion.md.

#### How Extra Options reads such a mod

1. **Discovery.** `mods\Forza Horizon 3\*\mod.json` (fh3/fh3_mods.c). Each mod's `media` folder is
   mounted as a mirror over `game:\media\` after the game's file system is up. Same-path files replace the
   game's; new files are visible too, but only when the mod folder name has no spaces (FH3 quirk).
2. **Database.** Mods with `database.json` are queued (fh3/fh3_addons.c). When the game opens its
   database, Extra Options copies it, applies each pack in dependency order inside a savepoint (FH2 packs
   translated first: FH2 string references rewritten, FH3-only columns filled with the declared default
   or FH3's most common value, the car's `List_Wheels` row synthesised because FH3 crashes without it,
   FH2 `update`/`delete` operations dropped as they name FH2 rows), checks foreign keys once, and points
   the game at the copy; on failure the original database is kept.
3. **Strings.** `strings.json` entries are added to the game's string tables as each table loads, so names
   show in the player's language with English fallback.
4. **Zip manifest and car zips.** FH3 mounts only car zips listed in `media\zipmanifest.xml` with their
   central-directory position. At start-up Extra Options reads the game's manifest, adds every zip under
   `media\cars` of each mod whose `mod.json` has `"fh3_cars": true` (replacing entries of zips a mod
   overrides, e.g. the library tyres), writes the result into its media mirror (`_eo_manifest`) and mounts
   it above all mods. Mods with car zips but without the flag are skipped with a log line: those zips are
   taken to be unconverted FH2/FM5 files, which hang the game.
5. **Other overrides in the mirror:** converted tyre assets (`media\cars\_library\scene\tires`),
   thumbnails (`media\stripped\RC0`), audio banks (`media\audio\fmodopus\xboxone`, loaded but silent).
6. **Rendering** is the game's own from there: `Data_Car.ModelId` selects the zip, `ManufacturerColors.bin`
   the paint, carbin/modelbins the parts, the Granny skeleton the wheel and suspension animation, the
   `scene\Drivers\male|female` files the player model. Those files are what the converter produces.

For a mod author: folder name without spaces, `mod.json` with a unique `id` and `database`, FH2-format
`database.json` and `strings.json`, converted zips named after the car, one manifest covering all
installed car zips.

## Add-on cars: tail lights and headlights stay dark (open)
- FH3 cars ship `Lights.bin` and `LightPresets.bin`; FM5 zips have neither and `fh3cars` writes none, so no light logic is attached to the light models of converted cars.
- `Lights.bin`: magic 0xDEADBEEF, version 1, entry count, then per entry `u32 length + game:\media\cars\<car>\scene\...\<part>.modelbin + u16 flags`, then a binary payload of light points (32-byte elements: position in car space + normal) grouped by light channel. The channel is the `Int` material parameter (0x493073a0 on `gls_lightbulb`, 0x19416cfe on `lights_ch1bump_ch1lmask`): FH3 uses 0, 1, 13, 17, 18, 32, 48, 69, 70, 71, 72. FM5 uses other values (25 on the DW12 bulb). The payload framing is not decoded yet (headers are not a fixed record size).
- Tried and did not light the DW12 rear light: (a) FH3 `exterior_litglass\gls_lightbulb` with Int 70 and red (panel went black), (b) the BAC Mono rain light material `lights_ch1bump_ch1lmask` (Int 48, red, grid texture; panel textured, not lit). Both are off by default; `FTCAR_LIGHTBULB=on` enables (b).
- Next step if continued: decode the payload using several small FH3 files (FER_599XX_10 is 1.3 KB, BAC_Mono_14 has a matching `taillightC_a`), write a `Lights.bin` containing only the car's own light models with the Mono payload, test in game.
