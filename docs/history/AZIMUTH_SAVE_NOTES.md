# Azimuth save hash/size override — 0.2.30

## Inspected host

Azimuth 0.9.6: 3,048,960-byte executable, PE timestamp `0x6aca9dc0`, image size
`0x349000`, SHA-256
`7e8449fb50a181c24d5d9f75366aafbf4cede645ab9e90db20486703802fceaf`.
All addresses below are RVAs. This compatibility option is included only in
the Azimuth adapter and runs after successful enabled mod initialization.

## Save validation

The v2 commit marker begins `Azimuth complete save revision v2`, followed by
`blobs N` and a `SHA256 SIZE NAME` line per blob. The parser at `0x12f6a0`
requires well-formed hexadecimal hashes, names, counts and sizes. Revision
selection at `0x12fe00` checks file existence and size. At `0x130700` it
compares the actual file size in RBX with the marker's size at `[RDI+0x40]`.
The six-byte mismatch branch at `0x130704` is now bypassed by this option.

The reader at `0x130df0` loads each blob. It compares the loaded size against
the marker at `0x13108f`; the mismatch branch at `0x131093` is also bypassed.
For a v2 hash it
uses BCrypt to hash the raw bytes, finishes a 32-byte digest, and converts it
to 64 hexadecimal characters. The comparison at `0x1313e2` checks hash-string
length; the call at `0x1313ed` compares the hash characters with `memcmp`.
`jne` at `0x1313f8` routes mismatches to the rejection flag at `0x13140a`.
That flag is checked at `0x13145a`, leading to the blob/marker mismatch error.

## Change

`Compatibility.DisableAzimuthSaveChecksums=1` now replaces three mismatch
branches with equal-length NOPs:

| RVA | Original | Replacement |
|---|---|---|
| `0x130704` | `0f 85 dd 02 00 00` | Six NOPs: revision recorded-size mismatch. |
| `0x131093` | `0f 85 68 03 00 00` | Six NOPs: loaded blob recorded-size mismatch. |
| `0x1313f8` | `75 10` | Two NOPs: hash content mismatch. |

The hash still gets computed; the size and hash mismatch rejection branches are
skipped. Marker syntax, hash-string length and the legacy empty-hash path remain.
Buffers keep their actual loaded file lengths; the patch does not substitute
the marker's recorded length for buffer allocation or data size.

The reader guard covers `0x131080` through `0x131464` (0x3e5 bytes). SHA-256:

- Original: `a49f89cb71b0f9dc86321315c2bb55d2273c01003d7a86237cd9d1068942f80c`.
- Patched: `8d2de866bb83540dc567d9d9679d1547f2f147dd670324430d38e50a994a0f2b`.

The revision guard covers `0x1306b9` through `0x130709` (0x51 bytes). SHA-256:

- Original: `58b357191c95c327e8491d7a6f37799f165f3106c0e9539716216182986953d8`.
- Patched: `201b9b8488df01299a300c44a605a740a95a6afe52e2c2021e63ed1b4b506c95`.

`azimuth_host.c` checks the x64 executable headers and both code hashes before
changing any permissions. It obtains write access to both regions before editing
bytes, flushes the instruction cache and restores protection in reverse order.
Failed updates roll back saved bytes and protections; unknown code skips the
entire save override. Already patched regions are recognized independently.
The livery module uses the same helper for its separate region. Neither executable
is changed on disk.

The option defaults to 1, including when absent from an existing INI. Use 0
and restart for native hash and size verification. The global master switch disables the
override. There is no live reload or unload-time unpatching.

## Scope

Blob byte lengths can differ from the marker. The files must still exist and
be readable, and any existing marker must be well formed. Since 0.2.62 the
option rebuilds missing markers as described below; it preserves existing
markers and does not validate the game's internal save contents.
The normal writer still hashes new/rewritten blobs; a loaded but unchanged
blob can retain its existing marker hash. Storage quotas, disk-capacity checks,
IO failures and revision publication are unchanged. The converter continues
to produce and verify correct manifests. Mezcal is unaffected.

## Tests

`development/tests/test_azimuth_save.py` maps the owner's host executable and executes its
actual comparison and size-decision instructions at both revision selection and
blob loading. Wrappers supply the native register/local layouts; only `memcmp`
and the success/error exits are redirected.
The test reproduces ordinary rejection, verifies first/middle/last-character
mismatches are accepted only after applying the override. Equal, shorter, longer,
empty and imported stale sizes (4856 versus 4) are accepted with the override on;
native mismatch rejection works with it off. Hash-length rejection remains.
It also checks idempotence, changed-region/header refusal, both pre-write memory
protection failures and restored RX protection. A whole-image comparison checks
that only the fourteen intended host bytes change.
No save files or native file-write routines are exercised by that test.

## Azimuth 0.9.7 update

Inspected `azimuth.exe`: 3,087,872 bytes, timestamp `0x6acb70a6`, image size
`0x352000`, SHA-256
`5d3ce30b440dab2604fe42ff7cd129394f0b98418e22b2a44270f548199bc6a9`.
The same three mismatch branches remain, at these new RVAs:

