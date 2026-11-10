# Forza Motorsport 5 — initial Azimuth support

**Development only:** FM5 is excluded from the v0.4.6 release and from normal
builds. This document describes the retained unfinished profile. Explicitly use
`--azimuth --fm5` only when resuming FM5 development; its INI is not shipped in
the FH2-only release.

The FM5 development build of Extra Options v0.4.6 includes a separate FM5 profile in the Azimuth `winmm.dll`.
The loaded package must report **Title ID 7B35D917**. FH2 continues to use its
existing profile for **1A96545B**. Other titles remain inactive.

## Installation and settings

Close Azimuth and replace Extra Options' `winmm.dll` beside `azimuth.exe`.
Place `FM5_ExtraOptions.ini` beside that DLL. Keep the existing FH2 INI and DAT;
FM5 never reads or writes them. Its log is `FM5_ExtraOptions.log` in the DLL's
directory. These settings are read at startup; restart after changes.

```ini
[Global]
Enabled=1
[Frontend]
SkipIntro=1
AllRimsCategory=1
JumpToManufacturer=1
[Content]
OfflineVIP=1
[Files]
EnableMediaOverrides=1
[Compatibility]
GenerateMissingSaveCommits=1
[Camera]
DriftCamera=1
FovControls=1
Headlights=1
```

All INI switches default to 1; 0 disables the selected feature. Global Enabled=0
leaves FM5's game code unchanged. Camera values are changed in the HUD menu and
saved separately in `FM5_ExtraOptions.dat`, beside the DLL. Keep that file when
updating. The camera controls apply immediately; restart only for INI switches.

Intro skip follows FM5's own DisableSplashIntro path through setup and completion,
leaving the Press Start screen. After a successful local profile load, VIP uses
the native setter to synchronize Main/IsVip and the gamer-data VIP bit before
badge/reward consumers see them. Pending/failed loads retain their original
status and behavior. The setter preserves other incoming gamer flag bits;
native notifications and profile storage remain in use. This is VIP status,
not a DLC-content installation system.

Version 0.4.0 missed this offline load boundary: its forced setter only helped
when the network VIP callback ran. The unrelated serialized-value reader is
left native in 0.4.1. `OfflineVIP=0` installs none of these VIP changes.

## Expanded rim categories

`Frontend.AllRimsCategory=1` adds **All aftermarket rims** and **All stock rims**
alongside the original rim-style categories. It defaults to On, including when
updating an older INI. Set it to 0 and restart to retain only the native lists.
The inspected FM5 database supplies 100 aftermarket rims and 322 distinct factory
wheel sets. Factory rims show their localized donor-car names and car-brand logos.
Both category names and descriptions use FM5's native localization references.
Borrowed factory rims share the description: "The stock rim from another vehicle."

The factory pool is independent of garage ownership. It uses the existing wheel
IDs, prices, upgrade installation, purchased-part records and vehicle physics.
Nothing is inserted into Data_Car or the autoshow. Four authored model paths and
their ambient-occlusion textures are extracted from each donor carbin, including
different front/rear and left/right wheel layouts. The donor archive is loaded
through the game's own preload and preview paths; WheelPart retains its native
load queue, replacement and resource ownership. Other rim categories and ordinary
aftermarket assets keep their native paths. Missing models retain the current
resources, and missing AO uses the native fallback.

There is no startup scan of car archives, database mutation, added per-frame
worker or gameplay polling. The checked-in FM5 route table is generated with
`development/tools/generate_fm5_stock_wheels.py`. New donor carbins require
regenerating that table; FH2's addon-car rim discovery has not been ported.

## Jump to manufacturer

`Frontend.JumpToManufacturer=1` adds a **View / JUMP TO MANUFACTURER** prompt
to all native car selection menus, including **Buy Cars**, **My Cars**,
**Designs/Paint**, **Career**, **Free Play**, **Rivals**, lobby selectors and
**Marketplace**. Initial-purchase and ForzaVista demo selectors use it too.
Each variant retains its original initialization, updates and destruction;
the grid reads only that screen's existing filtered collection. Marketplace's
separate scene receives the same View prompt as the shared car-select scene.
The rim-style lists use **Y** to preserve the
setup manager on View. This covers the original styles, All aftermarket rims
and All stock rims. It opens FM5's native popup with the vinyl picker's native
`VirtualizingTilePanel`: manufacturer logos, localized names, selection and
cancel prompts, scrolling and native transitions.

