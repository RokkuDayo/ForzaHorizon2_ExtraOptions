# Pause-menu garage: Get In Car

> Current placement control (0.2.62): Settings → HUD → Change Car Position
> replaces SnapToRoadOnCarChange. Nearest Road is the default; Same Location
> leaves the native stored position and heading unchanged. The choice applies
> to the next swap without reinstalling a callback or restarting.

Supported image base: 0x140000000. All addresses below refer to the supported
original FH2 build; runtime writes use RVAs and accept relocation.

The pause-menu garage factory at 0x140B09230 constructs the ordinary
CarSelectGarage page. Its registration function at 0x140B36E70 assigns page ID
0x1C and the name CarSelectGaragePauseMenu; the page ID lives at object+0x228.

The shared garage action-menu builder at 0x140CCF600 first compares the selected
garage-car ID with the currently driven ID. Its branch at 0x140CCF651 skips the
change-car entry when those match. That branch is preserved.

The following comparison checks page ID 0x1C. The two-byte conditional branch
at 0x140CCF660 skips construction of action 0 specifically on the pause page.
The label binding is 0x14354BD20, initialized at 0x140160090 from IDS_Get_In_Car.

| Patch | Value |
|---|---|
| RVA | 0xCCF660 |
| Raw file offset (verification only) | 0xCCEC60 |
| Original bytes | 74 74 â€” JE 0x140CCF6D6 |
| Enabled bytes | 90 90 â€” two NOPs |
| INI setting | Frontend.GetInCarFromPauseMenu |

The existing popup callback's vtable is 0x142DDEE70. Its invocation thunk at
0x140CDADE0 enters 0x140CC34E0, which reads the chosen action ID at menu-item+0x68
and calls 0x140CCC370 with the selected garage record.

Action 0 branches to 0x140CCC638. This path calls 0x1402DE4E0 with the selected
garage ID and current profile, calls the selected-car service virtual method at
vtable+0x1A8, requests a profile save through 0x1402F3820, and dispatches
car_selected. That event is registered at 0x140140C20. There is no additional
page-ID rejection in this handler.

Version 0.2.0 restored only this action's visibility. The user's in-game test
showed that selection returned to pause without a vehicle swap. The handler
updates the selected car and saves the profile, but does not reload the world car.
The earlier documentation and mocked test labeled the save call as a world-load
request; that label was incorrect and has been corrected.

## State-flow diagnosis and confirmed 0.2.1 fix

Media/stateflow.zip contains ui_screens.xml. Its leaf state
car_select_garage_pause_menu maps the UIPageCarSelectGaragePauseMenu behavior's
car_selected transition directly to .back. In pause.xml, that back exit goes to
pause_menu.enterback. This explains the observed behavior.

The DLL changes two raw XML resources immediately before their original parser
runs. In ui_screens.xml it adds a car_selected exit to that one state and maps
that behavior's car_selected event to .car_selected. In pause.xml it adds a
matching exit to the _car_select_garage_pause_menu node, targeting a six-step
chain inside pause_menu_flow:

1. global.store_car_position
2. ui_screens.show_empty_scene_and_continue
3. global.unload_player_car
4. global.load_player_car
5. global.restore_car_position
6. global.reset_camera_to_player

These are the first six nodes of the shipped car_reload.root flow. The chain
then targets the existing unpause_and_exit_to_hud node. Its .exit_to_hud exit
returns to pause_menu_tiled_flow, which restores the HUD, enables car control,
unpauses, and clears the pause mixer snapshot. The added chain uses the outer
pause flow's GameControlWrapper rather than taking control through a nested
wrapper. Back, Car View, and all other menu states retain their original routes.

## Native resource hook

The CXML4Resource constructor at 0x1413DDD70 assigns its primary vtable
0x142EC4F38. Resource processing invokes virtual slot +0xA8 at 0x1413D6723.
That slot normally points to 0x1413DE7E0, which consumes the raw text and creates
its XML stream, freeing the raw buffer on successful stream creation.

| Item | Value |
|---|---|
| Parse vtable slot RVA | 0x2EC4FE0 |
| Original parse function RVA | 0x13DE7E0 |
| Raw load method RVA | 0x13DE5A0 |
| Cleanup method RVA | 0x13DE900 |
| Resource filename | MSVC std::string at +0xB8, length +0xC8, capacity +0xD0 |
| Raw XML pointer / length | +0xE0 / uint32 at +0xE8 |
| Game allocator / free RVAs | 0x3229C0 / 0x31C7B0 |

