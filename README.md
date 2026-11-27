![Key art](https://i.imgur.com/KVz8DvA.png)

<div align="center">
A quality-of-life mod for the Forza titles released on the Xbox One.
</div>

# Forza Horizon 2

Wiki page: https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/wiki/1.1.-Features-%E2%80%90-Forza-Horizon-2

## Features
 - Mod loader with addon and hooking support.
 - "mediapc" modding folder support.
 - "Jump to manufacturer" on car and rim menus.
 - "Return to garage" button in the pause menu.
 - Time of day and weather control in the pause menu.
 - Change your car anywhere (as seen in Forza Horizon 3/4/5/6).
 - Drift camera (as seen in Forza Horizon 4/5/6).
 - Autodrive (as seen in Forza Horizon 6).
 - Expanded photo mode controls (Time, weather, driver visiblity, vehicle lights).
 - All one-off races from online available offline.
 - Ability to rev your engine in the engine swap menu.
 - FOV sliders for every camera type.
 - Roof toggle for supported cars during free-roam.
 - Lights, hazards, and turn-signal toggles for all cars.
 - New rim style category with all stock rims.
 - Driver model selector
 - Custom spawn locations.
 
## Tweaks
 - Skip for the developer opening videos.
 - All DLC packs properly unlocked.
 - VIP status & "Treasure Map" unlocks.
 - Overrides for 4K liveries w/ enhanced sampling.
 - Drivatars can now use liveries from your local library.
 - New rim style category with all aftermarket rims.
 - Toggle for having the minimap follow the orbit camera.
 - "Cars" tab moved to the right of the "Home" tab.
 - Unrestricted photo mode.
 - Stock rims can now be resized.
 - Choosing "Yes" in the "Leave Autoshow" prompt now places you inside the newly bought car.
 - The left mirror on the SRT Viper ACR GTS has been fixed to no longer use the material for the right mirror.
 - Choosing "Yes" in the "Exit Autoshow" prompt will now place you in the newly purchased car.

## Experiments (Unstable; Must be enabled manually.)
 - Add all engines to the engine swap list.
 - Add all transmissions to the transmission swap list.
 - Aero tuning without equipping Forza Aero parts.

# Forza Motorsport 5

Wiki page: W.I.P.

## Features
 - Mod loader with addon and hooking support.
 - "mediapc" modding folder support.
 - "Jump to manufacturer" on car and rim menus.
 - New rim style category with all stock rims.
 - Drift camera (as seen in Forza Horizon 4/5/6).
 - Expanded photo mode controls (Focus ring is now properly hidden, driver visiblity, vehicle lights, reset visual damage).
 - FOV sliders for every camera type.
 - Volume sliders for each audio channel.
 - Restored cut Zender and Lorinser rims.
 - Driver model selector
 - Your Gamerpic as an added badge emblem.
 - E3 start screen as an option.
 - Light toggles for all cars.

## Tweaks
 - Skip for the developer opening videos.
 - VIP status unlocked.
 - Overrides for 4K liveries w/ enhanced sampling.
 - Motion blur is now rendered in the cockpit view.
 - Drivatars can now use liveries from your local library.
 - Fix for the missing AO texture on the Trial front bumper for the Supra RZ.
 - New rim style category with all aftermarket rims.
 - Stock rims can now be resized.
 - Unrestricted photo mode.

# Forza Horizon 3

Wiki page: https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/wiki/1.2.-Features-%E2%80%90-Forza-Horizon-3

A port of the Extra Options feature set by @qiangqiang101. Contains much of the same features and tweaks as Forza Horizon 2, with the addition of:

 - FPS counter with optional GPU usage and frame time.
 - Every car hidden from the Autoshow can be bought, including Forzathon prizes (Horizon Editions optional).
 - All Forzathon horns unlocked.
 - "Horn Accelerator" add-on unlocked.
 - Every Horizon Edition car can be won in Wheelspins.

# Forza Motorsport 6

Wiki page: W.I.P.

## Features
 - FOV sliders for all five driving camera types (Driver, Hood, Bumper, Chase, and Far Chase).
 - Independent vertical FOV adjustments from 50 to 67 degrees in 0.5-degree increments, with numerical readouts.
 - Motion blur intensity presets (Off, Short, and Long).
 - Separate toggle for rendering motion blur in the cockpit view.
 - Native HUD options menu integration, with live adjustments and settings saved to `FM6_ExtraOptions.ini`.

## Tweaks
 - Motion blur is now rendered in the cockpit view when enabled.
 - Skip for the developer opening videos and startup logo parade.
 - Optional skip for the first-race presentation and the Porsche/NASCAR expansion introduction videos.
 - Optional DLC ownership-check override for previously owned content (disabled by default; does not install missing content or grant account entitlements).

## Notes
 - Short uses the stock motion blur intensity; Long uses three times the stock intensity. Custom intensity values from 0 to 400 percent can be configured in the INI.
 - Set a camera's FOV entry to 0 in the INI to restore its original field of view.
 - HUD camera and motion blur adjustments are saved immediately. Intro, presentation and DLC settings require a game restart.
 - FM6 uses `FM6ExtraOptions.dll` alongside the shared `winmm.dll` loader.
 - VIP support is not included at this time.
 - The camera and HUD integration was tested in-game in RC1 Hotfix 2. The additional RC2 startup tweaks still require runtime testing.

The intro, presentation and DLC tweaks are based on [Victor Alves' Forza Motorsport Extra Options](https://github.com/victoralvesf/ForzaMotorsport_ExtraOptions). The original MIT license notice is included with the distribution.

# Installation

Move `winmm.dll`, `FM6ExtraOptions.dll`, the `mods` folder, and all the INI files to the main directory of your Azimuth installation where `azimuth.exe` is located. Keep the included license notice with the distribution.

For FM6, remove any previous standalone Extra Options loader before installing the combined package. Back up your existing INI if you want to retain your camera settings.

Note: Add this command if you're running Linux and using Wine: `WINEDLLOVERRIDES="winmm.dll=n,b" %command%`
More information on Linux support here: https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions/pull/35#issuecomment-6233015259

# AI Usage Disclosure
Codex was used in this project to create the DLL. The actual research was done manually, mostly based on things I already toyed with in the other titles from the series, but I don't yet have the C language knowledge to code something like this fully from scratch (I was initially editing the raw executable). Leaving this here as I believe people have the right to know.


For the FM6 integration, ChatGPT was used to assist with code development, native camera and HUD integration, debugging, and documentation. In-game testing was performed by the FM6 contributor.
