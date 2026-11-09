# Forza Horizon 2 Extra Options

0.2.212 adds an optional private media directory for initialized DLL plugins.
Its files supplement only the owning active mod; ordinary mod media keeps priority.
This supports SPGarage 1.2 embedded camera resources without files in mod/media.
The 64-byte initializer ABI and existing callbacks remain unchanged.

0.2.211 adds an optional game-thread in-memory database callback for DLL plugins.
It supports the SPGarage port without writing binary pages into gamedbRC.slt.
The existing initializer ABI remains64bytes and existing plugins are unaffected.
Successful mod media is enabled before loading providers. Startup waits at most
five seconds for the loader worker, then invokes ready providers before the
native database factory returns; timeout/pending callbacks are logged and
handled on a subsequent local-car update. No per-frame SQL repeats.
The SPGarage port is distributed separately with its dealer flow and camera XML.

0.2.210 supports database mods with dangling declared foreign-key references
through an explicit policy.foreign_keys setting: error (default) or allow.
Allow preserves those values as-is; it does not invent missing parent records.
Operation validation, ID conflicts, update guards, atomic rollback and SQLite
integrity checks stay enabled. The loader logs when a mod opts into allow.
The diff tool accepts --foreign-keys allow; drag-and-drop prompts for the policy
when the selected export introduces such references. Its report lists the child
keys, referenced fields and missing parent values. Existing strict behavior is
retained when the policy is absent. Both the exporter AND DLL need this update.
The supplied nfslyy database exports successfully (4257 inserts, 32 overrides,
3 deletions, 66 changed tables; 48 preserved references). The scratch merge
compares all 288 tables against the edited source. Both-adapter native loader
checks pass, including the real export (REAL values to12 significant digits in
the SQL-text native fixture). Source databases and settings are unchanged.

0.2.209 pauses the world when entering Photo Mode from Car Meet car view.
It temporarily stops simulation while paused and uses the native game-manager
pause request, including renderer/animation notifications. Closing Photo Mode
restores the previous pause state and simulation policy. Garage/autoshow and
free-roam Photo Mode are unchanged. Private native pause-gate, lifecycle and
preview camera/flow tests pass; the visual result needs in-game confirmation.
Keep your existing INI and settings .dat.

0.2.208 removes the inherited orbit pitch restriction only while using Photo
Mode from garage/autoshow car previews. The native photo pitch range is retained;
free-roam Photo Mode, normal orbit cameras and zoom behavior are unchanged.
Car Meet car view now offers Photo Mode on X, with its own [Bindings] entries:
CarMeetPhotoXbox=X and CarMeetPhotoKeyboard=None. View keeps Change Camera.
All Jump to Manufacturer pickers now sort their displayed manufacturer names
alphabetically, independently of recommended cars and the current-car pin.
Native flow, input, actual pitch-clamp, resource, localization and free-roam
camera regression checks pass. Visual confirmation still requires the game.
Keep your existing INI and settings .dat; the new binding has its default even
when the keys are absent. Add those keys only if you want to customize it.

0.2.207 fixes the crash when a category volume reaches zero in 0.2.206.
The native audio command dispatcher now receives its required command pointer.
The revised regression reproduces the old null-read and exercises the actual
dispatcher and fader handler for all five categories, including restoring volume.
The results-screen prompts and Master Volume fix remain included.

0.2.206 restores Continue, Restart and Watch Replay prompts on one-off race
results while retaining the single-race columns. The added prompt state uses
native localized HelpButtons strings and is reset for other result screens.
Category volumes now reach silence at zero through the five native Audio Options
VCAs. Above zero, their normal game-controlled curves are retained. Changes use
the existing FMOD command queue and lock; unloaded mixer groups retry after load.
The working Master Volume control is unchanged. Native queue/fader tests, results
selection and real-resource/menu tests pass; audible and visual checks need the game.
Garage revving remains research only and is not included in this build.

0.2.205 adds Speed Zone Vignette directly below Skill Counter Shake in HUD
settings, default On. Off passes zero weight only at SpeedZoneActivationComponent
's dynamic-post update; the native speedZone blend, cleanup and world baseline
remain intact. It can be changed during gameplay. All 16 supported locales have
labels/descriptions. Settings version 19 appends the choice and preserves version
18 choices, defaulting the new toggle On. Both adapter call-site tests, menu/resource
checks, localization and Photo Mode regression pass; live rendering needs testing.
Drivatar livery quality has not been changed: local designs currently reuse stored
texture streams. Independent MSAA/supersampling needs a separate cache-generation
path, not the existing shared player/editor render-quality controls.

0.2.204 makes Photo Mode Hide UI hide the bottom button-bar backdrop too.
The native Invisible, FullScreen and Watermark visibility states now target the
outer BottomBar rather than only its prompt content. Returning to the normal UI
restores the bar through the existing visual-state system. Picture, Effects and
PreSave states and the photographic vignette effect remain unchanged. Both adapter
UI fixtures and the native resource/menu regression pass; visual confirmation
requires in-game testing. All 0.2.203 audio ownership fixes remain included.

0.2.203 fixes a critical mod-bank filename ownership bug introduced in 0.2.199.
Replace 0.2.199-0.2.202 before using mod-folder audio banks. The redirect used
native move operation 0x228ed0 with a borrowed registry alias, so audio-object
destruction cleared the permanent alias and passed its non-game-owned buffer
to the game allocator. Windows crash dumps from the reported freezes show heap
corruption detected during this audio filename destruction. The redirect now uses
the native string copy assignment, giving every audio object its own game-owned
filename. The permanent file mapping remains independent of audio-object lifetime.

Tests reproduce the previous invalid free through the real native destructor,
then verify 64 load/read/destroy cycles on each adapter, overlapping objects,
independent filename allocations and reopening the same registry alias after
destruction. Native audio open/read/seek/close remain in use. FMOD queue flags,
unloading and the working ANSI bridge are retained. These tests validate the
ownership fix; sustained in-game driving still requires confirmation. Photo Mode
Headlights and all other 0.2.202 changes remain included.

0.2.202 refreshes the native car-light material/render packet whenever the
Photo Mode Headlights option changes or restores its Default state. The previous
version only wrote the gameplay flags, which normally reach the renderer through
the paused car update. The new path calls only the lighting-expression publisher,
with a single lighting fade-settling tick; vehicle/physics updates remain paused.
Sampling restart and entry-state restoration are retained. Both adapters pass a
native light-expression/fade/render-packet test without any car update. The menu,
photo capture and vehicle-control regression checks pass; visual confirmation
is still required in-game. The 0.2.201 addon-audio fix is user-confirmed working.

0.2.201 connects the game-configured FMOD file callback to the registered mod
asset bridge. AudioManager installs an ANSI CreateFileA callback; the earlier
bridge only covered CreateFile2, used by FMOD's default backend. The 0.2.200 log
showed requests being queued without any bridged disk open. Only registered ASCII
aliases are handled by the new hook; ordinary ANSI calls retain the host path
handling and all arguments. The physical mod path remains Unicode-safe, and native
FMOD read/seek/close, queue flags and bank lifetime remain unchanged.

Both adapter fixtures reproduce 0.2.200's failure using AudioManager's actual open
callback, then read the complete real Falcon bank with the fix. Native read, seek,
close, mod priority, Unicode folders and ordinary-file fallback checks pass. The
AUDIO BANK diagnostics remain for confirming playback in-game. Photo Mode
Headlights and all 0.2.200 changes are retained.