| Check | 0.9.6 RVA | 0.9.7 RVA | Original bytes |
|---|---|---|---|
| Revision blob size | `0x130704` | `0x1347c4` | `0f 85 dd 02 00 00` |
| Loaded blob size | `0x131093` | `0x135153` | `0f 85 68 03 00 00` |
| SHA-256 content | `0x1313f8` | `0x1354b8` | `75 10` |

The new guarded revision region is RVA `0x134779`, 0x51 bytes; the reader
region is RVA `0x135140`, 0x3e5 bytes. Each build has separate original and
patched SHA-256 guards in `shared/adapters/azimuth_save.c`. The shared writer selects
the inspected build using its timestamp and image size, then verifies both
regions before changing them. The native decision tests run on both versions.
Marker format, hash-string length, quota and normal write paths retain the
same behavior as the previous release.

The new original `azimuth-tls.dll.old` is 4,608 bytes, SHA-256
`798864645e9dfabce907d6964a6eac807d9747b634da1ced204591de28ae343b`.
It contains TLS setup, not save validation. Its template is still 336 zero
bytes, aligned to 16 bytes, with no callbacks. Exports remain
`azimuth_tls_attached` and `azimuth_tls_index`. The new host TLS loader entry
is `0x92150`; the release DLL is exercised against it in the private fixture.

## Azimuth 0.9.8 update — 0.2.62

The inspected host has timestamp `0x6ace09b5`, image size `0x35a000` and SHA-256
`4c831c30727d0b09d6e239c9dd173157d5d85b235fff12cb0d0a725d43768f04`.
Its v2 marker parser is `0x133af0`, writer `0x1331f0`, revision selector
`0x134250` and blob reader `0x135240`. The same three rejection branches remain:

| Check | RVA | Original bytes |
|---|---|---|
| Revision blob size | `0x134b54` | `0f 85 dd 02 00 00` |
| Loaded blob size | `0x1354e3` | `0f 85 68 03 00 00` |
| SHA-256 content | `0x135848` | `75 10` |

Separate SHA-256 guards cover revision region `0x134b09:0x51` and reader
region `0x1354d0:0x3e5`. The native decision fixture executes this build's
instructions and checks the existing refusal, rollback and idempotence paths.
The previous release did not recognize this host, so its comparisons stayed
enabled. The live log rejected a C_livery blob of 73,981 bytes whose existing
marker recorded 122 bytes. A separate imported livery revision had no marker.

## Persistent missing-marker repair

After successful save compatibility patching, `azimuth_markers.c` scans
`<host directory>/data/saves/<profile>/<container>/revisions/<16 hex digits>/`.
For a missing `commit`, it enumerates the flat `data` directory and computes
each actual byte length and SHA-256. The output matches the native v2 writer:

```text
Azimuth complete save revision v2
blobs N
SHA256 SIZE NAME
```

It creates a unique `commit.extra-options-*.tmp` in the revision, writes all
bytes, flushes and closes it, then publishes with MoveFileExW and no replacement
flag. A concurrent or pre-existing commit is never overwritten. Failed
publication removes the temporary file. Blob files are opened read-only;
exclusive access conflicts, partial reads, changed lengths and enumeration
errors prevent publication. Data must contain 1–128 flat regular files with
ASCII token names. Empty folders, invalid revision names and reparse paths are
not imported. There is no limit on the total number of repaired revisions.

This runs once during enabled startup, tied to
`Compatibility.DisableAzimuthSaveChecksums`. Global disable, option disable,
unknown host or failed code guards skip it. Native writers, revision selection
and future commit publication remain intact. Repair persists after removing
the mod; an existing marker with stale sizes is still preserved and requires
the mismatch override if the native reader would reject it.

`test_azimuth_markers.py` uses actual private filesystem fixtures, repairing
264 revisions across profiles with exact metadata, preserving existing markers
and blobs, refusing malformed/locked inputs, retrying safely and checking
idempotence and temporary-file cleanup. It never opens personal saves.

## Azimuth 0.9.9 — 0.2.64

Revision validation moved to RVA 0x174b69 (0x51-byte guarded region), with its
recorded-size rejection at +0x4b. The blob reader moved to RVA 0x175530
(0x3e5-byte guarded region), with the hash and size branches at +0x13/+0x378.
Their layouts and replacement instructions remain unchanged.

| Region | Original SHA-256 | Patched SHA-256 |
| --- | --- | --- |
| Revision | aec1b8fe4b4ba62b77dde47c53a855edfac49fb621ed8a084b04dc993441a1f3 | 443b16335749d5a3601088023f5cf190f455110a917e14200eec95686491ce35 |
| Reader | 480fdae8abafd13b4ffaa2c3cb463779d048deb91ff57d70d24fa0772ccc7729 | 6dcb795a890d0be2940acaca2400b36b579cb97051c2c25d20d83e0353005aa6 |

Native fixtures execute the new instructions with actual private files and
supplied filesystem/CRT boundaries. The host recognizer requires timestamp
0x6aceb567 and image size 0x3b9000; whole-region guards precede edits.
Missing-marker repair uses `data/titles/1A96545B/saves` for this host and retains
`data/saves` for older hosts. Existing markers and blobs remain unchanged.
