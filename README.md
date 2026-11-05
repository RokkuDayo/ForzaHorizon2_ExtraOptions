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
 - "Return to Garage" on the pause menu's X button: enter the nearest festival site's hub (garage, upgrades, paint) from anywhere.
 - Autodrive (as seen in Forza Horizon 6), with an optional cinematic camera.
 - Headlights, hazard lights and indicators for every car, from the ANNA menu or by holding D-Pad Left/Right.
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
 - Horn Accelerator add-on unlocked.
 - Every Horizon Edition car can be won in Wheelspins.
 - Overides for 4K liveries w/ enhanced sampling.
 - Unrestricted photo mode.
 - Xbox Live parental controls no longer block multiplayer and user-created content.
 - Bypass for Azimuth's save data checks.

### Plugins
These come as mods and are optional:
 - **No Pause**: the game keeps running, and keeps reading your controller, when it loses focus.
 - **Groove Music Radio**: brings back the Groove Music radio station with your own music: the Windows Music folder, Spotify (Premium), YouTube Music, QQ Music and NetEase Cloud Music. Settings are in "mods\GrooveMusicRadio\GrooveMusicRadio.ini".

### Installation
 - Extract everything from the "fh3-azimuth" ZIP to Azimuth's main folder where "Azimuth.exe" is located: "winmm.dll", "FH3_ExtraOptions.ini" and the "mods" folder with the two plugins.
 - Mods go in that "mods" folder, one folder per mod (for example "mods\NoPause"). Delete a plugin's folder if you don't want it, or turn it off in the INI with `<ModName>=0` under `[Mods]`.
 - Spotify in Groove Music Radio also needs Spotify's "soloist.exe" and your Soloist API key; see "GrooveMusicRadio.ini".
 - When updating, keep your own "GrooveMusicRadio.ini" (and the other INI files) instead of the new ones, or your settings and API key are replaced with the defaults.

### Information
Most of the features can be toggled and tweaked inside the game through the HUD and Video settings, or in "FH3_ExtraOptions.ini".

#### Autodrive
Autodrive is engaged by pressing L3+R3 on a controller or F2 on a keyboard, or from the ANNA menu (D-Pad Down, then "Car"). Throttle, brake or steering hands the car back to you.

#### ANNA "Car" menu
Open the ANNA menu (D-Pad Down); "Car" is first in the list. Select with D-Pad Right:
 - "Autodrive" starts or stops Autodrive.
 - "Lights" toggles the headlights, hazard lights and left/right indicators.
 - "Doors" opens and closes the roof, hood, trunk or doors (or all at once). Parts only open while the car is stationary and close again once it moves.

D-Pad Left goes back from "Lights" or "Doors" to the "Car" menu.

#### Indicators
Hold D-Pad Left/Right for 1 second to turn that indicator on or off; a short tap still changes the radio station. Indicators turn off once a turn is made and the steering is centered.

#### Return to Garage
The pause menu's X button (which used to open Help) reads "Return to Garage". It closes the pause menu and enters the nearest festival site the same way the game does when you join an online session: a loading screen, then the festival's menus (Autoshow, Garage, upgrades, paint...). When you leave, you drive out at that festival. Set "ReturnToGarage" to 0 in the INI to keep the Help button.

#### FPS counter
The FPS counter is off by default. Turn it on in Settings > HUD and Gameplay, where you can also add GPU usage and frame time and remove its background.

#### Forzathon cars
The hidden cars, Forzathon horns and Wheelspin changes are made in memory when the game starts; the game files are never changed. Horizon Edition cars are only won in Wheelspins by default. Set "UnlockForzathonHorizonEditionCarsInAutoshow" to 1 in the INI to also sell them in the Autoshow.

#### Horn Accelerator
The Horn Accelerator add-on is reported as owned and shown as purchased in Extras. Set "UnlockHornAccelerator" to 0 in the INI to turn this off.

#### Parental controls
"DisableParentalLocks" (on by default) stops the game's Xbox Live privilege checks from blocking multiplayer and user-created content. It does not bring back online content: "Find New Designs", Storefront and other shared designs still need the Forza Horizon 3 servers and a real Xbox Live sign-in, so on Azimuth they show "The Forza Horizon 3 server is not available at this time".

#### Treasure Map
The treasure map is disabled by default. Open the INI and set "UnlockTreasureMap" to 1 if you wish to have it.

# AI Usage Disclosure
Codex was used in this project to create the DLL. The actual research was done manually, mostly based on things I already toyed with in the other titles from the series, but I don't yet have the C language knowledge to code something like this fully from scratch (I was initially editing the raw executable). Leaving this here as I believe people have the right to know.
