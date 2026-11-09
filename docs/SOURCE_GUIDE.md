# Current source — v0.6.0

The current Azimuth development DLL includes separate FH2 and FM5 title profiles.
FH2 code lives in `fh2`, FM5 code in `fm5`, and common loader/support code in
`shared`. Settings templates are in `config`, documentation in `docs`, and
tests/tools/fixtures in `development`. See [BUILD.md](BUILD.md).

Azimuth support is title-gated and includes v1.0.4; see
[AZIMUTH_NOTES.md](AZIMUTH_NOTES.md). Normal builds remain FH2-only unless
`--azimuth --fm5` is passed.

# Source guide

For FH2, start with `fh2/runtime.c`. It reads the settings, verifies the game file, applies
the requested changes, and writes the startup log. The adapters provide the
loaded game image, executable path and settings directory to this entry point.
Mezcal uses Mount. Azimuth uses the loaded helper DLL's directory for settings
and logging, while resolving the game executable separately.

## Layout

| File | Responsibility |
|---|---|
| `fh2/dllmain.c` | Windows entry point and the retained `Fh2TweaksStatus` export. |
| `fh2/adapters/mezcal.c` | Recognizes the game process and starts the optional XG hook. |
| `shared/adapters/azimuth.c` | Preserves native TLS, discovers the mapped game, and resolves separate game and mod paths. |
| `shared/adapters/azimuth_host.c/.h` | Checks inspected host headers and code hashes, applies memory edits, restores protection and rolls back failures. |
| `shared/adapters/azimuth_markers.c/.h` | Rebuilds missing v2 commit markers from actual blob sizes and hashes, preserving existing markers and save contents. |
| `fh2/runtime.c` | Coordinates startup. |
| `fm5/fm5_rims.c`, `fm5_stock_wheel_assets.h` | FM5 rim lists, localized donor names, authored geometry/AO routes and native archive preload/load hooks. |
| `fm5/fm5.c/.h` | Independent FM5 title verification, INI/log, intro/VIP changes and native folder mounts. Azimuth never enters FH2's runtime for FM5. |
| `fm5/fm5_video.c/.h`, `fm5/ui/video_options.xaml` | Video options, original calibration, independent saved livery quality and FM5 render-target patches. See `FM5_VIDEO_NOTES.md`. |
| `shared/extra_options.h` | Shared option and patch types, version, and supported build constants. |
| `fh2/settings.c` | Defaults and table-driven INI parsing; the same definitions drive settings logging. |
| `shared/logging.c` | Startup and feature log output. |
| `fh2/patch_engine.c` | Validates all sites, gathers pages, installs changes, and rolls back failed writes. |
| `fh2/verification.c` | Hashes the supported executable after normalizing its known patch ranges. |
| `shared/sha256.c` | The standalone hash implementation. |
| `shared/support.c` | Bounded paths/text searches, safe pointer reads, finite-float checks and status handling. |
| `fh2/game_addresses.h` | Named native game addresses used by the feature modules. |
| `fh2/garage.c` | Shared frontend XML parse hook; independently plans pause-garage, race dialogue and mainland cold-boot placement. |
| `fh2/festival_flow.h` | Mainland/island and opening-experience branches for festival placement during cold boot. |
| `fh2/garage_flow.h` | The car-change and resume stateflow payloads. |
| `fh2/road_placement.c` | Computes and stores a validated nearby road destination before the car change. |
| `fh2/rims.c` | Adds combined aftermarket and stock-rim library categories through bounded query edits, preserving the native loader, stock restore action and database file. |
| `fh2/stock_wheels.c` | Routes stock wheel archive preloads and replaces models through the native four-wheel routine. |
| `fh2/stock_wheel_assets.h` | Generated donor paths taken from the original car definitions; ordinary builds need no game assets. |
| `fh2/minimap.c` | Supplies the current camera heading to the native minimap update, preserving other arguments and falling back when unavailable. |
| `fh2/video_settings.c/.h` | Stores camera, graphics and HUD choices in a versioned sidecar, imports legacy INI values once, validates ranges and resets each page separately. |
| `fh2/camera_fov.c` | Overrides the six driving-camera FOV getters with five independent live settings. |
| `shared/video_options_text.c/.h` | All 16 text-localization catalogs, shared descriptions and normally cased mode labels; selects the active native text-table language. |
| `fh2/video_options.c` | Native Video/HUD properties, choice collections, resource ownership and per-page reset. |
| `fh2/video_options_xaml.c/.h` | Bounded localized layout generation and centered brightness preview. |
| `fh2/sun_shadows.c` | Preserves recognized layer shadow defaults for the Default/Off/On override. |
| `fh2/photo_sampling.c` | Chooses a native accumulation grid density for the requested Photo Mode sample target, preserving native generation and capture counts. |
| `fh2/camera_views.h` | Named native camera view identifiers shared by behavior and tuning. |
| `fh2/drift_camera.c` | Uses the loose native spring for chase and a continuous relative drift offset in the cockpit, with independent live HUD menu tuning and preserved position/look controls. |
| `fh2/media_overrides.c` | Mounts two loose-asset folders on the first native file lookup and preserves the original priority resolver. |
| `fh2/vehicle_controls.c/.h` | Native car-update gestures, retained headlight requests and stationary roof toggling at full animation detail. |
| `fh2/treasure_map.c` | Runs native board discovery after board initialization. |
| `fh2/autodrive.c` | Driving state, cancellation, delayed cinematic, flashes and cleanup. |
| `fh2/game_input.c/.h` | Lazy keyboard API initialization and separate keyboard/focus polling. |
| `fh2/autodrive_game.c/.h` | Native GPS, AI, camera, HUD and reference ownership operations. |
| `fh2/autodrive_notice.c/.h` | Native XP award popup creation, scoped text conversion and queue cleanup. |
| `fh2/autodrive_ui.c/.h` | Gates complete UI presentation, per-screen draws and WaitRenderer draws during cinematics, preserving references without touching graphics targets. |
| `fh2/patches.json` | Original/accepted/replacement bytes and reverse-engineered patch metadata. |
| `patches_generated.c/.h` | Generated fixed patch definitions and supported file hash. |
| `build.py` | Builds either adapter, test DLLs, or the development diagnostic adapter. |
| `generate_data.py` | Validates the manifest and generates the C patch tables. |
| `development/tools/convert_mezcal_save.py` | Inspects, copies, verifies, and publishes a new Azimuth save revision. |
| `development/tests/test_support.py` | Shared native test ABI structures. |
| `development/tests/test_*.py` / `development/tests/test_azimuth_loader.c` | Isolated regression tests and the native loader fixture harness. |
| `development/tests/run_tests.py` | Runs the test groups and saves their results to a new scratch folder. |

