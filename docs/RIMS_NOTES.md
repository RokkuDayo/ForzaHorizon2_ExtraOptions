# Virtual rim libraries

`Frontend.AllRimsCategory` adds combined aftermarket and stock-rim library rows
after stock, sport, multi-piece and specialized categories. Omitted keys default to 0. The supplied INI enables
it. The original database and categories remain intact.

## Native paths

Addresses below are RVAs in the supported FH2 executable, SHA-256
`3ec04c6163fb5614e3d351ba0570d39b846041a257715d6340eceb03ccb7f0a3`.

| Path | RVA | Behavior |
| --- | --- | --- |
| Rim category query builder | `0xece280` | Reads `WheelCategories`; stock uses the car make's icon. |
| Rim category selection | `0xebfcb0` | Reads `IsStock`, then forwards `Category` to the rim screen without a fixed category range. |
| Rim list query builder | `0xece770` | Joins the profile's `View_UpgradeParts`, upgrades, wheel annotations/categories and manufacturers. |
| Category query load call | `0xece71f` | Six-byte call plus padding replaced by an indirect callback call. |
| Rim list query load call | `0xecf307` | Same callback; virtual categories 5/6 rewrite their own queries. |
| Callback pointer | `0xecf5d8` | Eight aligned unused bytes between functions, checked before writing. |
| Original query loader | `0x140c3f0` | Copies SQL into an owned native string before preparing its database statement. |
| Allocate / free | `0x3229c0` / `0x31c7b0` | Own the temporary rewritten query until the native loader returns. |

The ordinary and Everyman category classes share the category builder. The
original three aftermarket lists contain 47, 26 and 26 distinct wheel IDs;
none belongs to more than one category in the inspected database.

## Query edits

The category SELECT receives a `UNION ALL` row for category 5, non-stock,
display order 5. Its name comes from `UpgradeTypes.Name` for `PartEnum = 43`,
the parent menu's Rim Style item (`_&4038008212`). Its description comes from
`WheelCategories.Description` for category 2 (`_&2583489723`). Both are native
localization references, resolved through the game's selected language.
It also reuses category 2's existing icon and large image. Other columns have
the same neutral values as an ordinary aftermarket category. Category 6 adds
a second non-stock navigation row with display order 6, reusing category 1's
Stock Rim Style name, description and generic icon/image. Marking this virtual
row non-stock opens its library instead of the native restore-stock action.

For category 5 only, the existing category equality becomes
`WheelCategories.IsStock = 0`. Sorting concatenates the original category
orders. Everything else in the native SELECT remains: wheel IDs, manufacturers,
profile-specific parts eligibility, equipped/purchased status, descriptions,
icons, credit/token prices and discounts. Normal categories pass through byte
for byte. Purchases and previews use the existing rim screen and part IDs.

No database connection is replaced and no SQL writes are issued. Unsupported
query shapes, allocation failure and queries that exceed the bounded output
capacity fall back to the original query. The shared callback adds no worker
thread, UI asset or external library. Both calls and the pointer participate
in the existing patch preflight/rollback transaction.

## Validation and limits

`development/tests/test_rims.py` executes the game's actual category/list builders and
query loader in a relocated private image. Native string helpers and profile
state are supplied by the fixture; statements execute against an in-memory
copy of the read-only database. It checks the fifth entry, localization
references, all 99 rims,
unchanged original rows, car/equipped-wheel/discount combinations, exclusions,
empty lists, allocation ownership, fallback and restoration. The native
category selection method forwards category 5 correctly.

The combined aftermarket list retains normal eligibility. The stock library
uses all stock IDs referenced by game cars, as described below. Database mods that redefine category 5 are outside this feature's
tested layout. The user confirmed the combined category works in-game; the
subsequent localization adjustment is verified by the native query harness.

## 0.2.69 stock-rim library

The database contains 519 List_Wheels rows marked IsStock. None has a
WheelAnnotations row. Of these, 347 distinct wheel IDs are referenced by the
354 Data_Car records; the library includes those actual car rims and omits
unreferenced legacy definitions. The database's stock flag stays unchanged.

Only virtual category 6 replaces the profile-specific PartTable source with a
read-only derived SELECT of those wheel IDs. Synthetic rows are selectable
(IsStock=0, IsException=1, Level=1); their real IDs, manufacturer IDs and prices
come from List_Wheels. Level 1 selects the generic level -1 Upgrade record once,
avoiding duplicate stock/generic Upgrade rows. The annotation/category join is
replaced by a single representative donor car and its make. Names and icons use
native localized car/make data; there are no missing stock-rim thumbnail lookups.