0.2.200 adds Headlights directly below Driver in Photo Mode Effects, with
Default/Off/On choices. It uses the existing native headlight controls, restarts
sampling when changed, and restores the entry state on exit. The heading is
translated with the other mod UI strings; choices use existing native collections.

The addon-bank silence remains under investigation. This build logs actual FMOD
load results, disk-open errors, and changes reported by the native bank-loading
state getter under AUDIO BANK. It preserves native queue flags, return values and
ownership, and does not wait for loading or retain bank objects. A Falcon load
in-game is still needed to identify the remaining failure. Offline tests execute
FMOD's native OS-open path (including conversion and file metadata), verify the
real bank bytes and priority, and verify diagnostics preserve load/state results.
FMOD playback is not reproduced by these tests.

0.2.199 fixes the second mod-bank loading path. AudioContentBankFmod requests a
disk filename and hands it directly to FMOD, bypassing the normal virtual-file
opens. The native disk-path method still returned the synthetic mod mount path.
For banks resolved to mod folders, the loader now returns a registered disk alias
through the existing native file bridge. The original virtual bank identity,
audio queue, FMOD ownership and unload behavior remain unchanged. Native bank
loading now reaches the real Falcon bank at FMOD's file-open boundary in tests;
actual playback needs an in-game test. The log records up to 16 redirected bank
paths per process under AUDIO BANK.


0.2.198 restarts full Photo Mode sampling when Driver, Time of Day or Weather
changes. Unchanged selections and the capture/save states keep their existing
sampling. LB/RB in Jump to Manufacturer now skips three columns.

Mod-folder audio banks now participate in the native pre-queue hash lookup.
The old folder method reported zero without a hash manifest, so native engine
bank discovery skipped these files. Existing mod .bank files now return the same
nonzero marker (1) used by the native unhashed loose-folder scan. Missing files
stay zero, mod priority is preserved, other file hashes are unchanged, and the
original queue, bank lifetime and sync/async reads are retained. No string-bank
JSON generation or append system is involved. Native fixtures pass with the real
Falcon bank; full engine audio playback still requires an in-game check.


0.2.197 fixes the Photo Mode preset spinner crash: game LocalizedString objects
were incorrectly passed to the AVUI collection. Presets now resolve their actual
GameOptions2/TheIsland string IDs and are copied into native AVUI strings, like
the other settings. The headings resolve from GameOptions2 too. The scrollbar has
a separate 40px gutter to the right; effects keep their existing width and spacing.


0.2.196 adds temporary Time of Day and Weather controls to Photo Mode Effects.
Both begin at Default and use the game's localized presets; unavailable weather
presets are omitted for the current map. Default returns to the conditions captured
on entry. Leaving Photo Mode restores those conditions and their original override
flags. Effects scroll inside the panel, preserving label/slider spacing. These
selections are not saved to the INI or settings file. Native fixture checks passed;
visual scrolling and environment rendering still need in-game confirmation.

FMOD string-bank JSON appending and its diff/export commands have been removed.
Ordinary bank file replacements continue through the existing asset loader.

0.2.195 fills blank engine-swap names with the donor car's localized name when
AllEngineSwaps is enabled. Existing engine names are preserved. The engine menu
resolves those references at display time. No part IDs or physics data change.

0.2.194 names the added transmissions after their donor cars, using the original
localization references so the native upgrade tiles show their names correctly.
Gearbox IDs and physical data are unchanged.

0.2.193 adds the default-off `[Experiments] AllTransmissionSwaps` option. It copies
existing gearbox variants (including stock donors), with their ratios, final drive,
shift time and inertia, into each supported drivetrain's transmission choices.
Donor variants keep stable identities even when ratios coincide. Names use the
donor car's localized display name; the description lists final drive and ratios. Added
swaps allow full ratio tuning. Existing authored upgrades remain unchanged.
Modded gearbox rows loaded before expansion are included when their drivetrain
IDs are 1–8191 and original levels 0–7. Other IDs retain authored choices only.
Enable before starting the game; keep enabled while using saved added gearboxes.
This experiment increases database size, initial load time and menu length.

0.2.192 keeps festival-garage entry behind its existing native black/spinner
screen until streaming completes. The garage camera starts under the screen,
then a frame lets its streaming focus update before WaitForStreaming. Only then
does the native flow snap the car and reveal the garage. There is no fixed
loading timer or distance threshold; already-loaded entries pass the native
readiness wait. Island hub flows are unchanged. Far-distance in-game testing
is still required.

0.2.191 groups database ID conflict handling under policy.id_conflicts. Older
top-level settings remain compatible; conflicting duplicate definitions reject
the mod. The batch diff tool prompts for both runtime policies and explains
carbins inherit matching ID remaps from their mod’s database policy.

