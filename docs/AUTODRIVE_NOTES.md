# Autodrive event expansion — 0.2.136

HUD `AutodriveEvents` is appended at persistent slot 25 (settings version 10,
112 bytes). Version 9's 108-byte record preserves every old slot and defaults
the new toggle Off. The menu inserts this row above `AutodriveCinematic` without
reordering saved IDs. The master INI switch still enables/disables all Autodrive.

Native `0xf62450(car)` returns the shared RouteBindings at car+0x4f70, vtable
0x2f5d850. CarRouteTrackingComponent's RouteType registration at 0xa2cc0 names
Personal=0, PersonalSatNav=1, GroupSatNav=2, Event=3. Its activation at 0x4bd490
maps these to bindings+0x30/+0x58/+0x80/+8 respectively. The native getter
0x4bb690(binding,out) copies the route property shared reference at binding+8;
the property points to a route whose +8 byte is its validity flag.
EntityModel.zip/Templates/templates_cars.xml gives the human car all four
trackers. The private fixture runs both native getters to verify this layout.

The mod reads Event first (bindings+0x10), then GroupSatNav (+0x88) and
PersonalSatNav (+0x60) outside single-player free roam. Personal/random routing
is never used. Event routes are borrowed: no tracker creation, destruction,
property write or checkpoint/lap modification. Native f79f10 switches human/AI
input as before. A shared local-car update calls the event tick, even without
any SatNav component updates. GPS updates defer while an event binding owns
input. Events retaining only the original GPS path use the original tracker
ownership contract instead. Route changes/removal, loss of driving permission,
menus, focus loss, user driving and disabling expansion restore control and
clear rewind, as do the existing pause/context hooks.

Signals/hazards use native gameplay scope 0, unpaused simulation and driving
presentation modes 3/4/6/9/12 in every event context. The native
DisablePlayerCarControlsWrapper (8823c0/884060) and PlayerCarControl (8eb370)
push/remove 0xc0 menu scopes; their state-flow use in race_flow.xml was checked.
New activations are blocked there, while existing lights remain enabled.

Native tests cover thirteen binding/context combinations, both Xbox/keyboard,
stale GPS arrival, event property ownership, GPS-only routed events, invalid
routes and seven cancellation causes. Existing camera/UI/rewind tests pass.
These fixtures do not run a live race or validate AI route-following quality;
race/road-trip/head-to-head transitions still need an in-game check.

The notes below retain the original free-roam investigation.

# Experimental autodrive - 0.2.34

> Current controls (0.2.62): graphics options are in Settings → Video;
> drift/FOV controls, minimap orientation, Autodrive cinematic camera and championship dialogue are
> in Settings → HUD. The moved INI values are accepted only for initial migration
> into the sidecar; the drift master INI switch is retired. The notes below retain
> historical investigation details.

This module combines FH2's existing native AI input switch with a GPS event-route
tracker and live replay-camera group selection. It is disabled by default.
The target remains the executable identified in patches.json. Addresses below
are RVAs, relative to the loaded game image, rather than absolute addresses.

## Settings and game-thread entry

`Autodrive.Enabled=1` installs thirteen eight-byte callback pointers. `Cinematic=1`
enables the three-second delay, Slow Spin camera and UI drawing suppression. The settings
are read once at startup. F2 is edge-triggered while the owning process has
foreground focus; Escape, pause and loss of focus cancel. User32 entry points
are resolved lazily from the system directory on the native update path,
outside DLL initialization. No worker thread executes game methods.

| Vtable slot | Original function | Hook role |
|---|---|---|
| 0x2cc3198 | 0x6b4d30 | SatNavComponent update; original first, then mod tick. |
| 0x2cc31b0 | 0x6c0c70 | Stop before GPS deactivation. |
| 0x2cc3288 | 0x6b4e40 | Remember the native route-tracking component; original update retained. |
| 0x2cc3278 | 0x6a26e0 | Stop and clear remembered tracker before deleting destructor. |
| 0x2d16698 | 0x8e98e0 | Stop before PauseGame entry. |
| 0x2d1ea50 | 0x912860 | Stop before SetCurrentPlayerEventContext entry. |
| 0x2e7f040 | 0x1140ed0 | Wrapped WaitRenderer draw; follows the cinematic rendering state. |
| 0x2e832a8 | 0x1140ed0 | Plain WaitRenderer draw; same gate and owned-surface cleanup. |
| 0x2ecf6c8 | 0x140a800 | NumUnitConverter; reserved notice value becomes localized On. |
| 0x2ecfaa8 | 0x15b51a0 | GameApplication screen draw; suppressed during cinematic. |
| 0x2ecfaa0 | 0x15b5110 | GameApplication complete UI presentation; suppressed during cinematic. |
| 0x2f09088 | 0x15b51a0 | Base AVUI Application screen draw; same owned-root gate. |
| 0x2f09080 | 0x15b5110 | Base AVUI Application complete UI presentation; same gate. |

