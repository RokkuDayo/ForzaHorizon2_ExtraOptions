# Historical research — feature removed in 0.2.65

Azimuth now provides this setting. The mod no longer installs the sample-reader
hook or exposes its menu control. Earlier Graphics.MotionBlurSamples keys are
ignored. The following notes describe earlier builds.

# Motion blur samples

> Current controls (0.2.62): graphics options are in Settings → Video;
> drift/FOV controls, minimap orientation, Autodrive cinematic camera and championship dialogue are
> in Settings → HUD. The moved INI values are accepted only for initial migration
> into the sidecar; the drift master INI switch is retired. The notes below retain
> historical investigation details.

`Video → Motion Blur Samples` is read at startup on either layer. `0` retains
the asset-defined value; integers `1` through `21` override it. The supplied
INI uses `0`. The original `RenderScenarios/Global.xml` selects `9`.
The option changes the sample count, while the scenario's MotionBlur enable
flag, shutter amount and other rendering settings retain their normal behavior.
Restart the game after changing it. Higher counts increase the blur sampling work.

## Native control

The supported executable contains `CDynamicRenderMotionBlurSamplesControl` at
vtable RVA `0x2eb9a68`. Its name getter `0x1307420` returns
`MotionBlurSamples`, and the containing `CDynamicRenderSettings` constructor
`0x12fa330` initializes that control at settings offset `0xb8` with value `9.0f`
at offset `0xc0`. The default setter `0x130bb80` also writes `9.0f` to the
control's value at offset `8`.

Its range getter `0x1303d70` writes step `1.0f`, minimum `1.0f`, maximum `21.0f`.
The INI follows this inspected native control range rather than accepting
arbitrary sample counts. `0` is a mod-specific passthrough value.

The native XML value reader at `0x130ab40` forwards the reader, control value
address and `value` attribute name to the reader's float method at virtual
offset `0x1a0`. Only this control's reader pointer, vtable offset `0x30`
(RVA `0x2eb9a98`), is replaced. Other float controls share the original reader
function and keep their own original vtable pointers.

The callback runs the original reader, then supplies the selected float count
to this control. Its native return value and all adjacent fields are preserved.
XML files remain untouched. The configured override is committed only after the
existing guarded patch transaction succeeds. A disabled override restores the
native reader pointer, and master disable skips the transaction.

## Verification

`development/tests/test_motion_blur.py` executes the actual native default,
range and reader routines in a private relocated game image. Its XML float
provider is the service boundary. It covers passthrough and counts
1, 2, 9, 17 and 21 against three XML values and two return values, checking
forwarded arguments, adjacent bytes, pointer restoration, invalid selection
and conflict refusal. The runtime tests cover INI parsing and numeric bounds.
The production Azimuth loader fixture also covers the override by itself,
with other features, disabled, master disabled and invalid configuration.

These tests establish the native control change and startup installation.
Motion blur appearance and GPU cost still require an in-game check.
