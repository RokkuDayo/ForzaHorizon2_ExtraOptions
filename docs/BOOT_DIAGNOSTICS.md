# Mod loading diagnostic build

This temporary Azimuth DLL supports FH2 and FM5 and measures Extra Options' boot
and mod-loading paths. Normal builds compile the instrumentation out. It does
not change mod order, cache validation, database edits or asset selection.

Build separately from the normal DLL, from the workspace root:

```text
python source/build.py toolchain/zig-x86_64-windows-0.15.2/zig.exe --azimuth --fm5 --boot-diagnostics --build-dir .build/boot-diagnostics --output outputs/boot-diagnostic/winmm.dll
```

Do not distribute a build using `--test`; that switch adds private fixture exports.

## Capture

1. Close Azimuth and keep a copy of the normal winmm.dll.
2. Replace only winmm.dll with the diagnostic build. Keep existing INIs, DATs,
   mods, load order and mods/Cache untouched.
3. Boot with the usual mod stack and wait until the game finishes loading.
4. Copy FH2_BootPerformance.log or FM5_BootPerformance.log from Azimuth's folder.
   Include the ordinary Extra Options log as well for cache/rejection explanations.
5. For a useful comparison, boot again unchanged and save that log separately.
   Each boot replaces the diagnostic log for that title. Do not clear the cache.
6. Restore the normal DLL after the captures.

## Reading the log

TSV `span` records contain milliseconds since game-mapping handoff, duration,
thread ID, stage and mod/path detail. Milestone summaries rank the 30 largest
accumulated stage/detail totals, showing maximum duration and call count.
Repeated transactions indicate merge retries, rather than necessarily different mods.

Recorded stages include native executable mapping and verification, hook setup,
missing commit generation, mod discovery/manifest parsing, base database hashing,
cache fingerprint scans by mod, cached database loading, database clones, ID and
reference planning, per-mod row updates and foreign-key checks, final integrity
validation, cache writing, experimental swap expansion, string tables, archive
registration, new carbin patch preparation and plugin loading/initialization.
ID planning is split into key collection and reference rewriting by mod, schema
reading, identity grouping, conflict resolution and reference propagation.
Row application also records `database.apply_table` by table name, aggregated
across mods. Its individual timestamps fall within the enclosing mod's span.
Normal uncached stacks show `foreign_keys_batch_baseline` and
`foreign_keys_batch_final`. Per-mod before/after checks appear on fallback or
when explicit foreign-key opt-outs retain the legacy policy path. A final result
of `retry_per_mod` explains the subsequent database rebuild.

`cache.restore_ID_plan: restored` measures restoration of the resolved runtime
mappings. Accepted warm loads no longer run `database.ID_conflict_reference_planning`.
`cache.load_total: hit` includes checksum and plan validation; package activation
consistency can still reject it afterwards. The ordinary log confirms whether
the merged cache was actually accepted. Carbin and archive entries can arrive after a database summary;
their individual spans still carry durations and timestamps.

These are inclusive wall times: nested and parallel spans overlap. Do not add
them together to obtain total boot time. They do not profile all native game
loading, GPU work, or background tasks started independently by plugins.
The logger reports its own approximate accumulated overhead as `logger_ms`.

Storage is bounded to 1024 aggregate stage/detail keys and 4096 individual span
records. Aggregation continues after the event limit. `dropped_keys` reports
aggregate overflow; `events=4096` means individual output reached its limit.
There are no per-row or per-file-open traces, polling workers or GPU queries.
The log can contain local mod and plugin paths.

## Validation

`development/tests/test_boot_diagnostics.py` checks concurrent log records,
ranking, error-state preservation and title separation, then exercises the real
database merger through cold/warm cache reuse, invalidation, failed transactions,
ID remapping and corrupt-cache recovery. This verifies the instrumentation and
database behavior privately; in-game boot timings require a capture.
