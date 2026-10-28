# Forza Horizon 2 - Extra Options
A quality-of-life mod for the Xbox One version of Forza Horizon 2 that adds several tweaks to the game.

# WARNING
An individual or group of people is trying to use Forza Horizon 2 and the mod's name to perform a scan that adds a keylogger to your computer. There is **NO** "All-In-One" installation tutorial and anything that requires you to insert and run things on your PowerShell is a **scam.**

# Features
 - Built-in mod loader with addon and hooking support.
 - Change your car anywhere.
 - "Jump to Manufacturer" on car and rim menus.
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
 - Native keyboard & mouse (optional, off by default).
 
# Enhancements
 - Skip developer opening videos.
 - No repeated "controller disconnected" pause without a pad (optional, off by default).
 - All DLC packs properly unlocked.
 - VIP status & "Treasure Map" unlocks.
 - Toggle for 4K liveries w/ enhanced sampling.
 - Unrestricted Photo Mode.
 - "Return to Garage" button in the pause menu.
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

### Keyboard & Mouse
Off by default. Add this to `FH2_ExtraOptions.ini` to turn it on (Azimuth 1.0.2):
```ini
[KeyboardMouse]
Enabled=1
```
Keys answer the game's own input actions, so no virtual controller is involved and a pad keeps working. `Mode=Auto` (default) follows the last used device, `KBM` uses only the keyboard and mouse, and `PAD` leaves the gamepad path untouched.

Default keys follow the PC Forza titles: W/S throttle and brake, A/D steering, Space handbrake, E/Q gear up/down, Left Shift clutch, Tab camera, R rewind, Down or B look back, Enter/F/X activate, P photo mode, Left/Right radio, M map, Esc pause. In menus: Enter confirm, Esc back, arrow keys, Q/E tabs, X and Y buttons, Page Up/Down triggers, and Numpad 8/2/4/6 as the D-pad.

Every action above can be rebound with its name (`Throttle`, `Brake`, `SteerLeft`, `SteerRight`, `Handbrake`, `ShiftUp`, `ShiftDown`, `Clutch`, `Camera`, `Rewind`, `LookBack`, `Activate`, `PhotoMode`, `RadioPrevious`, `RadioNext`, `Map`, `Pause`, `Confirm`, `Back`, `Up`, `Down`, `Left`, `Right`, `TabLeft`, `TabRight`, `MenuX`, `MenuY`, `MenuLeftTrigger`, `MenuRightTrigger`, `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight`), up to three keys each, for example `Throttle=W,Up`. One key cannot drive two actions of the same group (driving or menus); such an INI is refused.

| Key | Default | Meaning |
| --- | --- | --- |
| `MouseLook` | 1 | The mouse turns the driving look camera. |
| `MouseLookPitch` | 0 | The mouse also looks up and down. |
| `InvertMouseY` | 0 | Inverts up/down mouse look. |
| `MouseCapture` | 1 | Keeps the mouse inside the window while driving. |
| `MouseSensitivity` | 80 | Mouse look speed (1-2000). |
| `MouseReturnDelay` | 1200 | Milliseconds before the look camera re-centres (0-60000). |
| `MouseReturnSpeed` | 240 | Re-centring speed (10-3600). |
| `SteeringSpeed` | 100 | Keyboard steering speed in percent (25-400). |
| `InvertSteering` | 0 | Swaps keyboard steering directions. |
| `Escape` | 1 | Esc pauses and goes back in menus. |
| `MenuCursor` | 1 | Mouse pointer in menus, the map, photo mode and the car viewer. |
| `CursorSize` | 250 | Pointer size in percent (50-800). |
| `MapCursorSpeed` | 400 | Map cursor speed in percent (50-800). |
| `MenuArrows` | Stick | Arrow keys act as the left stick (`Stick`) or the D-pad (`DPad`) in menus. |
| `Trace` | 0 | Logs every key and mouse event (diagnostics). |

With keyboard & mouse on and no pad attached, the game never pauses for a missing controller (see Controller-Missing Pause).

With keyboard & mouse on, the pause menu's Settings page also gets a **KEYBOARD & MOUSE** tile next to Controls. It opens its own page with the main options (input mode, mouse look, mouse look up/down, invert mouse Y, mouse sensitivity, keyboard steering speed, menu pointer, cursor size, map cursor speed) and one row per action. To rebind an action, choose its row (Enter, or Left/Right), then press the new key: Esc cancels, Backspace or Delete clears it, and a key already used by another action of the same group is refused. The Reset Key Bindings row restores the default keys, and the page's Reset to Default restores the options. Every change is written to `[KeyboardMouse]` in `FH2_ExtraOptions.ini` and applies at once.

### Controller-Missing Pause
Off by default. Without a paired pad (for example playing with a keyboard, or with a pad the game never paired to your profile), the game's "controller lost" rule pauses again every time the pause menu is closed. With this on, it pauses only when a pad that was connected is actually unplugged:
```ini
[Frontend]
PauseOnlyOnControllerUnplug=1
```
Esc, Menu and every other way of pausing work as before.

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