Each feature owns its callback state. Headers declare interfaces; feature
implementations compile separately. The patch engine refers to thirty-nine callback
descriptors, one optional density control and the generated fixed-patch table
through the same patch type.

## Patch application

`patch_engine_apply` checks the executable headers, prepares callbacks, and
validates every loaded patch site before making changes. It pins the DLL when
callbacks will be installed. `collect_patch_pages` and
`make_patch_pages_writable` prepare the memory transaction. `commit_transaction`
writes the patch bytes, flushes the instruction cache and restores page
protections. The page table is allocated from a bound calculated using the
actual changed patch spans and the OS page size. Duplicate pages share one
record; each record retains that page's original protection. The temporary table
is released after commit or failure. There is no fixed page-count ceiling.
A failed flush restores the saved bytes. Language selection, motion blur and
anisotropic filtering are handled by the launcher; none has a mod hook.

The callback modules perform resource work on the game's normal call paths.
Startup only prepares and installs them. PNG decoding happens on the first
local-player picture request, outside DLL initialization. Autodrive input is
polled on the native GPS update path; vehicle controls use the local-car update.
Their User32 APIs load lazily on the first input request.
Native driving, camera and notice operations remain on the game thread.
The UI draw hooks read the cinematic rendering state through interlocked operations.
Loose-asset folders mount once on the normal file-resolution path, after the
primary filesystem volume exists. Their constructor, reference ownership,
registration, sorting, disk access and fallback use native game routines.
No mount construction or asset loading runs inside DLL initialization.
The standalone car-meet bypass owns one mainland-only boot branch.
Festival startup requires that bypass and uses the shared XML hook.
The added placement nodes run between native car setup and streaming readiness.
They use native world/progression checks and leave all other startup nodes intact.
See `FESTIVAL_SPAWN_NOTES.md` for the inspected branch and locator.