These use the same preflight, relocated-original recognition, module pinning,
page protection, atomic pointer writes and rollback as the existing callbacks.
Autodrive does not change game or layer files on disk.

## Native route and driver

The game global at 0x34aba60 supplies the simulation and local player. The
simulation's byte at +0x28 must be unpaused. UIContext from native getter
0x2129a0 has EventContext at +0x60; value 2 identifies single-player free roam.
Other contexts refuse activation.

Since 0.2.68, activation requires the active controller's gameplay input scope,
as well as an unpaused single-player free-roam session. The controller's deque
at +0x38 contains eight-byte owner/permission records in two-record ring blocks.
Native 0x1073090 → 0x17594B0 pushes a context to the front; 0x1072F60 removes one
matching owner. Native 0x1072890 reads only the front record and tests the requested
scope bit. Gameplay is scope 0; the menu mask 0xC0 allows scopes 6/7 instead.
A normal 0xFFFFFFFF context permits gameplay. The mask records are permission
contexts, not a set of masks to combine as restrictions.

The 0.2.67 check incorrectly rejected every record containing 0xC0, including
normal all-scope gameplay records and inactive contexts below the active one.
The corrected bounded reader follows the native front-record lookup and checks
bit 0. Empty, unreadable or inconsistent deque state refuses activation.

The shared gameplay-input predicate also gates vehicle gestures from 0.2.69.
Autodrive separately requires the single-player free-roam context, while
headlights may use active race contexts. The autodrive guard applies only when starting autodrive. The existing stop/restore lifecycle
and telemetry behavior are preserved. Both input edges are consumed before the
activation check, so closing a menu while holding F2/Down cannot queue a start.
The fixture keeps a persistent 0xFFFFFFFF gameplay entry, pushes/pops real native
menu contexts, and checks the original 0x1072890 scope reader independently.
It covers both controls, nested owners, active gameplay above an inactive menu,
ring wrap and malformed state. This updated fixture reproduces the regression
in 0.2.67 and passes with 0.2.68.

SatNavComponent has vtable 0x2cc3188, active byte +0x160 and route-control holder
+0x150. Its first route control has reached byte +0x8a and primary route +0x118;
the route's +8 validity byte must be set. The saved route identity is checked
each update. GPS arrival or a changed/cleared route returns control.

SatNavRouteTrackingComponent has vtable 0x2cc3278. Its reference wrapper at +0x98
resolves to the GPS component itself, not the owning SatNavModule. Native
0x3eb620 -> 0x3fce80 -> 0x1212820 locks the weak reference inside wrapper +0x38;
the referent/counter are therefore at tracker +0xd0/+0xd8. The owner wrapper
at +0xe0 uses referent/counter +0x118/+0x120. The mod checks live strong counts,
locks both references, checks owner routing readiness and balances their releases
before calling the route constructor, which requires both references to be live.
+0x130 is the event-routing property and +0x128 is the native tracking task.
The module calls
0x6bec10 to construct the current GPS EventRoute and start tracking only when
no task already exists. Native 0x6a5fa0 stops the task and clears the property.
Failed route construction is rolled back before the AI switch.

Native 0x21e0c0 gets the simulation's view manager; its unused extra argument is
retained by the existing binding. 0x2da060 gets an acquired local-player car reference.
The car's vtable must be 0x2c5cb80. +0x4d30 is driver mode, 1 for player and 0
for AI. Native 0xf79f10(car, enabled) switches modes and calls the underlying
driver's virtual +0x48 with the corresponding input ownership. The mod checks
the mode after each switch. Acquired car references use virtual +0x18 release.
An existing non-player mode refuses activation. A car/control change cancels.

