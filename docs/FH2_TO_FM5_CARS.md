# FH2 car to FM5 addon converter

`source/development/tools/port_fh2_car.py` builds an experimental FM5 addon from
an FH2 car ZIP and the two installed databases. Python 3.10+ and the standard
library are sufficient. Keep it alongside `carbin_json.py`, `car_physics.py`,
`car_materials.py`, `edit_car_lights.py`, `export_database_diff.py` and `export_string_tables.py`.

## Edit light assignments yourself

Export a human-editable inventory from the **original input car ZIP**:

```powershell
python source/development/tools/edit_car_lights.py `
  "E:/Games/Forza Horizon 2/Mount/media/cars/LAM_Huracan_15.zip" `
  -o huracan-lights.json
```

The default preset fills `value` with the current converter's automatic result.
Use `--preset source` to start with untouched FH2 assignments instead. Open the
JSON in a text editor. Entries are grouped by ZIP path and material, with the
carbin part role, parameter name/hash, byte offset, `source_value` and `value`.
Edit **`value`** to the integer you want; `source_value` and labels are reference
information. Offsets/hashes/source hashes identify the field and should not be
edited. The tool does not assume that the same number means the same thing in
different parameter namespaces.

The exported file has `automatic: false`: its values control the exported light
parameters directly. Set `automatic: true` to run built-in remapping first, then
apply your explicit values over it. `value: null` leaves that field at the source
value when automatic is false, or the automatic result when true. Integer zero
is an explicit override, not a missing/default value.

Add this to the normal converter command:

```text
--light-map "huracan-lights.json"
```

Use the **same original ZIP** and a new output directory for each attempt.
Mismatched source hashes, duplicate offsets, wrong parameter hashes/types,
invalid values and unused file mappings are rejected. The output includes the
applied `light-map.json` and records manual changes in `conversion-report.json`.
Replace the installed addon contents and restart to try the result; do not
enable multiple copies of the same car addon.

The inventory covers explicit light parameters in `.modelbin` and
`.materialbin` members of the ZIP, including the known custom rear-light
parameters. It does not synthesize fields inherited from external base
materials. With automatic remapping disabled, unlisted material parameters are
left at their original values.

### Skip localization generation

Add `--skip-strings` to the converter command to bypass localization processing
and omit `strings.json`. Database string references are retained. If the target
game lacks those strings, labels may be missing; this switch does not rewrite
them. `conversion-report.json` records `strings_skipped: true`.

## Abarth test

From the workspace root:

```powershell
python source/development/tools/port_fh2_car.py `
  "E:/Games/Forza Horizon 2/Mount/media/cars/FIA_131AbarthStradale_80.zip" `
  --fh2-media "E:/Games/Forza Horizon 2/Mount/media" `
  --fm5-media "E:/Games/Forza Motorsport 5/Mount/media" `
  --replace-existing -o outputs/fh2-to-fm5-abarth
```

The output folder must not exist. Inputs are opened read-only, their hashes are
checked before and after, and publication occurs after scratch validation.
Existing game ZIPs, databases, INIs and the DLL are not changed. The sole ZIP
created is the converted **car asset ZIP**, not an archive of the entire mod.

Copy the complete `fh2-to-fm5-abarth` folder into
`E:/Games/Azimuth/mods/Forza Motorsport 5/`, then restart FM5. The loader appends it to the FM5
load order. Select the existing 1980 Fiat 131 Abarth; this test deliberately
overrides that car rather than making a duplicate entry. Remove/disable the
addon and restart to restore the original. No DLL replacement is required.

Test exterior/cockpit rendering, liveries, lights, wheel positions, driving,
collisions, engine/drivetrain swaps and body/suspension/tyre upgrades. In
particular, the rally tyre level and rally suspension require external assets.
Game rendering/driving have not yet been tested; the validations below use a
private executable image and workspace-only database copies.

## What the Abarth comparison established

