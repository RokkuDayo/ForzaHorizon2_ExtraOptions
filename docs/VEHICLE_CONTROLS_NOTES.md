# Native vehicle controls â€” 0.2.62

ForzaCar's primary update slot `0x2c5cc00` points to `0x2a9400`. The replacement
forwards `(car, dt, frame_dt, frame)` unchanged and runs gestures only for the
local player's car. It is installed under Global.Enabled. GPS updates retain
their original Autodrive gate; vehicle controls do not depend on GPS scheduling.

The local-player getter acquires a ForzaCar with vtable `0x2c5cb80`. Every
reference is released in the same update. Controller input uses the native
record's right-thumb mask `0x80` at offset `0xc0`. Focus, pause/menu, disconnect,
car and animation changes cancel pending gestures until release.

Headlight setters `0xf74ee0` and `0xf74ef0` control manual override and its
on/off value (`0x51a1` and `0x51a2`). The automatic value is at `0x51b4`. A short
release toggles the effective state, then stores the requested value separately.
It is reapplied before and after the original update, which can reset these
fields. Subsequent presses toggle the stored request rather than a transient
native reset. The live S2000 report showed the forced bit resetting within one
frame; the native fixture reproduces that reset.

Animator `car+0x4d40` owns an animation graph at `animator+0x1c8`. The native
event map begins at `graph+0x268`. Finder `0x16fd4d0` accepts a 32-byte native
small string. CCar dispatcher `0xf5d300` sends an event through `0x16fb540`,
which returns void. The mod checks for `roof:open` and `roof:close`, and prefers
`roof:toggle` when available. This avoids assuming an initially closed roof.
Graphs exposing only open/close events retain session-based alternation.

The live Honda S2000 CR graph exposed all three roof events. Its cached car
animation detail was 1. Native input update `0xf7bcc0` selects 0 for presentation
cars, 1 for the active free-roam car and 2 for other cars, updating the graph
when cached detail changes. Setter `0x16fbe90` writes graph detail at `+0x10`
and the float input `GameInputs:AnimationLOD`. A roof request switches this
graph to detail 0 and retains that setting across its normal updates. This
uses the presentation detail setting without entering ForzaVista or launching
a cinematic. The final visual outcome still requires in-game confirmation.

Velocity wrapper `0xf646b0` belongs to the camera-target base of ForzaCar,
whose RTTI complete-object locator records offset `0x20` (vtable `0x2c5ce28`).
It receives `car+0x20`, so target-relative body `+0x4be8` is primary-relative
`car+0x4c08`. Its virtual getter is `+0x288`. Earlier builds incorrectly passed
the primary car address and used the unrelated field at primary `+0x4be8`.
The live control report confirmed that every stationary check failed while
the two long holds were correctly consumed. All three finite velocity axes must stay within a combined speed
of 0.1 m/s throughout the two-second hold. The output is 16-byte aligned.
A long hold is consumed once even when moving or unsupported, and never
doubles as a headlight press.

Native fixtures execute controller conversion, velocity wrapper, light setters,
event hashing/lookup/dispatch and detail setter. Retrieval, velocity production,
the original update and final animation consumption are fixtures. Tests check
30/60/120 Hz gestures, argument forwarding, resets, event preference, cancellation
and reference balance. They do not render cars or prove every animation graph
can animate its roof in free roam.


## Hold duration and branch activation

The hold uses GetTickCount64, independent of physics interpolation timing,
and is checked on release as well as during the press. A consumed long hold
never toggles lights. The current threshold is 2,000 milliseconds.
The S2000 GSF asset selects its Autovista branch when AnimationLOD is zero;
its transition from InRace_Focus lasts 0.25 seconds. Roof control prepares
full detail, waits 350 milliseconds, then dispatches the event. Cancellation
and motion checks remain active while waiting. The current tests exercise
both subobjects with distinct physics data, so calling the velocity wrapper
on the wrong base address fails the regression. No animation asset is edited.

## Removable-roof fade

