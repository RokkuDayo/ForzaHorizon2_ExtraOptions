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
 - FPS counter and performance log (optional, off by default).

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

### FPS Counter
Off by default. Put `fh2-monitor.dll` in a `monitor` folder beside `FH2_ExtraOptions.ini` (Azimuth 1.0.2) and add:
```ini
[Monitor]
ShowFps=1   ; 0 off, 1 FPS, 2 FPS with frame times
PerfLog=0   ; 1 writes fh2-perf.csv beside the INI
```
`ShowFps=1` shows the FPS, GPU load and video memory in the top-right corner. `ShowFps=2` adds the frame time, the 1% low and the GPU frame time. `PerfLog=1` appends one line per second to `fh2-perf.csv`: FPS, average, 99th percentile and longest frame times, the 1% low and the GPU frame time. The counter is drawn by the game's own swap chain on its presenting queue and follows HDR brightness.

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