The DLL installs a native function pointer in the parse slot, separately
validating the original pointer after relocation. It calls the original parser
by its original function address, avoiding recursion through the hooked slot.
Before installing it, GetModuleHandleExW pins this DLL for the process lifetime;
the callback remains valid even if the optional XG probe releases its handle.
The aligned eight-byte vtable pointer is written with one native pointer store,
and the DLL includes x64 unwind metadata for callbacks.
Only basenames ui_screens.xml and pause.xml are eligible for garage changes. Edits require unique,
known leaf-state/node anchors and original transition/exit text; searches use
the explicit raw length and reject oversized, ambiguous, embedded-NUL, or already
modified input. Replacement memory comes from the same game allocator as the
original loader. The old buffer is freed once; the original parser/cleanup owns
the replacement buffer through the normal resource fields.

Since 0.2.35 the parse slot also serves the independent car-ineligibility dialogue
option for race_trigger_activation.xml; see RACE_DIALOGUE_NOTES.md. A disabled
garage option leaves both garage resources unchanged even if that shared hook
remains installed. Failure states are separate.

The two-byte action gate and eight-byte vtable slot participate in the same
startup preflight, permission changes, and rollback as the other tweaks. The
vtable slot is not normalized in the EXE file hash. It therefore must still be
original on disk. Disabling GetInCarFromPauseMenu restores both original sites.
The master switch skips all writes, preserving its established behavior.

If a matching resource cannot be transformed or allocated, the hook hides Get
In Car for the session and stops further menu-flow edits. Logs distinguish hook
installation from the two resource callbacks. Archives are never modified, and
no game XML file is included in this package.

The native resource tests execute the game's raw load and cleanup methods with
mocked file I/O, heap, and parser dependencies. They validate field layout,
callback dispatch, transformation, pass-through, and buffer ownership. Tests also
parse the edited documents and follow the complete reload/resume route. The user confirmed that 0.2.1 successfully changes cars in-game through this
callback. The user confirmed 0.2.2 displays the loading screen and cinematic, but
reported muted loading audio and missing car controls afterward. See the 0.2.3
correction below.


## 0.2.2 loading screen and fast-travel arrival

The user confirmed the swap works, but the empty UI scene exposes an odd camera
angle while the vehicle loads. The revised chain shows
ui_screens.show_fmv_loading_screen_and_continue with the same loading_reason as
fast_travel.pre_fast_travel. It yields one frame before unloading the old car so
the loading UI can be presented. BASE_Loading is enabled/disabled as a matched
pair around loading. Replay recording is stopped and restarted around the swap
and cinematic.

After the proven unload/load/restore sequence, the new car's controls and rumble
are disabled, it is stopped and held with its parking brake, and its camera is
reset under the loading screen. Only then does the flow load the cutscene. This
avoids preparing its player-car binding against the vehicle being discarded.
The flow waits for cutscene loading, world streaming at the current position,
and render readiness at LOD0 before revealing the scene. The existing render
readiness state includes its shipped ten-second fallback.

The native fast-travel flow identifies the cutscene as fast_travel_cutscene on
entity global/pause_flow, with is_sibling=false. Templates/templates_global.xml
in EntityModel.zip assigns that component to pause_flow and names its cinematic
fasttravel_camera_01.xml. The cinematic exists in Cinematics.zip; its CarProxy
binds player1 and uses NoCarPositioning=true. The arrival therefore uses the new
player car at the restored location.

The added flow uses the original load, wait-loaded, play, wait-complete, unload,
and white-out camera-reset arguments from fast_travel.fast_travel_to_position.
It unpauses before playback while keeping input disabled and the parking brake
applied. The FMV screen is replaced by the empty UI scene only after
play_cutscene has performed its own cinematic-readiness wait and started
playback. After completion, the flow unloads the cutscene, resets the camera
with show_white_out=true, restores replay recording and rumble, releases the
parking brake, and uses the existing pause exit. This first version omitted the
matching enable for its own input disable; the outer pause enable released only
one of two locks. Version 0.2.3 corrects that below.