The per-car `.avpins` actions choose `Fadeopenroof` / `Fadecloseroof` for simple
removable roofs. Three cars select visible full animations instead: BMW_Z4_11
(ID 1218; 13 seconds), FER_California_09 (ID 1062; 21 seconds) and MAZ_MX5_13
(ID 2004; 12 seconds). The native query at `0xf60770` selects Data_Car using
`car+0x118`; this is the model ID used for the three animation exceptions.
The default for other supported roof graphs is to cover the change.

The stock fade cinematics send `roof:open@x50` / `roof:close@x50` under black.
CarProxy's AnimEvent setter at `0x173d2d0` strips the speed suffix, calls native
animation-speed setter `0xf74e00`, then dispatches the plain event. That setter
writes `animator+0x20`. The mod retains the working toggle-event selection,
uses the same 50x speed during its covered interval, and restores the old speed.

`roof_fade.c` wraps CameraFadeComponent's color getter at vtable `0x2c81388`
(original `0x477c50`). Its interface starts at component+0x98 and returns an
aligned RGBA vector copied from interface+0x18. The mod overlays black in the
returned vector only; it leaves component fields, native fade timers and renderer
registration untouched. Stronger native fades keep their returned color. Outside
a roof transition, this getter has exactly its original behavior.

The gesture prepares animation detail, fades out over 350 ms, and waits until
the native render path requests a fully opaque black color before sending the
roof event. It holds black for 750 ms with 50x animation speed, restores speed,
and fades in over 350 ms. Interlocked opacity and acknowledgement fields cross
the car-update/render boundary. No animation event can fire solely because a
timer expired. If the overlay is unavailable, the request cancels after five
seconds. Focus/menu/controller/car/graph changes clear the overlay and restore
speed on the same animator; graph replacement does not leave its animator at 50x.

Native fixtures execute both the unchanged getter and animation-speed setter.
They check black-frame ordering, all three animated exceptions, open/close at
30/60/120 Hz, original fade preservation, timeout and cancellation before/after
dispatch. They do not render a complete game session; visual coverage remains
to be confirmed by the user.

## Roof gesture lock â€” 0.2.65

A dispatched event starts a per-car animation lock. Full-animation durations
follow the shipped 13/21/12-second ForzaVista actions, plus a half-second settling
margin. Covered roofs use the existing 750-ms interval at 50x speed plus that
margin; their fade/queued event also keeps the action busy. Countdown uses active
wall time scaled by the current animator speed. Menus/focus loss pause it;
long gaps count at most 250 ms, so returning from a paused session cannot expire
an unfinished animation. Very low update rates still eventually unlock.

A gesture records whether the roof was busy when the press began. Such a hold
can never become a deferred reversal, even if the lock expires before release.
A fresh press is required. Consumed long holds never toggle headlights; short
headlight presses remain available. Controller/focus cancellation retains an
already dispatched animation lock. Switching the active car or replacing its
animator clears the obsolete lock. No additional LOD changes are made.

Native fixtures cover all three animated cars and a covered S2000 roof,
back-to-back holds, holding through completion, fresh-press rearming, pause,
focus loss, long gaps, slow animation speed and low update rates.

## State and call reduction â€” 0.2.66

HeadlightState and RoofState no longer live inside VehicleGesture. The gesture
handler receives the already retained local car. It stops velocity reads after a
hold is consumed or ceases to be stationary; pending roof dispatch still checks
velocity independently. Maintenance skips untouched controls. Existing frame
forwarding, short/long press semantics, roof locking and reference balance pass.
The same 29,389-update fixture retains all 48 roof events while reducing local-car
queries from 46,915 to 29,389 and velocity queries from 17,570 to 6,157.
These are native-call counts in the fixture, not a measured game FPS change.

## 0.2.69 race input and car-meet closure

The short-release headlight gesture now requires the shared active gameplay
input scope and an unpaused simulation, rather than EventContext 2. This allows
races while preserving the menu refusal proven by the 0.2.68 autodrive guard.
A roof gesture additionally records free-roam eligibility and continuous
stationary status. Crossing between race and free-roam contexts cancels a
pending gesture; a menu press cannot carry into gameplay after release.