0.2.190 removes the stock-wheel lock from the native front/rear rim-size menu,
so the current car factory wheels can use its existing diameter upgrades.
Normal available sizes and purchase paths remain native. Aftermarket maker
picker tile backgrounds are now fully white (#FFFFFF).

0.2.189 restores literal stock-rim manufacturer names while retaining native
localization for aftermarket references. Aftermarket picker tiles now use light
backgrounds and dark text; stock-rim and car-manufacturer tiles retain their
existing dark style. Selection keeps its orange underline.

0.2.188 resolves aftermarket rim manufacturer labels through the native
localized database field reader. Database string references (_&...) now use
the same resolution path as the native upgrade UI; literal addon names remain
supported. Rim selection, logos and bindings are unchanged.

0.2.187 fixes the missing rim manufacturer picker by attaching it to the
actual Wheel Styles pages, which have separate native lifecycle hooks from
general upgrade parts. Both standard and Everyman wheel pages are covered.
Y and the independent RimJumpToManufacturer bindings remain as in 0.2.186;
View keeps its native Setup Manager action.

0.2.186 moves the rim manufacturer picker to **Y**, preserving **View** for
Setup Manager. Configure it separately under [Bindings] with
RimJumpToManufacturerXbox=Y and RimJumpToManufacturerKeyboard=Tab.
Existing INIs that omit these keys use those defaults. Car-menu manufacturer
jump bindings are unchanged.

0.2.185 adds **Jump to Manufacturer** to every rim-style category, including
All and Stock. The picker lists the manufacturers present in the current
category, uses their rim-brand logos, and opens on the selected rim's maker.
Selecting a manufacturer jumps to its first rim; cancelling keeps the selection.
It uses the existing configurable manufacturer-jump binding (View by default).
On rim pages this takes the View footer slot normally used by Setup Manager.
Other upgrade categories keep their original controls.

0.2.184 removes the unsupported cutscene claim from Player Character's
description. Player Character, Open World Drivatars and the Photo Mode Driver
label now follow the game's language. Missing translations for skill-counter
animation, skill-point notifications, power/spring units, and the Ben/Ashley,
wheelspin and checkpoint descriptions are filled across all 16 supported
language codes (15 translation tables). FOV headings are localized too.
The open-world restart warning is retained in every language. Native rendering,
settings defaults and saved values are unchanged from 0.2.183.

0.2.183 removes the experimental TL_Player_01 cutscene mesh substitution
introduced in 0.2.182, which could crash the native track renderer during race
loading. Player Character still selects the driving model; cutscenes keep
their original character until a complete native render binding is available.
Photo Mode controls and all other existing features are retained.

0.2.173 replaces carousel-handler repetition with menu input turbo. After
holding LB/RB for 500 ms, normal native release/press pairs are routed through
the WindowLayerManager, including the game's Scene input handler. This covers
carousels that consume the buttons before AVUI receives them. Gameplay input
is excluded. Configure [Frontend] MenuTurboIntervalMs (default 100; 20-1000;
0 disables). The paint booth's owned/all-car Select Car pages now also offer
Jump to manufacturer. F1 UI hiding from 0.2.172 remains included.

0.2.171 prevents the shared StartDriving callback from rebuilding the local
livery cache at race Go. Only the native single-player free-roam context may
open that cache gate; races retain their already prepared designs and start
immediately. Cold boot and return-to-free-roam preparation still work while
the simulation is paused by loading.

0.2.170 makes **Stock Cars Only** apply exclusively to Drivatars in standalone
offline races. Player selection and eligibility use the fitted car's class and
division. The event-wide native stock override stays disabled so choosing a
different garage car preserves its installed upgrades during loading. The AI
stock flag is captured separately with the committed setup; changing or
rebuilding the pending menu does not alter it. Stock opponents retain factory
class eligibility and receive no PI upgrade target.

0.2.169 adds **Power Units** and **Spring Units** directly below **Units** in
HUD settings. Choose Default / hp / PS / kW and Default / N/mm / lb/in / kgf/mm
independently. Default follows the game's existing unit preference. Native
conversion factors, localized labels and tuning conversions are reused; other
measurement categories retain their original mappings. Choices persist in
version 16 of the settings sidecar, with older choices preserved and both new
selectors set to Default. The native mapping is selected atomically from
immutable snapshots; no database or tuning data is rewritten.

0.2.168 adds DLL plugins from Azimuth's `plugins` folder or Mezcal's
`Mount/plugins`, loaded once on a deferred worker. No manifest or hook framework
is required. See [PLUGINS.md](../PLUGINS.md) for the optional initialization API,
mapped game address, loading rules and a small C example.

HUD settings add **Skill Point Earned Notification**, default On. Turning it
Off suppresses its pop-up and accompanying dialogue. **Wheelspin Notification**
now also suppresses its intro/reminder dialogue. Skill points, wheelspins and
the post-race wheelspin screen remain on their normal paths. Existing `.dat`
choices migrate to version 15 with the new toggle On.

0.2.167 moves free-roam preparation to the final native StartDriving handoff.
The early traffic/player-load waits remain unchanged, removing the 15-second
startup dependency introduced in 0.2.166. Free-roam preparation uses model IDs
from instantiated Drivatars and the native upcoming roster, without requiring
render interfaces or base livery handles. Instantiated models take priority.
The working race assignment hook is retained.

`[Experiments] MaxDrivatarLiveries=6` sets the maximum decorated
cars and prepared designs (0-32). Six is the default, including with an older INI.
Each spawn gets one 50% eligibility draw. Choices remain stable while cars stay
loaded; the cap is a maximum, not a guaranteed count. This distribution is a mod
policy, not a confirmed vanilla percentage.

This build adds an experimental local-design source for Drivatars in free roam
and races. `[Experiments] LocalDrivatarLiveries=1` (default) uses matching
saved designs from the active profile. Set it to `0` and restart to disable.
The native Drivatar livery preference is respected. With no matching design,
the car keeps its native appearance. The player and traffic are excluded.

The native save API enumerates the library during loading. Matching designs
are selected from the current native car lineup, opened through the native
container API and prepared through native mode-5 display loading. Only ready
resources replace a car's appearance. A 15-second timeout lets loading continue
with native paint if preparation fails. No editor regeneration is used.

During driving, new Drivatars can reuse already prepared matching designs.
There are no new library enumerations, design file reads or texture preparation
requests on the gameplay tick. Cars without a prepared matching design retain
native paint. The bounded memory cache stays resident until the next race or
free-roam loading transition, which refreshes it and releases previous handles.
Despawn releases appearance snapshots without touching stale car pointers.
The native renderer controls final asynchronous resource release.

The existing `Turn10Temp.scratch/LiveryCache` stores downloaded designs using
64-bit server livery IDs. Mapping saved-header identities to those IDs has not
been established.
Local containers already contain saved texture streams. This implementation
uses those streams and native loading calls with a bounded memory cache. It
does not rewrite the server cache manifest or invent server IDs.

Designs are matched by the car ID in standard `Livery_<car ID>_<slot>` names.
Manually renamed containers without that car ID are skipped and logged;
resave those designs through the game to create a standard slot. New or changed
design slots are refreshed at the next loading transition. Disabling the feature
restores the original appearance of live cars. No saved data is changed.
Native fixture checks pass; the new loading integration needs an in-game test.

The unsuccessful design-thumbnail changes remain completely removed.
Manufacturer shoulder jumps remain four columns. The open-world Drivatar
setting description warns that a restart is required.

Pre-release **0.2.173** by Arthur (RokkuDayo).

Audio adds **Master Volume** directly below **Preset**. All six volume sliders
reach **0**. Master controls the final output, defaults to full volume, and saves
in the existing .dat. Other-car volume remains unchanged.

Audio also includes **Ben's Photo Mode Dialogue**, **Ashley's Garage Dialogue**
and **Checkpoint Sound Effect**. HUD includes **Wheelspin Notification**. All
four default to On and apply without restarting. The dialogue options control
Ben's repeated-photo line and Ashley's recurring garage greeting. Turning off
the free-roam wheelspin notification preserves the reward. The post-race
wheelspin message remains visible. Existing saved options are retained.

`[Frontend] CarsNextToHome=1` places **Cars** immediately after **Home** in the
main pause menu. Set it to `0` and restart to restore the original tab order.
It defaults to `1`; the native Cars actions and other tab order stay intact.

Buy Cars and My Cars now have **Jump to manufacturer**. Press **View** on a
controller or **Tab** on a keyboard to open a native scrolling logo grid built
with the vinyl picker's `CommonListBox` / `VirtualizingTilePanel`. Select a make
to jump to its first car in the current list, or cancel to keep your selection.
The list follows your current filters and includes available addon makes. View
still opens Photo Mode in the separate 3D preview.

Manufacturer logos now bind directly to the same native image source as the
car carousel. My Cars jumps past the pinned current car to the manufacturer's
regular group when one exists. The dark tiles preserve logo proportions, and Select/Cancel prompts use the
native footer. The scrollbar has a fixed track below the tiles; fixed dimensions
and layout rounding stabilize the scrolling viewport.
The grid targets the active Anthem popup and native binding type. Jump/cancel
behavior and the separate preview Photo Mode controls remain unchanged.

Database mods no longer require `addon_id`. Optional `"policy": {"id_conflicts": "nearest"}`
resolves conflicting inserted IDs in load order: later mods keep their requested
IDs, and earlier opted-in mods receive the nearest free IDs. Matching database
and carbin references are updated in memory. Conflicts and final assignments
are logged. See [MODS.md](../MODS.md) for scope and reference declarations.

Buy Cars previews retain the localized **Take photo** button prompt after
the menu rebuilds its button bar. Existing Photo Mode controls are unchanged.

HUD → **Custom Showcases** defaults to **Off**. Enable it to choose any owned
car directly from the race signup picker before entering a career showcase.
Its saved garage setup is restored after the showcase prepares its preset and
immediately before the final player-car load; the forced showcase PI is cleared.
The original showcase options, scripts and race flow remain in use. Back cancels
entry; the option also supports choosing the current car and restarting the race.
Selecting a car now skips the redundant immediate reload; showcase preparation
loads it once through the native route. Existing HUD settings are preserved.
The previous selection/racing flow was confirmed in-game; this loading
optimization still needs an in-game timing check.

Render parts bound to a loose/generated carbin can now open full virtual asset
paths through the complete filesystem, including original ZIP models and new
loose addon models. The original car ZIP stays intact. The BRZ example uses the
existing street-wing menu level so its new part has a menu definition.

Carbin JSON patches now merge individual upgrades/models at runtime from
`mods/<mod>/media/cars/<car>/<car>.carbin.json`. The standalone Python tool
exports/imports carbins and compares vanilla/edited files to produce a patch.
Independent part additions merge in load order, and `mediaoverride`/`mediapc`
keep priority. See [CARBIN.md](../CARBIN.md) for commands, limits and a BRZ example.

**Experiments → AllEngineSwaps** defaults to Off and expands Engine Swap to the
complete combustion-engine catalog when enabled. Disabled skips catalog creation
and its private database copy unless needed by database addons. Authored swaps retain their IDs, prices and
physics; new swaps use stable part IDs, donor engine mass, the recipient's engine
placement and native engine upgrade data. The stock engine remains the original
return-to-stock choice. Runtime addon engines/cars are included before profile
and car caches initialize; the installed SLT is unchanged. Keep the option enabled
when loading cars saved with added swaps. Electric powertrain conversions are not
added. Car and engine IDs from 1 through 32767 are supported by the stable added
part namespace; larger IDs retain their authored swaps and produce a log notice.

Engine catalog insertion builds its indexes after the bulk copy, removes temporary
catalog tables, and indexes engine-stock, per-car level and engine-parts lookups.
Signals and hazards work during driving in free roam and races. Native driving
input permissions, presentation and focus checks reject menus and prevent held
menu gestures from becoming vehicle actions after closing them.
Existing active lights remain on. Hood and bumper camera updates return directly
from the native update without sampling or applying drift correction, in every mode.

HUD → **Show All Events** exposes the online free-roam race setup points offline.
It starts off. Offline Event Options supplies class, division, Drivatar count,
grid order, stock-only cars, weather, time and circuit lap count. Teams and
Collisions are omitted; these races use one player group and ordinary collision
behavior. Standalone events use the ordinary non-career
results path instead of waiting for a career race or advancing championships.
The race launch, AI grid, credit payouts and corrected car-picker transition
have been confirmed in-game. XP results use the normal offline breakdown, and
standalone races supply localized track titles to the intro and race menus.
Standalone event intros use the game's existing Horizon Solo localization from
PauseMenu, preserving the native region name. Standalone races block the native finish timer at its start call and suspend
the countdown HUD, including its independent fallback timer. This covers the
single-player-group path that bypasses the bucket option. Ordinary online and
career race countdowns remain native. Standalone results now select the native single-race
presentation used online, choosing circuit, point-to-point or team layouts from
the selected race mode. The ordinary championship points column is omitted for
single races; results navigation and the offline rewards flow remain unchanged.

Event Options adds **Car Division** directly below **Car Class**, defaulting to
**Any**. Select a division such as Track Toys to restrict the current car,
owned/buy-car pickers and Drivatar models alongside the class selection. Names
and memberships come from the runtime database, including addon divisions and
cars. Drivatar upgrade targets follow the player's current PI with a varied spread
of roughly fifteen display points, clamped to the selected class. Stock cars
already above that target range are excluded when suitable alternatives exist.
The game's native upgrade system builds the opponents' setups; achievable PI
can vary with the parts available for each model. The shuffled pool cycles
through eligible models before repeating, independent of garage ownership.

**Drivatar Count** ranges from 0 to 11 (default 11). **Grid Order** offers Default,
Random, Lowest PI First and Highest PI First; Default preserves the route preset.
**Stock Cars Only** defaults to Off. On uses the native factory-parts/tuning
restriction, filters the player and AI by factory class, and disables the mod's
AI PI upgrades for that race. Ordinary races keep player-relative PI balancing.
**Traffic** appears only for Street Race setup, defaulting to On. Turning it off
suppresses that race's traffic; all other race types keep traffic disabled.

The event car picker and buy-car page filter by the selected class, using upgraded
classes for owned cars normally and factory classes with Stock Cars Only. Unrestricted setup remains unrestricted. After a filtered
selection, the flow follows the native car reload/cutscene path straight into race
preparation. The initial current-car eligibility check remains. If the player's
PI cannot be read, division-restricted AI pools use stock models at or below the
class limit; Any division uses the highest eligible stock class. If every eligible
model exceeds the target PI range, the nearest stock model is used without
requesting an impossible downgrade.

Returning from pre-race submenus refreshes the localized track title with correct
ownership of the native string reference, preventing its previous double release.

All mod action bindings are editable in **[Bindings]** in the INI. Each action
has independent `Xbox` and `Keyboard` entries; either input can trigger it.
Xbox combinations use `+`, for example `HeadlightsXbox=LB+Y`. Keyboard entries
accept letters, digits, F1–F24, named keys, or a Windows virtual-key code such as
`0xBA`. Use `None` to disable either input. Omitted entries retain the existing
bindings. Restart after editing; invalid names are rejected with an INI error.

The actions are `Autodrive`, `CancelAutodrive`, `Headlights`, `Roof`, `SignalLeft`,
`SignalRight`, `Hazards`, `PhotoMode`, `ShowroomPhoto`, `ReturnToGarage`, `JumpToManufacturer` and `RimJumpToManufacturer`.
For example, `RoofKeyboard=R` assigns R while `RoofXbox=RightStick` remains
available. Roof still requires a stationary 1.5-second hold, signals a 500 ms
hold and free-roam Photo Mode a one-second hold. Assigning headlights/roof or
hazards/Photo Mode to the same input preserves their tap/hold distinction.
Menu and focus guards remain active. Vanilla game actions are not remapped by
these INI entries.

Addon database SHA-256 mismatches now log a warning with the expected and actual
hashes and continue through normal database validation. Missing active mod
folders are removed from `loadorder.txt`; comments, disabled entries and existing
order are preserved. Turn signals use a 500 ms hold and roofs a 1.5 second hold.
The D-pad Up Photo Mode hold remains one second.

Showroom Photo Mode retains the confirmed movement fix from 0.2.120: the photo
camera owns its movement update, preventing the shared preview path from damping
held-stick travel. Effects still stops the camera and normal preview controls
resume on exit. The renderer's track-settings merge is now also suspended for
that photo view: this merge rewrites saturation, sepia tint and vignette fields
after the sliders apply them. Camera and dynamic overlays remain suspended too;
all three layers resume on exit. Other views and free-roam Photo Mode stay native.
The preview entry/return flow and mixed-case **Take photo** label are unchanged.

Addon car ZIPs now open on demand after host initialization, fixing the placeholder
car that could appear when starting the game in an addon vehicle. Per-archive
diagnostics in `FH2_ExtraOptions.log` show registration, physical reopen results
and the first car-file lookup outcome.

The loader discovers mod folders and appends new ones to `mods/loadorder.txt`
beside Azimuth.exe, or under
`Mount/mods` beside the Mezcal game EXE. Each listed folder may contain a
`database.json` and `media/`. Database additions and explicit overrides apply to
an in-memory copy before the game builds its car lists. Later mods win for
assets; `mediaoverride` and `mediapc` retain higher priority. Thumbnails follow the native `media/stripped/RC<number>/` paths or RC ZIPs.
Loose `media/ui/videos/*.bk2` files also override Bink videos through registered
virtual filenames, including in Azimuth. Windows paths remain inside the mod
file-opening bridge rather than being passed to the translation layer. Optional `strings.json` files add or override localized text in the
native string tables, with per-language entries and English fallback. Addon
stock rims discover their models and AO textures from the donor carbin rather
than requiring additions to the vanilla routing header. Media-only mods are supported; `mod.json` is optional. Mod ZIPs are
registered automatically from their own directory metadata, so creators do not
need `zipmanifest.xml` or a separate car list. See [MODS.md](../MODS.md) for layout, JSON rules and examples.

Ending autodrive clears the native rewind history after restoring human control
and closing the temporary GPS route, then restarts recording. Rewind history
builds again from manual driving, so it cannot restore the AI driver state.
This applies to every autodrive exit, including cancellation, arrival and menus.

A short D-pad Up press toggles hazard lights on release. Holding Up for one
second opens Photo Mode while the button is still held, subject to the game's
normal availability checks. A Photo Mode hold does not toggle hazards, and
steering does not cancel hazards. Menus ignore hazard and turn-signal gestures,
including Photo Mode and the garage. Release the D-pad after leaving a menu
before starting a new gesture. Already active lights stay active.

The Fast & Furious Fairlady Z now has a separate stock-rim entry with its own
front/rear models and textures. The regular/Team Forza set retains its existing
ID and model. A virtual wheel row extends the native wheel queries in memory,
including installation and reload metadata, without editing the database file.
Both entries use their donor names in selection and Buy and Install prompts.

D-pad Left/Right holds of 500 ms toggle the corresponding blinker during
active gameplay. Short presses change the radio station on release. Blinkers
switch off on a repeated hold, or after steering moves away from center and
returns; activating them with centered steering leaves them on. Menus ignore
blinker gestures. Selecting the other side replaces the active indicator.

`[Experiments] UnlockAeroTuning=1` enables experimental aero tuning without Forza
race bodywork. It is off by default. Fixed aero definitions use the car's Forza
race-aero slider default, downforce and drag ranges; a race-aero fallback applies
when no matching part exists. Already-adjustable definitions remain unchanged.
There is no added zero limit or factory-value remapping. **Enabling this tweak can
change PI and handling even before moving a slider.** A startup warning is shown and logged when enabled.
Set it to `0` and restart to restore the original equipment locks and ranges.
Aero tunes made with earlier builds may need resetting after the range change.

Blinker holds are also checked directly during radio input polling, so the
lights engage at 500 ms while the D-pad remains held. Releasing a long
hold does not change stations or toggle the indicator again.

Return to Garage uses the current map's native garage entry, including its
parking-brake handoff, fade and loading screen. No extra timer or scenery wait
is added.

The added stock-rim list now uses the same dark tile background as the vanilla
stock slot. NULL CAR, PG BUS and PG BoxTruck rims are hidden from this list;
already installed parts remain loadable. Traffic Audi S4 rims use the existing
neutral AO texture in their donor archive instead of the incompatible wheel map.
The Scirocco AO correction remains separate.

Livery resolution, MSAA and sampling are now independent Video-menu overrides.
Default preserves the current translation layer's settings; explicit choices
apply after restarting. Photo Mode Samples starts and resets to Default.
Previously saved photo choices are preserved.

Compilation now uses parallel jobs and caches unchanged objects. Azimuth was
built and validated first. See the build guide for --jobs and --rebuild.

This update fixes accented stock-rim names by keeping the donor localization key
until the UI requests its UTF-16 name. The top manufacturer logo preserves its
image proportions inside the existing tile.

In the free-roam pause menu, **X / Return to Garage** opens a native confirmation.
Confirm enters the festival garage on the current map; Storm Island stays on
Storm Island. Cancel leaves the pause menu open. The shortcut requires the
supported pause and garage flows and is unavailable during races and car meets.
It follows the normal garage transition and exit flow.

The Azimuth adapter in this release supports **only Azimuth v0.9.10** with executable
SHA-256 `78a5f635b3241918707b5acddeaa96bd59ccb3819490e9218ee152497ca73639`.
Other builds show a version warning and receive no mod patches. The mod's
2× MSAA fragment-mask compatibility patch has been removed; Azimuth handles it.
The save checksum/size option and missing-marker repair remain available.

Photo Mode Samples and Real-time Shadows remain in Video as overrides. Default
uses the current translation layer's settings. The photo override recognizes
Azimuth's native High/Ultra values, fixing the startup refusal at
`photo_mode_sample_density`. Unexpected values still cause a safe refusal.
Real-time Shadows now offers Default / Off / On. Both descriptions are shortened
in all supported languages for the original two-line description area.

The Driver drift-camera group is retuned using the supplied first-person
reference clip: a progressive look into the drift, a quicker crossover and
more damping to avoid spring overshoot. Sensitivity now scales the body-rotation
correction as well as the target angle; values 1 and 2 no longer enable a full
rotation correction. Only the cockpit uses this tuning; hood and bumper retain their native behavior. Manual look,
native grip behavior and the chase controls retain their existing paths.

Driver sliders remain 0–100. Sensitivity covers zero to full momentum following
with finer low-end control. Range covers 0–45° and eases toward its limit. Look
Speed covers slower to faster settling with critical damping. Driver defaults
are **70 / 55 / 65** for Sensitivity / Look Speed / Range.

Settings use a 104-byte version-8 sidecar. Older layouts migrate without losing choices.
Legacy Livery INI keys import once, then the Video menu owns those choices. Existing explicit shadow Off/On choices
are preserved when upgrading from version 6. Earlier driver settings migrate as follows: The previous default driver trio
adopts the new defaults; customized driver values convert to the closest supported
strength, response frequency and angular limit. Other choices are preserved.
If you customized the driver settings and want to try the reference profile,
use **Reset to Default in Settings → HUD**; this also resets other HUD choices.
Alternatively set just the three Driver sliders to 70 / 55 / 65.

The compact stock-wheel asset table now contains 348 wheel sets, which
share 33 model layouts and 38 texture layouts instead of repeating eight full
paths per set. The generated header is about 90% smaller. Authored corner paths,
donor aliases and optional texture fallbacks remain the same. Wheel lookup uses
a sorted table; paths expand into bounded local buffers when a wheel changes.
Path lookup needs no new file scanning, runtime archive parsing or allocation.

The game still opens donor archives and loads replacement geometry through its
native functions. The table supplies the authored paths those functions need.
Existing INI files and in-game settings remain compatible.

The Azimuth adapter continues to load through `winmm.dll` beside the launcher,
retaining its original TLS helper and refusing unsupported executable hashes.
Small local reads replace repeated same-process ReadProcessMemory calls and are
protected by Windows structured exception handling. Invalid pointer, inaccessible
page and page-crossing tests retain safe refusal. A Windows microbenchmark shows
lower read overhead; a game frame-rate or Wine improvement has not been measured.

The DLLs have product/version metadata and reproducible unsigned builds. They
remain unsigned. Antivirus results for this release are **unverified**; see
[ANTIVIRUS_REVIEW.md](../ANTIVIRUS_REVIEW.md) for the source rebuild audit, observed
detections and vendor-review route. No antivirus settings are changed.

The patch engine removes the patch engine's fixed 64-page tracking limit. The tracker
now sizes temporary storage from the patches that actually need changing,
including patches that cross a memory-page boundary. Existing byte validation,
rollback and per-page protection restoration remain active. Unrelated edits and
page permissions from other mods are preserved.

Stock rims retain localized donor names, and the MR2 pop-up headlight fix from
0.2.73 is included. Existing INI files and saved settings remain compatible.

Stock rim swaps now load their models from the donor car archives. Each set uses
the four geometry and texture paths authored in that car's definition, including
front/rear variants and model-name aliases. Original rims and aftermarket wheels
keep their native loading paths. The database and car ZIPs are unchanged.

Headlights can be toggled during races and free roam. Both right-stick
controls require active gameplay input, so menus ignore them. Roof gestures
remain limited to stationary free roam; car-meet entry requests the closed roof
state once and clears queued openings.

Rim Style gains a separate stock-rim library alongside the combined aftermarket
category. It contains 348 loadable stock rim sets, including the separate Fast &
Furious Fairlady variant; 345 are displayed after filtering hidden vehicles. It
uses native localized names and manufacturer icons. The current car's original
stock-rim entry keeps its normal restore-stock action. Both extra categories use
`Frontend.AllRimsCategory`; the game database is unchanged.

The previous build refactored input handling, vehicle state, settings migration,
localization catalogs, bounded text searches and host-version selection. Vehicle
controls reuse their retained car reference and stop checking velocity after a
hold is consumed. UI properties use a direct validated lookup; PNG validation
uses a compact CRC table. The existing controls and saved settings stay compatible.

Gamerpic file reads now handle short transfers. Failures separately report opening,
size queries, allocation, reading, PNG validation and decoding. File errors include
Win32 codes and expected/loaded byte counts; PNG failures include a reason.
The reported Wine message came after opening the file and before decoding.
The read handling was tested on Windows with simulated short reads/errors/EOF;
Wine execution was unavailable here, so the specific Linux cause is unconfirmed.

Right-stick vehicle controls are available during active gameplay:

- **Press and release the right stick** in free roam or a race to switch the headlights on or off.
- **Hold the right stick for 1.5 seconds while stationary in free roam** to open/close a roof
  that has native open/close animations. A long hold does not toggle the lights.

Removable roofs fade to black before changing, then fade back in. The roof
change runs at the game's native 50× presentation speed while covered. Cars
with full folding animations retain their normal visible animation: the 2011
BMW Z4, 2009 Ferrari California and 2013 Mazda MX-5. This distinction comes
from each car's ForzaVista cinematic selection.

Controls run independently of Autodrive or GPS. Menus reject both gestures, and car meets keep roofs closed. Short presses toggle headlights;
a consumed 1.5-second hold never toggles them. Cars without roof events ignore
the hold. Menus, focus loss, controller disconnects and car changes cancel a
pending transition and clear the fade. The underlying roof and headlight toggles
were confirmed on the Honda S2000 CR. Visible folding animations, including the BMW Z4 iDrive, retain their normal
presentation.

The graphics descriptions explain that Default follows the translation layer. The description box is back to its original 100-pixel height.
Its width and the brightness preview retain the original 806 pixels.

## In-game settings

**Settings → Video** contains Brightness, Subtitles and these graphics controls:

| Control | Choices | Default |
| --- | --- | --- |
| Real-time Shadows | Default / Off / On | Default |
| Photo Mode Samples | Default / 256 / 512 / 1024 / 2048 / 4096 | Default |
| 4K Resolution Liveries | Default / On | Default |
| Enhanced Livery MSAA | Default / 4x MSAA | Default |
| Enchanged Livery Sampling | Default / 2x | Default |

These graphics changes take effect after restarting the game. Default follows the
current translation layer for each option; explicit choices override it.

**Settings → HUD** contains all seven drift-camera controls, then the five FOV
sliders, followed by:

| Control | Choices | Default | Applies |
| --- | --- | --- | --- |
| Minimap Orientation | Follow Car / Follow Camera | Follow Camera | Immediately |
| Autodrive in Events | Off / On | Off | Next activation; disabling stops an active event drive |
| Autodrive Cinematic Camera | Off / On | On | Next autodrive activation |
| Skip Championship Dialogue | Off / On | Off | After restart |
| Spawn Location | Default / Car Meet Entrance / Festival Site | Default | After restart |
| Change Car Position | Nearest Road / Same Location | Nearest Road | Next car swap |

All controls and descriptions are localized for EN, GB, BR, DE, FR, ES, MX, IT,
JP, KO, CHT, CZ, HU, PL, RU and NL. Brazilian Portuguese retains Câmera de Drift
and Piloto. Autodrive remains untranslated in every language; its Brazilian
cinematic toggle is Câmera cinematográfica do Autodrive. The menus use FH2's native list, spinner and description controls.
The HUD list is now 1,000 pixels wide to give camera labels more space; the
description panel retains its native layout. Reset to Default changes only the
selected settings page, including all camera controls on HUD.
Change Car Position is the final HUD entry. It works with GetInCarFromPauseMenu.
Same Location preserves the original stored position and heading; Nearest Road
uses the existing guarded road lookup. Storm Island retains its normal startup
placement for every Spawn Location choice.

The default drift mode remains Driver (cockpit only). Slider defaults
are 35 / 70 / 80 for chase and 70 / 55 / 65 for driver views. FOV defaults are
48.5° / 58° / 48° / 66° / 48° for Chase / Far Chase / Driver / Hood / Bumper.

## Upgrading

When upgrading Azimuth from 0.2.74 or earlier, follow the new installation
steps below: restore its original TLS helper and move the mod/settings beside
the launcher. For Mezcal, replace xg.dll as usual. Keep your existing INI,
Gamerpic.png and FH2_ExtraOptions.dat beside the mod DLL. The older sidecar's
choices are preserved. The seven previously moved INI settings are imported only
from pre-version-4 sidecars. The old SpawnAtFestival, SkipCarMeetOnStartup and
SnapToRoadOnCarChange keys seed the two new placement choices once when upgrading
to version 5; later in-game choices take priority. Keep your old INI during the
first launch so its placement switches can be imported. Old sample
counts between presets use the nearest offered value. You can then remove those
old INI keys. The supplied INI contains only the remaining startup options.

Marketplace, Social, Clubs and Rivals are enabled offline by default. Existing
INIs can independently disable them using EnableMarketplace, EnableSocial,
EnableClubs and EnableRivals under [Frontend]. This makes the tabs accessible;
server-dependent features still need services that the translation layer provides.

The mod's source and build inputs are in `source/`; tests, tools and research
notes are in `source/development/`.

## Install with Azimuth 0.9.10

1. Close Azimuth and the game.
2. Restore Azimuth's **original** `data/bin/azimuth-tls.dll` from the matching
   Azimuth download or your backup. Do not leave an older Extra Options DLL there.
3. Place this release's **winmm.dll beside azimuth.exe**, not in `data/bin` or Mount.
   Keep an existing third-party winmm.dll intact; this release does not chain other proxies.
4. Copy your existing `FH2_ExtraOptions.ini`, `FH2_ExtraOptions.dat` and optional
   `Gamerpic.png` from `data/bin` to that same launcher folder. Keeping your .dat
   preserves the in-game settings. The supplied INI can be used for a new installation.
5. Launch normally. `FH2_ExtraOptions.log` is now beside winmm.dll. Look for
   `AZIMUTH: winmm proxy initialized; original TLS helper retained.`

No Azimuth or game file is replaced. The proxy forwards all 180 named Windows
multimedia exports and ordinal 2 to the installed system winmm. Initialization
happens on the launcher's timer call, outside DllMain. It briefly intercepts
one loader import, waits for the original TLS helper to load, then restores that
import before patching FH2. The game base comes directly from the validated loader
context; there is no process-wide image scan, polling thread or external injector.

This build supports exactly the inspected Azimuth 0.9.10 executable:
`78a5f635b3241918707b5acddeaa96bd59ccb3819490e9218ee152497ca73639`.
A different executable, including a different build with the same version label,
causes a warning dialog and disables Extra Options before any game/compatibility
patches are applied. There is no INI bypass. Original Windows multimedia calls
continue to work. An already modified loader/import is also refused.

The game directory still comes from `[title-1a96545b] folder` in
`data/azimuth.ini`, with the legacy launcher key as a fallback. Absolute and
relative game paths are supported; nothing depends on a specific drive or folder.
Selecting another title leaves Extra Options inactive.

For Wine, configure a **native, then builtin** override for `winmm` in the prefix
used to launch Azimuth (equivalent to `WINEDLLOVERRIDES="winmm=n,b"`). Otherwise
Wine can select its builtin winmm without loading the mod. Forwarding uses the
system directory inside the Wine prefix. Wine execution was unavailable during
validation, so this loader still needs a Linux smoke test. The override syntax
is documented by Wine: https://www.winehq.org/pipermail/wine-cvs/2010-June/067477.html

## Install with Mezcal

1. Close the game.
2. Put `xg.dll` and `FH2_ExtraOptions.ini` in the game's `Mount` folder, beside the EXE.
3. Put your optional `Gamerpic.png` in that same folder.
4. Launch normally. Check `FH2_ExtraOptions.log` for
   `Forza Horizon 2 Extra Options 0.2.98 - xg.dll hook` and an `OK:` line.

Replace an earlier version of this mod's `xg.dll`. This adapter relies on the
optional XG lookup observed in the tested Mezcal build and provides no graphics
exports. An unrelated existing `xg.dll` cannot be used alongside it.

## Options

Edit `FH2_ExtraOptions.ini` using UTF-8 or ASCII and restart the game.
Both `[Global]` and the older `[General]` master section work.

| Option | Values/default | Effect |
|---|---|---|
| Global.Enabled | 0 / 1; 1 | Master switch. |
| Files.EnableMediaOverrides | 0 / 1; 0 | Searches mediaoverride, then mediapc, beside the game EXE before original media. Supplied INI uses 1. |
| Content.UnlockDLC | 0 / 1; 1 | Offline DLC entitlements and catalog access. |
| Content.OfflineVIP | 0 / 1; 1 | Local VIP status and offline rewards. |
| Content.UnlockStormIslandCars | 0 / 1; 1 | Removes event requirements for buying the Ranger T6 and #7 SST; normal prices remain. |
| Content.UnlockTreasureMap | 0 / 1; 0 | Enables the native Treasure Map reveal on the mainland and Storm Island, independently of UnlockDLC. |
| Frontend.SkipIntro | 0 / 1; 1 | Skips the opening video and its wait. |
| Frontend.EnableMarketplace | 0 / 1; 1 | Enables owned-pack browsing in the pause Marketplace. |
| Frontend.EnableSocial / EnableClubs / EnableRivals | 0 / 1; 1 | Enables these pause tabs while offline; unavailable services are not restored. |
| Frontend.GetInCarFromPauseMenu | 0 / 1; 1 | Change cars through the pause garage, with a loading screen and arrival animation. |
| Compatibility.DisableParentalLocks | 0 / 1; 1 | Local multiplayer and user-created content privilege override. |
| Compatibility.DisableAzimuthSaveChecksums | 0 / 1; 1 | Skips save blob SHA-256 and recorded file-size mismatch rejection in the inspected Azimuth 0.9.10 builds. Rebuilds missing commit markers at startup. No effect on Mezcal. |
| Profile.CustomGamerpic | 0 / 1; 1 | Uses Gamerpic.png beside the mod DLL for the local player. |
| PhotoMode.FreeCamera | 0 / 1; 1 | Removes photo camera collision and position bounds. |
| Autodrive.Enabled | 0 / 1; 0 | Experimental F2 or D-pad Down toggle for native AI driving along the current GPS route. |

The three livery overrides operate independently. MSAA can override the layer
without enabling 4K, and 2x sampling can improve the intermediate render without
changing the final texture resolution. Default restores the recognized loaded
bytes rather than forcing vanilla settings. The former Livery INI section is
accepted only for one-time migration; use Settings → Video afterward.
The discarded `Frontend.HideOnlineTabs` key is accepted and ignored for
compatibility with older settings; it can be deleted.

## Photo Mode sampling

Select **Photo Mode Samples** in Settings → Video, then restart.
The initial and reset choice is Default. Selecting Default preserves the translation
layer's sample grid (the original game uses 129). The other choices are
256, 512, 1024, 2048 and 4096. They are approximate targets: for example,
1024 generates 1023 entries and 2048 generates 2049. Higher counts increase
capture work and preview convergence time. An explicit sample count overrides Azimuth's photo density even when its Motion Blur preset changes that density.
See [the sampling notes](../PHOTO_SAMPLING_NOTES.md).

## Loose-file overrides

```ini
[Files]
EnableMediaOverrides=1
```

Create `mediaoverride` or `mediapc` beside the **game EXE**, in the game's
`Mount` folder on both layers. These asset folders follow the game location;
Azimuth's INI, saved menu settings and gamerpic live beside `winmm.dll` and `azimuth.exe`.

Mirror the virtual path below `media`. For a ZIP member, use the archive's
folder path without `.zip`, followed by the member path. For example:

| Original asset | Loose replacement |
|---|---|
| `Mount/media/Camera.zip` → `CameraGroups.xml` | `Mount/mediaoverride/Camera/CameraGroups.xml` |
| `Mount/media/UI.zip` → `Controls/Example.xaml` | `Mount/mediaoverride/UI/Controls/Example.xaml` |
| `Mount/media/ControllerHaptics.xml` | `Mount/mediaoverride/ControllerHaptics.xml` |

The search order is `mediaoverride`, then `mediapc`, then the original mounts.
An override folder needs only the files you want to replace. Missing files
fall back to the game's original assets. Replacements must use a format the
game already supports. This does not discover or install new DLC packages.

Restart after changing the toggle or files; existing loaded resources are not
reloaded. The startup log reports hook installation, then reports the native
folder mounts on the first file lookup. Empty or absent folders use original
assets. Set the option to 0 and restart to disable these mounts.

FH2 retains the `mediaoverrides` startup setting and the native priority-based
mounting machinery, but the later PC folder names are absent. This option adds
the two folder mounts using those native routines. See
[the investigation notes](../MEDIA_OVERRIDE_NOTES.md) for evidence and limits.

## Sun shadows

Choose **Real-time Shadows** in Settings → Video and restart. Default retains
the current translation layer's settings. On forces the mod's smooth update
behavior; Off restores the original game's update decisions.

The renderer normally holds separate cached light directions for its cascades
and focus shadows until its refresh checks pass. This option refreshes both
directions on each renderer update. It also redraws all four world-shadow maps
per render frame instead of spreading them over separate frames. Shadow
resolution and time-of-day speed remain unchanged. It increases GPU work;
turn it off to restore vanilla behavior. It defaults to off. The user confirmed
the earlier shadow implementation; this build moves its control into Video settings.
See [the investigation notes](../SUN_SHADOW_NOTES.md).

## Experimental autodrive

Add this section, or change Enabled in the supplied INI, and restart:

```ini
[Autodrive]
Enabled=1
```

Set a GPS destination in single-player free roam and press F2 or D-pad Down.
Enable **Settings → HUD → Autodrive in Events**, directly above the cinematic
camera option, to use the same binding during races, road trips, head-to-heads
and other events with an active route. This expansion defaults to Off. The INI
`Autodrive.Enabled` setting still controls the whole feature.

Events use the car's native Event route first, then its Group/Personal SatNav
route, or the existing GPS tracker when appropriate. Routes without an active
binding cannot engage Autodrive. The mod does not replace or clear event routes,
checkpoints, lap counters or the game's route-tracking task. Event driving is
updated from the local car so it does not depend on the free-roam GPS updating.

The Skill Chain XP popup displays **AUTODRIVE / localized On** once after a successful
handoff. It uses the existing font, position and animation, and dismisses after
2.2 seconds without awarding XP. After three seconds, the mod attempts to select
the native replay camera **Slow Spin**
and disable UI drawing. When its animation finishes, the mod
selects **Pan Across**, then returns to Slow Spin when that shot finishes. This
repeats until autodrive ends, using each selected animation's own timer and final
keyframe. The same white flash used by fast travel
covers entry into the cinematic and return to the driving camera on cancellation
or arrival. It uses the live simulation without
starting a replay. Turn off **Autodrive Cinematic Camera** in Settings → HUD to drive with your usual camera
and HUD. Cinematic mode suppresses the main UI screen draw and the separate
spinner draw through one shared rendering state. It also gates the complete
UI presentation path that the previous screen-only hook missed. Both native
application vtables are covered, and no graphics targets are cleared. HUD services, skill logic,
queues and animations continue updating; no individual ring controls are hidden
or tracked. All UI, including telemetry, is invisible while the cinematic is
engaged. Rendering resumes before pause/event flows and after cancellation.

Press F2 or D-pad Down again, steer, accelerate or brake to return control.
Stick drift and trigger noise inside the game's deadzones do not cancel it.
Telemetry can open during autodrive, and the mod does not consume its inputs.
D-pad Down also toggles autodrive while telemetry is open. A release is required
after reconnecting a controller or returning focus, to avoid a held-button toggle.
Escape/pause, losing window focus, changing cars or routes, entering/leaving an
event, or the GPS reporting arrival also stops it
and restores the previous camera and UI rendering. GPS activation refuses an
already-owned GPS tracker. Event activation borrows its existing route without
starting or stopping the game's tracker. Setup, results and other menus must
allow driving input before either binding can activate Autodrive.
Pause, focus loss and event transitions restore control immediately, without
adding a flash to the destination UI or waiting for an effect to finish.

This is a prototype. The native input handoff, guards and cleanup have automated
tests; route-following quality, stopping behavior and the camera animation still
need gameplay testing. If Slow Spin is unavailable, it keeps the normal view.
The log beside the DLL records controller availability and actual failures.
Routine button presses, notices, starts, stops, flashes and camera cycles are silent.
See [the native investigation notes](../AUTODRIVE_NOTES.md).

## Treasure Map

`Content.UnlockTreasureMap=1` enables the mainland and Storm Island Treasure
Map properties, reveals barn markers and discovers uncollected bonus boards
through the game's normal discovery routine. Collected boards remain collected;
barn cars still follow ordinary progression. Storm Island must be mounted.

The same option exposes the locally included Treasure Map entry in Marketplace
Extras while offline. Its name, description and images are included in the game.
Keep `Frontend.EnableMarketplace=1` to access that tab. Online purchase and token
services remain unavailable.

Set the option to 0 and restart to restore the original checks. The mod does
not write purchase flags or award collectible completion. Board discovery uses
the game's normal save path, so recorded discoveries can remain after disabling
the option. See [the investigation notes](../TREASURE_MAP_NOTES.md).

## Gamerpic

Use a square PNG; 512x512 is a useful size. The limit is 2048 pixels in either
dimension and 16 MiB per file. RGB, RGBA, grayscale and palette PNGs work.
Restart after changing the image. A missing, unreadable, invalid or oversized
file falls back to the original picture behavior.

Place it beside `winmm.dll` in Azimuth's launcher folder, or beside `xg.dll`
in the game's `Mount` folder when using Mezcal. The INI and startup log use
the same folder.

Only local-player picture requests are overridden. Remote/Drivatar requests
retain their original behavior. The file is decoded on its first request and
cached for the session, using FH2's native texture pipeline. No account or save
is altered. The log records decoding/submission and any failure.

## Existing compatibility features

The Marketplace option enables local browsing; it does not restore the offline
servers, purchases or downloads. The parental option supplies Allowed for the
game's multiplayer and user-content checks without the layer's restricted
service response.

For importing a Mezcal save into Azimuth, `source/development/tools/convert_mezcal_save.py` creates
a separate copy with revision-v2 manifests and verifies every blob. See
[source/development/docs/ACCOUNT_SAVE_NOTES.md](../ACCOUNT_SAVE_NOTES.md). The storage-full
message in the inspected import was caused by mismatched manifests, so no
quota/error suppression patch is included. Personal saves are not in this folder.

`Compatibility.DisableAzimuthSaveChecksums=1` lets the inspected Azimuth 0.9.10
revision selector and blob reader accept content whose SHA-256 or byte length
differs from its revision marker. The marker must remain well formed and its
listed files must exist and be readable. New or rewritten blobs still receive normal hashes.
At startup it also repairs revisions missing their `commit` file, under
Azimuth 0.9.10's `data/titles/1A96545B/saves/<profile>/<container>/revisions/<revision>/`
folders; older versions use `data/saves/`.
Each new v2 marker records the actual sizes and SHA-256 hashes of the files in
`data/`. It is written to a temporary file, flushed, then renamed into place.
Existing markers and blob files are preserved. Empty, unreadable or malformed
revisions are left untouched. This does not repair the game's internal save
structure. Use 0 and restart for native verification and no automatic repair.
The startup log reports whether the override was applied, disabled, or skipped
because the host code did not match. See
[the save checksum notes](source/development/docs/AZIMUTH_SAVE_NOTES.md).

## Verification and limitations

The supported original EXE is 58,204,160 bytes with SHA-256
`3ec04c6163fb5614e3d351ba0570d39b846041a257715d6340eceb03ccb7f0a3`.
The DLL validates the normalized file hash, loaded PE headers, 77 fixed sites,
39 runtime hook sites and one optional density control before applying a transaction.
Unknown builds or conflicting bytes are refused. Page protection is restored
and the instruction cache is flushed. Azimuth compatibility patches separately
validate the inspected host code before changing memory.

See [VALIDATION.md](VALIDATION.md) for the tests and
[BUILD.md](../BUILD.md) to rebuild. Native fixtures cover
the settings resource path, bindings, reset, localization and sidecar migration.
They do not render the complete scenes or establish final label fitting and
controller navigation. The two extended menus and the newly enabled tabs need
an in-game check on your installation.

## Removal

For Mezcal, close the game and remove this mod's xg.dll. For Azimuth, remove
this mod's winmm.dll; leave the original TLS helper installed. The optional settings, log and picture may be removed.
Previously patched EXEs retain their on-disk changes.

Blinker pulse timing uses the game's native repeating light animation, without a second timer interrupting it.

Stock-rim labels follow the actual asset donor when editions share a wheel ID.
Buy and Install confirmations use the localized donor-car name, including
accented characters.

Showroom car previews in the Autoshow and garage include a mixed-case localized
**Take photo** prompt on View. The shortcut reads the active controller in the
preview screen, blocks nested menus, and requires a new press after entering or
returning to the preview. It leaves native Select/Change Camera controls intact.
Photo Mode runs outside the preview page's active binding. Its native orbit
setup pauses the looping cutscene first; Back restores the camera and reloads
the preview page to restore its UI and input binding.
Stock-rim variant queries reuse an indexed wheel table in a private in-memory database; aftermarket-only
queries retain their ordinary table and indexes. No on-disk database is changed.