No FastTravelMoveCar event, fast-travel destination placement, or road-snap state
is invoked. The ordinary fast-travel state group is not modified. Only the
pause-garage route gains this presentation. The DLL loader, hook slot, fixed
patch records, and all INI settings remain the same as 0.2.1.

Local tests follow the complete route, verify its state references and required
parameters, compare loading/cutscene arguments to the shipped fast-travel flow,
and verify reveal/cleanup ordering, loading snapshots, and replay recording.
These checks did not originally model the separate input holds. The user's
0.2.2 test confirmed cinematic playback and identified the cleanup issue.

## 0.2.3 input and audio cleanup

PlayerCarControl's entry at VA 0x1408EB370 dispatches disable to 0x141073090
with mask 0xC0 and the owner key from 0x1434CEEF0. It applies the operation to
eight input gates at [0x14365D7C8]+0x10+i*0x1300. Disable inserts an (owner, mask)
deque record using 0x1417594B0, including duplicates. Enable calls 0x141072F60,
which finds and erases only one matching owner record. These are counted holds,
not boolean flags. The original pause flow already disables car controls. Our
cinematic added another disable, but only the outer pause exit enabled them.
One mask 0xC0 remained, blocking car input while leaving pause-menu input usable.

The new _fh2_car_control_on node calls global.enable_car_control after cinematic
completion, unloading, camera reset, and replay restart. The outer enable is
unchanged. Each disable now has a matching enable, as in ordinary fast travel.

The original RadioPauseWrapper pauses radio on entry and sets the inverse value
on exit. global.unpause_radio calls the same setter with false. It now runs
before the FMV loading page, matching the original fast-travel ordering.
BASE_Paused was also left active throughout the added chain. AudioManager routes
snapshot changes to AudioSnapshotManagerFmod at VA 0x141005780. Enabling increments
AudioSnapshotInfo+0x18; disabling decrements and removes the active snapshot when
its count reaches zero. The chain suspends BASE_Paused before BASE_Loading and
returns its reference just before exit_to_hud, allowing the unchanged outer
snapshot disable to balance it. BASE_Loading retains its matched enable/disable.

tests/test_transition.py executes the original deque and snapshot operations against
the transformed route in a private mapped image. Snapshot-name lookup is mocked;
deque manipulation, snapshot counts, and active-vector insertion/erasure execute
natively. Omitting the three new cleanup nodes reproduces one leftover input
hold and active BASE_Paused during loading. Five corrected swaps finish without
leaks across all eight gates and preserve an unrelated owner's hold. Both audio
snapshot counts and the active list finish empty. These tests do not execute the
stateflow scheduler or FMOD. The user confirmed restored controls and audio in game with 0.2.3.

## 0.2.6 shorter loading and direct audio handoff

The added world-streaming and LOD0 wait nodes were removed. The game still
waits for the native car request to complete and for cinematic jobs/readiness.
The arrival sequence now exits through ._fh2_car_resumed and a dedicated outer
HUD/control/unpause route. It does not re-enable BASE_Paused after the animation.
The pause snapshot is consumed exactly once before loading, and each input
disable is matched by its own enable. Normal pause exits keep their original
routes. Native tests reproduce the earlier pause mix reactivation and verify
unrelated snapshot references survive ten repeated swaps. The user confirmed
this release working, with the shorter wait and corrected audio.

## 0.2.7 nearest-road placement during loading

The global snap_player_car_to_road behavior only adjusts ground height at the
current X/Z; it does not choose a road destination. FH2 also has a native reset
placement enum: NearestRoad=0, NearestEventRoad=1, NearestSatNavRoad=2,
LastStablePosition=3. This release borrows mode 0 for a synchronous calculation.

The first added node now references a local _fh2_car_store_road state whose
StoreCarPosition behavior carries the literal locator_name=_fh2_car_road and
restore_facing_next_event=false. This was unsafe: before any fallback, the original behavior invoked locator
lookup on a route component that was null in the pause garage. The user's
0.2.7 run crashed there. See the 0.2.8 correction below. A
callback in the behavior's OnEntry slot calls the original first and only then
acts on this exact marker. All other native StoreCarPosition uses pass through.

