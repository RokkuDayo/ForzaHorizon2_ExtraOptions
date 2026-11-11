# Weather Control 1.0.8

Time of Day Lock starts Off and Weather starts on Current (Dynamic) on every launch. Weather presets and the time lock last for the current session. Locked Hour is saved for next time.

The lock holds a freely running clock by passing zero elapsed time to that clock's update only. It does not set the native stopped flag. Native stopped/fixed/stop-locked clocks and timed transitions retain their original update.

## Automatic startup recovery

Once per launch, the plugin checks the clock after three continuous seconds of ordinary offline free roam with gameplay input enabled and Time Lock Off. It waits through menus, loading, Photo Mode and events. The check also requires an unpaused simulation, positive clock speed, and no native fixed/stop-lock/target flags.

If the clock is stopped at that point, recovery clears its stopped byte and preserves the time and speed. The weather check uses the same free-roam conditions and waits until the prologue is complete and weather scripts are enabled. It restores automatic transitions and releases pinned weather through the native weather manager. Each check runs once per launch. The game can preserve the resumed state during its normal save process.

Recovery runs automatically with either logging setting. Old `[Recovery] ResumeStoppedFreeRoamClock` entries can be removed.

## Read-only diagnosis

With the affected save loaded in offline free roam, run diagnostics/CollectClockState.cmd. Return focus to the game during the five-second lead-in, then remain driving or idle in free roam for 20 seconds. Do not pause or use Photo Mode. The collector reads process memory, plugin settings and existing logs; it never calls game routines, changes memory, edits settings or touches saved games. Its clock-state.json is written beside the script. An optional first argument selects another Azimuth installation path. Python 3 is required.

The native regression fixture requires the private Extra Options test environment and a supported game image.
