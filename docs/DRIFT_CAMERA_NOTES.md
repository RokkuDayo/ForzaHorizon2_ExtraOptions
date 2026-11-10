# Shared drift camera response

> Current controls (0.2.62): graphics options are in Settings → Video;
> drift/FOV controls, minimap orientation, Autodrive cinematic camera and championship dialogue are
> in Settings → HUD. The moved INI values are accepted only for initial migration
> into the sidecar; the drift master INI switch is retired. The notes below retain
> historical investigation details.

The drift-camera hooks are installed when the mod's global switch is enabled.
The retired Camera.DriftCamera INI switch is ignored. The feature covers
Chase Near/Far and Driver/Hood/Bumper.
Chase uses the loose native spring during lateral tire slip; mounted views use
the critically damped response described in DRIVER_REFERENCE_NOTES.md. Chase
views retain their original grip path. Mounted views ease the drift offset back
to zero as grip returns, then keep the exact vanilla path.
Settings / HUD supplies On,
Driver and Off modes plus separate sensitivity, look speed and range sliders
for chase and mounted cameras. Each slider accepts 0–100; chase defaults are 35, 70, 80 and driver defaults are
70, 55, 65. The default mode is Driver (cockpit only; hood and bumper stay native).
Version 6 converts older driver values to the new response ranges.
See [VIDEO_OPTIONS_NOTES.md](VIDEO_OPTIONS_NOTES.md) for bindings and persistence.

## Native path

Addresses are RVAs in the supported executable.

| Path | RVA / offset | Role |
| --- | --- | --- |
| Follow / Near / Far vtables | `0x2e783a0`, `0x2e78610`, `0x2e78880` | Chase views. |
| Driver / Hood / Bumper vtables | `0x2e78120`, `0x2e77ea8`, `0x2e703b0` | Three mounted views; update slot is `+0x20`. |
| Chase update | `0x103c820` | Calls the heading spring, then applies orbit, radius/height, effects and position. |
| Heading spring | `0x1036920` | Reads target axis 2; computes yaw; integrates the native spring. |
| Heading call | `0x10369c9` | Originally calls `atan2f(-axis.z, -axis.x)` at `0xf17d52`. |
| Leaf forwarder | `0x103b261` | Fifteen `CC` bytes after a return, outside unwind entries. |
| Chase heading call | `0x103c87d` | Originally calls the heading spring; now calls a wrapper with softer local spring settings. |
| Chase forwarder | `0x103b281` | Twelve `CC` bytes after another return, outside unwind entries. |
| Mounted update | `0x103c590` | Native input, mount offsets, world position and direction. |
| Driver / Bumper view getter | `0x1033e70`, vtable `+0x260` | Builds direction from target axes and native look yaw. |
| Hood view getter | `0x1033f10`, vtable `+0x260` | Builds direction with native look yaw and pitch. |
| Native sine/cosine | `0x1251ce0` → `0x278ec0` | Rotates the resulting mounted direction around world Y. |
| Camera target | camera `+8` | `ICameraTarget`; predicted targets wrap the same car at target `+8`. |
| Car camera-target vtables | `0x2c5ce28`, `0x2e6b348` | ForzaCar / Car secondary interface at object `+0x20`. |
| Target axis getter | target vtable `+0x20`, native `0xf62220` | Delegates to the physics body at target `+0x4be8`, slot `+0x970`. |
| Target velocity getter | target vtable `+0x10`, native `0xf646b0` | Reads the world velocity through physics slot `+0x288`. |
| Predicted target vtable | `0x2e79540` | Axis/car getters delegate to the wrapped target. |

The forwarder passes the native heading function's RSI camera in R8, preserving
the float arguments in XMM0 and XMM1, then tail-jumps to the DLL callback.
It neither changes the stack nor needs an unwind record. The callback has
normal compiler-generated unwind metadata. Its native `atan2f` call uses the
game's existing math import; vector normalization uses an SSE square root.
The new chase forwarder passes camera, time step and reset arguments unchanged.

## Grip gate

The body getter for the native camera parameter `Slip_Lateral` is vtable
`+0x1d8`, RVA `0x12c5cb0`. It reads each wheel's lateral slip at body `+0x1410`
with stride `0x4b0`. The generic camera-input calculator's enum entry 9 uses
that getter (RVA `0x1037f1a`); entry 10 uses a different longitudinal getter.
The contact getter `+0x4e0`, RVA `0x12e8040`, returns a contact only when the
wheel's body `+0x1550` flag is set. Both getter identities are checked.

For each axle, average the absolute lateral-slip signal of its grounded tires,
then take the larger axle value. The mod's normalized-slip threshold is 1.0;
the change eases from zero there to full weight at 1.35. These onset/blend
values are mod tuning and need a player test across tire setups and surfaces.
Unloaded wheels are excluded. Longitudinal slip alone never activates the gate;
non-finite samples or unsupported getter identities use the native path.

The weight blends the momentum target in every view and spring settings in chase. At zero,
the chase wrapper delegates directly to the original spring with its original
settings and body-spin coupling. Mounted views target zero drift offset and retain their
spring state until the angle and rate settle. Their native output is then
unchanged byte-for-byte. A brief zero-slip sample does not clear active history. No physics state is written.

## Shared heading policy

Once lateral slip enables the change, only horizontal velocity directs the view. At 4 m/s and above, the camera aims
along the direction of travel for forward and sideways motion. Between
2 and 4 m/s a smooth blend retains body heading near a stop. Motion more than
90 degrees away from the car's forward axis progressively fades this change;
at 120 degrees or more it uses normal body heading. This avoids a 180-degree
camera turn when reversing or spinning out.

## Chase spring tuning