| Item | RVA / layout |
|---|---|
| StoreCarPosition OnEntry vtable slot | 0x2D275D0 |
| Original OnEntry | 0x93CA40 |
| String parameter getter | 0x8898C0; locator parameter at behavior+0x118 |
| Local player / world car lookup | 0x21E0C0 / 0x2DA060; game singleton 0x34ABA60 |
| Native nearest-road destination calculation | 0xF5DB40 |
| Native road neighbor/heading selection | 0x19179E0 |
| Car reset mode / routing bindings | car+0x4F94 / car+0x4F70 |
| Closest road node | [[bindings+0xA8]+0x0]+0x10 |
| ForzaCar / RouteBindings vtables | 0x2C5CB80 / 0x2F5D850 |
| Instance owner | [car+0x4D00], virtual +0xC8; collection at owner+0xD0 |
| Existing PlayerState lookup | 0x52BF80, MSVC short-string key PlayerState |
| PlayerState vtable / stored position / stored forward | 0x2D44EF0 / +0x10 / +0x20 |
| Native RestoreCarPosition OnEntry | 0x8FDFF0 |
| Native world placement / vertical ground adjustment | 0xF713D0 / 0xF71420 |

The current car's routing bindings already contain its closest road node. The
native query chooses an adjacent polyline point using the current facing and
returns the road node's position plus a full orientation basis. The callback
validates node/edge/neighbor pointers and finite coordinates, borrows reset
mode 0 with pop_history=false, and immediately restores the original mode. It
rejects success results that do not match that road node (the native routine
can report success from its stable-position history fallback). A valid result
updates only the stored position and forward vector in the native PlayerState.

The original unload/load sequence follows. RestoreCarPosition consumes that
cache, builds a full basis from the stored forward vector, and performs native
placement and terrain adjustment on the replacement car. This occurs after
the loading page is shown and before cinematic loading/playback. Neither an
extra streaming wait nor a hand-of-god reset event is introduced. The ordinary
fast-travel flow is unchanged.

Missing or unsupported navigation/cache data keeps the original stored
position and produces a fallback log; it does not block the swap. The transient
car reference is released through its native intrusive-release virtual method.
The new pointer shares complete preflight/permission/rollback and module
residency with the existing hooks. Frontend.SnapToRoadOnCarChange defaults to 1;
0 disables this hook, preserving the established position restore.

tests/test_road.py executes actual road adjacency, destination/heading math, the
StringParam getter, and RestoreCarPosition against a private native image. It
records the physics placement rather than executing it. Test cases cover
lateral movement from an off-road stored position, headings in both directions
and on a perpendicular road, missing/invalid road data, incompatible or
event-facing caches, ordinary-store pass-through, history fallback rejection,
unchanged reset mode/history, balanced references, option disable, and callback
unwind metadata. The XML test verifies restore occurs under the loading page
and before the arrival cinematic. Full in-game nearest-road behavior is pending.

## 0.2.8 correction: do not activate locator lookup

The user's 2026-10-10 session log records a read of 0xE8 at game RVA 0x5E0D9B.
The caller is RVA 0x93CCC1 in StoreCarPosition, followed by xg.dll+0x881C.
RCX is null and R15 has the expected SaveStates::PlayerState vtable. The game
crashed inside the original store before road_marker or the road query ran.
The stock routine enters this path whenever locator_name has nonzero length.
It resolves the route_transforms component, then calls the locator helper
without checking that component pointer. The previous assumption that a
nonexistent locator would safely fall back was incorrect.

The corrected XML behavior has entity_id=_fh2_car_road, locator_name="", and
restore_facing_next_event=false. Its entity_id StringParam starts at behavior
offset +0x28 (literal short-string value +0x60), while locator_name retains its
default empty string at +0x118 (value +0x150). The native routine checks the
empty locator first and branches straight to its current-world-position
store; it does not use entity_id on this path. The hook compares the entity_id
marker after the stock store completes. All road calculation, cache validation,
and native restore behavior remain unchanged. This also fixes the same XML
path when SnapToRoadOnCarChange=0 and the road hook is not installed.

The earlier road test replaced the entire native store with a recorder and
therefore missed this branch. tests/test_road.py now executes the complete original
StoreCarPosition and its native cache setters. Only service/instance lookup,
thread context, short-string construction, and simulation getters are mocked.
An unexpected locator-path dependency is trapped. The logged seven-byte load
instruction is exercised with null input in a private leaf fragment, producing
the expected access violation, while corrected marked/ordinary stores all
complete. Both option settings, stored positions/headings, reference balance,
reset history, road fallbacks, and the original RestoreCarPosition are checked.
The user subsequently confirmed 0.2.8 and nearest-road placement in-game.
Version 0.2.10 retains this garage and road implementation unchanged.

