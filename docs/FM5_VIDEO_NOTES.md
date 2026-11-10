# FM5 Video options

Video opens a native HUD-style options page. BRIGHTNESS opens the original
calibration view; Back returns to the list. Initial-experience calibration
stays native. An existing Back callback is cloned and restored when present;
temporary callbacks are released when the page closes.
Back uses the native settings Cancel action `2D`, and Accept uses `12`.
Accept checks native IsKeyboardFocusWithin on BRIGHTNESS before completing the
page; it opens calibration when focused. Accept on a settings row finishes the page through the same native completion
interface as HUD options. BRIGHTNESS uses the spinner's LightestGray80Color
background and a white label while selected.

The following controls reuse FH2's localized labels, descriptions and
Default/On choices: 4K Resolution Liveries and Enhanced Livery MSAA. Labels are capitalized for FM5. Choices default to Default
and persist with camera/player settings in `FM5_ExtraOptions.dat`, beside the DLL. Y resets
them and resets COCKPIT MOTION BLUR to On. COCKPIT MOTION BLUR now appears
immediately below BRIGHTNESS in Video instead of HUD and keeps its existing
FM5_ExtraOptions.dat storage. This remains available with HUD drift/FOV options
disabled. Rendering changes apply on restart; editing never rebuilds a live livery.

FM5-specific render sites:

- `7AA671`/`7AA6A6`: final other/player texture dimensions, 4096 when enabled.
- `7BA4AD`: livery MSAA count. Enhancement raises a recognized lower layer
  value to four, preserving an existing eight samples.
- `7BA536`: intermediate dimensions, with a leaf in verified padding at
  `7BA707`. Fourfold input dimensions for 4K resolution. Supersampling was removed;
  legacy saved sampling values are ignored. Version 2 writes only the two
  remaining livery choices and reads existing version 1 files.
- `7B9C3C`/`7B9C4C`: inverse scaling for matching raster extents. Independent
  half-region centering calculations keep their native factor.

Default leaves the corresponding native/layer path alone. Patches join FM5's
verified startup transaction. There are no render-loop hooks or polling.

`fm5/ui/video_options.xaml` is the readable layout; `build.py --fm5` generates
its embedded header. Native UI controls and transitions are used, retaining
the original calibration art and visual states.

`development/tests/test_fm5_video.py` checks all eight combinations, executes
the actual dimension and extent leaves on a private executable mapping, and
verifies native property/collection/string ABI, persistence, brightness/Back
navigation, callback restoration, and first-run behavior. AVUI dispatch and
GPU rendering remain fixture boundaries; in-game appearance and quality
still require a game test.
