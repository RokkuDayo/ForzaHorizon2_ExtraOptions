# Forza Horizon 2 - Extra Options
A quality-of-life mod for the Xbox One version of Forza Horizon 2 that adds several tweaks to the game.

# WARNING
An individual or group of people is trying to use Forza Horizon 2 and the mod's name to perform a scan that adds a keylogger to your computer. There is **NO** "All-In-One" installation tutorial and anything that requires you to insert and run things on your PowerShell is a **scam.**

# Features
 - Built-in mod loader with addon and hooking support.
 - Change your car anywhere.
 - "Jump to Manufacturer" on car and rim menus.
 - "Return to garage" button in the pause menu.
 - Weather control.
 - All one-off races from online available offline.
 - Autodrive (as seen in Forza Horizon 6).
 - Drift Camera (as seen in Forza Horizon 4/5/6).
 - Ability to rev your engine in the engine swap menu.
 - FOV sliders for every camera type.
 - Roof toggle for supported cars during free-roam.
 - Lights, hazards, and turn-signal toggles for all cars.
 - New rim style category with all stock rims.
 - Custom spawn locations.
 - "mediapc" modding folder support.
 - Custom language packs with their own fonts and voice subtitles (optional, off by default).
 
# Enhancements
 - Skip developer opening videos.
 - All DLC packs properly unlocked.
 - VIP status & "Treasure Map" unlocks.
 - Overides for 4K liveries w/ enhanced sampling.
 - Unrestricted photo mode.
 - Aero tuning without equipping Forza Aero parts.
 - New rim style category with all aftermarket rims.
 - Toggle for having the minimap follow the orbit camera.
 - Bypass for Azimuth's save data checks.
 - Custom Gamerpic support.

# Installation

The installation is different depending on the translation layer you're using.

Azimuth:

 - Extract all the 3 files from the "Azimuth" folder in this ZIP to Azimuth's main folder where "Azimuth.exe" is located.

Mezcal:

 - Extract all the 4 files from the "Mezcal" folder in this ZIP to "Forza Horizon 2\Mount" which is where "forza_durango_release_final.exe" is located.

Note: Add this command if you're running Linux and using Wine: `WINEDLLOVERRIDES="winmm.dll=n,b" %command%`
More information on Linux support here: https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/pull/35#issuecomment-6233015259

# Information

Most of the features can now be toggled and tweaked inside the game itself through the native menus.

### Treasure Map
The treasure map is disabled by default. Open the INI and set "UnlockTreasureMap" to 1 if you wish to have it.

### Autodrive
Autodrive is engaged by pressing D-Pad Down on a controller or F2 on a keyboard.

### Custom Languages and Fonts
Off by default. A language pack plays the game in a language it does not ship, with its own fonts and subtitles for the spoken lines, and leaves the game's archives untouched. The pack takes over one of the game's language slots:
```ini
[Language]
Custom=uk   ; the pack in <game>\Mount\languages\uk\ (letters, digits, - and _)
```
Then choose the slot's language (for the Ukrainian pack: Russian, `ru-RU`) in the Azimuth launcher. Other slots keep the game's own languages.

The pack holds only the game files it changes, laid out like `mediaoverride`: an entry `media\X.zip\a\b` goes to `languages\<id>\X\a\b` (or the pack ships a whole `X.zip`), a loose `media\a\b` to `languages\<id>\a\b`. It is mounted over the game's media below `mediaoverride` and above `mediapc`, so `mediaoverride` > language pack > `mediapc` > mods > original files.
```text
Mount\languages\uk\
  language.ini
  stripped\stringtables\RU\*.str                 ; media\stripped\stringtables\RU.zip
  stripped\stringtables\RU\VoiceSubtitles.str    ; optional: voice subtitles
  ui\Fonts\Anthem_A.vfont ... verdana.vfont      ; media\ui\Fonts.zip: the pack's fonts
  cars\<car>\Cinematics\*_subtitles_RU.xml       ; loose files keep their path
```
`language.ini` names the pack and the slot it takes over (save it as UTF-16 when the name is not plain ASCII):
```ini
[Language]
Name=Українська
Slot=ru-RU   ; en-US, en-GB, de-DE, fr-FR, it-IT, es-ES, es-MX, pt-BR, pl-PL, ru-RU, ja-JP, ko-KR, zh-TW, zh-HK, nl-NL, cs-CZ or hu-HU
```
A pack without `language.ini` or with an unknown slot is logged and left off.

