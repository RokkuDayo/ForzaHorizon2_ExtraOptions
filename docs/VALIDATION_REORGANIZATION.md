# Manufacturer tiles and source reorganization validation

Current build: Extra Options v0.4.6, Azimuth with title-gated FH2 and FM5 profiles.

- Build succeeded from `source/build.py` using the relocated local toolchain.
- FH2 startup/INI/patch lifecycle: 16 option combinations passed, including the
  shipped INI, cache switch validation, unknown-key rejection and rollback.
- Manufacturer native fixture: Buy Cars, My Cars, rim categories, localized names,
  alphabetical order, paging, recommended-car exclusion, footer registration and
  all 31 hook-site rollback checks passed. Generated XAML checks include bounded
  names, ellipsis, selected/hover scrolling, explicit logo effects and crop.
- FM5 Photo Mode footer, damage reset, altitude and existing bindings passed.
- Both games' native SQLite cache reuse and invalidation checks passed.
- FM5 mod-media lookup, strings, ZIPs and generated carbin publication passed
  with the new cache paths.
- FH2 carbin round trips covered 354 original cars; native asynchronous reads,
  generated volume ownership, base/loose model lookup and invalidation passed.
- The relocated standalone diff tool passed 16 database-wizard tests and 17
  string/ZIP tests.

No in-game visual verification was performed for the new logo crop or native
selected-text scrolling. No installed game files or old installed caches were
changed. The latest DLL and configuration templates are in `azimuth/`.

The maintained project has been copied out of the former `work` and `outputs`
trees. Automatic approval review still rejected recursive deletion even after
folder-specific write permission was granted. Those two obsolete trees therefore
remain. `development/tools/Remove-ObsoleteWorkspace.ps1` performs only that
remaining cleanup when run by the user.