The manufacturers are deduplicated and sorted alphabetically from the current
car list, including its active filters. Opening selects the highlighted car's
manufacturer and scrolls it into view on opening. Accepting jumps to its first
regular car in the current carousel, skipping the recommended-car strip;
cancelling preserves the selection. Cars are neither bought nor filtered by
the jump operation. In My Cars, the pinned current car is skipped when its brand
also has a regular entry; a brand represented only by that car remains reachable.

Stock rims group by their donor car's manufacturer, preserving literal brand
names. Aftermarket rims resolve the database's packed localization references
through FM5's own recordset accessor. Both use the database LogoPath; aftermarket
logos get white tiles with dark labels, stock brands retain dark tiles. The rim
jump selects a row in the current list and never purchases or installs a part.
Native footer rebuilds refresh the single Y prompt and its action together.

Representative records and popup collections are released
when the picker closes or the parent page is destroyed.

The shared popup's grid layout is enabled only on the marked manufacturer
dialog. Its styles are declared after the page's native vertical-list style,
so ordinary popups (including Edit Layer) inherit the original vertical template
and scrolling instead of falling back to the default horizontal layout.
Arrow navigation reaches the full manufacturer collection and scrolls
the selected tile into view. LB/RB jumps three columns left/right while retaining
the row, clamping to the last available entry in an incomplete final column;
outer edges stop without wrapping. Navigation uses the tile panel's native
index rectangle and MakeVisible operation so an offscreen shoulder destination
is revealed immediately, even before its tile container is realized. Other native
popup menus retain their layouts. Collection work happens
on opening, with no controller polling, database writes or gameplay worker.

## Drift camera, field of view and cockpit motion blur

The native HUD menu adds these controls immediately after **Camera Motion Effects**:

- Drift Camera: On, Driver, Off. Default: Driver only.
- Chase Sensitivity / Look Speed / Range: 35 / 70 / 80.
- Driver Sensitivity / Look Speed / Range: 70 / 55 / 65.
- Chase, Far Chase, Driver, Hood and Bumper FOV: 20–120 degrees, in 0.5-degree
  steps. FM5's native 48-degree defaults preserve its original FOV calculation,
  including split-screen adjustment.

- **COCKPIT MOTION BLUR** is in Video, immediately below BRIGHTNESS. Default: **ON**. It enables
  the native motion-blur pass in cockpit view by bypassing its cockpit-only
  pass veto; **OFF** restores that native veto. Cockpit lighting and camera flags
  remain unchanged.
  Other views, blur strength, and native photo/pause eligibility remain unchanged.
  Existing camera DAT files migrate automatically, preserving their saved values.

Drift response follows FH2's tire-slip/momentum blending, sensitivity curves,
look-speed spring tuning, angle limits, reverse/low-speed filtering, and smooth
recovery. Hood and bumper never rotate with drifting, including in On mode.
FM5 has its own camera/physics interfaces: the implementation uses FM5 addresses,
its native heading spring and target getters, and rotates the cockpit look-at
point around the native eye position. Shared camera settings are never modified.

The HUD uses the game's spinner and Audio Options slider template, adds a native
scroll viewer, and reuses Extra Options' existing camera translations. Native HUD
Reset to default resets the added controls too. As with FH2, custom settings apply
and persist immediately, including when backing out of the options screen.

`Camera.DriftCamera=0` removes only the drift hooks and HUD rows;
`Camera.FovControls=0` removes only the FOV hooks and rows. Restart after changing
these switches. The settings file has a versioned, validated format and uses a
separate temporary file for replacement. No game profile data is changed.
There are no additional camera threads, polling loops, per-frame allocations or
per-frame log/disk operations. Runtime hooks run only when the game updates or
queries the corresponding cameras.