The existing local-car update observes the native presentation mode. Services
at 0x354f4f8 own the presentation manager at +0x50. Getter 0xf9ae70 uses the
manager's default ID at +0x18 and returns a retained presentation reference;
its mode is at +0x3cc. Native switch 0xfaf8b0 and the name table in 0xf908d0
identify mode 11 as CarMeet. References are released on every query.

On entry, once per local car/animation instance, the mod cancels queued opening
requests, waits the existing 350 ms for the full animation branch, sends the
explicit roof:close event and uses the existing 50x presentation speed for
750 ms. It restores the previous speed and gives animation detail back to the
game. Car-meet entry owns the visual transition; this path adds no extra fade.
Unsupported roofs are ignored. Changing car/graph restores any owned speed
before starting the new closure. No additional hook or patch site is needed.

The fixture executes the original presentation getter and mode setter against
a private retained object, tests race headlight toggles and menu input masks,
and checks closure with/without a prior mod-owned open, pending openings,
replacement cars, speed restoration, one-time dispatch and reference balance.
Complete car-meet rendering was not automated.

A not-yet-populated animation event map is retried on later updates. A native
animation speed of zero is temporarily advanced for closure and restored to
zero afterward; neither state permanently skips car-meet closure.


## Popup headlights after roof changes — 0.2.73

TOY_MR2GT_95.gsf has different headlight controllers in its LOD branches.
InRace_Focus follows the headLights scalar, while Autovista listens for separate
headlightL:open/close and headlightR:open/close events. Its range parameters default
to 1. Retaining the roof-capable detail therefore stops the normal scalar from
moving the headlights, even after closing the roof.

After branch settling, a mod-owned roof graph receives the supported popup
events matching the effective manual/automatic light state. A small retained
state emits them once per car/graph or effective-state change; stable updates
never restart the animations. Normal driving graphs and unsupported cars retain
the original path. Event lookup now accepts borrowed heap-form string views as
well as inline names, because the close-event names are 16 bytes long. Native
fixtures exercise both forms and manual/automatic changes; visual confirmation
in the actual MR2 remains an in-game check.


## Blinkers (0.2.90)

The WaitForPause radio queries at RVAs 0x964d08/0x964d38 retain their native
eligibility checks and event dispatch, forwarding only the button query through
unused padding at 0x964db4. D-pad holds suppress radio; short releases queue one
station change for at most 200 ms. Non-controller radio bindings keep the native
query. No shared input packet or mapping is modified. Radio polling also advances
the hold timer, so activation does not depend on releasing the D-pad or on the
next local-car update.

The local-car update drives the existing left/right lighting inputs at +0x51bc
and +0x51bd, already consumed by native material preparation at 0xf5c900.
The selected input stays enabled while native light timelines pulse it. Steering uses the native
post-deadzone controller value at +0xa0: 0.15 arms cancellation and 0.05 centers
it. Focus/input ownership changes cancel holds and a gap over 250 ms does not
count toward activation. One hold performs one toggle.

## Aero tuning (0.2.90)

TuningViewModel's aero initializer at 0xe9ab00 queries installed front/rear aero
levels through 0x347a50/0x347aa0. These require level 3 and feed both IsReadOnly
and range initialization. Only the two call sites at 0xe9ab3b/0xe9aba7 return
true; the equipment classification functions themselves remain unchanged.
The native AeroDefinition loaders at 0x1057635 and 0x10576b0 now pass through
padding callbacks at 0x1061234 and 0x1061064. The original 0x1061240 loader still
parses both definitions. Fixed stock downforce receives a positive range from
the matching level-3 bumper/wing row; a native same-side race part provides a
fallback if that family has none. The lower endpoint keeps stock downforce and
drag, and DefaultTuneSlider becomes zero. Existing adjustable definitions are
unchanged. The native parser at 0x105a600 and runtime AeroDefinition layout
(0x50 bytes) are exercised against all 708 stock part records by the fixtures.
The range getter at 0x344e30, slider implementation, normalized front/rear tune
values and physics interpolation retain their native paths. SQL only reads the
current game's database; no database changes or hard-coded aero lists are used.

