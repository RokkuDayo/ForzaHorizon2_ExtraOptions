# Sun-shadow direction refresh

> Current controls (0.2.62): graphics options are in Settings → Video;
> drift/FOV controls, minimap orientation, Autodrive cinematic camera and championship dialogue are
> in Settings → HUD. The moved INI values are accepted only for initial migration
> into the sidecar; the drift master INI switch is retired. The notes below retain
> historical investigation details.

Inspected against the supported original FH2 executable. `Graphics.SmoothSunShadows`
is optional and defaults to 0. It makes three guarded changes in the
native `ShadowMapGeneratorAnthem11`, on either translation layer.

Constructor 0x1121330 installs vtable 0x2e82b88. Update 0x1131950 calls
0x1153880 to collect the current light/camera state, followed by 0x1155610
to prepare the focus shadow projections. The light direction comes from the
world renderer's virtual +0x2e8 and is stored at generator +0x2340.

## Cascade directions

0x1153880 checks camera movement, sun direction, projection change and an elapsed
frame budget before updating its cached data. Direction comparison uses a dot
product with +0x23d0; the initialized tolerance at +0x23e0 is 0.01. The frame
budget at +0x23f0 starts at 1800. These are refresh conditions, rather than a
single fixed interval for all world shadows.

At 0x1153e5a, `jne +0x0c` becomes `jmp +0x0c`. The existing native cache refresh
then copies the current direction to the four cascade records (+0x3e0, +0x790,
+0xb40, +0xef0) and its comparison cache +0x23d0 on every update. Native position,
projection and last-refresh bookkeeping still run. No memory offsets are added
to the renderer and no resource allocations or drawing functions are hooked.

## Focus direction

0x1155610 separately holds a direction at +0x2400. It compares
`dot(current, cached)` with `1 - float(int(+0x2418) * double(+0x2410))`.
The constructor initializes the latter fields to 50 and 0.0000001, giving an
approximately 0.18-degree angular step for normalized directions. This does
not imply a fixed duration; the elapsed time depends on sun movement.

At 0x11556b5, `jae +0x06` becomes two NOPs. The native aligned vector copy always
refreshes +0x2400, which is passed into the focus projection builder 0x112c8f0.

## Scope and validation

Shadow resolution, filtering,
camera texel stabilization, the weather assets and time-of-day speed are unchanged.
The option reduces direction caching and refreshes all four cascade maps each
renderer frame. Increased shadow drawing may cost
GPU time. It cannot guarantee that other causes of visible stepping disappear.

The patch engine verifies all three original sites along with all existing patches,
supports their known patched states, and restores their native bytes when the
option is off. Master disable retains the existing no-change behavior.

`development/tests/test_sun_shadows.py` executes all three original decision windows in a private
relocated image. It checks small/large/negative direction changes, all four
cached cascade directions, focus-cache quantization, toggling on/off and master
disable. It also checks 27 drawing-budget decisions: configured budgets 1/2/4,
on/off/restoration, the native skip gate and initial full refresh. Corrupting the
budget instruction must refuse the entire patch transaction without modifying
the image. It preserves the relevant native register ABI and exits before scene
services or GPU submission. Runtime tests cover all three site states and settings
parsing. No user save or game asset is modified or distributed.

Final shadow appearance, frame cost and compatibility with the layers' actual
GPU rendering require an in-game check.

## Per-frame drawing budget (0.2.30)

The user confirmed 0.2.29 removes the large steps, but reported a lower shadow
rate at 60 FPS. A read-only live sample showed about 59 renderer updates per
second, with generator +0x21bc still equal to 1. Native 0x1131950 reads that
budget at 0x1131c02, then walks a four-entry round-robin index at +0x21b8.

At 0x1131c02, the seven-byte budget load becomes `mov eax,4; nop; nop`. With
SmoothSunShadows enabled, the normal loop therefore redraws all four maps per
renderer frame. Its existing skip gate at +0x23f8, initial full refresh and
camera-movement handling remain intact. The configured stored budget is not
overwritten; disabling the option restores its normal use. This raises GPU
work and does not force a 60 FPS game rate.

The live sample's sun-direction changes averaged about 30 per second, but its
recorded intervals were about 16.7 ms during active changes. It cannot establish
a hard 30 Hz sun clock because focus/pause state was not sampled. The drawing
budget change addresses the proven native round-robin limit; final appearance
still needs an in-game test.

## Layer defaults and explicit overrides — 0.2.79

Video now offers Default / Off / On. The runtime option uses -1 / 0 / 1.
Before its first transaction for a mapped game, sun_shadows.c remembers each of
the three recognized loaded byte sequences. Default targets those saved bytes;
Off targets the original native decisions; On targets the existing per-frame
updates. Validation, write rollback and original page protection still apply.
Unrecognized bytes are never adopted as a baseline and fail preflight.

The fixture checks all eight combinations of original/patched startup sites,
then Default → On → Off → Default, in addition to the native refresh/budget
branch checks. A restart is required to change the startup override.