## Headlights and Photo Mode vehicle controls

`Camera.Headlights=1` enables right-stick click to toggle the local player's
headlight materials while driving. Menus and Photo Mode do not activate this
hotkey, held clicks do not repeat, and other cars retain their native lights.
Set this INI option to 0 and restart to disable the driving hotkey.

Photo Mode's Effects panel adds **Driver** (Default / Off) and **Headlights**
(Default / Off / On), using the existing localized FH2 labels and FM5's native
spinner template. The panel scrolls to accommodate the extra rows. Changes
restart photo accumulation, and both options return to Default on exit, restoring
the focused car's original driver visibility and light amount. Preview cameras
and race cameras resolve their own native presentations. No FH2 light-source or
beam patches are installed in FM5.
The two vehicle controls are hidden in garage/showroom Photo Mode using the
same native context binding as the race-only damage-reset prompt.

The implementation is in `fm5/fm5_vehicle.c`. It uses native queued renderer
setters, the ordinary car update, and Photo Mode's own page lifetime. It adds no
worker, per-frame allocation, file access or diagnostic logging. Private-PE
validation covers original patch bytes, the native call bridge, R3 edge handling,
menu/AI guards, photo defaults/restoration, and native car reference balance.
The resolver accepts both base and `TRefCountedObjectThreadSafe` presentations;
the native factory at `841EF0` constructs the latter proxy (`22CFBD0`), and
`88DEA0` constructs the reference-counted renderer (`22D6C08`). Driver publication
is intercepted in both proxy tables. Regression coverage executes the native
queued packet builders, dispatch callbacks and renderer setters for all four
base/reference-counted combinations, with queue scheduling supplied by the test.
Photo headlights hold the latest selected amount at the focused renderer's
native setter while Photo Mode is active, so stale queued light updates cannot
undo it. Sampling restarts once the renderer reports that amount, rather than
immediately when the UI queues the change. Default holds the captured amount;
exit clears the override and queues restoration. These guards also work with
the R3 hotkey disabled and do not change other cars' lights.

## Missing save commits

`Compatibility.GenerateMissingSaveCommits=1` enables the existing Azimuth v2
commit generator for FM5. Before the game starts reading profiles, it scans only
`data/titles/7B35D917/saves` beside `azimuth.exe`. Each missing `commit` is built
from the revision's actual blob names, sizes and SHA-256 hashes. Existing commits
and blob contents are preserved. Publication uses a flushed temporary file and
a non-replacing rename. Malformed, empty or unreadable revisions are skipped.

This defaults to On even when an older FM5 INI lacks the key. Set it to 0 to
disable generation, or set Global Enabled=0 to disable the whole FM5 profile.
It does not disable Azimuth's checksum validation or touch another title's saves.

## Loose media folders

Put replacements beside **FM5's game executable**, normally inside its `Mount`
folder. Keep paths relative to `media`:

```text
Mount/
  forza_durango_release_final.exe
  media/                         original assets
  mediapc/Camera/CameraGroups.xml
  mediaoverride/Camera/CameraGroups.xml
```

Priority is **mediaoverride > mediapc > original media**. Missing files fall back
to the next native mount. Newly added files do not need a hash.manifest entry.
These are FM5's own folder volumes and file resolver; no per-frame scanning,
file copying, replacement async loader or new worker thread is involved.
Archives, outstanding reads and volume reference ownership remain native.

This initial FM5 profile does not start FH2's mod discovery, DLL plugins,
database/carbin JSON loader, audio bridges, UI or save-checksum bypass.
The folders above are the supported override mechanism for this first version.

## Supported executable and implementation

The inspected local FM5 executable is 39,317,504 bytes, timestamp `53D8D1F2`,
mapped image size `0279D000`, SHA-256:

`94dad21851d335e9a33231dc2a1de2ecd4b93eefdf5f6772085bf6886bf47db7`.