The chase wrapper copies the native 48-byte spring-settings prefix onto its stack.
At full slide weight and default stiffness it scales yaw stiffness by 0.55 and
damping by 0.5, and sets body-angular-velocity coupling to zero. Partial weight
blends these from vanilla. With vanilla values, K changes from 4.5 to 2.475, damping
from 4.4 to 2.2, and body coupling from 0.67 to 0. The angular speed limit stays
unchanged. This gives softer response and some natural overshoot while body
spin no longer kicks the view away from its momentum target.

For Look Speed S, the spring scale is `0.25 + 0.015*S` below 50 and
`1 + 0.06*(S-50)` above 50. Stiffness uses `0.55*scale`, damping
`0.5*sqrt(scale)`. Sensitivity scales the drift angle by `value/50`; Range
caps it at `value*1.2` degrees. These mappings now apply only to chase views.
Changes take effect without clearing an active offset or restarting.

Chase spring activation decays toward recovered grip over approximately a
quarter-second time constant. This avoids switching spring and body-spin
coupling abruptly when lateral slip flickers below the threshold. The heading
target still follows actual slip; settled grip delegates to vanilla.

Native integration runs on a private `HeadingFrame` containing the vtable,
target, local settings and current yaw/rate at their known offsets. Only the
resulting yaw/rate are committed to the real chase camera. Shared settings
and the real settings pointer never change. Native grip adjustment, frame
delta, angle wrapping, right-stick orbit and subsequent effects remain.

## Mounted views

All three update slots originally point to the same mounted update. The hook
calls it first, preserving each view's geometry and controls. It then integrates
the relative yaw difference between the momentum target and native body heading,
using the same native spring and stock K=4.5, damping=4.4 and speed limit=2.
The driver Look Speed controls a critically damped spring independently of chase:
frequency is `2 + 0.1*S`, K is frequency squared and damping is twice frequency.

As of 0.2.77, the retained offset is rebased by the car's heading change before
integration. Previously the camera smoothed only a body-relative offset; body
rotation itself bypassed that spring. During full forward slide the rebasing
cancels that direct rotation, so the final world heading follows the same spring
trajectory as a world-heading spring at full sensitivity and sufficient Range. The car-axis
getter is validated separately; manual orbit is not part of the rebasing.

As of 0.2.78, the rebasing weight scales continuously with sensitivity squared;
the old nonzero-sensitivity condition incorrectly enabled full correction even
at 1 or 2. Sensitivity covers gain 0–1, and driver Range covers 0–45° with a soft
shoulder over the last quarter of the limit. Chase tuning is unchanged.
The rebasing weight uses the existing lateral-slip, speed and reverse gates.
Its release uses the same quarter-second decay as the chase spring, blending
back to normal body following as grip returns. Zero Sensitivity/Range and Off
do not introduce body-rotation lag. The final mounted offset is clamped to Range;
outward angular speed is cleared at a limit to avoid winding up against it.

The mounted spring uses its configured tuning during both drift and recovery.
Body-spin coupling stays zero because the native direction already follows body
rotation. Raw lateral slip blends only the target offset. This prevents a brief
grip sample or nose alignment from changing the spring abruptly. When slip falls
to zero, the spring eases the offset back to zero rather than discarding it.

Three small `DriftOffset` records hold camera/target identity, relative angle,
world angular rate, previous body heading and release weight. A new identity
initializes zero offset and leaves the first active frame
unchanged. Same-view native reset flags preserve an active offset; the original
mounted update still receives that flag. Once the angle is below 0.00001 radians
and rate below 0.0001 radians/sec at zero slip, the record is cleared. These
sub-visible cleanup thresholds avoid an endless tail. Invalid physics or a new
camera/target cannot reuse stale state.

The native direction at camera `+0x1c0` rotates horizontally by the integrated
offset. Vertical direction, vector length, look yaw/pitch and camera position
remain in their native paths. The dirty flag `+0x21a` is set after the change.
No car transform or rendering target is changed.

Unsupported camera/target types, unavailable target/body pointers, non-finite
samples or degenerate/extreme speeds fall back to the original calculation.
The existing preflight, page-protection and rollback transaction controls both
heading calls, both forwarders and all three update pointers. Disabling
restores original calls, padding and update pointers.

## Validation

`development/tests/test_drift_camera.py` executes FH2's heading function, patched call,
forwarders, mounted update, both mounted direction getters, native sine/cosine,
target-axis/velocity getters and predicted-target delegation in a relocated
private image. The fixture supplies physics samples, input/effect boundaries,
bounding-box/position services and the math import.

It checks 1,680 heading and 1,680 mounted-view cases; 168 byte-exact gripping
cases at 30/60 Hz, airborne/invalid slip, progressive onset and smooth regrip;
equal mounted/chase trajectories, smooth first-frame grip loss at 30/60 Hz
including zero delta, and five Look Speed levels with increasing response;
636 native transition frames cover grip flicker, nose alignment, drift reversal
and same-view reset flags. Twenty-four samples verify independent chase/FPS tuning;
chase body-spin independence; low-speed/reverse and invalid-data fallback;
native positions, pitch, look and vector length; and disabled/master-off behavior.
The rotating-body regression now checks proportional correction at sensitivity
0, 1, 2, 10, 25, 50, 70 and 100 at 30/60/120 Hz, plus wrapped headings. Another
3,600 frames cover Range, zero Sensitivity/Range and moving-body fallbacks at
low/extreme speed and in reverse. The reference default settles monotonically
and reaches 90% of a fixed target in roughly half a second at all three rates.
Full rendering and camera feel require a player test. These are FH2 camera
routines with new tuning, rather than FH5's camera code or exact parameters.
