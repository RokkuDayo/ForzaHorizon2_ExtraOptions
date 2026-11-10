# Database foreign-key policy (0.2.210+)

Under the database.json policy group:

```json
"policy": {
  "id_conflicts": "error",
  "update_conflicts": "error",
  "foreign_keys": "allow"
}
```

`foreign_keys` defaults to `error`: newly introduced dangling references reject
the addon. `allow` preserves references even if their declared parent is missing.
It does not repair, filter or manufacture rows and does not disable SQLite
constraints globally. Invalid operation selectors, duplicate inserts, guarded
value conflicts and structural integrity failures still reject/roll back.
Each opt-in is logged with the mod name. This is independent of ID reassignment.

For runtime stacks without an explicit `allow` policy, validation now compares
the completed merged database against a single baseline of the opened source.
This accepts a reference supplied by a later mod and avoids two relation scans
per package. Existing source violations are tolerated; new final violations are
not. If the final comparison fails, the loader discards the speculative merge
and retries with the original per-mod checks to attribute and reject invalid
packages. Guarded operations and ID reservations are rebuilt from the original
source. SQLite operation checks/savepoints remain active in both paths.

Stacks containing an explicit `allow` policy, or whose baseline cannot be
captured within the existing validation limits, retain per-mod validation.
Standalone diff/export validation is unchanged. The database cache revision was
advanced, so an older cache rebuilds once before normal warm reuse resumes.

CLI: `mod_diff.py database vanilla.slt edited.slt --updates override --deletes include
--foreign-keys allow -o database.json --report database.report.json`

The two-file drag-and-drop wizard inspects the selected operations and prompts
only when they introduce dangling declared references. Reject is the default.
The validation report names child keys, reference columns/values and parent
tables. Scratch data must still match the requested export. The report's
no_new_declared_foreign_key_violations field is false when allowed violations
are preserved; scratch_merge PASS does not claim those references are valid.

The user's gamedbRC_nfslyy.slt has48 violations itself: CarPartPositions20,
List_SpringDamperPhysics8, List_AntiSwayPhysics6, List_UpgradeBrakes4,
List_TireCompound6, List_UpgradeRearWing3, WheelAnnotations1. The vanilla file
next to it has0. Export results:4257 inserts/32 updates/3 deletes over66 tables.
Modern SQLite scratch comparison matches all288 source table row multisets
exactly. Native SQLite3.6 DLL replay also compares all288 tables, using12
significant digits for REALs because SQL-text fixture setup uses the old reader.
No source DB edited. Runtime tests exercise strict rejection/rollback, explicit
allow, strict following an allowed addon, invalid policies, and duplicate keys.