### Native indicator timing (0.2.90)

The left/right bytes at car+0x51bc/0x51bd enable light functions; they are not direct bulb brightness. Keep the selected input asserted and let the native material controller run its repeating timeline (0xf5c6d0 / 0xf5c7c0). The previous 500 ms input gate interfered with that timeline and produced uneven short flashes. No new hooks are needed.


## Porsche 918 automatic aero with roof removed — 0.2.213

roof_aero.c augments the matching 918 GState character information before its
animation references are bound. It clones the InRace_Focus ActiveAero subtree
into Autovista and redirects only the root_boneWing pose input. Autovista's
existing speed/brake forwarding is retained, as are the native thresholds,
wing/airbrake animations, transitions and all roof controls. Original driving
branches and physics are unchanged. The manual showroom aero node remains
registered, but its wing-pose output is replaced by the automatic controller.
No replacement GSF asset or external Granny DLL is required at runtime.

The native GetCharacterInfo call at 170baa9 uses a bridge in verified padding
170bb14; original 1716e20 still performs reading/conversion. New context IDs
start above the actual maximum (the vanilla IDs have gaps), while the declared
unique-token count grows by the number of cloned contexts. Internal graph
links are remapped and the copied controller's parent/speed/brake references
point to Autovista. The copy commits only after all allocations succeed.

Allocations belong to the source GSF file and are freed after native cleanup
through its resource callback at 2f32278 (170c390). A different model or an
unsupported graph shape passes through. Matching modded graphs are augmented
semantically rather than overwritten with vanilla bytes.

Offline validation parses the real vanilla GSF, runs native Granny member
lookup/sizing, checks that only the intended three source ranges change,
compares all copied conditions/animation parameters, verifies graph links,
unique context IDs, bridge dispatch, repeat rejection and resource cleanup.
Existing roof/light/indicator/aero tests pass. Actual wing/airbrake rendering
with the roof removed still requires an in-game test.


## Controls menu gesture switches

Photo Mode now uses a 250 ms hold instead of 1000 ms. Two localized On/Off
rows are appended to the native Controls list, both defaulting to Off:

- Switch Hazards / Photo Mode: On assigns Photo Mode to a short release and
  hazards to a 250 ms hold.
- Switch Radio / Turn Signals: On assigns turn signals to a short release and
  radio cycling to a 500 ms hold.

Each gesture fires once; completing a hold suppresses its release action.
Existing keyboard/controller bindings and gameplay/menu eligibility checks
remain in use. Changing or resetting a switch cancels pending gestures until
all relevant buttons are released, without switching off active indicators.
The native Controls reset action resets only these added settings and the
original game controls. Settings sidecar version 21 appends two flags; version
20 files retain their 40 existing settings and initialize both flags to Off.
Native-image tests cover photo dispatch and radio/signals at 30/60/120 Hz,
menu construction, typed bindings, reset, persistence and v20 migration.


## Daylight projectors on the high-beam channel

The daytime beam upload override also recognizes HighBeamAmount (17), alongside
HeadlightAmount (13). The native enum table at 1ffd170 identifies channel 17;
DefaultLightControllers.xml computes it from max(Headlight, Highbeam * 2).
The shipped Rabbit GTI Lights.bin assigns all six front beam/glow instances to
17 and has no instances on 13, explaining why the earlier filter missed them.
The selected player's preset thresholds are temporarily lowered for upload and
restored immediately. Native intensity, brakes, indicators, world lighting and
other players retain their existing handling. Default/Off and car changes use
the existing GPU cache invalidation. Validation reproduces the old rejection
and covers the actual Rabbit light assignments, both upload systems and reset.
Live rendering still requires confirmation in game.
