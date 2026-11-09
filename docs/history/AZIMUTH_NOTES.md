# Current loader: 0.2.75

The production adapter is now a winmm proxy beside azimuth.exe. The original
TLS helper remains unchanged. The following older TLS-replacement notes are
historical and are not installation instructions for this release.

Azimuth 0.9.9 imports timeBeginPeriod through winmm.dll, absent from its folder.
Its inspected timer call returns at RVA 0x7759f before mapping the selected title.
The proxy forwards to an absolute System32 path and performs startup work outside
DllMain. All named exports and the unnamed ordinal are forwarded with the x64
register and stack arguments preserved; the forwarding stubs have unwind records.

Before a host pointer is changed, verify the full on-disk SHA-256
579ed8d8b0b16cbf7890380cb7b925c972b837631ede367eada55eae4afc39bc,
AMD64 PE/image metadata, the native helper-loader prologue at 0xbe6a0, its
LoadLibraryExW call ending at 0xbe74c, and the unowned IAT slot at 0x26f378.
Unsupported builds emit a log and a MessageBoxW warning; no patch installation
or compatibility routine is called. A future build needs fresh inspection.

An assembly shim preserves the native helper loader's RDI context as the fourth
argument to the import wrapper. It is consumed only when the return address is
exactly the validated helper call site. Call the real LoadLibraryExW first, then
restore the import and its page protection. Obtain the guest base from context[0]
and validate its PE headers before the normal file/site-checked patch transaction.
This keeps Windows-assigned TLS indices and the original helper's attach-thread
check intact. There is no whole-process scan, injected thread, or game disk edit.

The source-only test entry points accept private host mappings and real input
paths, without changing the production fingerprint checks. Native fixtures call
the original helper loader and copy the original helper only into private test
folders. Positive tests do not launch Azimuth or execute the guest entry point.
Production export forwarding is tested separately against Windows' winmm.

---

# Azimuth adapter - 0.2.18

## Inspected build and startup order

Azimuth 0.9.5, 3,044,352-byte azimuth.exe:
SHA-256 `15d3da606a4b525048a900e6b5fb6cbfadec1f5c6e30dee7aa743015de360dbe`.

Azimuth 0.9.6, 3,048,960-byte azimuth.exe, inspected during this release:
SHA-256 `7e8449fb50a181c24d5d9f75366aafbf4cede645ab9e90db20486703802fceaf`.
Its native helper loader moved to RVA 0x905b0. The TLS contract and helper path
remain the same. The updated 4,608-byte helper supplied as azimuth-tls.dll.old
has SHA-256 `0c43c6f2d013e3e5da59012a9fb1fa4618f69c7312e3ddca8209142259427978`.
It still has the 336-byte zero TLS template, 16-byte alignment, no callbacks,
the same two exports and a process-attach GetCurrentThreadId initializer.
The mod already implements these TLS requirements; the native test harness
supplies the inspected addresses for each host hash. TLS setup and mapped-game
discovery in `azimuth.c` do not depend on host RVAs. The separately guarded host
compatibility modules added in 0.2.24/0.2.25 do use inspected host RVAs; see
[AZIMUTH_LIVERY_NOTES.md](fragment-mask/AZIMUTH_LIVERY_NOTES.md) and
[AZIMUTH_SAVE_NOTES.md](AZIMUTH_SAVE_NOTES.md).

The Windows main module is the launcher. It manually maps the supported
FH2 PE image, copies headers/sections and applies relocations in its mapper
at RVA 0x6bcb0. The game constructor at 0x8ef50 also pins the original FH2
whole-file SHA-256. It does not launch FH2 as a separate Windows process.

At RVA 0x225a0, the inspected order is game constructor (call 0x226e4),
Azimuth code adjustments (0x22711 -> 0x8e8a0), native TLS helper setup
(0x2276a -> 0x8f640), import binding (0x227ec -> 0x20260), then section
protection/exception registration (0x22862 -> 0x6b900). The helper setup
calls the real Windows LoadLibraryExW with the absolute data/bin helper
path and flags 0x900. It then uses GetProcAddress for the two data symbols.

The legacy DLL's filename/main-module check cannot initialize in this host,
even if something loads it. Azimuth's game-facing dynamic loader and pinned
XG mapping are also different from the previous layer's optional xg.dll route.
Renaming the existing mod to azimuth-tls.dll cannot work: that DLL lacks the
required TLS directory and data exports.

## Helper contract

The original helper was 4,608 bytes, SHA-256
`200708b1496c689989bac4d84e2b7e853b8107443516b4e87791cf742ba3eedf`.
It has a 0x150-byte, all-zero static TLS template, 16-byte alignment,
no TLS callbacks, zero additional fill, and two data exports:

- azimuth_tls_index: DWORD addressed by the PE TLS directory. Windows assigns
  its actual static TLS index before DllMain; initial sentinel is 0xFFFFFFFF.
