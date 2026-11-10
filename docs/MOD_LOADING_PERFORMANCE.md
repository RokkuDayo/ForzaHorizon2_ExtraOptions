# Database loading performance: 2026-11-03

The supplied FH2 boot capture spends 7.579 seconds in database merging, including
2.944 seconds planning IDs, 2.835 seconds applying rows and about 1.4 seconds in
foreign-key validation. It shows a cache miss, not a warm-cache boot. The final
asset-mount timestamp is 9.050 seconds after handoff; this is an instrumented
interval, not a measurement of all loading through playable free roam.

## Implemented

The shared FH2/FM5 ID planner now indexes requested IDs by case-insensitive
table/column/value, keeps each mod's own slot list and builds linked identity
groups once. Duplicate detection, parent lookup, sharing and conflict resolution
search those candidates instead of repeatedly scanning every requested ID.
Full comparisons remain after hashing. Candidate and group iteration retain the
old ordering, including later-mod priority and nearest-ID tie behavior. Per-row
shared-identity checks also use the relevant mod's slots.

The index is reset with the existing plan. Memory is bounded: 8192 bucket heads,
128 per-mod heads and five additional pointers per requested slot. No worker,
per-frame operation, cache-format change or new external dependency is added.

Diagnostic builds additionally record key collection and reference rewriting by
mod, schema link reading, identity grouping, conflict resolution and reference
propagation. Regular builds compile those timings out.

## Private replay results

Database JSONs and manifests were copied from the installed stack to scratch.
The complete vanilla database was reconstructed in a private connection using
the game's relocated SQLite implementation. No user mods, saves or game files
were changed, and the game entry point was not executed.

| Stage | Previous | Indexed planner |
| --- | ---: | ---: |
| ID planning | 2.719 s | 0.164 s |
| Full cold database merge | 7.130 s | 4.669 s |

This is about 35% less cold merge time in that private replay, not a promised
in-game boot reduction. Separate optimized runs measured 4.63–4.67 seconds.
All 288 tables' resulting content matched the baseline fingerprint:
`dda8b3c6603cb37449dd4c346e6fddc2f4d25a1669f88bd626b80cce54e0880e`.

Additional validation compared baseline and candidate across 24 generated stacks
of 12 mods: nearest/error/shared policies, reversed priorities, duplicate IDs,
PK/FK identity groups, case-insensitive columns and failed winners retained the
same rows, references and activation. Transaction fixtures verify rollback,
foreign-key policies, guarded updates/deletions and malformed-input rejection.
Cold/warm cache fixtures verify equivalence, invalidation and failed-mod state.

## Report recommendations requiring care

- The final check is SQLite `quick_check`, not a foreign-key check. The game's
  SQLite 3.6.19 does not implement `PRAGMA foreign_key_check`. Removing per-mod
  relation checks without replacement would change validation. The subsequent
  implementation below retains a separate final relation check and fallback.
- Relation validation already restricts data queries to affected relationships
  when no triggers exist; schema enumeration is still repeated. Schema metadata
  caching could be investigated without dropping validation.
- The log alone does not prove quadratic FK cost, or that schema indexes should
  be removed during merging. Index changes can affect constraints and triggers.
- Executable verification remains a full-content check. Size/mtime reuse would
  weaken the current verification guarantee.
- Archive mounting uses native filesystem state; independence/thread safety is
  not established merely because folders are different. It remains sequential.
- Individual string-table spans are small and are not the main bottleneck. A
  diagnostic handoff summary can precede later per-table reads.

Next evidence: capture the updated diagnostic DLL on an unchanged stack, then
repeat without clearing `mods/Cache`, saving each log before the next boot.
That distinguishes remaining cold merge costs from the normal cached path.

## Follow-up: deferred foreign-key validation

The second user capture measured a 4.923-second merge, including 1.395 seconds
of before/after foreign-key scans. The default runtime path now captures one
source baseline and compares it to the completed stack. A failed comparison
restarts from the original source with per-mod checks to identify bad packages.
Explicit `allow` stacks and unavailable/oversized baselines retain the legacy
path. Standalone export validation is unchanged.

