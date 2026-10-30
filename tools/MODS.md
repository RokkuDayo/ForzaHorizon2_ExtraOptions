# Addon loader — 0.3.0

Native asynchronous opens are supported for generated carbins and loose media
assets, including model files. Their original virtual filenames are retained;
private registered game paths route the underlying read handles to the mod or
temporary cache. Native stream/device parameters are passed through unchanged.

Carbin patches are now supported at the carbin's regular virtual path plus
`.json`, for example `media/cars/SUB_BRZ_13/SUB_BRZ_13.carbin.json`.
See [CARBIN.md](CARBIN.md) for the converter, diff commands, record merge rules
and a matching BRZ wing/database example.

Addon ZIPs open on demand through private backing names, including the first
selected car at startup and subsequent archive reopens. Their physical paths
remain in the mod folder. The archive-open redirect installs when mod archives
are first registered, after host initialization; ordinary game ZIP opens retain
the translation layer's handling. Mod authors do not need to change ZIP contents.

`FH2_ExtraOptions.log` names each discovered ZIP and reports whether it mounted,
its entry count, or the reason it was skipped (including Windows open errors).
The first carbin lookup for each archive also reports whether it resolved from
that ZIP, another volume, or failed. Each outcome is logged once per archive to
avoid repeated messages while browsing cars. Registration alone does not prove
that the car's binary model data is compatible with the game.

## Drag-and-drop database comparison

Keep `mod_diff.py` and `Addon JSON Generator.cmd` together. Drop the vanilla and edited
SQLite databases onto the batch file, confirm their order, and select the
prompted export options. The same tool detects and compares carbin files.
See `MOD_DIFF.md` for output naming and advanced commands.

## Installation and layout

Azimuth v1.0.1: put `winmm.dll` and `mods/` beside **Azimuth.exe**, not inside
`data/bin` or the selected game folder. The selected game folder can be anywhere.
Mezcal: put `mods/` in **Mount**, beside the game EXE and `xg.dll`.
Keep your existing INI and settings DAT. No new INI switch is required.
`Global.Enabled=0` disables the mod, including the loader.

```text
Azimuth/
  Azimuth.exe
  winmm.dll
  mods/
    loadorder.txt
    my-car/
      mod.json          optional manifest
      database.json       optional; exporter format 1 or 2
      strings.json        optional; localized string additions/overrides
      media/
        cars/             game-relative assets
        stripped/RC0/
          Thumbnail_2546_Big.swatchbin
          Thumbnail_2546_Small.swatchbin
```

The same `mods` structure goes in `Mount/mods` for Mezcal. New mod folders are
appended to `loadorder.txt` automatically. Restart after changing mods or order.
A mod may contain only `media/`; neither a database nor a manifest is required.
Cars are defined by their database JSON, with no extra car list in `mod.json`.

## Load order

Use UTF-8 text, one folder name per line. Blank lines and lines beginning with
`#` are ignored. To disable a discovered folder, comment out its exact name:
`# my-car`. Such folders are not automatically re-enabled. Folder names may contain spaces and Unicode; paths outside
`mods` are rejected. Example:

```text
# First loaded, lowest addon priority
my-car
my-car-balance-patch
# Last loaded, highest addon priority
```

New folders are appended automatically. Active entries whose folders no longer
exist are removed; the remaining order, comments, disabled entries and line
endings are preserved. Updates replace the file only after the new contents
have been written and flushed successfully.

Asset priority, highest first:

1. The game's `mediaoverride/`.
2. The game's `mediapc/`.
3. Enabled mods, from the last listed folder back to the first.
4. Original loose files and ZIP members.

With addons present, the two existing override folders retain priority even
when the INI's separate MediaOverrides switch is off. Absent files fall through
to lower-priority mods and original assets. When the file is absent or empty,
new folders are discovered and written in alphabetical order. Existing entries,
comments and order are preserved; newly discovered folders are appended at the
end. Dot folders and directory links are not discovered. A duplicate folder or invalid load-order entry disables the
addon plan and is reported in `FH2_ExtraOptions.log`.

## Media files

Place assets below `media/` with the same paths they have below the game's
`media/`. For a ZIP member, use its virtual directory without `.zip`:

```text
Game:  media/Camera.zip -> CameraPhysics.xml
Addon: media/Camera/CameraPhysics.xml
```