## Slow Spin and camera ownership

The owner's shipped CameraGroups.xml associates group **17** with
IDS_CameraGroupCarRelative13_AvoidHashCollision. The corresponding shipped
English InGame.str label is **SLOW SPIN**. CarRelativeCams.xml contains the
group's orbit paths, with 25 keys per variant and a 16-second orbit repeated
over 48 seconds. Native camera parsing reads the numeric Group field directly.
Those proprietary assets are not included in this release.

The simulation's view manager (+0x98) supplies its primary view; virtual +0x38
returns the multi-camera object, validated against vtable 0x2c6c840. Native
0x399ff0 reads the back of its checked group deque at +0x1b0. The core table at
+0x188/+0x190 has 64-byte entries and a target at +0x30. **Core slot 17 is
Rear View**, not the XML label 17. Slow Spin belongs to core slot 13,
CarRelativeCams. The relative object is at camera +0x2f0; its +0x188 points
to a container with begin/end at +8/+0x10 and 0x4d0-byte camera configurations.
Each configuration's +0x20 byte contains its XML group label. The selected
configuration is the double at relative +0x748.

Native 0xfe5e30(camera, label) traverses the replay menu's full selector:
0x393310 counts core/relative choices, 0x393260 advances core slots and relative
indices, 0xfe4170 checks availability, and 0x3946c0 compares the XML label.
0xfe5780 activates the selected core slot. The mod checks label 17 exists,
the relative target is mounted, the saved selector is valid and the current
core slot is 0 or 1. It calls 0xfe5e30 with **label** 17, then confirms core
slot 13 and that the chosen relative configuration has label 17. Failure
rolls back camera/selector changes. It does not create a replay session.
Cleanup restores the previous core slot and relative selector only when the
same camera and selected relative configuration remain owned by autodrive.

## Cinematic shot loop — 0.2.30

The owner's `media/Camera.zip` / `CarRelativeCams.xml` defines 13 Slow Spin
variants with XML label 17, all ending at 48 seconds. Pan Across has XML label 19,
two replay labels to the right, with 30 variants whose last keyframes range
from 2.926999 to 35.080002 seconds. These assets remain outside this release.

The car-relative object is a `Camera::CTSCCamera` (vtable RVA 0x2c6cb70).
Its +0x740 active configuration index is established by native 0x3969e0;
type 0 uses an embedded `CCamAnimPlayer` at +0x190 (vtable 0x2e779d8).
Configuration +0x80 contains the animation data. Native 0x1035930 binds it
at player +0x188 and resets the player's +0x288 elapsed time to zero.
Native 0x1036f70 builds 64-byte animation keys at player +0x190/+0x198.
The first float of each key is time. The 0x103bf20 advance/clamp block at
0x103c1d7..0x103c22c clamps elapsed time at the final key.

The mod reads those selected-player keys and the actual elapsed clock instead
of accumulating a separate timer or hardcoding a Pan Across duration. At the
final key it calls 0xfe5e30 with the next label, alternating 17 and 19. The
native selector advances relative +0x748 and calls the active camera's update
with zero dt; 0x3979a0 notices the changed selection and rebinds the animation.
No clock writes or additional game patch sites are needed.

Every cut rechecks camera identity, core group 13, selector ownership, current
label, active configuration, player type, bound data and bounded key vectors.
Missing or malformed timing data causes no cut. Missing next-camera data or an
external camera/selection override stops cycling. Successful selection updates
the owned selector so cancellation during either shot can restore the original
driving camera. Notice lifetime and entry/exit flashes are unchanged.

The private regression fixture executes the actual native animation binding,
advance/clamp block and replay selector. It checks repeated cycles, 5- and
20-second Pan Across variants, cancellation during Pan Across, missing next
camera/player, NaN timing and external camera/selection changes. Rendering,
GPS pathfinding and full camera activation/update remain outside this fixture.

Cinematic entry no longer calls the normal/AR HUD hide routines, writes the
HUD suspension byte or retains HUD handles. The short activation notice still
uses the normal HUD's native weak-lock and balanced shared-reference helpers.

