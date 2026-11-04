# Forza Horizon 2 - Extra Options
A quality-of-life mod for the Xbox One version of Forza Horizon 2 that adds several tweaks to the game.

Forza Horizon 3 (Azimuth) is now supported too, see [Forza Horizon 3](#forza-horizon-3) below.

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

# Forza Horizon 3
A separate build of the mod for the Xbox One version of Forza Horizon 3, running on Azimuth.

### Features
 - Built-in mod loader with plugin support.
 - "Jump to Manufacturer" grid on every car list (Autoshow, My Cars, Marketplace car packs...), opened with View or Backspace.
 - Social, Clubs, Rivals and Marketplace pause menu tabs available offline.
 - Autodrive (as seen in Forza Horizon 6), with an optional cinematic camera.
 - Open and close the roof, hood, trunk and doors from the ANNA menu.
 - Drift Camera (as seen in Forza Horizon 4/5/6).
 - FOV sliders for every camera type.
 - FPS counter with optional GPU usage and frame time.
 - Toggle for having the minimap follow the orbit camera.
 - "mediaoverride" and "mediapc" modding folder support.

### Enhancements
 - Skip developer opening videos.
 - VIP status & "Treasure Map" unlocks.
 - Every car hidden from the Autoshow can be bought, including Forzathon prizes (Horizon Editions optional).
 - All Forzathon horns unlocked.
 - Every Horizon Edition car can be won in Wheelspins.
 - Overides for 4K liveries w/ enhanced sampling.
 - Unrestricted photo mode.
 - Bypass for Azimuth's save data checks.

### Plugins
These come as mods and are optional:
 - **No Pause**: the game keeps running, and keeps reading your controller, when it loses focus.
 - **Groove Music Radio**: brings back the Groove Music radio station with your own music: the Windows Music folder, Spotify (Premium), YouTube Music, QQ Music and NetEase Cloud Music. Settings are in "GrooveMusicRadio.ini", which is created on first start.

### Installation
 - Extract everything from the "fh3-azimuth" ZIP to Azimuth's main folder where "Azimuth.exe" is located: "winmm.dll", "FH3_ExtraOptions.ini" and the "mods" folder with the two plugins.
 - Mods go in that "mods" folder, one folder per mod (for example "mods\NoPause"). Delete a plugin's folder if you don't want it, or turn it off in the INI with `<ModName>=0` under `[Mods]`.
 - Spotify in Groove Music Radio also needs Spotify's "soloist.exe" and your Soloist API key; see "GrooveMusicRadio.ini".

### Information
Most of the features can be toggled and tweaked inside the game through the HUD and Video settings, or in "FH3_ExtraOptions.ini".

#### Autodrive
Autodrive is engaged by pressing L3+R3 on a controller or F2 on a keyboard, or from the ANNA menu (D-Pad Down). Throttle, brake or steering hands the car back to you.

#### Car parts
Open the ANNA menu (D-Pad Down) and choose "Car" to open the roof, hood, trunk or doors. Parts only open while the car is stationary and close again once it moves.

#### FPS counter
The FPS counter is off by default. Turn it on in Settings > HUD and Gameplay, where you can also add GPU usage and frame time and remove its background.

#### Forzathon cars
The hidden cars, Forzathon horns and Wheelspin changes are made in memory when the game starts; the game files are never changed. Horizon Edition cars are only won in Wheelspins by default. Set "UnlockForzathonHorizonEditionCarsInAutoshow" to 1 in the INI to also sell them in the Autoshow.

#### Treasure Map
The treasure map is disabled by default. Open the INI and set "UnlockTreasureMap" to 1 if you wish to have it.

# AI Usage Disclosure
Codex was used in this project to create the DLL. The actual research was done manually, mostly based on things I already toyed with in the other titles from the series, but I don't yet have the C language knowledge to code something like this fully from scratch (I was initially editing the raw executable). Leaving this here as I believe people have the right to know.
