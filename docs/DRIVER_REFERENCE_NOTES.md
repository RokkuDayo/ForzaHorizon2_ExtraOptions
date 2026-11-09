# Driver drift response — 0.2.78

The user supplied a 16.57-second, 1920×1080, 30 Hz first-person drift clip titled
"Drift Camera FINALLY in Horizon 4 / Lets drift the mountain in first person on
a wheel!!!". It was inspected through sampled frames, including the sustained
rightward look around 2–5 seconds, the crossover around 8.5–10 seconds and the
gradual recovery near the end. The clip is not redistributed with the mod.

The visible cues are a progressive look into the drift, a fairly quick change
of side and little apparent spring overshoot. Body/momentum telemetry, camera
input and calibrated FOV are unavailable, so this is a qualitative approximation,
not a recovery of that game's camera parameters.

## Driver controls

| Slider | Mapping | Default |
| --- | --- | --- |
| Sensitivity | gain = (S / 100)², from zero to full momentum following | 70 |
| Look Speed | natural frequency = 2 + 0.1S, with K = frequency² and damping = 2 × frequency | 55 |
| Range | 0–45°; target eases toward its limit above 75% of the limit | 65 |

The default gain is 0.49, frequency 7.5 and maximum relative angle 29.25°.
The existing native heading integrator and 2 rad/s speed limit remain in use.
The three sliders affect Driver/Hood/Bumper; chase mappings are unchanged.

Both the desired angle and the body-rotation correction use the same sensitivity
gain. Previously any nonzero sensitivity enabled full body correction, producing
large transient movement even when the requested angle was tiny. With a fixed
travel direction and a rotating body, an unclipped driver camera now retains
(1-gain) of the body rotation. At 1 and 2, a 60° slip requests just 0.006° and
0.024° respectively. Native orbit remains independent of that correction.

The target has a smooth shoulder at the range limit; the final offset retains a
hard safety cap and clears outward speed at the cap. The existing grip, speed,
reverse, initialization and recovery rules remain in place. Critical damping
reduces overshoot while moving between drift directions.

## Saved choices

The sidecar is version 6 with the same 92-byte size and field order. Older
default driver settings adopt 70/55/65. Customized settings map to the nearest
new gain, frequency squared and angular limit, clamping values outside the new
supported range. This preserves intent rather than reinterpreting the old raw
percentages. Damping and the soft limit intentionally change. Other fields are
preserved, apart from the existing retired-field cleanup and legacy INI import.
Version-5 upgrades are saved immediately; earlier formats complete their normal
one-time INI migration before saving. Reopening version 6 does not convert again.

## Checks

Native fixtures cover continuous correction at 0/1/2 and higher sensitivity,
monotonic settling and frame-rate agreement at 30/60/120 Hz, five Look Speed
levels, range/zero settings, wrapped headings, grip recovery, view resets,
native manual look/pitch/position and unknown/invalid physics. Separate menu
fixtures verify default values, all supported older sidecar formats, custom
conversion and idempotent loading. Rendering and feel still need a player test.