## Fast-travel flash

ResetCameraToPlayer (0x8fd810) obtains primary view 0 through 0xf9e6e0 and,
after resetting the camera, calls **0xfddae0(view, 3, 0.6f)** when show_white_out
is enabled. The direct view-manager white-out wrapper at 0x300c00 uses the
same arguments. This build makes the same call after successful Slow Spin/UI
entry, and after restoring the previous camera/UI on a voluntary toggle or
GPS arrival/route change. It does not enter the fast-travel stateflow, pause
simulation, wait for the effect, change audio or take additional input control.

The gameplay primary view is a **SplitView** (vtable 0x2c66188), constructed
by 0x2d5a50 -> 0x33b700. Its constructor calls MultiCamView's 0xfb95b0 before
replacing the base vtable. Both types share the SimulationView flash fields.
Version 0.2.21 required the exact base MultiCamView vtable (0x2e6e890) and thus
rejected SplitView. This build accepts both inspected types.
Virtual +0x38 (0x345500)
returns its multi-camera object at view +0xc0. **View +0x40 is a separate render
camera**, whose virtual +0x178 supplies the properties required by the flash.
0xfddae0 refuses an active effect at view +0xb1, sets that flag, snapshots the
render-camera property at +0x18 into view +0xb8, writes a three-frame hold count at view +0xb4
and the flash intensity 0.6 * 1.58 into view +0xbc. Native view update
0xfdc270 decrements the frame count, then blends +0xbc back toward +0xb8
until the effect completes. Earlier notes incorrectly called these arguments
an effect mode and a duration in seconds. The mod validates the view, camera
method and writable effect fields and preserves any effect already in progress.
No flash is added when another native flow replaced the selected camera, on
pause/event/destruction hooks, on focus loss, or with Cinematic=0.

## Controller input and driving cancellation

CControllerManager global 0x365d7c8 has vtable 0x2e7a2e8. Its current controller
pointer is +0xab28, initialized to slot 0 at manager +0x10 and updated by native
0x1075490 when controller activity changes. Eight physical controller slots have
stride 0x1300; the mod refuses pointers outside those slots and disconnected
controllers (+4 not 1). Native 0x1075490 sets +4 when polling succeeds and
clears it on disconnection. The optional WinRT device at +0x60 need not exist.
Native **0x1074410** receives the latest Xbox input state and copies all 16 bytes
to normalized-state +0x4c, or controller **+0xbc**. Its cached physical button
word is therefore **controller +0xc0**, with Down mask 0x0002. Native 0x1073dd0
also normalizes that input into the button bitset at controller +0x78; later
code filters directions and can synthesize additional bits. Reading the raw
snapshot avoids those changes. Version 0.2.21 incorrectly read controller +0x0c,
which is in the rumble state initialized at +8, not the input snapshot.
The mod reads it without consuming EngineInputCmd or setting native handled bits.

Down uses an independent press edge alongside F2. Reconnection, a change of
selected controller or focus loss requires one released frame before Down can
toggle again. TelemetryActive is no longer read; telemetry never blocks either
toggle, and no native handled bits or UI properties are written.

Native 0x1073dd0 normalizes the two triggers at controller +0x70/+0x71 after
applying their configured deadzones. The stick converter 0x1074a10 writes the
normalized left-stick horizontal component at +0xa0. Nonzero normalized triggers
or steering outside +/-0.05 cancel AI and restore the owned camera/UI. Right
stick and non-toggle D-pad input do not cancel AI, so telemetry can remain open.
Inputs already held when starting will cancel on the next update.

## Cinematic loading overlay

Frame11 virtual +0x328, 0x10cea70, builds a loading message and spinner batch;
it does not draw an already active batch. The previous message hook therefore
missed a spinner that was active before the cinematic began. That hook and its
MSVC string validation/cleanup are removed.

WaitRenderer virtual +0x88, 0x1140ed0, is the actual draw boundary. Both its
wrapped vtable 0x2e7efb8 and plain vtable 0x2e83220 use that function. The hook
delegates unchanged outside cinematic mode. While hidden, it skips the draw
and releases the owned graphics-wrapper argument through virtual +0x18,
matching the original consuming ABI. The native method temporarily AddRefs
through +0x10 before drawing its panel at +0x960; none of that is needed when
the draw is skipped. Null surfaces and missing panels retain native cleanup.