After successful enabled startup, the Azimuth adapter can generate missing save
commit markers. Native save checksum and size validation remain unchanged.
The native renderer handles fragment masks without an Extra Options patch.
The Mezcal build does not include these host modules.

The rim category shares one callback between two native query call sites. It
rewrites only recognized menu queries, uses the game allocator and releases the
temporary SQL after the native loader has copied it. All other categories and
queries pass through unchanged. See `RIMS_NOTES.md` for the addresses and checks.

The drift camera reads horizontal velocity, grounded wheel contacts and lateral
slip through native interfaces. Settled grip uses the unchanged native camera path.
Both view families reuse native yaw integration in a private heading frame,
with the same spring adjustment and independent in-game settings.
The chase wrapper uses a local copy of its spring settings, commits
only yaw/rate, and leaves subsequent orbit and effects in place. Mounted views
run their native update first, then rotate only the resulting view direction.
Three per-view records retain their relative angle/rate. Camera/target changes
start at zero offset; same-view resets retain the active
offset, and regrip eases it back to zero before clearing history. Runtime pointer slots use single pointer writes;
the instruction forwarders use the ordinary byte-copy transaction. See
`DRIFT_CAMERA_NOTES.md` for the inspected path, fallbacks and native tests.

## Making changes

Add fixed byte changes to `fh2/patches.json` and regenerate the tables. For a feature
that needs native behavior, implement it in a feature module and expose only
the functions the runtime or patch engine needs. Add settings to `ModOptions`,
its defaults, and `setting_definitions`; keep the native test ABI in sync.
Keep test-only scripts and harnesses in `development/tests/`, optional save tools
in `development/tools/`, and documentation in `docs/`. Production build
and data-generation scripts remain in the source root.
The public INI keys and the exported status/TLS symbols are compatibility
interfaces and should stay stable.

The `*_NOTES.md` files retain the investigation evidence and native addresses.
[BUILD.md](BUILD.md) gives the compiler and test commands.

## Formatting

C uses `development/.clang-format`: four spaces, braces on control flow, and
100-column wrapping. The C refactor was formatted with clang-format 20.1.0.
Python retains the established 100-column style. No formatter is required
to build the mod. Generated byte tables can also be formatted after generation.

The race dialogue option and garage option share one XML callback. Their settings
are committed only after a successful memory transaction; each resource family
has its own failure state. [RACE_DIALOGUE_NOTES.md](RACE_DIALOGUE_NOTES.md) records
the warning flow and the separate free-roam reminder.

## Readability changes in 0.2.53

Video menu layout generation, language catalogs and native binding/ownership now
have separate modules. A single row table supplies slider binding/resource pairs.
Native Video entry points and camera ABI fields have names; camera view identifiers
are shared with the tuning code. Chase and mounted views share spring tuning,
while retaining their established integration paths and saved settings format.
Pointer reads and finite-value validation use shared helpers instead of local
copies. Startup feature logging has its own function. The patch engine derives
its total descriptor count from its callback table. Test paths were updated for
the development directory; release builds have no dependency on it.

Native Video/HUD controls and migration are documented in [VIDEO_OPTIONS_NOTES.md](VIDEO_OPTIONS_NOTES.md).

`fh2/roof_fade.c` applies the roof transition to the native camera-fade color output. Its gesture timing and animation-speed ownership live in `fh2/vehicle_controls.c`.

## Refactor â€” 0.2.66

Keyboard/focus API ownership moved to game_input.c. Focus checks no longer poll
F2/Escape. Vehicle gestures, headlights and roof requests have independent state;
the update hook passes its retained local car into the gesture handler. Stationary
checks stop after consumption or detected motion, and inactive controls skip
maintenance work. Native references, event timing and pause/fade cleanup remain
covered by the same fixtures.

Patch-group policy is separate from byte selection. Azimuth recognition and
patch-set selection use named build tables. Menu descriptors use a validated
constant-time index. Defaults use named setting indexes; a layout table validates
v1â€“v5 records without shifting their fields. Localization catalogs use designated
members instead of positional initializers. Shared bounded search helpers retain
unique-match refusal and overlapping-match behavior. The duplicated XML test ABI
was replaced with the common 34-field definition.


Use development/tools/format_source.py with your clang-format executable.
It applies the development formatting policy to production C/header files without
relying on the checkout's default clang-format discovery path.