A sequential native-SQLite replay of the current copied mod stack measured
4.506 seconds before and 3.464 seconds after this change (about 23% less merge
time). All 288 table contents retained the fingerprint above. These are private
merge measurements, not an in-game total-boot prediction. The full final relation
scan costs more than one scoped per-mod scan; savings still exceed one second.

Native tests cover later-mod reference completion, invalid-package attribution,
guarded-operation retry, shared-ID fallback, pre-existing base violations,
explicit `allow`, oversized baselines and warm-cache activation. Twenty-four
generated 12-mod stacks also retain baseline data/reference/activation results.
Cache revision 4 forces one rebuild because final-stack validation can now
accept references supplied later in load order.

## Follow-up: wide-row validation

The third capture measured a 3.646-second merge, including 2.877 seconds applying
rows, 266 ms planning IDs and 120 ms of batch relation validation. Row processing
is now the main measured merge cost.

Insert validation previously compared each column name against every earlier
column in the row. This is especially expensive for `List_TorqueCurve`, which
has 250 columns. It now reuses a bounded hash lookup across inserted rows within
each mod transaction. Hash collisions still require full case-insensitive name
comparisons. Allocation failure or an unusually wide schema retains the original
validation; transaction cleanup releases the scratch storage. SQL operations,
constraints, conflict policies and cache format are unchanged.

Two sequential native-SQLite comparison pairs measured cold merges of
4.025 -> 3.701 seconds and 3.527 -> 3.457 seconds. This modest reduction varies
with system load; it is not a total-boot prediction. All 288 tables retained the
same content fingerprint above. Native regression fixtures cover 2-, 250- and
1000-column rows, reordered fields, exact Unicode/NUL values, case-insensitive
duplicate columns and atomic rollback. The ID, relation-validation and cache
regression suites also pass.

The diagnostic build now records `database.apply_table` by table name. These
inclusive spans identify the remaining costly tables without per-row logging.
The normal build compiles the timing calls out. Existing revision-4 caches remain
valid; there is no forced rebuild for this validation-only optimization.

## Follow-up: cached resolved ID plans

The unchanged-stack game capture accepted revision-4 cache data, reducing database
processing from 3.599 seconds to 585 ms. It still spent 264 ms rebuilding the ID
plan. Revision 5 now persists the resolved domains needed by binary/JSON carbin
references and the blocked-package state, alongside existing activation/exclusion
flags. Database reference edits are already present in the cached SQLite payload.
Planner graphs, JSON pointers and temporary SQL state are not serialized.

The existing source/content/order/settings/version fingerprint protects reuse.
Plan metadata is covered by the header checksum and receives bounded structural
validation. Corruption, allocation failure or incompatible metadata causes a
normal cold rebuild. Old revision-4 caches rebuild once; no settings migration
or user-file cleanup is needed. Both titles share this path.

A private native-SQLite replay of the copied installed stack measured 248.872 ms
planning on a warm baseline versus 0.004 ms restoring the new plan. All 288 tables
retained the same fingerprint. Whole warm merge times were 949 ms and 538 ms,
but other timings varied with system load, so the isolated planning measurement
is the more useful comparison. These are not in-game total boot measurements.

Regression tests verify warm hits contain no planning spans, car/wing mappings
survive restoration, failed packages remain inactive, content/order/source changes
invalidate, and corrupt/truncated metadata rebuilds. Malformed flags, counts,
package indices and string lengths are rejected even with a recomputed checksum.

## Large ID plans

ID planning no longer shares a 32,768-record ceiling across inserted keys, schema
links and propagated reference mappings. These records already use dynamic heap
allocations and now scale with the bounded input documents. The nearest-ID search
radius remains 32,768; conflict policies and the 128-package limit are unchanged.
Allocation failures are explicitly logged. Cached mapping counts are validated
against the actual payload size and the existing 18 MiB plan byte limit.

`development/tests/test_large_id_stack.py` covers 40,001 insert rows, priority
conflict resolution, foreign-key remapping and warm cache reuse. Native cache
validation and differential planner tests cover unchanged behavior for existing
stacks. A private copy of the installed 36,464-row FH2 stack reproduces the old
global failure and merges with this fix; its explicit `error` policies still
reject individual conflicting packages as intended.