## Pause Return to Garage — 0.2.90

ActivationPromptComponent::Activate at RVA 0x407b20 locks its location component,
checks 0x99ecd0 and 0x559860, then calls 0x564320. The latter reads TargetFlow at
component+0xb0 and invokes 0x5641b0 with the original entity context. None of these
entry functions compares the player's distance to the festival. The prompt's
availability and spatial checks precede this call.

The 0.2.82 log showed timeout before any native activation attempt. An idle
GameStateMachineComponent is not subscribed to GameIndependentUpdate: 0x563f90
adds the subscription via 0x19053e0 only when a flow starts, and cleanup removes
it via 0x19051d0. Waiting for the garage's idle update cannot start its first flow.

The replacement hook is the component's normal activation lifecycle: vtable
RVA 0x2c98d10, original 0x559260. The native function runs first. A garage target
captures its self weak reference at +0x30; this references its +0x28 interface,
as used by native 0x1759580/0x535260. A retained weak count keeps the control block
valid without retaining an old world. Shared/exclusive SRW locking protects
replacement and weak-lock acquisition. Shared use counts are acquired only while
positive, avoiding the native throwing lock on an expired weak pointer.

The confirmation still waits for modal closure before native Back. The existing
local-car vehicle update calls return_garage_tick after its normal update.
When a request is pending, free-roam simulation, gameplay input and the native
activation gate must be ready. The weak location is locked for the native call,
the request is consumed once, and the temporary shared reference is released.
No controller gesture or autodrive option is needed for this update. Idle calls
return after checking the pending flag. Timeout logs distinguish unavailable
location references from simulation/input/activation gates.

The mainland target is garage.root_festival. Storm Island requires the component
name suffix island_destination_hub/activation_state_machine, excluding city hubs.
It enters a small garage._fh2_return_island wrapper around the existing native
destination_hub pre/garage/post states. The native garage pre_functional_location state gains one global.wait_for_time
node with time=1.0 after asset/cutscene setup and before show_empty_scene. This
keeps the native loading scene visible while streaming continues. Other garage
states and the Return to Garage portions of pause/ui_screens remain unchanged.

The native fixture gives an idle garage no flow updates, captures its activation,
and dispatches from the independent regular tick. It executes native activation,
Back, modal-close and default StartFlow wrappers with the deeper factory supplied.
It checks expired-reference rejection, weak-count replacement/release, unrelated
hubs, menu/input/unpause gates, cancel and one-shot handoff. Vehicle control
regressions cover the added local-car tick call. Full gameplay is unverified.

## 0.2.214: fail speed zone on Change Car entry

SpeedZoneSpeedComputationComponent update slot 0x2cc38c0 (original 0x6ba2f0)
records only active (+0x3b9) components using their standard weak reference
(+0x30, secondary object at +0x28). No scans or strong retention between calls.
The existing CarSelectGarage initializer hook calls speed_zone_car_selection
before native 0xccd450, limited to page ID 0x1c at +0x228.
The cache is consumed, locked to a strong reference and type/active checked.
Native 0x6a2ae0 broadcasts SpeedZoneAborted through the owning entity and tail
calls 0x6a80b0, which clears active flags, releases activation/car reference
+0x3a8/+0x3b0, unregisters updates, cancels the optional event and clears HUD.
Normal pauses, other car pages, inactive and expired components are untouched.
Cancelling the picker does not restore the failed attempt.

Validation: work/speed-zone-car-change/test_speed_zone.py runs native abort and
reset code in a private relocated game image, mocking only outer event/HUD
services. Nine checks cover ordering, weak expiration/destruction, replacement,
inactive/type guards, one-shot abort and actual car reference release. Existing
vehicle-controls and manufacturer-picker regressions pass. No live game test.

## Prologue guard

Return to garage now requires the profile's native `iestate` stage 4, matching
`IsIEComplete` at game+0x89e490 (record getter game+0xa65ca0, stage at +8).
Missing state and incomplete stages block the added footer label and confirmation,
including rebound keyboard/controller shortcuts. Queued travel checks the same
condition again before location activation and cancels if it is no longer met.
This does not change progression or save data. The native menu fixture covers
incomplete/unknown/missing stages, completed-stage operation and queued cancellation.