The adapter uses the already-supported Azimuth 1.0.1/1.0.2/1.0.3/1.0.4 native map
handoff and reads the title/path from the loaded package, not launcher settings.
FM5 independently checks PE headers, image readability, file hash and every
enabled patch site before any write. Disabled features have no patches installed.
All write permissions are acquired before committing the startup transaction;
original protections are restored in reverse order, including shared pages.

`fm5/fm5.c` contains this profile; `fm5_rims.c` implements the rim port.
`fm5_camera.c` implements camera hooks and state; `fm5_camera_menu.c` implements
the native HUD resource/property bindings.
All enabled sites participate in the same startup transaction. No FH2 addresses
are used:

| Purpose | FM5 RVA |
| --- | --- |
| Native DisableSplashIntro branches | 13A345, 13A82C, 13B9D5 |
| Native VIP setter forced input | 1A130E |
| Local profile load call / native loader | 22EA79 / 1A0490 |
| Inspected local-load bridge | 22E9F2 |
| Native file-system initialization call | 5CFC68 (to 5FE0E0) |
| Inspected 12-byte CC bridge | 5CF963 |
| Folder allocation / constructor | 1CDBB0 / 794A50 |
| FM5 mount / sort virtual offsets | 1A8 / 1B8 |
| Rim categories / items query calls | 49C056 / 49D46E |
| Factory rim archive preload / startup / preview | 5B1D84 / 8708FE / 877DA1 |
| WheelPart startup / preview calls | 6E8112 / 6ED856 |
| Native WheelPart load / replace | 6CEDF0 / 6CF610 |
| Camera heading / driver update | 63EDB0 / 637150 |
| Native follow / mounted FOV | 63EC40 / 637490 |
| Follow heading / atan2 call | 63E6DE / 63EE55 |
| HUD stream / LoadComponent resource calls | B64554 / B64868 |
| HUD reset call | 3ACC1E |
| HUD property registry | 2463490 |

