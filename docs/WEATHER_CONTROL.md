# EO Weather Control v1.0.4

Requires Extra Options v0.3.4 or later for FH2. Install the accompanying core
update first. This is a separate plugin, not a new page in Settings > Video.

Version 1.0.4 removes the unused breadcrumb slash from the standalone title and
fades the pause tiles out/in over 0.2 seconds, matching the native element
transition duration. Rapid close/reopen preserves the current fade position.

With the game closed, extract `Extra Options - Weather Control` into your `mods`
folder. Enable it in loadorder.txt if you use an explicit load order. Disable or
remove the original Set Time & Weather/TimeLock addon: do not load both DLLs.
Keep the plugin's `EO_WeatherControl` media directory alongside its DLL.

Open the free-roam pause menu and press **Y — Change time**, after the **X — Return
to garage** prompt. The native options page contains Time of day lock, Locked
hour, and Weather. Weather uses the game's Current/Sunny/Wet/Rain/Storm choices.
Current releases the plugin's weather override. Off restores normal clock progression.
Locked Hour uses fifteen-minute steps and is saved immediately in the plugin INI.
Time of Day Lock and Weather are session-only and start Off and Current (Dynamic)
on every launch. Event-owned clock and weather controls follow the game.

Configure `plugins/EO_WeatherControl.ini` beside `EO_WeatherControl.dll`:

- `[Bindings] ChangeTimeXbox=Y`: Y, X, A, B, View, or None. Pick an unused pause
  button; X is already Return to garage. Restart after changing bindings.
- `ChangeTimeKeyboard=0`: optional numeric Windows virtual key, e.g. 117 for F6;
  zero leaves it unbound. Input is read only on the focused pause menu.
- `[TimeLock] Enabled=0`, `Hour=12`: defaults leave normal time progression.
- `[Weather] Mode=Dynamic` is the startup state. The UI calls it Current.
  Older preset values in the INI are reset to Dynamic on launch.
- `[Logging] Enabled=0`: set to 1 to write `plugins/EO_WeatherControl.log`.

Old TimeLock.ini hour and preset times can be copied into the matching TimeLock
and Presets sections. The new configuration lives beside the DLL.

The core update changes the English prompt capitalization to 'Return to garage'
and prevents the standalone page from receiving normal Video settings.

All sixteen authored language pages and native resource names are checked.
Private native fixtures pass for clock/weather ownership, event controls,
controller/keyboard rebinding, focus/held input, page ownership and reset scope.
Actual menu rendering and gameplay still require an in-game test.

Source and GPL-3.0 license are included. To rebuild on Windows, use Python 3 and
Zig 0.15.2: `python source/build.py C:/path/to/zig.exe`. The build creates a release
folder beside source; test binaries stay in build and should not be installed.
