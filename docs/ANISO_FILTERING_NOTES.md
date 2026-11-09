# Historical research — feature removed in 0.2.64

Azimuth now provides this control. The mod no longer installs the filtering
hook or exposes its INI/menu setting. The following notes describe prior builds.

# Forced anisotropic filtering

> Current controls (0.2.62): graphics options are in Settings → Video;
> drift/FOV controls, minimap orientation, Autodrive cinematic camera and championship dialogue are
> in Settings → HUD. The moved INI values are accepted only for initial migration
> into the sidecar; the drift master INI switch is retired. The notes below retain
> historical investigation details.

Video → Anisotropic Filtering uses the game's existing enum:

| INI value | Native name | Filtering |
|---|---|---|
| 0 | Default | Preserve each scenario XML value |
| 1 | Aniso2x | 2× |
| 2 | Aniso4x | 4× |
| 3 | Aniso8x | 8× |

There is no native entry 4 or Aniso16x. Nonnegative integers above 3 clamp to 3
before patch planning; negatives, fractions, hex and text are rejected. Omitted
keys default to 0; the supplied INI uses 3. Restart after editing.

The native enum getter 0x1303d20 exposes four entries at 0x2eba780. The default
setter 0x130bb40 stores zero. CDynamicRenderForceAnisoFilteringControl's XML reader
is 0x130ab00, installed at vtable slot 0x2eba5c0. It calls the XML provider's
integer reader at +0x188 and returns an integer pointer. The hook preserves that
ABI, executes the original first, and only overrides control+8 when requested.
PhotoMode.xml already requests value 3.

Twenty native reader cases passed, including the actual enum/default routines,
XML values, pointer return, adjacent-field preservation, restoration, safe parsing
of very large integers, invalid options and conflict refusal. Production Azimuth
loader cases also cover 3, clamped 4 and XML passthrough. Visual filtering remains
to be tested in-game. Extending the enum alone would not establish working 16×.