The filesystem callback calls the original initializer first, constructs two native
CFileVolumes, mounts them under `game:\media\` without hash-manifest filtering,
then invokes the original sort. Their reads and destruction use the native vtables.

## Validation

`development/tests/test_fm5.py` executes the real intro branch sites, VIP setter,
folder constructor, registration, priority sorting, disk existence checks and
resolver inside a relocated private PE. The VIP fixture enters through the real
patched local-load call, supplies offline/non-VIP profile data, executes the
native VIP and gamer-header setters, and checks pending/failed statuses, incoming
flags, and repeated notifications. Host I/O, allocation/locking and profile
storage are fixture boundaries. It checks all sixteen feature combinations,
master disable, strict title/image/file/site rejection, reference ownership,
fallback, added files, non-media paths, and permission-failure rollback on RX pages.

`test_azimuth_proxy.py` accepts an optional FM5 executable as its fifth argument
and exercises the real temporary handoff with a mapper stand-in for both titles,
including cross-title refusal. It preserves all four native mapper arguments and
restores the host call/padding. These tests do not launch the game or exercise the
actual Azimuth mapper/TLS, rendering, gameplay or save persistence. Intro skip,
VIP and media overrides were confirmed in-game by the user before this rim port;
the new rim menus and their visual results still require in-game testing.

`test_fm5_rims.py` executes FM5's real category/item SQL builders and native query
loader in a private relocated image. It checks the original 54/17/29 categories,
the combined 100 aftermarket entries, all 322 factory entries, localized names,
all 2,576 authored model/AO paths, donor archive concatenation signatures and
322 model-loader input cases. Profile state, database result endpoints and
render-resource loading are fixture boundaries; it does not render a car.
It also checks missing-asset/allocation fallbacks and avoids duplicate native
SQL-function registration across repeated menu opens.

`test_azimuth_markers.py` exercises missing-marker generation on private files,
including exact sizes/hashes, preserved existing commits, locked blobs, repeat
runs, and separate FM5/FH2/unsupported-title directories. It never opens the
installed personal save folder.

Routine workspace builds now default to Azimuth. Mezcal support is retained in
source; build its adapter only when explicitly preparing a requested release.

`test_fm5_camera.py` executes the FM5 heading spring, target/physics getters,
wheel contact/slip methods and six FOV getters in a private relocated image.
It checks feature switches, exact site refusal and permission-failure rollback,
settings ranges, localized HUD XML, default FOV passthrough, shared-spring
preservation, hood/bumper exclusions and cockpit rotation about a nonzero eye
position. Physics vector producers and the cockpit's native update are fixture
boundaries; the test does not run a simulation or render a frame.
`test_fm5_camera_bindings.py` executes the native property descriptor factories,
collection constructor/insert dispatch, extended resource ownership and custom
get/set/reset bindings. AVUI thread/type infrastructure, collection storage and
event dispatch are fixture boundaries. The camera port and the final rendered
HUD layout still need an in-game test.

### HUD source-stream correction

The initial camera preview extended only FileResourceLoader's LoadComponent
resource handoff. Ordinary pages can use GetStream instead, so the unmodified
HUD remained visible even with both Camera INI switches enabled. The corrected
build also extends the final GetStream handoff at B64554. The early existence
checks and other page resources remain native. Both paths use the same narrowly
matched HUD resource replacement and native reference ownership.

The FM5 log now distinguishes startup scheduling from a successful HUD resource
extension. A successfully loaded extended page writes `HUD: camera controls added
to the native FM5 HUD resource.` once. `test_fm5_camera_bindings.py` now executes
the actual native GetStream routine and stream destructor, with file reading as
the fixture boundary, to verify this previously untested route.

### Shared resource and target interface revision

The source-stream correction still did not activate on the user's live FM5
HUD. The next development build intercepts the shared smart-pointer accessor at
40D690, reproducing its complete getter behavior and accepting only the native
ByteArray vtable 2325760 before inspecting a resource. Non-buffer objects are
returned unchanged. Only the HUD class is transformed; parser and rendering
routines are retained. Caller-specific resource patches are removed.

The drift port now uses the camera target's own physics-source and axis virtual
methods, instead of accepting only the concrete ForzaCar target vtable and
reading its private body-pointer field. Physics slip/contact methods remain
validated before use. A fixture covers an alternate target wrapper that was
rejected by the initial implementation.

One-time FM5 log entries record resource-accessor/byte-buffer activation, a HUD
match, camera FOV/chase/driver callbacks, physics availability, timestep class
and first active slip blend. They do not stream per-frame logs. This revision
still requires live testing; previous offline fixture success did not establish
that the initially selected routes were used by the running game.

### Normalized tire-slip correction

Live logs confirmed that the driver/chase callbacks and target physics were
active, while the drift blend never activated. The initial port read the raw
wheel-slip getter (slot 400, RVA 7FC2A0) and incorrectly treated it as normalized.
The corrected port uses FM5's native normalized getter (slot 478, RVA 7FC670),
which multiplies the raw reading at wheel+14B0 by the tire-specific factor at
wheel+10EC. The same 1.0–1.35 grip blend then receives the scale it expects.
The regression fixture supplies raw slip below one together with a normalization
factor and executes both native getters, checking that sliding activates drift
while below-grip input remains inactive. Shared tire/physics state is not edited.

### Cockpit world-velocity correction

The cockpit path incorrectly used physics slot 260 (7FBD10), which returns a
scalar speed in XMM0 and does not fill a vector output. The velocity-vector
getter is slot 248 (7FBCD0); its paired setter at slot 250 (7FBCE0) updates the
world vector and cached scalar speed. The port now verifies and calls that
vector getter. Temporary velocity/axis buffers are initialized explicitly.

The earlier fixture stubbed slot 260 with a vector-producing callback, masking
this signature error. It now runs the real native setter, scalar-speed getter
and world-velocity getter, and checks cockpit rotation with both positive-time
simulation updates and subsequent zero-time post-updates. Native physics and
camera settings remain unmodified by the runtime hook.

### HUD list viewport and labels

The extended HUD list uses the native ScrollViewer with a 590-unit viewport,
matching the original list's visible area inside the 751-unit body. Its parent
centers an Auto-height row, so an unconstrained viewer previously expanded to
the entire list and placed the remaining options under the footer. The viewer
is non-focusable, clips its content, and uses native focus-driven scrolling.
The existing named options panel and transition targets are retained.

Camera headings use the FH2 wording and order, converted to uppercase for FM5.
English slider headings shorten only DRIFT CAMERA to DRIFT CAM. (for example,
DRIFT CAM. (DRIVER) - SENSITIVITY); the mode selector retains DRIFT CAMERA.
Slider labels are constrained to the left column, and the numeric value has a
reserved 90-unit area with a gap between it and the slider track.
Localized headings and mode choices are uppercased using invariant Unicode
mapping when the page is built; no camera-frame processing is added. Offline
checks validate the finite viewport, row order and English/Portuguese labels.
Scrolling and visual layout still require confirmation in the running game.

## Photo Mode UI

Photo Mode displays FM5's native localized **Hide UI** prompt for **View**,
matching ForzaVista. The existing native View toggle is unchanged. Hiding the UI
also hides the focus ring; showing it restores the native camera overlay.
The prompt is hidden on the save/cancel screen. No input polling is added.

## Mod packages (v0.4.5)

FM5 loads packages from `mods/Forza Motorsport 5` beside `azimuth.exe`. FH2 uses
`mods/Forza Horizon 2`; the folders and load-order files are independent.
Both titles store loader caches under `mods/Cache`. See [Mods and caching](Mods-and-Caching.md).

Supported package inputs are `database.json`, `strings.json`, loose files and
ordinary ZIP archives under `media`, carbin JSON patches, and DLLs under `plugins`.
FM5 carbin render models use version 14; the updated diff tool supports these
alongside FH2 version 15. FM5 database patches must target `media/db/gamedb.slt`.
This remains an Azimuth development build with FM5 enabled explicitly.

### v0.4.6 loose package routing

Loose package volumes use private `game:` aliases instead of physical Windows
roots. File existence and open callbacks translate those aliases into the active
package's media directory. Native archive/model lookups remain game paths;
ordinary file requests keep the host's original callbacks. The first twelve
carbin resolutions log their selected package index (`-1` means a native source).
The Supra AO replacement was checked using native offline lookups and real mod
bytes; final AO rendering still needs confirmation in game.

The canonical folder-override INI key is `[Files] EnableMediaOverrides`, matching FH2.
The older `MediaOverrides` spelling remains a fallback when the canonical key is absent.
Mod database caches live in `mods/Cache/fm5-db`, generated carbin files in
`mods/Cache/fm5-carbins`; FH2 uses the corresponding `fh2-db` and `fh2-carbins` folders.

## Photo-save accumulation and offline Marketplace

The PhotoModeScene update at game+0x37e100 uses page+0xBA4 for its state.
The passive preview and settled-quality restarts at 0x37E35D and 0x37E3B9
are now skipped only during states 3 through 6 (PreSave, watermark and saving).
The explicit capture routine at 0x380880 remains untouched. Other states retain
the native renderer interface at world+0x730+0x10, its restart method at slot
0x98, and the native sample-vector initialization/count. The surrounding base
scene update still runs. Verified blocks are split into startup transaction
sites; no background polling or saved-photo files are modified.

`[Frontend] EnableMarketplace=1` (default) enables the main home menu's
IsMarketplaceEnabled property and its matching button action without changing
connectivity globals. Catalog rendering, entitlements and purchase requests
remain native. Setting it to 0 restores the stock availability checks.

Private executable tests reproduce the original reset during saving, verify
preserved accumulation and preview flags in all four save states, check normal
camera/effects restarts and repeated shots, and exercise the native Marketplace
getter/click while offline. These tests mock GPU and UI event dispatch; the
final saved image and full menu flow require in-game verification.

### Level-faster notification

`[Frontend] HideLevelFasterNotification=1` hides the blue "Press Y to level
faster" banner on pre-race, post-race and Free Play screens. Set it to `0` to restore
the native banner; restart after changing it. Accelerator status/timers,
rewards and the Y Accelerator action stay native. Other screens are unaffected.

## Manual UI toggle and Photo Mode light publication

F1 hides/shows the UI during driving. Opening the pause menu or another menu,
or losing window focus, restores it. Input and native UI updates continue while
rendering is suppressed. The draw wrapper consumes the same owned screen
reference as the original method. FH2 is unaffected.

Rebind `[Bindings] ToggleUIKeyboard=F1` and `ToggleUIXbox=None` in
FM5_ExtraOptions.ini. Keyboard supports F1-F24, letters, named keys and decimal
or hexadecimal virtual-key codes. Controller supports button names or a numeric
button mask; it has no binding by default. Settings are read at startup.

Photo Mode Headlights changes restart preview sampling immediately, then restart
settled sampling after the selected car's native material update (874C90 calling
8798B0) has run. Receiving SetHeadLightAmount only updates the renderer's 108C
field and is not sufficient to confirm the materials were published. Atomic
selection revisions prevent old queued updates from acknowledging a newer
selection. Other cars and the native exit restoration remain unchanged.

Private-image regressions verify hook bytes, delayed/out-of-order light packets,
material-update acknowledgement, input edges, menu/focus restoration and draw
reference ownership. Full rendering still requires an in-game check.

## Unified settings and Player Character

Camera/HUD, Video and Player Character values now share `FM5_ExtraOptions.dat`.
Version 3 contains magic `0x35434F45`, version 3, thirteen signed camera values,
two livery-quality values and the selected DriverModels ID (all little-endian
32-bit fields; 72 bytes total). Default player selection is 0, which leaves the
native request unchanged. Writes replace the whole file atomically, preserving
values from the other settings page. There is no legacy migration in the DLL.

The one-time developer tool `development/tools/merge_fm5_settings.py` converts
old camera and Video DATs into a new v3 output. The removed supersampling field
in old Video v1 files is ignored. Existing personal files were converted offline;
the originals are backed up in the workspace.

PLAYER CHARACTER is the last HUD row. It uses the localized FH2 label and
description, in FM5's uppercase heading style. Choices come from the merged
DriverModels table (including mod entries), with Default first and Stig next.
Only local car presentation requests are overridden, through the native string
assignment at 149190 following 5E9760; AI models and saved car records stay native.
The selection applies the next time the model loads, as described in the menu.
The table is also read when package loading is disabled, using a separate verified
native database-open hook; it never duplicates the package loader's hook.

## E3 start screen

Set `[Frontend] E3StartScreen=1` in FM5_ExtraOptions.ini (default 0).
Restart to apply. The shipped UI.zip includes the native F002_DemoSplash page
and Backgrounds.zip contains Bg_Start_E3 and its shadow/highlight layers.
The executable also retains DemoSplash registration (34A330, page ID 37h) and
the E3 demo configuration/transition flow.

EO reuses the complete authored F002 page and storyboards, rather than replacing
individual textures or enabling the restricted demo. It feeds that layout into
the existing F000 Splash controller through the native ByteArray resource getter.
The root class stays F000; the retail FailedSignIn storyboard and its hide-spin
event are retained so cancelling/failing sign-in can restore the start prompt.
The retail/demo factories and UITransitions are unchanged. The adapted native
markup is in fm5/ui/e3_start.xaml, with its C header regenerated during builds.
No user game files or texture archives are modified.

Private-image validation exercises the actual installed resource getter, its
reference ownership, missing/Off/On INI states, exact preservation of the E3
storyboards, and unchanged page factories. The live AVUI presentation and
sign-in transition still require in-game confirmation.

## Early legal disclaimer

`[Frontend] SkipIntro=1` also selects the early renderer's native no-intro
Forza loading background at 13C59F, replacing its legal-disclaimer background.
The renderer and startup initialization still run normally; loading may still
be visible until initialization finishes. `SkipIntro=0` retains the legal screen.
This is FM5-only and does not change the selected retail/E3 Press Start screen.