| File/data | FH2 -> FM5 treatment |
| --- | --- |
| CarScene `.carbin` | Both use v5; decode and re-encode all records. Render records v15 become v14, removing their v15-only four-byte field. Rewrite remapped upgrade/body IDs. |
| `.modelbin` | Preserve model containers, vertices, bones, morphs and material records. The leading skeleton field is a **bone count**, not a version. FH2's Abarth has two extra wiper bones; preserve their indices. |
| `.swatchbin` | Preserve every source texture byte-for-byte, without recompression/conversion. |
| `.materialbin` / embedded materials | Preserve containers and textures; translate typed FH2 light-function assignments. Night selector 32 and beam selector 37 use headlights (13); composite brake selector 69 uses car-light parameter 0, matching the working Huracan centre brake lamp. Front beam layers 13/15/17/37 are routed to the manual headlight channel using the carbin's `PrimaryLights` assignment. Secondary fallback 70 is disabled with function 0 on PrimaryLights; other materials retain the working v3 mapping of 1. This avoids the secondary brake response on front beams. Generic shader functions and car-light parameter indices are separate namespaces. Custom duplicate light parameters in the outer rear materials are converted too. Only parameter values change; offsets and container sizes stay intact. |
| `.shaderbin`, `.pso`, `.vso` | Supply native FM5 shader families of the same name. FH2 shader scenarios/programs differ; changing a version tag would not convert their bytecode. |
| `physicsdefinition.bin` | Parse the complete v20 definition tree, preserve all fixed fields/NaN bit patterns and packed collision shapes, and write v13. Relative offsets, collision flags and global-index arrays are preserved. No physics donor is used. |
| Animation/other assets | Keep source assets; omit debug companions. Compatibility of all animation paths still needs in-game verification. |
| Database | Project to FM5 columns, preserve target shared lookup definitions, allocate nonconflicting IDs for changed shared engine/body/drivetrain/upgrade records, and rewrite their references. |

The Studio physics parser provides the outer field order. The old games' type-5
shape payload is a **packed point-cloud structure**, not just an array of
Vector3 values: its header includes flags, a relative vertex pointer, count and
size. Interpreting that header as coordinates would corrupt it. The converter
keeps the complete blob and checks its bounds. FM5's actual readers at RVAs
`81FB50`, `81FDE0` and `820790` confirm the old layout.

## Required database graph

Do not export only `Data_Car`, or follow all reverse foreign keys. Many important
links are undeclared, and following every reverse link pulls in unrelated cars
which happen to share an engine. Selection starts with the exact car ordinal and
only traverses ownership backwards through the engine/body/drivetrain/motor
upgrade families. Shared lookup dependencies are followed forwards.

| Domain | Required records |
| --- | --- |
| Identity/availability | `Data_Car`; cameras, exceptions, part positions, colours/stripes and invalid default parts for that ordinal when present. |
| Body/graphics | `List_UpgradeCarBody`, `Data_CarBody`, all body-specific bumper, hood, skirt, tyre-width, weight and chassis-stiffness lists; wings and front/rear rim sizes. |
| Engines | Every applicable `List_UpgradeEngine` row, its `Data_Engine`, all engine-specific upgrade lists, torque curves, aspiration/configuration/cylinder/variable-timing definitions. Include swap engines too. |
| Drivetrain/motor | Applicable drivetrain/motor roots and data, clutches, transmissions, drivelines, differentials and motor parts. |
| Chassis/tyres | Brakes, spring/dampers and physics rows, both anti-sway lists and physics rows, tyre compounds, affect/friction curves and aero physics. |
| Appearance/lookups | Stock wheel definition, make/part manufacturers, country/region, class/type/family, engine placement, powertrain, material/colour, brake types, part strings, suspension types, damage definitions and steering override. |
| Upgrade UI | `UpgradeTypes` selected by **PartEnum**, `Upgrades` selected by its type ID and actual levels, plus upgrade areas. Part enums and upgrade-type IDs are distinct domains. |
| Localization | Referenced string-table keys; export missing/different values to `strings.json` for installed target languages, with English fallback. |

The Abarth extraction contains **599 source records across 83 tables**. Its
fitted FM5 addon inserts 56 rows and updates 25 rows. Existing compatible/shared
records become requirements rather than redundant inserts. Thirty numeric IDs
are remapped to avoid modifying definitions used by other stock cars. The exact
table counts, dropped columns, mappings, requirements and operations are in the
generated JSON; these numbers are specific to the installed databases.

