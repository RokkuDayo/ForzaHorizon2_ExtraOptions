# Driving-camera field of view

Five Settings / HUD sliders control vertical FOV, 20–120 degrees in 0.5-degree
steps. Stock defaults come from media/Camera.zip/CameraSettings.ini:

| Control | Native key | Degrees |
|---|---|---|
| Chase | FollowLowCam\FOV | 48.5 |
| Far Chase | FollowHighCam\FOV | 58 |
| Driver | Driver\FOV | 48 |
| Hood | HoodCam\FOV | 66 |
| Bumper | BumperHighCam\FOV | 48 |

The feature installs getter slot +0x70 on Follow / Near / Far vtables 0x2e783a0,
0x2e78610, 0x2e78880 and Driver / Hood / Bumper 0x2e78120, 0x2e77ea8, 0x2e703b0.
Native getters are 0x1034120 and 0x10340f0. The default choice returns the original
result byte-for-byte, including existing XML changes. A custom choice rescales
the native result using its current degree/radian base. Native split-screen
scaling remains intact. Camera objects, springs, positions, input and drift state
are not written. Replay and Photo Mode camera vtables are untouched.

State and migration live in video_settings.c; native bindings/layout remain in
video_options.c and video_options_xaml.c. This feature is independent of the drift
master switch. Existing saved drift choices remain valid.

The isolated native fixture passed 120 getter cases, including all six classes,
both split flags, stock/custom bases, min/max/half-degree selections, byte-exact
stock behavior, independent controls, unchanged state, guards and conflict refusal.
The native menu fixture verifies all five bindings and defaults read from Camera.zip.
GPU rendering and complete menu interaction still need an in-game check.
