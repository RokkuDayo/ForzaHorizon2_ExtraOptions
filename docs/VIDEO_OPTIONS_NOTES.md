# Native Video and HUD settings

`video_options.c` extends the native AVUI type metadata before parsing the
generated menu resource. `video_options_xaml.c` edits only the two known XAML
classes; `video_options_text.c` contains all sixteen supported language catalogs.

| Menu | XAML class | Native registration / registry | Reset implementation / slot |
| --- | --- | --- | --- |
| Video | ForzaUI.A_VideoOptions | 0xeca710 / 0x35470d0 | 0xebf060 / 0x2e60900 |
| HUD | ForzaUI.A_HudOptions | 0xd453d0 / 0x35235e0 | 0xd33ae0 / 0x2df07c8 |

All addresses are RVAs in the supported FH2 executable. Both classes use the
existing renderer LoadComponent resource call at 0x16ebb37, redirected through
the verified unused thunk at 0x13fe354. The original resource getter runs first.
The returned bytes, rather than the URI, identify the menu. Other resources,
changed layouts, allocation failures and already-extended resources keep their
original bytes. A native reference-counted resource owns the replacement buffer.
Its destructor calls the original cleanup and releases that single allocation.

The native integer, float and collection descriptor factories are 0xed81f0,
0xed6e90 and 0xed77d0. Property registration uses 0x155ee30, change notification
uses 0xcf9c60, boxing uses 0x4702d0 / 0x3f5990. There are nineteen active setting
descriptors and six choice collections, with independent metadata links per
page. Collections and descriptors are initialized once per game image and reused.
Native strings are made by 0x13fe8e0 and inserted by 0xb52170.

Video keeps its original Brightness and Subtitles bindings, followed by two
graphics selectors: four rows. The list is 220 pixels high. The panel remains
806 pixels wide; the brightness preview uses its original five squares and is
centered. HUD retains its original eleven rows and height 550, widens the list
from 688 to 1,000, then adds seven drift controls, five FOV sliders and three
extra selectors plus two placement choices: twenty-eight rows. The horizontal container centers the complete
list/description pair; only the list width changes. The description panel, title,
native sliders, row height and scrolling styles remain in use.
Page ownership is independent of saved field order, so moving controls preserves
existing sidecars.
The original page reset runs first. Mod reset changes only that page's values
and notifies the matching bindings.

Generated resources include exactly one UTF-8 byte-order mark. The native XML
reader at 0x15581c0 sets its UTF-8 flag only when that mark is present; without
it, 0x152f960 widens each byte and corrupts non-ASCII text. Attributes are XML
escaped. Choices are native UTF-16 strings. All sixteen locales are tested
through the game's character reader, including Portuguese accents and CJK.

## Saved choices and application

`FH2_ExtraOptions.dat` is version 6: magic/version plus twenty-one 32-bit values,
92 bytes. Drift uses integer percentages, FOVs half-degree units, other settings
bounded choice indexes. Exact size, version, signature and every range are
validated. Missing or corrupt records begin with defaults. Versions 1, 2 and 3
keep their drift/FOV choices; the retired version-3 extra value is discarded.
The seven moved legacy INI values seed the new choices once. Version 4 preserves all nineteen existing choices and imports only the new
placement options. Version 6 retains the same size/field order while converting
the driver group's changed scales once. The old default 50/100/20 becomes
70/55/65; custom strength, natural frequency and range use their nearest new
percentages. Other settings retain priority over the old INI. Graphics presets use the nearest
offered legacy sample value. Setters and resets persist beside the loaded DLL.

Drift, FOV, minimap and cinematic camera state are read live. The minimap hook
stays installed and forwards the original car heading when Follow Car is chosen.
The two graphics controls configure startup patches/render-resource readers.
Championship dialogue is edited in a cached stateflow resource. Their descriptions
therefore state that a restart is needed. Higher photo targets still use the
native sample grid, so generated counts can differ slightly from the label.
Spawn Location maps Default to both existing startup flags off, Car Meet Entrance
to the car-meet bypass only, and Festival Site to both flags on. Its startup
patches/resource edit apply on the next launch. Storm Island behavior is unchanged.
Change Car Position is a live choice: the road callback remains installed while
pause-garage swaps are enabled. Its atomic mode gates the extra road lookup after
the original StoreCarPosition runs. Same Location uses that native result exactly;
Nearest Road retains all existing nav/cache validation. Both changed and
already-matching patch transactions commit feature modes through one helper.
New and reset settings use 12 motion samples, 256 photo samples.
Skip Championship Dialogue defaults to Off. Existing valid records keep
their values, including a previously enabled dialogue skip. Every locale retains the Autodrive
name; motion-blur descriptions refer to the standard game setting.

## Verification

The fixture maps a private relocated game image and executes the original
LoadComponent and renderer resource path for both pages, capturing the stream
at the complete-scene parser boundary. It executes native descriptor factories,
boxing, strings, collection insertion/growth, metadata links and stream lifetime.
Only allocation, disk resource, type bootstrap and full-scene parsing boundaries
are supplied. It checks repeated opening, all settings/choices, independent page
reset, sidecar upgrades, one-time import, malformed ranges and global disable.
Full scene rendering and final controller navigation require user testing.

## Retired settings — 0.2.64

The anisotropic selector, binding, collection and renderer hook were removed.
Saved field 15 remains reserved to preserve the v5 record layout and later HUD
indexes. A valid older 0–3 value is accepted during loading, then cleared;
setters do not expose it. Video resets notify only its three active mod controls.
The old Language.Text/Audio and Graphics.ForceAnisoFiltering INI keys are ignored.
Menu localization still follows FH2's active native text-table language.

## Retired motion-blur control — 0.2.65

The sample-reader hook, selector, collection and translated labels are removed.
Saved field 13 remains reserved beside retired filtering field 15. Values from
older v1–v5 records are validated, then cleared; later camera/HUD settings retain
their existing offsets. Video now has four rows and a 220-pixel list. The old
Graphics.MotionBlurSamples INI key is ignored. The native reader at 0x130ab40
remains in vtable slot 0x2eb9a98, leaving control to the launcher.

## Readability and dispatch — 0.2.66

Collection indexes are named enums. Native property lookup validates range and
alignment before deriving its index, replacing the linear descriptor scan.
Defaults use designated saved-field indexes; record validation uses the known
version/size/count table. The record remains v5 and 92 bytes. Translation
catalogs use named fields, and the unused chase-heading member is removed.
Bounded text search is shared with the garage/query transformations. Direct
Video/HUD transformation benchmarks produce byte-identical output to 0.2.65.

## Layer-aware graphics choices — 0.2.79

Both graphics controls remain. Real-time Shadows has a dedicated three-item
ShadowChoices collection: Default / Off / On. Photo Mode Samples retains its
Default and numeric choices. Default restores the recognized startup values
provided by the translation layer. Descriptions in all 16 text languages are
shortened to at most 120 characters for the original 806-pixel, 100-pixel-high
panel, with its 30-pixel side margins and 22-pixel body font. Full-scene line
wrapping still requires in-game verification.

Sidecar v7 keeps the 92-byte layout. Version 6 shadow Off/On indexes 0/1 migrate
once to 1/2; new Default is 0. Existing explicit choices, photo sampling, camera
settings and HUD choices are retained. Earlier versions keep their driver and
placement migrations. A repeat load of v7 does not rewrite or retune it.