FH2-only campaign, buckets, events, traffic and profile/progression records are
excluded. Fourteen FH2-only `Data_Car` columns are omitted because FM5 has no
corresponding fields. A new car has native availability flags set to installed,
purchased and drivable with `ContentId=0`; an existing car requires the explicit
`--replace-existing` switch and matching media name/ordinal.

The rally tyre entry demonstrates why menu data matters: FH2 uses level 5 and
FM5 lacks the matching `Upgrades` row. The addon includes that row, its text and
off-road icon. It also supplies the two rally suspension XMLs and both thumbnail
sizes from outside the car ZIP.

## Output

```text
fh2-to-fm5-abarth/
  database.json                 runtime Extra Options format 2
  strings.json                  generated when localization additions are needed
  source-records.json           full original FH2 rows for inspection
  conversion-report.json        hashes, remaps, operations, asset provenance
  media/cars/<media-name>.zip    converted car
  media/...                     external dependencies and native shader backings
```

Absolute virtual references found in car assets/database changes are checked
against the generated archive and native FM5 media. Missing source dependencies
are copied recursively as loose addon media. Thumbnail placeholders are expanded
to `_Big.swatchbin` and `_Small.swatchbin`. Unresolved explicit asset references
reject publication unless the identical reference already exists in the identical
original FH2 member and also cannot be resolved in FH2. Those inherited missing
references are preserved and reported as warnings under
`external_assets.preexisting_source_missing`, rather than repaired or hidden.
For example, the Huracán dashboard XAML references `Fuel_Blue.png`, which has no
matching source PNG or swatchbin. A new reference in a converted/new member still
rejects publication, even if another original member uses that same missing path.
This check is not a proof that every engine-generated or
relative filename has been enumerated.

Shader families are searched first in the matching FM5 car (or the optional
`--shader-donor FM5.zip`), then in other FM5 car archives. Native program paths
inside the shaderbin are retained. Their owning paths are also exposed through
loose addon files so the original donor ZIP need not already be mounted. A
shader family unavailable in FM5 is rejected; there is no shader cross-compiler.

## Other cars and limits

Use the same command with another FH2 ZIP and omit `--replace-existing` for a
new car. The Huracán archive actually installed here is `LAM_Huracan_15.zip`,
ordinal 2164; do the Abarth game test first.

The script checks every selected engine's native FM5 audio bank. FH2-exclusive
FMOD banks are **not** renamed or silently copied into FM5. If a bank is missing,
choose an explicit native engine sound, for example:

```text
--engine-audio FH2_ENGINE_MEDIA=FM5_ENGINE_MEDIA
```

Repeat for each exclusive engine. The script records this substitution. It does
not implement FMOD event/GUID/master-bank conversion. Unsupported physics
versions, shape layouts, missing shader families, incompatible schemas,
unresolved assets, ordinal collisions and new declared foreign-key violations
stop conversion with an error. It does not overwrite a different existing car.

Physics units are shared, but FH2 tuning and precomputed PI/performance ratings
are not FM5 rebalanced/recalculated. Native FM5 global tyre/lookup data is reused
where present. A structurally valid port still needs driving/performance tests.

## Validation

- All **354 FH2** and **322 FM5** installed physics files decode and round-trip
  byte-for-byte with the Python codec.
- All 354 converted FH2 files pass the **actual FM5 native deserializer** in a
  private image. Native definition construction, allocation and pointer
  relocation preserve all **706 packed shapes**. Only primitive byte-stream I/O
  is supplied by the fixture; no live process is touched.
- Abarth source/conversion carbin round trips, ZIP CRC checks and all **85 source
  textures** are verified. Source input hashes stay unchanged.
- The actual FM5 SQLite/addon JSON implementation applies the generated addon to
  a scratch database. Every database table matches Python replay exactly; all
  other `Data_Car` rows stay unchanged and SQLite integrity/foreign-key checks
  pass.
- No in-game renderer, collision simulation, audio playback, animations or save
  behaviour is claimed verified by those tests.

Tests: `test_car_physics_native.py` and `test_port_fh2_car.py` in
`source/development/tests/`. Physics format research used the local Studio source
at `ForzaTechStudio/Services/PhysicsDefinitionParser.cs`; see
`source/licenses/ForzaTechStudio-MIT.txt` for its upstream license.
