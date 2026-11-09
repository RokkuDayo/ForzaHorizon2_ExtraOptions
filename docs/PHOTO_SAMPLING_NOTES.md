# Photo Mode accumulation sampling

> Current controls (0.2.62): graphics options are in Settings → Video;
> drift/FOV controls, minimap orientation, Autodrive cinematic camera and championship dialogue are
> in Settings → HUD. The moved INI values are accepted only for initial migration
> into the sidecar; the drift master INI switch is retired. The notes below retain
> historical investigation details.

`Video → Photo Mode Samples=Default` retains the layer's accumulation grid.
In the original game, density 295 produces 129 entries in each of two sample vectors. Values from 16 to 4096 request an
approximate count. The mod chooses the closest count within a small density
window around the disk-area estimate. It changes the four-byte density control,
then leaves generation, allocation, sorting, cache invalidation and capture
requests to the game. It never advertises a count larger than the generated lists.

The supported image's native generator has been executed for these settings:

| INI Samples | Native entries per vector | Grid density |
| --- | --- | --- |
| 0 or 129 | 129 | 295 |
| 256 | 255 | 580 |
| 512 | 512 | 1165 |
| 1024 | 1023 | 2316 |
| 2048 | 2049 | 4627 |
| 4096 | 4095 | 9269 |

Both lists are read with the same accumulation index. The first contains disk
offsets and weights; the second contains randomized spatial offsets and a
temporal component. The render preparation uses them alongside Photo Mode's
camera effects. This supports interpreting them as paired lens/shutter samples
in one accumulation sequence, rather than separate counts multiplied together.
The final Photo Mode capture requests the generated count with parameter 1;
the settled preview requests the same count with parameter 5. Those parameters
are preserved; the fixture does not establish an exact preview render count.

This is separate from the normal RenderScenarios motion blur control, whose
vanilla Global.xml value is 9. PhotoMode.xml enables DepthOfField and sets
ForceAnisoFiltering to 3, but has no separate DOF sample-count or motion blur
sample-count property. Shader tap counts have not been identified. The
ordinary motion-blur control is retired; Azimuth owns that reader.

## Native locations

All locations are RVAs in the supported executable.

| Location | Role |
| --- | --- |
| `0x2e834e4` | Integer grid density, stock 295. |
| `0x11ad0d0` | Generates and sorts both sample vectors. |
| `0x11938e0` | Generates if needed, then returns the first vector's length. |
| `0x11930f0`, `0x1193090` | Fetch one entry from each list using the shared index. |
| `0x10e0685`, `0x10e069e` | Render preparation reads the paired entries. |
| `0xdcf678`, `0xdcf68a` | Settled Photo Mode preview gets the count and requests accumulation. |
| `0xdd0abb`, `0xdd0acf` | Final capture gets the count and requests accumulation. |
| `0x34dcf0`, `0x34dd10` | Render-thread restart forwarding and native request fields. |

The first list starts each grid coordinate at -1, advances by
`2 / sqrt(density)`, and retains points with radius less than 0.75. Its density
estimate is therefore `sample_target * 64 / (9 * pi)`. The mod mirrors the
native float operations while choosing density, including rounding at each step.

## Validation and limits

The private-image fixture runs the actual sample generator and count getter
with preallocated vectors. Random offsets and sorting are stubbed because they
cannot change the count; no GPU or game process runs. Eleven cases verify both
list lengths, density-cache regeneration, neighboring fields, restoration to
stock, option bounds, master disable and conflict refusal. Unsupported density
bytes are refused before any patch writes. The source EXE is unchanged.

Higher counts add accumulation work and can take longer to settle or capture.
Image quality and complete Photo Mode capture require an in-game check. This is
the native shared accumulation generator; it is not a separate DOF shader-tap
override. Restart the game after changing the INI.

## Azimuth v0.9.10 compatibility — 0.2.79

Azimuth's graphics patch loop at host RVA 0x3fb70 traverses 48-byte rows from
0x2b3430 to 0x2b38e0. Rows 0x2b3820/0x2b3850 select Motion Blur setting index 4,
High/Ultra values 2/3, profile member 0x270 and one integer 587/1170. The profile
patch substructure begins at +0xa8, so the member maps through +0x318 to game
RVA 0x2e834e4. The expected original hash at host 0x2ba120 matches SHA-256 of
little-endian 295. The loop validates sites before copying table values.

The native game generator produces 257 samples at density 587 and 514 at 1170.
The Azimuth build accepts only these inspected layer values alongside the
original and its own selected/applied values. It captures the recognized startup
value once per mapped game. Default restores that layer value after an override;
explicit 129 still chooses the original 295 density. The Mezcal build has no
additional Azimuth acceptance entries. Unknown values retain transaction refusal.

The fixture verifies these table rows and the exact host hash, executes the game
sample getter for native/high/ultra, switches to custom counts and back to Default,
and checks conflict refusal with no partial writes. Restart remains required.

## 0.3.5 custom Photo Mode time

PhotoTimeOfDay now selects Off/On. PhotoHour is a native float property in hours
(0..23.75, quarter-hour steps). The temporary hour initializes from the captured
clock through native GetTime with inverse remapping. Turning the lock off
restores the exact entry clock while preserving Photo Mode pause; scene exit
restores original state. Both setters reset accumulation through the existing
photo invalidation gate; saving retains its existing guard. Weather, headlights
and driver controls keep their behavior. The addon free-roam INI is untouched.

## 0.3.9 native hour slider template

The native slider fill binds its transform to a normalized value; feeding hours
into it saturates the fill. PhotoHourPosition now exposes quarterIndex/95 and
accepts 0..1 input, rounding to one of the 96 quarter-hour positions. PhotoHour
continues to expose decimal hours. Both properties notify after changes. The
shipped slider and numeric-row templates are copied at resource load; their
focus triggers, named children, fonts and vertical spacing are retained. The
readout MaxWidth grows from 40 to 80 for decimal hours and the slider reserves
that extra width. No custom value-lane layout remains.