**Fonts.** Extra Options ships no fonts; it lets a pack bring them. A `.vfont` in the pack's `ui\Fonts\` replaces the game's font of the same name from `media\ui\Fonts.zip` through the same mount, for all text the game draws with it. The game picks its fonts per language in `Fonts.zip\fontsettings.xml`, which a pack can replace the same way. The pack is mounted whatever language the launcher runs, so its fonts apply to every language: keep the game's glyphs in them.

**Cyrillic.** The game's fonts cover Russian, but only partly:
- `Anthem_A`-`Anthem_D`, the game's UI fonts, have Latin glyphs plus `Ё ё` and no other Cyrillic.
- For the `RU` language `fontsettings.xml` adds `verdana.vfont` as their fallback. It has the 66 Russian letters, so Russian letters fall back to Verdana.
- None of them has the Ukrainian `Є є І і Ї ї Ґ ґ`, nor the Belarusian `Ў ў`, the Serbian/Macedonian `Ђ Ј Љ Њ Ћ Џ ...`, the Kazakh letters and so on. Those characters are missing from the text.
- For apostrophes, the fonts have `'` (U+0027) and `’` (U+2019), but not `ʼ` (U+02BC), so a translation uses `’`.

A Cyrillic-script language therefore reuses the `ru-RU` slot. That slot already loads the Cyrillic fallback for every UI font and has Russian voice audio and subtitles to replace. The pack then ships the fonts with its extra letters. The Ukrainian pack ships `Anthem_A`-`Anthem_D` with the full Cyrillic set added (320 glyphs instead of 248, so text keeps the game's typeface) and `verdana` with the 8 Ukrainian letters added. In other slots (`*` in `fontsettings.xml`) the game loads no Cyrillic font at all, so a pack there must bring complete fonts or its own `fontsettings.xml`.

**Voice subtitles.** The game shows no subtitles for many spoken lines (Ben, Ashley, ANNA). A pack can add them in `stripped\stringtables\<archive>\VoiceSubtitles.str`, a game string table (LSB2), loose or as a stored (uncompressed) entry of `<archive>.zip`:
- The row id is the last path component of the voice event, and its text is shown in the game's subtitle box while the line plays.
- An optional `<id>~t` row gives the line's length in seconds. Lines are shown for that time plus 0.6 s, at least 2 s and at most 60 s, or 3.6 s without the row.
- The display time follows the system clock, not the frame rate.

Voice subtitles are installed only when the launcher runs the game in the pack's slot. On Mezcal, the Windows locale counts as that choice.

`tools/make_language_pack.py` builds a pack from a translation mod's replacement archives. It compares them with the original media and keeps only what changed. The Ukrainian pack is 389 changed files (about 14 MB, fonts and voice subtitles included) instead of whole replaced archives.

### Drift Camera
The drift camera can be toggled and tweaked in the HUD settings inside the game.
Cockpit drift camera is enabled by default.

### Vehicle Lights
Lights can be toggled by clicking the right-stick.

Hazards can be toggled by tapping D-Pad Up.

Turn signals can be engaged by holding D-Pad Left/Right for 1 second; They will de-engage once a turn is made and the steering is centered.

Note: Some cars have glitched turn signals. More info at the [mod's Wiki page.](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/wiki/4.-Oddities)

### Roofs
Roofs can be toggled by pressing and holding the right-stick for 3 seconds while stationary.

# Custom gamerpic 
You can edit the "Gamerpic.png" image located where you extracted the mod files to with any image you like and it will be used as your avatar in-game.
The maximum dimensions supported are 2048x2048 and the image must weight less than 16 megabytes.

# AI Usage Disclosure
Codex was used in this project to create the DLL. The actual research was done manually, mostly based on things I already toyed with in the other titles from the series, but I don't yet have the C language knowledge to code something like this fully from scratch (I was initially editing the raw executable). Leaving this here as I believe people have the right to know.
