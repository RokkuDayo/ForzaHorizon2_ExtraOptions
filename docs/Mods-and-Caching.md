# Mods and database caching — v0.4.5

## Separate title folders

Beside `azimuth.exe`, FH2 reads `mods/Forza Horizon 2` and FM5 reads
`mods/Forza Motorsport 5`. Each has its own `loadorder.txt`. The loader creates
both game folders and the shared `mods/Cache` folder on startup. New package
folders are discovered as in FH2. Later entries have higher priority. A package
can contain:

```
My Mod/
  mod.json                 (optional manifest)
  database.json            (optional database edits)
  strings.json             (optional localized strings)
  media/                   (loose files, carbin JSON patches and ordinary ZIPs)
  plugins/                 (optional DLL plugins and their resources)
```

`addon_id` is not required. Existing manifest/dependency handling, transactional
rollback and `policy` settings are shared by both titles. A database package's
media is activated only after its database transaction succeeds. Native
mediaoverride and mediapc folders retain priority over package media.

FM5 database JSON targets `media/db/gamedb.slt`; FH2 targets
`media/stripped/gamedbRC.slt`. The diff launcher's target-path prompt accepts
both. The updated standalone `mod_diff.py` also supports FM5 v14 carbins.
The JSON schemas otherwise stay the same. Mods must still contain assets and
records suitable for the destination game: this does not convert FH2 content.

### Migrating the previous FH2 layout

If `mods/loadorder.txt` exists, the loader moves the legacy mod directories into
`mods/Forza Horizon 2` and moves that load-order file last. The game folders
and `Cache` stay at the root. Disabled packages and the load-order file's bytes
are preserved. Other loose files at the root stay there. Without a root
`loadorder.txt`, nothing is migrated. The old `mods-fm5` development folder is
not migrated.

If a mod folder already exists in the FH2 folder, that copy takes precedence
and the legacy copy stays at the root. A conflicting destination file, linked
destination, or existing destination load order stops migration and is logged
with its path. Resolve the conflict, then restart.
If Windows denies a move because a mod file is still open, the loader continues
moving the other folders and retains the original root load-order file. For
that launch, FH2 reads the original order and only loads packages present in
the new folder. The marker retains its priority and disabled entries. Close
the program holding the file and restart to complete the move. The log
identifies the first blocked path and Windows error. FM5 remains in its own game folder.
Linked legacy folders need to be moved manually. Migration runs only after the
game executable is validated and Extra Options is enabled. Old `mods-cache`
files are left alone; the new cache location rebuilds automatically.

## Persistent merged database

Both INIs accept:

```ini
[Mods]
DatabaseCache=1
```

FM5 additionally accepts `Enabled=1` in this section. Existing INIs need not be
replaced: these values default to 1. Restart after editing mods or configuration.

The first load merges and validates normally. Subsequent loads can restore the
same merged database into a private in-memory connection. The original game
SLT and package JSONs are never modified. Resolved ID/reference mappings, blocked
packages and activation state are cached alongside the database. Warm loads
restore these mappings for inherited carbin remapping without running conflict
planning or rewriting database JSON again.
Plugin database callbacks run after the resulting database is ready.

The key includes the actual incoming SQLite snapshot, Extra Options version,
experimental database features, ordered enabled packages, parsed JSON content,
and package-file names, sizes and modification times. JSON contents are hashed
as well as metadata. Runtime `.log` changes and directory timestamps are ignored.
Adding, removing, reordering or editing packages rebuilds the cache automatically.
Full database overrides through mediapc/mediaoverride also invalidate it because
the actual opened database is fingerprinted. No directory watcher or per-frame
hashing is used.

Files live in `mods/Cache/fh2-db` or `mods/Cache/fm5-db` beside the corresponding INI.
They may be deleted with the game closed. Revision 5 stores a bounded, pointer-free
ID plan in the metadata, covered by the header checksum. Counts, names, package
indices and flags are validated before restoring it. Older revisions rebuild once.
Each cache has checksummed metadata and database payload; a damaged or mismatched
pair is ignored. Writes use temporary files
and publish the header last. Unavailable cache storage falls back to a normal
in-memory merge. Set `DatabaseCache=0` to disable persistent caching.

Cold merges normally compare one source foreign-key baseline with the completed
stack, instead of scanning before and after every package. Invalid final results
discard the speculative merge and retry with per-mod validation, which scopes
checks to affected relationships unless triggers require complete scanning.
Explicit `foreign_keys: allow` stacks keep that per-mod policy behavior. One
separate SQLite integrity check also covers the completed merge. The cache
revision change causes one rebuild; unchanged subsequent boots reuse it.

FM5 carbin outputs are separate temporary, process-lifetime files in
`mods/Cache/fm5-carbins`; their contents are deleted automatically when the process
closes. They are not the persistent database cache.

## FM5 DLL plugins

FM5 scans only active packages' `plugins` directories. DLLs can use their usual
entry point, or export `FM5PluginInitialize` and `FM5PluginDatabaseUpdate` using
`fh2/fm5_plugin.h` (aliases the existing ABI; the context's image size identifies FM5).
FH2-specific initializer/database/vehicle exports are not called on FM5. A DLL
must be written for FM5; placing an FH2-only plugin in this folder does not port it.

## Validation limits

Native routines are exercised in private relocated executable fixtures. These
checks cover SQLite transactions/cache, strings, ZIP resolution and carbin
publication. Gameplay, audio and rendering still require in-game confirmation.

## Runtime plugin files

Database fingerprints exclude the `plugins` subtree of each package. Plugins
load separately and receive the ready database as usual. Their generated text
logs and mutable INIs are not database merge inputs. Previously, only `.log`
files were ignored, so logs such as `plugins/KITT_LOG.txt` changed the cache key
on every launch even with unchanged mod data. Database/manifest/string JSON,
media assets, enabled package order and the incoming database remain checked.
Cache format 3 causes one normal rebuild on upgrade; subsequent unchanged loads
reuse the published database. No installed old caches need manual deletion.