- azimuth_tls_attached: DWORD set to GetCurrentThreadId on process attach.

The host validates the game's same-sized zero template and empty callbacks,
loads the helper, checks that Windows assigned its index and that DllMain ran
on the calling thread, then copies the index into the game's TLS index field.
The new helper independently implements this contract. It does not ship,
forward to, patch, or call the original helper binary. Windows continues
allocating separate TLS storage for threads; thread notifications are retained.

## Mod initialization

The replacement helper finds exactly one supported, complete readable image
with VirtualQuery and ReadProcessMemory. It checks the FH2 PE timestamp, type,
image size, and common allocation before handing the image to the same patch
engine. The engine still verifies the supported disk image and every fixed
patch/callback value, including relocated original callbacks, before writes.
An unexpected image or site refuses all startup patches. The helper still
returns success and preserves Azimuth's TLS setup when the mod refuses.

It reads title-1a96545b.folder from data/azimuth.ini beside the host EXE,
falling back to launcher.game_folder for older configurations,
resolves relative paths against that EXE, and verifies the executable in
game_folder/Mount. GetModuleFileNameW(instance) independently provides the
loaded helper DLL's directory for FH2_ExtraOptions.ini, Gamerpic.png and
FH2_ExtraOptions.log, normally data/bin. No fallback to Mount settings occurs.
Missing options still use the previous defaults. The master toggle
disables the mod's memory changes; it does not disable required native TLS.

The adapter uses no host RVAs or host code patches in production. Future
Azimuth versions must retain this helper-loading order/contract; this release
has been checked against 0.9.5 and 0.9.6. Both hosts' eleven inspected startup
code-adjustment spans do not intersect any of the 51 mod fixed patch sites
or the added Treasure Map callback slot.

## Validation and limits

tests/test_azimuth.py verifies Windows TLS registration, zero templates on initial
and newly created threads, storage isolation and safe mapped-image discovery.
tests/test_azimuth_loader.py executes the actual native 0x8f640 (0.9.5) or
0x905b0 (0.9.6) routine from the owner's Azimuth binary. It calls real Windows
loader APIs, loads the release
adapter under an azimuth.exe harness, copies the native index into the game,
and checks all 51 fixed sites and five callback slots in eleven configurations.
Those include defaults, all features off, global disable, independent text/
audio and SSAA, photo camera off, gamerpic off, parental off, the obsolete
ignored tab key, Treasure Map on/off independent of UnlockDLC, and a refused
invalid INI.
Relative/absolute paths and UTF-16/ASCII launcher INIs are exercised.
Every case has a deliberately invalid INI in Mount: the release DLL must ignore
it and read its own folder. The fixture also captures the release module's
data section to verify the initialized Gamerpic.png path without adding a
production test export or running a picture request during DllMain.

This is an isolated loader handoff test, not an entire Azimuth launch or
gameplay test. The launcher UI, graphics preparation/rendering, game entry,
scheduler, audio output and user saves are not exercised. The user confirmed the
adapter, parental changes, imported save, Mount gamerpic and free photo camera
in-game through 0.2.14, and Treasure Map/Extras through 0.2.17. The new Azimuth
file locations have native loader coverage and need a full-game smoke check.

The 0.2.12 parental checks added two guest sites and no host patches.
See ACCOUNT_SAVE_NOTES.md for the new request entries and save evidence.

The gamerpic path is resolved from the loaded helper DLL's directory at startup,
alongside FH2_ExtraOptions.ini and FH2_ExtraOptions.log. Gamerpic.png goes in
data/bin beside azimuth-tls.dll. Only the local-player UI converter
is redirected; no Azimuth code or picture-service implementation is patched.
OS PNG decoding is lazy, outside DllMain. See GAMERPIC_NOTES.md.

## Azimuth 0.9.9 — 0.2.64

The inspected host has timestamp 0x6aceb567, image size 0x3b9000 and SHA-256
`579ed8d8b0b16cbf7890380cb7b925c972b837631ede367eada55eae4afc39bc`.
Its helper SHA-256 is
`ffa229b6c62228bf6126de6f140c6a70326c536524bec00f9304ac6591805bb7`.
The TLS ABI is unchanged. The full helper loader starts at RVA 0xbe6a0;
0xbe6c8 is a split unwind fragment inside it, not a callable entry point.
The production-loader fixture executes the full routine with real Windows
LoadLibraryExW/GetProcAddress/TLS registration.

Launcher configuration moved to `[title-1a96545b] folder`. The adapter prefers
that key over `[launcher] game_folder`, including when both are present.
Relative/absolute paths and ASCII/UTF-16 launcher INIs are covered. The real
installed INI resolves read-only to the supported FH2 executable. Saves moved
to `data/titles/1A96545B/saves`; marker repair follows that directory on 0.9.9.
The separately guarded livery/save patches have new inspected host regions.