The same interlocked state controls the main UI draw. Loading messages, task
queues and clocks continue running; only drawing is gated. All existing stop
paths restore rendering before native pause/event flows.

## Activation notice

`Controls/HUDXPAwardControl.xaml` in the user's UI.zip is the left-of-center
two-line popup in the reference image. `XPReasonString` supplies the smaller
reason heading; `XPEarned` uses NumUnitConverter with `experiencepoints` and
the larger 36-point Anthem_A style. `Scenes/Anthem/A_Hud.xaml` places
`HUDStatusXPAward` with margin `0,0,730,50`. Its Shown/Hidden states provide the
existing translation/opacity animation and XP-earned UI sound. Assets are read
for inspection only; no modified XAML or proprietary UI assets are included.

After a successful route/AI handoff, the mod obtains the same normal HUD wrapper
through getter 0x91a4a0 and locks its weak HUD reference at
wrapper +0x50 through 0x3eb640. Both strong handles are retained until dismissal.
An unready XP category (0x34e1e38 bit 0), suspended HUD (+0xa8) or absent wrapper
registration in the HUD's +0x78/+0x80 owner registry skips only the notice.

The XP status constructor **0xac59b0** creates UiStatusXPAward (vtable
0x2d5ad58), then **0xaa8920** sets its existing native category at 0x34e1e18.
This reproduces factory 0xac2c20's initialized-category path while checking
allocation failure before dereferencing the status. The native string factory
**0x3d9820** takes a pointer to an ANSI string pointer; constructor 0x1663220
converts it into the game's UTF-16 AVUI string. It does not take a wchar_t input.
**0xacc240** sets the title at +0x120 and Int32 value at +0x128, then consumes
the passed string reference. **0x777db0** submits the status through the wrapper,
also consuming its pointer argument. AVUI references use +0x10, distinct from
the native shared control blocks' strong/weak counts at +8/+12.

The reason is `AUTODRIVE`; the value is the reserved Int32 minimum sentinel.
The new NumUnitConverter callback changes only that value, verified against
native Int32 type 0x15c8b20 and a native string parameter equal to
`experiencepoints`, into the selected language's `GameOptionValues:IDS_DisplayName_29`
string. The shipped LOC table identifies this singular On value as key `0xdfd4`.
Native table-ID
lookup `0x123d940` and UTF-16 text lookup `0x123d780` use the current string-table
manager at `0x3662990`. The UTF-16 AVUI factory `0xb943d0` copies the result and
retains the converter's ownership contract. Table contents are never changed.
Examples from the shipped tables: English `On`, Portuguese `Ligado`. An unready
manager, empty result or allocation failure
falls back to the native converter. Every other value/unit
delegates to 0x140a800 with all six arguments intact. The converter interface
slot is 0x2ecf6c8. This is presentation data: no progress, skill-score, save or
XP-awarding function is called.

At 2.2 seconds, or on any stop path, **0x7858f0(HUD, wrapper, &status, 0)**
removes the exact status from the native queues/group containers and dismisses
its active control via 0x77a6c0. Calling 0x77a6c0 alone would leave queue ownership
behind. If the owner registration disappeared, 0xaaccc0 clears the status's
active flag before removal, avoiding the dismissal routine's unchecked owner
lookup. The final AVUI reference and both shared HUD handles are then released.
The native half-second Hidden animation has time to finish before the existing
three-second cinematic delay. The message also works with Cinematic=0.

Native tests execute native localized lookup with shipped EN/br/DE/FR/JP tables,
the UTF-16 status factory, the ANSI-to-UTF16 title constructor, XP status
constructor/configuration, wrapper submission/weak locking, and queue removal
in a private relocated image. Allocators, base initialization, type queries,
property observers and actual UI queue insertion are mocked. They check both
complete lines, ordinary XP/unit passthrough, the 2.2-second deadline, cancellation,
failed title/status allocation, suspended/unregistered HUD, missing category,
disappearing owner and balanced references. Live popup placement, animation,
interaction with real skill notifications and rendering on each layer require
an in-game check.

## Whole UI rendering switch (0.2.34)

