# Azimuth v1.0.1 / v1.0.2 / v1.0.3 / v1.0.4 adapter — Extra Options v0.4.4

Each host has its own exact fingerprint and startup addresses:

| Host | File bytes | PE timestamp | Image size | Timer return | Mapper | Call | Padding |
| --- | ---: | --- | --- | --- | --- | --- | --- |
| 1.0.1 | 9,692,672 | 0x6adff280 | 0x9a6000 | 0x9e950 | 0x400e0 | 0xa027b | 0x91170 |
| 1.0.2 | 9,715,712 | 0x6ae05dd1 | 0x9af000 | 0x9dbc0 | 0x3f2e0 | 0x9f4eb | 0x903e0 |
| 1.0.3 | 9,887,232 | 0x6ae0f17a | 0x9d8000 | 0xa5150 | 0x3fd50 | 0xa697b | 0xa0454 |
| 1.0.4 | 8,613,376 | 0x6ae393eb | 0x8a2000 | 0xa4e09 | 0x3f2c0 | 0xa684b | 0xa1384 |

SHA-256, v1.0.1:
`6e9a2290971500a133699999b3024eec7be93cf5d4116d18d54b8372d11172f7`.
SHA-256, v1.0.2:
`42527964f48c90a537f2414200b8598ab9a6f92919bfbe046dbecccb36e7c597`.
SHA-256, v1.0.3:
`ebfd5d43a165dcec5d4007b9d41ed5b2ed122fcd02ecfef7e4ed35b40826821f`.
SHA-256, v1.0.4:
`56d2c9cb6ccf3ae1836dee9676b64285f3a247efa2d12cf02c6c742696b3b092`.
Header selection is followed by one full-file hash check. No alternate-build
bypass is exposed. Unsupported or mismatched host files are left unpatched.

Outside DllMain, the native timeBeginPeriod call arms the listed game-mapping
call using twelve bytes of inspected CC padding. Both edits have exact
original/installed guards and transactional protection/rollback. The handoff
restores both edits before calling the original four-argument mapper.
The mapper continues to own host patching, TLS setup, imports and protections.
There is no LoadLibraryExW interception or replacement TLS helper in this version.

After the native mapper returns, the adapter reads its returned context and game
base. The bound package's parsed XboxLive TitleId (std::wstring at +0x28) must
be exactly 1A96545B for this FH2-only release, accepting hex case differences.
FM5 is inactive, including its settings, code and save paths. The source can
opt into FM5 development with `--fm5`; that flag is not used for this release.
The resolved executable path (std::wstring at +0xc0) supplies the actual filename. Launcher INI
selection is not used: command-line game selection can differ from that value.
The mapped guest must also pass AMD64/PE/header, supported timestamp/image-size,
readability, game-file and per-patch byte checks before initialization succeeds.

Other parsed title IDs return INACTIVE after the native map, with no settings
load, addon/plugin discovery or game/save patching. Normal multimedia forwarding
is retained in other host processes. Successful initialization records the
host version/title; the generic log heading contains only mod name and version.

Native fixtures cover call/thunk execution with a mapper stand-in, argument
preservation, actual-package title/path handling, altered host files/headers,
code ownership, inaccessible guest memory and protection-failure rollback.
They do not run Azimuth's actual mapper/TLS, launcher UI, rendering or gameplay.
The production proxy separately passes all named/ordinal forwarding checks.

v1.0.2 changes the addresses but the mapper, package parser and blob-validator
instruction shapes match v1.0.1 after relocation normalization. Both profiles
pass their own private native fixtures. The initial v1.0.0 investigation was
superseded by the installed v1.0.1.
Older adapters and their addresses remain documented only under `history/`.

The v1.0.3 handoff, startup/timer and save-reader instruction shapes match
v1.0.2 after normalizing relocated branches and RIP references. The mapper has
one internal host member-offset change (e0 to d8); its native implementation
is retained. The four mapper arguments and package title/path layout are kept.

The v1.0.4 mapper (228 instructions), handoff (775 instructions) and guarded
save-reader region (210 instructions) match v1.0.3 after normalizing native call,
branch and data relocations. The loader and startup timer call were relocated;
package title/path layouts are unchanged. Its save guard is now 4DE82B, with
separate original/patched SHA-256 guards over the full 3C1-byte region.
