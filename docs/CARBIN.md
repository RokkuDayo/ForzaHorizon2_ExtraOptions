# Carbin JSON patches

Use DLL version **0.2.140 or later**. Render parts retain the carbin's owning
volume and request full virtual model paths from it. Earlier loose/cache volumes
accepted only relative paths, leaving those model requests unresolved even when
the carbin itself could be read. Owning-volume full paths now resolve through the
complete filesystem, retaining original ZIP fallback and addon/override order.
The loader does not repack or modify the original car ZIP.

The loader accepts editable, record-based FH2 carbin patches. Place a patch next
to the virtual location of its carbin, with `.json` appended:

```text
mods/Forza Horizon 2/my-brz-wing/
  database.json                                  optional, new upgrade rows
  media/cars/SUB_BRZ_13/SUB_BRZ_13.carbin.json      carbin patch
  media/cars/SUB_BRZ_13/custom/wing.modelbin        your model, if needed
```

The original carbin can remain inside the game's ZIP. A mod adding an entirely
new car supplies its complete carbin first; other mods can patch that carbin too.
No manifest or database is required for a mod that only edits existing graphics.
Restart the game after editing patches or load order.

## Export, edit and compare

`source/development/tools/carbin_json.py` is standalone Python 3.10+ and needs
no extra packages. These examples assume it has been copied beside your files:

```powershell
# Export directly from the original car ZIP, or from an extracted .carbin.
python carbin_json.py export SUB_BRZ_13.zip vanilla.json
python carbin_json.py export edited.carbin edited.json

# Or edit a copy of vanilla.json, then convert it back to a carbin.
python carbin_json.py import edited.json edited.carbin

# Export only the changed records, for use by the mod loader.
python carbin_json.py diff SUB_BRZ_13.zip edited.carbin SUB_BRZ_13.carbin.json

# Optional: preview the exact merged binary locally before launching the game.
python carbin_json.py apply SUB_BRZ_13.zip SUB_BRZ_13.carbin.json preview.carbin
```

`export` verifies a byte-identical binary round trip before writing JSON.
`diff` checks that its patch reproduces the changed records. Unchanged record
order is retained; new records append. Ordering records without changing their
contents is not emitted as a change. A ZIP input must contain exactly one file
ending in `.carbin`; the `_debug` companion is ignored. Outputs cannot overwrite
an input path. Original ZIPs, databases and carbins are never modified by the loader.

Full JSON uses `format: "fh2-carbin"`. Runtime patches use
`format: "fh2-carbin-patch"`; both use `format_version: 1`. The loader reads only
the patch format. Exported full JSON is a working document for editing, not a
runtime patch.

## Adding a wing

In full JSON, `groups` contains the upgradable parts. `part_type: 9` is the rear
wing. Each group has:

- `upgrades`: IDs, levels, stock flags and parent body IDs.
- `models`: model paths, LOD/bone/material/AO information, model GUIDs, and
  `upgrade_ids` selecting which upgrade entries use that model.

ForzaTech Studio treats adding a **model**, assigning that model an **Upgrade ID**,
and adding an entry to the group's **Upgrades** list as separate actions. Adding
only the model and its ID does not create an upgrade record. The diff exports
exactly what the edited carbin contains; it cannot guess the missing record's
level, stock flags, parent body or opaque data. It now warns on newly introduced
model IDs with no matching upgrade, naming the group, ID and model path. Existing
vanilla references are left alone. If an upgrade entry was actually added or
changed, the diff includes it in `groups[].upgrades` automatically.

Copy a compatible upgrade and a compatible model in that group. Give the new
upgrade an unused `id` and an appropriate existing menu `level`. Give the model a new unique 16-byte `guid`, set
its `upgrade_ids` to the new ID, and set `path` to your model. Preserve the bone,
transform, opaque fields and material layout unless you know what they do.
Multiple model records may be needed for the part's LODs/material variants.
Use the exact virtual path of the asset you provide. For example,
`scene/_library/scene/86_frs_brz/...` and `scene/_library/86_frs_brz/...` are
different directories; the extra `scene` must agree with the actual files.

Also insert the matching row in `List_UpgradeRearWing` through `database.json`:
its `Id`, `Level`, `IsStock`, car ordinal and parent body relationship must agree
with the carbin. The level must also have a matching entry in `Upgrades` for
its `UpgradeTypes` type; otherwise the part can be absent from the menu.
Multiple parts may share a level (the vanilla BRZ has two level-1 wings).
A new numeric level needs a corresponding `Upgrades` definition; it is not just
an arbitrary sequence number. `RequiresGraphics` should reflect the new graphics. The carbin
does not create the upgrade menu entry, physics, price or localization by itself.
For a bumper/skirt/hood use the corresponding database table and part group.

FH2 wire part types include 34 front bumper, 35 rear bumper, 36 hood and 37 side
skirts. Values are preserved as stored in FH2; they are not renumbered to match
newer Forza games. Other existing groups can be edited with the same exporter.

`examples/carbin/brz-sample-wing/` contains a matching database insert and carbin
patch. Copy that **single folder** into `mods` to try it. It adds ID 1655099,
level 1 (street), sequence 7, using the existing localized street-wing name and a price
of 101 CR. Its graphics deliberately reuse the BRZ's stock wing: this is a
structural example, not a new wing model. Replace the model path and supply your
own assets to make a different wing. Remove it after testing; avoid using its
sample ID in a released pack.

## Merge behavior