ZIP archives placed in `media/` are discovered recursively and registered with
the game's native ZIP reader. For example, `media/cars/POR_911GT3RS_23.zip` exposes
its members under `game:\media\cars\POR_911GT3RS_23\`. Within one mod,
loose files override ZIP members; later mods still override earlier ones.

**No `zipmanifest.xml` is needed.** The loader reads directory offsets, lengths
and entry counts directly from each archive. A mod's `media/zipmanifest.xml` is
ignored, preserving the game's complete archive list. Standard single-volume
ZIPs with stored or deflated entries are supported; ZIP64 and split archives
are not. Archive registration waits for the mod's database transaction, if any,
to succeed. No ZIP extraction or original archive edits are required.

Existing texture/model formats are required. This does not convert assets,
execute scripts or load addon DLLs. Mod files are read-only. A whole
`media/stripped/gamedbRC.slt` inside an addon is deliberately ignored; use JSON
for database changes. Original game files and RC archives remain untouched.

## Car thumbnails

Thumbnails use the game's regular folder structure. For a database `Thumbnail`
path such as `game:\Media\Stripped\RC0\Thumbnail_2546.png`, supply:

```text
media/stripped/RC0/Thumbnail_2546_Big.swatchbin
media/stripped/RC0/Thumbnail_2546_Small.swatchbin
```

The RC number must match the database path. Native RC ZIPs are supported too.
The special `media/thumbnails` shortcut is no longer used. These must be real
swatchbin textures. The native game adds the size suffix; JSON backslashes must
be escaped.

## Videos

Loose Bink videos such as `media/ui/videos/press_start.bk2` override the matching
native video. The loader registers an internal filename for Bink and resolves
its read-only file opens directly, preserving Unicode mod-folder paths. Later
active mods take priority, with `mediaoverride` then `mediapc`
above mods. Missing overrides use the original video. Keep the native relative
path and file format.

## Database JSON

Use the bundled `tools/export_database_diff.py` (Python 3):

```text
python export_database_diff.py vanilla.slt custom.slt -o database.json --id-conflicts nearest
```

Omit `--id-conflicts nearest` to keep conflicts as errors. `--updates override`
exports deliberate field changes; `--deletes include` exports guarded deletions.
Both source databases remain unchanged. `--addon-id` is optional legacy metadata.
It compares a vanilla SLT with an edited copy. The loader accepts the earlier
Porsche **format 1** (inserts) and exporter **format 2** (inserts, explicit
field updates and guarded deletions). The target is currently limited to
`media/stripped/gamedbRC.slt`. Schema changes and arbitrary SQL are not supported.

Each addon runs as one transaction against a private in-memory database copied
from the game's normal database connection. Invalid data or conflicts roll back
that entire addon; its media and dependent addons are then disabled. Other
valid addons continue. The original SLT is not written. Native database functions
and car-list construction continue after the merged connection is ready.

Format 2 requires `key` for each table. Inserts contain every column and never
replace an existing row. An update has `where` (the complete key), `set` (only
changed fields), and `expect` (the original values of those fields). Thus two
addons changing different fields can coexist. `update_conflicts: "error"` is
the default: a changed expectation rejects the addon. `"last_wins"` deliberately
allows later field overrides despite old expectations; it still requires exactly
one target row and never turns duplicate inserts into replacements.

```json
{
  "format_version": 2,
  "target": {"database": "media/stripped/gamedbRC.slt"},
  "policy": {"id_conflicts": "error", "update_conflicts": "last_wins"},
  "requires": {"records": {}},
  "tables": {
    "Data_Car": {
      "key": ["Id"],
      "update": [{
        "where": {"Id": 2546},
        "set": {"BaseCost": 250000},
        "expect": {"BaseCost": 200000}
      }]
    }
  }
}
```

The optional `target.base_sha256` compares the original database file in the
selected game's Mount folder. A mismatch or unavailable hash logs a warning
with the addon ID and expected/actual hashes; the loader still attempts the merge.
Exporter-provided required records, table/column
names, selectors, SQLite constraints and new declared foreign-key violations
are validated. The game's database does not declare every logical relationship;
addon authors must include all technical records and required assets themselves.
Use `{"blob_hex":"00ff"}` for BLOB values. Strings remain Unicode.

## ID conflicts and automatic reassignment

`database.json` loads without an `addon_id`. That legacy field is ignored;
`mod.json` identifies the mod when present, otherwise the containing folder does.

Put this option in the `policy` object of `database.json` to opt in:

```json
{
  "policy": {"id_conflicts": "nearest", "update_conflicts": "error"},
  "format_version": 2,
  "target": {"database": "media/stripped/gamedbRC.slt"},
  "tables": {}
}
```

Later entries in `loadorder.txt` keep their requested IDs. An earlier mod with
`nearest` gets the closest available positive ID (the higher ID wins a distance
tie). IDs already in the vanilla database are always reserved. Original IDs
requested by other active mods are reserved before replacements are allocated.
Without this option, or with `"error"`, a conflicting mod is skipped. A failed
mod releases its reservations before the merge is replanned. The log identifies
the mod, table, ID, competing owner and final reassignment.

Reassignment covers inserted positive signed 32-bit, single-column primary keys.
Matching inserted fields and update `set` fields follow SQLite foreign keys and
known FH2 car, engine, body, make, manufacturer and wheel references. Shared
primary/foreign keys move together. Numeric strings are supported. Existing-row
`where`, `expect` and `requires.records` selectors keep their original meaning.
Prices, physics values and unrelated matching numbers are not replaced.

For a relationship absent from the schema and known FH2 references, declare it
explicitly in `id_references`, alongside `tables`. For example, a custom column
holding a car ordinal would use:

```json
"id_references": [
  {"table": "MyTable", "column": "OwnerCar",
   "target_table": "Data_Car", "target_column": "Id"}
]
```

These must name existing database columns; this does not add tables or columns.
Declarations describe relationships, not arbitrary number substitutions. They
supplement the schema; numeric assignments remain scoped to each mod. References
to IDs supplied by another mod are not automatically retargeted.

Matching carbin JSON upgrade keys, upgrade IDs, body IDs and model `upgrade_ids`
are remapped for the same mod. Whole addon carbins also have their car ordinal
and matching upgrade/body/model references remapped before loading. This happens
in memory; source JSON, binary assets and the vanilla SLT are never rewritten.
Model GUIDs, asset paths, opaque blobs and non-carbin formats are not rewritten.
Authors still need distinct model GUIDs and paths when adding independent assets.
Higher-priority `mediaoverride` and `mediapc` files retain their normal behavior.

Composite or nonnumeric keys are logged as conflicts and are not automatically
reassigned. Ambiguous mappings reject the affected mod. Allocation searches at
most 32,768 IDs in either direction, with a bounded plan of 32,768 ID/reference
records. Keep the mod list and load order stable for saves using reassigned IDs:
changing them can change the IDs assigned on the next launch.

## Optional manifest and dependencies

`mod.json` is optional. The old `addon.json` name remains accepted for existing
packages; `mod.json` takes precedence if both exist.

Without `mod.json`, the loader looks for `database.json`, `strings.json` and
`media/`, and uses the folder name as the mod's identity.
A manifest allows explicit IDs, alternate relative paths and dependencies.
A media-only mod may omit the `database` key or set it to `null`:

```json
{"format_version": 1, "id": "camera-pack", "media": "media", "database": null}
```

Example database mod:

```json
{
  "format_version": 1,
  "id": "my-car-balance-patch",
  "name": "My car balance patch",
  "version": "1.0.0",
  "database": "database.json",
  "media": "media",
  "strings": "strings.json",
  "dependencies": ["my-car"]
}
```

Manifest IDs use lowercase ASCII letters, digits and single hyphens. Legacy
database `addon_id` values do not need to match. Dependencies name the manifest
ID, or the folder name when there is no manifest. They must be listed earlier and load
successfully. Set `"database": null` for a media-only manifest, or `"media": null`
for data only. A missing explicitly named database rejects the addon. Duplicate
manifest IDs reject the later addon. Omitted optional media folders are harmless.

Limits: 128 addons, 64 KiB load-order text, 16 MiB per JSON document, 64 MiB total
input text, JSON nesting 64 levels and 262,144 values per document. The loader targets the supported game build and Azimuth v1.0.1; all existing
executable/version guards remain active. See `VALIDATION.md` for what was tested.

## Localized strings

Place an optional `strings.json` beside a mod's `database.json`. It uses UTF-8
JSON; text is converted to the game's UTF-16 string pool in memory. Original
language ZIPs and STR files remain unchanged. For example:

```json
{
  "format_version": 1,
  "fallback_language": "EN",
  "languages": {
    "EN": {"Data_Car": {"27932": "911 GT3 RS (992)"}},
    "BR": {"Data_Car": {"27932": "911 GT3 RS (992)"}}
  }
}
```

Language codes match the installed language ZIPs (case-insensitive): EN, GB,
BR, CHT, CZ, DE, ES, FR, HU, IT, JP, KO, MX, NL, PL, RU, DEV and LOC. Exact-language
entries override the fallback language within each mod. English is the default
fallback. Later mods override earlier mods, and each supplied key replaces or
adds that entry; omitted keys retain the native text. New strings also work
when the table was loaded before the mod's database: the loader refreshes it
after a successful database handoff. Failed mods/dependencies add no strings.

Keys are the table's **string IDs**, not car IDs. Keep them aligned with the
localization keys in the database: Porsche key `_&3444141340` addresses
Data_Car ID `27932`. Use decimal IDs from 0 through 65534; 65535 is reserved.
Table names omit `.str` and must refer to existing game tables. Accents, CJK,
supplementary Unicode, line breaks and empty text are supported. Embedded NULs
are rejected. `"strings": "other-name.json"` selects another file in mod.json;
`"strings": null` disables string data. Neither a database nor media is required
for a strings-only mod.

The included `source/development/tools/export_string_tables.py` uses only Python's
standard library. It exports a full STR, a changed/new-key diff, or one table
from every language ZIP. Example commands:

```text
python export_string_tables.py export Data_Car.str full.json --table Data_Car --language EN
python export_string_tables.py diff Data_Car.str Data_Car_Porsche.str strings.json --table Data_Car --language EN
python export_string_tables.py languages PATH_TO_STRINGTABLES reference.json --table Data_Car
```

Use the **diff** as a deployable mod file. Full exports are editing references;
loading a full English table with English fallback would intentionally replace
those keys in other languages too. Deletions are not inferred by the diff.

## Stock rims for addon cars

The merged database supplies each new wheel's donor car through StockWheelID.
On first use, the loader reads that donor's carbin through the normal filesystem
(including mod ZIPs), and discovers all four wheel models and AO textures from
its authored spindle/hub references. Paths are cached for later previews and
loads. No generated stock_wheel_assets.h entry or mod manifest car list is
required. Existing vanilla wheel routes retain their deliberate AO corrections.
The addon must include the referenced wheel assets in a supported carbin layout.

### Filtered table exclusions

The database exporter accepts a table replaced by a filtered view of the form
`CREATE VIEW RaceParticipants AS SELECT * FROM RaceParticipants_all WHERE Ordinal<>2032`,
provided its new backing table has the original table definition. It compares
only the rows visible through the view and exports excluded existing rows as
normal guarded deletions. Choose **Export deletions** in the batch tool, or use
`--deletes include`. No view, helper table, or runtime exclusion rule is installed;
rows inserted by other mods are not automatically excluded. Other schema changes
remain unsupported. Foreign-key validation still applies.

### Combined string-table extractor

`mod_diff.py` also embeds the string-table exporter; no separate Python module
is needed. Drop one STR or string-table ZIP (such as `EN.zip`) onto
`Mod Diff.cmd` for a full export, or two for an added/changed-string diff.
ZIPs default to all contained STR tables; you can select a single table instead.
The prompts select table(s), language and destination. Language defaults to the
ZIP's language code when recognized, otherwise EN. Nested folders are supported;
archives are read directly without extraction or modification. Unchanged tables
are omitted from diffs. Deleted strings/tables cannot be removed by strings.json.
Reserved ID 65535 is omitted with a note; duplicate table names are rejected.
CLI commands use `python mod_diff.py strings export ...`, `strings diff ...`, or
`strings languages ...`, with the same arguments as `export_string_tables.py`.

```text
python mod_diff.py strings export EN.zip strings.json
python mod_diff.py strings diff vanilla_EN.zip edited_EN.zip strings.json --language EN
python mod_diff.py strings diff vanilla_EN.zip edited_EN.zip strings.json --table List_PartManufacturer
```

For ZIP inputs, omitting `--table` includes all tables. A mixed ZIP/STR comparison
requires `--table NAME`. Directory language exports retain the Data_Car default;
use `--table "*"` to include every table from each language ZIP.

Older top-level `id_conflicts` remains accepted for compatibility. If both locations
are supplied, their values must agree. New exports use only `policy.id_conflicts`.
The batch workflow prompts for both runtime conflict policies; export-only choices
(changed/deleted rows) determine which operations are written, not runtime policy.
