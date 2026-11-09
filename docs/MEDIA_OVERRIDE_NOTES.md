# Loose asset overrides — 0.2.40

Inspected executable: `forza_durango_release_final.exe`, SHA-256
`3ec04c6163fb5614e3d351ba0570d39b846041a257715d6340eceb03ccb7f0a3`.
Addresses below are RVAs; the preferred image base is `0x140000000`.

## Retained code

The executable contains `mediaoverrides` at `0x1ffab50`. The startup parser
at `0xf6dd60` stores its argument in the native string at game-options offset
`0x2b40`. Its length is at `0x2b50` and capacity at `0x2b58`.

The archive initializer at `0xfd1600` tests that string's length at `0xfd1671`.
A nonempty value selects ZIP rediscovery, as does `findzips`, instead of the
ordinary ZIP-manifest path. No consumer that mounts its supplied path was
found. Neither `mediaoverride` nor `mediapc` appears as a folder name in this
build. Merely enabling that startup setting would not establish loose-file
override behavior.

FH2's IOSys filesystem already resolves overlapping disk and archive mounts
by priority. Original loose media uses the root disk volume; ZIP members
appear beneath virtual folder prefixes such as `game:\media\Camera\`.

| Native component | RVA / field |
|---|---|
| `CFileSys` vtable | `0x2e6ed18` |
| Resolver, vtable `+0x210` | `0x11f49b0`; pointer at `0x2e6ef28` |
| Register mount, vtable `+0x1e8` | `0x11f82b0` |
| Sort mounts, vtable `+0x1f8` | `0x12039d0` |
| Priority comparator | `0x11fb550`, unsigned descending |
| Primary filesystem volume | filesystem `+0x68` |
| Native game allocator | `0x3229c0` |
| Disk-volume constructor | `0x11e05a0`, object size `0x50` |
| Disk-volume vtable | `0x2eab918` |
| Disk-volume root string | volume `+0x28` |
| Disk-file existence path | `0x11f9c70` / `0x11f9d30` |
| Prefix/relative-path construction | `0x11fb470` |

## Implementation

`Files.EnableMediaOverrides=1` installs one resolver vtable callback. On the
first lookup after the primary volume exists, it constructs native disk
volumes rooted at `game:\mediaoverride\` and `game:\mediapc\`. Both mount
at `game:\media\`, at priorities `0x7ffffffe` and `0x7ffffffd` respectively,
above the original assets. Native registration retains a reference; the
callback releases its construction reference and uses the native sort routine.
Hash-manifest filtering is disabled for these folders so newly supplied files
are visible.

Each new folder volume uses a private copy of the native method table,
including its RTTI locator. Only its file-existence method is replaced: paths
with archive selectors or characters invalid in disk filenames are declined
before the native disk check. Ordinary paths still use native disk existence.
The original game volumes and their method tables are unchanged.

The original nine-argument resolver still performs lookup, existence checks,
output ownership and fallback. The hook preserves all arguments and returns
the original result. There is no custom archive reader or file-handle shim.
Mounts are created once per game image. Native mount/sort routines handle their
normal filesystem locks. The callback does no mounting until normal file I/O,
avoiding native allocation and file work during the adapters' `DllMain`.

The game's `game:` device determines the physical root on each layer. Thus
override assets belong beside the game executable, normally in `Mount`, even
though Azimuth's settings are beside `azimuth-tls.dll` elsewhere.

For `media/Camera.zip` member `CameraGroups.xml`, place the replacement at
`mediaoverride/Camera/CameraGroups.xml`. For a nested member, preserve its full
relative path. `mediapc` uses the same layout but has lower precedence.
Unchanged members retain their original archive source.

## Verification and limits

`development/tests/test_media_overrides.py` relocates a private copy of the supported image
and executes its real filesystem constructor, disk-volume constructor, mount
registration, sorting, path matching, disk existence and priority resolver.
The fixture supplies a private `game:` root. Game heap/TLS locking and I/O
event reporting are mocked; ZIP existence uses a small temporary ZIP through
the volume interface, rather than running the native ZIP decoder.

Checks cover disabled lookup, folder precedence, ZIP-member and loose-file
fallback, case-insensitive virtual prefixes, missing files, unrelated drives,
repeated lookups without duplicate mounts, all sixteen combinations of the
four ABI flags, conflicting hook refusal and master disable. Production
adapter loading and option combinations are checked separately.

## Azimuth archive-selector crash

The user's 0.2.37 game test reached
`game:\mediaoverride\tracks\Anthem\GeoChunk0\<*>0` during world loading.
Azimuth 0.9.7 reported `Invalid runtime path component` at game RVA `0x11f9f9e`,
the return address of the disk-volume `GetFileAttributesExA` call. The original
disk volume has a hash catalog and normally declines that archive-internal
request without calling the host filesystem. The new folders intentionally
have no catalog, so 0.2.37 attempted a real disk lookup for `<*>0`.

Version 0.2.40 declines these requests only in its two folder volumes. The
priority resolver can then select the original archive volume. The regression
fixture reproduces ten selector/invalid-name requests, including the exact
crash path. Running it against the 0.2.37 test DLL produces twenty invalid
override disk requests; the corrected build produces zero. The fixture also
checks short and long native strings, preserved RTTI and stable native volume
references across repeated lookups. The original catalog's rejection result
is emulated at the host boundary for these virtual requests.

## Remaining limits

The mod does not force asset-cache invalidation or reload already open files.
Restart after changing files or settings. In-game rendering and streaming of
replacement assets have not been tested in this session. Only paths resolved
through this filesystem are covered; direct volume access, archive enumeration
and new DLC-package discovery are outside this option's scope.

No game assets or translation-layer binaries are included in the release.