Native equipped/purchased status and discount/price expressions remain intact.
The current car's original stock category, IsStock flag and part ID -1 restore
action are untouched, as are the three ordinary aftermarket queries and the
combined category 5. Query edits are bounded, uniquely matched, sorted and
checked for overlap before any output is written. Unsupported shapes and
allocation/size failures retain the original SQL.

The extended native fixture verifies both virtual selections (5 and 6), the
unchanged stock restore dispatch, all 347 unique stock IDs, localized metadata,
equipped/purchased states, stock-flag changes, normal exclusions and car/discount
combinations. No database writes are made outside the fixture's in-memory copy.
Actual wheel geometry, preview rendering and installation in a complete game
session still require an in-game check.

## 0.2.70 stock geometry loading

The 0.2.69 library exposed real stock IDs through the ordinary selectable-part
path. The native loader consequently searched the aftermarket directory:
`game:\media\cars\_library\scene\wheels\<WheelMediaName>\`. No
stock wheel packs exist there. Stock geometry instead belongs to donor car ZIPs.
This is an asset-path mismatch, not a renderer restriction to the current car.

`0xf592d0` selects the current car's stock path only for wheel part ID -1; a
positive stock-library ID follows the replacement path. `WheelPart` already
supports four independent replacement models through `0x11b9da0`, selected by
LF/RF/RR/LR target locators. Existing -1 restoration remains untouched.

| Changed call | RVA | Original target | New behavior |
| --- | --- | --- | --- |
| Wheel ZIP preload | `0xfd054c` | `0x2b3750` | Form a donor car archive path for known stock media names. |
| Render-car startup preload | `0x138e634` | `0x211e10` | Retain the donor ZIP through the existing archive-resource owner. |
| Render-car preview preload | `0x13a92bf` | `0x211e10` | The same archive path, with native lifetime and release behavior. |
| WheelPart initial replacement | `0x11af25b` | `0x11b0fb0` | Supply the donor's authored four model/texture paths. |
| WheelPart preview replacement | `0x11b0f30` | `0x11b0fb0` | The same replacement for changes made in the garage. |

Three 12-byte callback thunks use checked alignment padding at `0xecd6a4`,
`0xecde31` and `0xecfc22`. All eight new sites participate in the existing
RIMS patch transaction and restore when AllRimsCategory is disabled.

`development/tools/generate_stock_wheels.py` opens the database and car ZIPs
read-only. It reads length-checked model references and spindle/hub corner
locators from each `.carbin`, checks each geometry entry exists in its archive,
and emits a checked-in table for all 347 stock IDs. It handles shared wheels,
spelling differences, LF/LR naming, different front/rear geometry and authored
AO texture paths. The native corner order is LF/RF/RR/LR. The table contains
paths only; no game geometry or textures are distributed.

A known stock selection prechecks all four geometry resources before replacing
anything. Missing geometry leaves the previous model and cached selection
unchanged. Missing optional AO textures disable the native texture override
slots through WheelPart+0x190. No enabled texture slot receives an empty filename. The native replacement copies paths and owns
model resources; temporary borrowed string views never escape the callback.
Unmapped wheels and already-cached selections call the original loader.

`stock_wheels_fixture.py` runs the actual native four-corner replacement loop.
Its resource endpoint reads 1,388 geometry entries from the original car ZIPs,
checks corner paths and optional texture fallback, and verifies cache/missing-
file behavior and unchanged aftermarket dispatch. Production loader tests check
the five calls and three thunks under independent RIMS and global toggles.
These fixtures do not exercise GPU rendering or actual wheel installation.
The generated paths describe the supported original assets; database mods
that introduce additional stock wheels need a regenerated/extended table.

### 0.2.71 empty-path repair

Azimuth 0.9.9 stopped at `game+0x11f9f9e` while processing an empty
texture-resource path. 0.2.70 copied empty strings into enabled AO slots when
optional textures were unavailable. The native four-wheel replacement treats
WheelPart+0x190 as the no-texture-override mode: it clears each slot's active
flag at slot+0x44 as well as the owned filename. The callback now uses this mode
unless all four optional AO paths resolve. Geometry still loads independently.

The fixture rejects empty filesystem requests and simulates the later resource
notification, requiring every enabled AO slot to contain an existing filename.
It tests complete, absent and partially absent AO sets. These checks reproduce
the 0.2.70 failure before testing the repair. Paths contain only virtual game:
addresses; physical game folders remain the translation layer's responsibility.

### 0.2.72 native string initialization

Archive concatenation returns a newly constructed string into uninitialized
caller storage. Native `0x22c300` is assignment, not construction: it reads the
existing capacity/pointer and may reuse or free that buffer. The two archive
callbacks now explicitly construct empty small-string storage (capacity 15)
before assigning the donor path. Prefix and name arguments remain caller-owned.

The regression supplies 0xA5-filled output storage and rejects assignments to
unknown pointers. The guarded native assignment routine itself executes in the
fixture, with only reserve/copy boundaries mocked. 0.2.71 fails this contract
before it can perform an invalid memory write; the repaired callbacks initialize
the output and preserve ownership. The AO-slot repair remains in place.


## Donor names — 0.2.73 (superseded below)

The rim detail UI treats Name as plain text, so Data_Car.DisplayName's `_&`
reference must be resolved before returning the SQL row. Only the virtual stock
list uses FH2_STOCK_RIM_NAME; other lists/categories retain their native queries.
The function resolves the packed decimal ID through manager 0x3662990,
region-table resolver 0x123d830 and UTF-16 lookup 0x123d780, then returns UTF-8.
Missing translations produce an empty result and SQL falls back to MediaName.

The existing CDatabase string-function registrar 0x1093440 creates the native
wrapper; 0x1090460 checks for an existing arity-1 function before registration.
The wrapper at 0x108c690 consumes both a returned GameString and an input passed
by value. The callback initializes the result's inline capacity before assignment
and releases a heap-backed input. The native wrapper regression checks these
ownership rules, Unicode conversion and selected-language changes. No additional
patch site or database write is introduced.

## Compact asset routing — 0.2.76

The native aftermarket loader at 0x11b0fb0 builds conventional paths under
`game:\media\cars\_library\scene\wheels`. It does not perform an independent
lookup of the four paths authored in another car's definition. The existing
archive owner and 0x11b9da0 replacement routine remain responsible for resource
loading. Supplying a compact routing table avoids loading and parsing donor car
definitions at runtime; a general native donor-metadata lookup has not been found.

The generator now removes the common virtual prefix and substitutes donor/wheel
names into shared layouts. The 347 wheel sets use 33 geometry layouts and 38 AO
layouts. Each wheel stores its name, an optional donor alias and two byte-sized
layout IDs. Same, front/rear and left/right corner macros keep shared layouts
readable; authored exceptions retain explicit paths. The checked-in header shrank
from 335,521 to 35,188 bytes after formatting.

Runtime lookup is case-insensitive binary search over the sorted wheel names.
Path expansion uses bounded stack buffers sized from the generated longest path.
All eight authored paths, four donor aliases, native LF/RF/RR/LR order and
complete/absent/partial texture behavior are preserved. It adds no file reads,
heap allocations, hooks or retained cache. The native replacement copies the
temporary strings before the callback returns.

The path fixture compares 8,328 compiled paths with the original donor data:
347 wheels × two resource kinds × four corners × three name casings. Exact-size
buffers, insufficient capacity, unknown names and deterministic generation are
checked separately. The existing native model fixture also verifies all wheel
sets, initialized archive strings, optional textures, caching and aftermarket
fallbacks.

## UTF-16 display boundary and logo — 0.2.79

The SQL Name column now keeps StockCar.DisplayName's packed `_&` key. The
SelectedPartName getter call at 0xebc61e redirects through verified padding at
0xeb0274. For category 6 it resolves that key through 0x123d830/0x123d780 and
constructs a native AVUI UTF-16 string with 0x13fe8e0. Other categories and plain
fallback text use the original 0xed7d90 getter. The outer property's existing
reference-count handling and uppercase converter remain unchanged. This avoids
widening UTF-8 bytes into mojibake. Manufacturer SQL metadata retains its existing
function registration and ownership rules.

The shared XAML stream hook adds Stretch="Uniform" to the uniquely matched
SelectedItem.LogoPath material. Its square background and carousel icons stay
unchanged. The stream limit now accommodates the 44,454-byte upgrade scene.
Native AVUI string tests include Coupé, Citroën, Portuguese, CJK and Cyrillic.

## Stock-rim presentation and traffic S4 — 0.2.90

The virtual stock query sets UseDarkBg=1. Shared_TiledMenus.xaml already applies
A_FZMaterialPresetGlass_Standard and #cc14171a for that property; no new tile
style or gameplay IsStock flag is introduced. Other categories retain their SQL.

Only the virtual list filters wheel media names NUL_Car_00, PG_BUS_14 and
PG_BoxTruck_14 (case insensitive). Their model routes remain in the loader for
existing installed parts. The original 347 loadable routes remain supported;
the list exposes 344 entries for the inspected database.

The traffic Audi S4 carbin references wheel_ao.swatchbin, absent from its archive.
The detailed AUD_S4_04_wheelLF_AO map is not suitable for this traffic rim. The
override uses the existing small glass_ao.swatchbin from that same archive as a
neutral AO map. It is passed through the normal four-wheel material replacement.
The Scirocco retains its wheelLF_AO override in a separate layout, so changing
the Audi no longer changes the VW. No game asset or database file is rewritten.

## Asset donor labels and purchase names — 0.2.90

When multiple Data_Car rows share a StockWheelID, the virtual stock list now
prefers the car matching the compiled model route's donor name. The existing
lowest-ID fallback applies only when no matching donor is present. A small
FH2_STOCK_RIM_DONOR query function reuses the asset lookup rather than adding
a second list of exceptions. Wheel 91 resolves to NIS_FairladyZTF_03 (2463),
whose assets are loaded, instead of NIS_FairladyZFF_02 (2391). No wheel model
route, ID, save record or database row is changed.

The purchase flow at 0xec0336 obtains its part name through the same virtual
method (+0x5c8) used during selection at 0xec5f35. The rim-page slots at
0x2d826a0 and 0x2e5ef20 now resolve category-6 localization tokens using the
native localized IString factory (0x123a4d0). The caller retains the expected
IString interface and reference ownership; converting only the AVUI heading
was insufficient for the confirmation message. Other categories and plain or
unresolved names keep the original 0xeb70b0 method. The earlier heading
resolver remains a fallback.

The regression executes the original localized-string factory and UTF-16
getter for all 344 displayed rims in three language fixtures through both
page variants (2,064 cases). It checks the Fairlady donor, short-name and
case-insensitive query-function inputs, missing keys, ownership and native
fallbacks. Existing model/AO loading tests cover all 347 loadable wheel sets.

The All Aftermarket Rims category in FH2 and FM5 orders by ManufacturerName
using the game's LOCALIZED_NOCASE collation, which resolves string-table keys
before comparing names. WheelAnnotations.DisplayOrder and List_Wheels.Id are
stable secondary keys within each manufacturer. The combined list no longer
inherits the category display order; original categories keep their own ordering.

The All Stock Rims category in both games also sorts by localized manufacturer
name rather than MakeID. Donor year, donor car ID and rim ID retain a stable
order inside each brand. Factory-rim eligibility and asset routing are unchanged.

Stock ManufacturerName is already resolved by FH2_STOCK_RIM_NAME / FM5_RIM_NAME,
so its sorting must use plain NOCASE. LOCALIZED_NOCASE constructs localized
references from its arguments (FM5 68CF00 -> 7C70B0); passing literal names to it
resolves them as key zero and incorrectly lets the year key determine ordering.
The native-reference collation remains correct for the aftermarket query, whose
ManufacturerName contains the original localization reference. The test collation
now models this reference-only behavior instead of accepting literal names.

FM5 stock-rim names retain Data_Car.DisplayName as a native localization
reference. Resolving that reference to UTF-8 in the SQL Name field loses
Unicode in the rim item's literal-string conversion. ManufacturerName still
resolves to text for sorting; the two fields intentionally use different paths.

FM5 also registers factory-wheel donors after the mod database merge (including
a cache hit), using Data_Car.StockWheelID and List_Wheels.MediaName. Unknown
wheel names and aliases read their donor carbin through FM5's native filesystem
on first use and cache the four model/AO routes. This shares FH2's parser, with
separate FM5 file interfaces and the existing vanilla authored routes preserved.
The currently driven car is not excluded. The Huracan addon (wheel 12414)
was previously missing because it had no compiled vanilla donor route.

Validation: native FM5 rim builders, localized references, manufacturer order,
addon donor aliases and current-car eligibility; all 322 vanilla asset routes;
actual Huracan carbin and native ZIP resolution/model-loader dispatch (sync
stream supplied by the fixture); FH2 Porsche addon-wheel regression.

FM5 current-car factory rim resizing: the three calls to the factory-wheel
predicate 498DA0 at 495087 (part lock), 49A7B4 (category lock SQL) and 49AC8B
(category selection) return false locally. Front/rear diameter availability,
purchasing, installed parts and rendering continue through the native paths.
The predicate itself is untouched. This is part of AllRimsCategory, matching
FH2's rim feature group. FM5 cockpit motion blur defaults/reset to On; saved
choices are preserved.