The in-game 0.2.33 test showed that the bound graphics target at the per-screen
hook can contain the 3D world. Clearing that target made the scene black while
cached HUD elements remained visible. The previous native fixture verified the
clear ABI but incorrectly modeled the target as a separate UI-only surface.
All GetTargets/ClearTarget code and forced-refresh state are removed.

AVUI has two draw boundaries. Application virtual +0x50, 0x15b51a0, renders an
owned screen with pipeline tree passes +0x88 and +0x80. Virtual +0x48, 0x15b5110,
presents the complete UI through pipeline passes +0x70 and +0x78. The latter
has no owned screen argument and was not gated in the earlier rework. Native
frame rendering at 0x2251d0 calls Application +0x48 at 0x2255436 after the UI
frame setup; screen rendering continues on the separate 0x1421500 path.
Skipping screen refresh alone does not cover the complete presentation call.

Both functions exist on GameApplication vtable 0x2ecfa58 and base AVUI
Application vtable 0x2f09038. Four callback slots cover these two boundaries on
both types. The complete-presentation hook delegates unchanged when visible
and does nothing while hidden. The screen hook also delegates unchanged when
visible; while hidden it only consumes the passed root reference through the
native refcount at +0x10 and 0x257730 final-owner destruction.

The same interlocked cinematic flag gates both WaitRenderer vtables. There
is no per-control registry, visibility override, graphics context access,
target clear/bind, pending redraw or persistent HUD ownership. The native
dirty scan at 0x140b3c0 is unpatched. UI logical updates, caches, skill queues
and animation clocks continue to run. Existing stop paths restore presentation
before pause/event entry, cancellation and arrival. Cinematic=0 retains the UI.

## Evidence and limits

The autodrive test executes the original native AI input-switch, checked group
deque, replay-label traversal/matching, wrapped weak-reference locks, UI screen draw and dirty scan in a private
relocated image. UI submission dependencies are mocked; no GPU work executes.
The original primary-view getter and 0xfddae0 flash routine also execute unchanged
with both SplitView and MultiCamView. The render-camera properties getter is mocked.
The original **0x1074410** input update and its native conversion routines fill
the controller snapshot for every fixture tick, with memset and atan2f supplied
by the test host. The fixture leaves the optional WinRT device null
and sets a conflicting rumble field, so the previous reader fails these checks.
Controller fixtures exercise native processed axes/triggers using physical
deadzone magnitudes (not fractions), with memset and atan2f supplied by the test
host. Telemetry availability and active state have no influence on either toggle.
The original WaitRenderer draw runs with its panel submission mocked, checking
both installed vtables, consuming references, null arguments and missing panels.
Native Application +0x48 and +0x50 execute outside cinematics with only the
renderer operations/pipeline submissions mocked. Both are suppressed while
hidden, across both vtables. A shared-world-target fixture records graphics
access and clears: neither hook may access or erase it. Native dirty caching
and UI/HUD snapshots remain unchanged. Restoration and repeated camera
switches are covered. This is an ownership/submission regression test;
it does not exercise GPU compositing on either translation layer.
Existing effects,
missing render cameras, arrival, native camera overrides and immediate pause
cleanup are checked separately.
Controller availability changes and actual setup/handoff failures are logged.
Routine button presses, start/stop reasons, activation notices, flashes and
camera cycles do not write log entries. The on-screen localized activation
notice is retained.
GPS scheduling, route construction, camera activation and UI service getters
remain mocked. It checks F2 edges, delayed cinematics, label/core-slot distinction,
relative selector restoration, unchanged HUD state, final screen-owner releases,
expired notice HUD references, missing labels/targets and camera changes by other native flows.
The existing refusal/arrival/cancellation and thirteen callback transaction checks
are retained. No proprietary assets or game/host binaries are distributed.

The release adapter is tested through the real inspected Azimuth helper loader,
including autodrive on/off, Cinematic=0, master disable and invalid settings.
The existing regression suite covers the retained features separately.

These checks establish mod state/ABI/ownership behavior at the inspected
boundaries. They do not prove AI navigation quality, traffic behavior, braking
at arrival, live camera selection/animation or graphics on either layer. The
EventRoute and native AI combination remains a gameplay experiment and may
need additional native driver configuration after the user's in-game test.