Parts/groups match by `part_type`, upgrades by `id`, models by their raw 16-byte
GUID. Existing groups are merged through `upgrades`/`models` operations; whole
new groups/parts can be inserted too. Each operation is one of:

- `upsert`: insert a new record or replace the record with that key, using `value`.
- `remove`: remove that key; an already absent record is harmless.
- `merge`: edit individual records in an existing part/group, leaving its other
  records unchanged. A missing group/part is an error.

Independent new wings coexist. If two mods change the **same model or upgrade
record**, the later record wins as a whole; this first version does not combine
individual fields within that record. Use different new IDs/GUIDs for independent
parts. `_comment` fields are ignored. Unknown fields, duplicate operations,
duplicate identities, invalid types, truncated binary data and unsupported
versions reject the patch, with a named `MODS CARBIN` entry in the log.

Patches run in `loadorder.txt` order over the winning complete carbin. A later
complete carbin replaces earlier carbin patches, just as it replaces an earlier
complete file. Patches in that same mod and subsequent mods then apply. Above
all mods, `mediaoverride` and `mediapc` retain their existing priority. A failing
carbin patch leaves the previous valid binary in use; it does not undo unrelated
database/media changes from that mod. Dependencies can be declared in `mod.json`
as usual.

`base_sha256` is optional (or null). The exporter records the input's SHA-256;
a mismatch logs a warning and attempts the structural merge. Each patch in the
chain is compared against the selected complete base, not the previous patch's
output. Car ordinal and media name must still match. Review mismatches: a hash
warning cannot guarantee that a different base has compatible bones/materials.

## Limits and preservation

This version supports FH2 CarScene v5, render models v15, part records v1/v2,
upgrade groups v3 and upgrade records v1–v3. All 354 installed vanilla cars have
been verified to survive both Python conversion and runtime JSON encoding
byte for byte. It intentionally rejects other Forza versions, unknown trailing
data and car identity/header changes in a diff.

`transform_hex` stores the 16 little-endian float32 values of the model matrix.
`data_hex`, `unknown*_hex`, GUIDs and material Bundle bytes preserve undecoded
data exactly, including floating-point bit patterns. They are not guessed or
zeroed. Copy these fields from a compatible record rather than inventing values.
Material and AO texture paths remain editable strings.

Carbins are limited to 32 MiB, JSON to 16 MiB, a list to 4,096 records, and a merge
to 32,768 total parsed records. JSON parsing has the loader's existing depth and
node limits. There are at most 1,024 cached carbin paths in one session. Processing
is lazy, and repeated reads reuse the result. Generated binaries live in private
temporary files with independent native file handles and delete-on-close cleanup;
they are not written into `media`, mod folders or original ZIPs.

The wire-layout research references are [ForzaTech Studio](https://github.com/D3FEKT/ForzaTechStudio)
and the [ForzaTech extraction format documentation](https://github.com/Doliman100/ForzaTech-extraction-tools).
The converter and runtime implementation here were written independently and
verified against the local FH2 files; neither tool is needed at runtime.

Generated carbins use read-only temporary binary files with native synchronous
and asynchronous open/read/seek/close/reopen behavior. Full virtual asset requests
made directly against their owning volumes are routed through the complete game
filesystem. Relative requests still address the original owner. This also covers
plain loose carbins without JSON; models can remain in the original ZIP or be
provided as loose addon assets. The first owning-volume full-path read is logged
as `FILES: owning-volume full asset paths use the complete filesystem.`

## Database ID reassignment (0.2.146+)

If the same mod's database opts into `"policy": {"id_conflicts": "nearest"}`, matching
carbin JSON upgrade keys, upgrade records, car-body references and model
`upgrade_ids` follow its reassigned IDs. Whole addon carbins also receive matching
ordinal and upgrade/body/model updates. Changes are applied only to the in-memory
loaded carbin. JSON source identity checks still use the original carbin ordinal
and hash. Original files, model GUIDs, material indices and paths are unchanged.
See [MODS.md](MODS.md) for priority, relationship declarations and limitations.

## Combined drag-and-drop tool

Keep `mod_diff.py` beside `Mod Diff.cmd` and drop two carbins (or two car ZIPs)
onto the batch file. First file is vanilla, second is edited; confirm or swap the
order when prompted. The JSON is written beside the edited file, using the
vanilla filename plus `.json`. Python 3.10 or newer is required.

The same launcher accepts two SQLite/SLT databases. It prompts for overrides,
deletions, runtime value conflicts, automatic ID reassignment, optional table
selection/custom keys, target path, output and validation report. Existing output
requires confirmation. Inputs remain unchanged. Mixed file types are rejected.
Automatic upgrade generation is not included; the original lossless carbin
behavior and missing-reference warnings are preserved.

Advanced commands remain available in the single script:
`python mod_diff.py carbin diff vanilla.carbin edited.carbin patch.carbin.json`
and `python mod_diff.py database vanilla.slt edited.slt -o database.json --updates override`.

The batch exporter explains and confirms these inherited rules before a carbin diff.
There is no independent carbin ID policy: use the same mod’s database policy.
Base hash mismatches still warn and apply; invalid operations or another car’s
identity are rejected.

The drag-and-drop database wizard inspects the selected tables before prompting
for changed and removed rows. It shows affected table counts; the defaults export
field overrides and explicit row deletions. Ignore and Cancel remain available.
The wizard also permits custom carbin output and validation-report paths. Runtime
conflict policies continue to default to rejection. Advanced database CLI defaults
remain unchanged.
