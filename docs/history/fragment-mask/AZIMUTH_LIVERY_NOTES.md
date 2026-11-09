# Azimuth stock-livery crash — 0.2.24

## Captured cause

The user reproduced this on Azimuth 0.9.6 with wrapped cars, including with the
original TLS helper and with a fresh graphics cache. Unwrapped cars load.
The diagnostic adapter forwards D3D12 calls unchanged and records descriptors.

The final operation before `GetDeviceRemovedReason` returned `0x887a0001` was
`CreateShaderResourceView`, caller host RVA `0x1021dd`. It requested an R8_UINT
ordinary Texture2D view of a 654x300, RGBA8_UNORM_SRGB, two-sample render target.
The preceding normal Texture2DMS SRV and RTV matched that same resource.

This is an Xbox FMASK metadata view, not a color-texture alias. Azimuth's helper
at RVA `0x100b70` recognizes the integer views of four/eight-sample textures
and creates a separate immutable mask texture. It omits two samples. The
unrecognized view falls through and is passed to D3D12 against the color target,
with incompatible format and sample dimension.

The previous MSAA=4 diagnostic test also exercised two samples: existing mod
semantics apply the IntermediateMSAA patch only when Enable4K=1. Enable4K=0
restores all stock livery code regardless of that key.

## Correction

- At host RVA `0x100bbb`, replace `cmp ebx,4 / jne check_eight` with
  `test bl,6 / je check_eight`. Legal texture sample counts are powers of two;
  this accepts 2 and 4 for the existing R8_UINT mask path while preserving the
  separate 8-sample R32_UINT case. One-sample and other counts remain rejected.
- At host RVA `0x100c2c`, select fragment-index width as `samples >> 1`, giving
  1, 2 and 4 bits for 2, 4 and 8 samples. Preserve the existing loop registers
  and native sample remapping. Remaining bytes are NOPs; no code is relocated.
- The fully expanded two-sample mask is byte `0x02` (indices 0 and 1). Existing
  four/eight-sample masks remain `0xe4` and `0x76543210`. These match the expanded
  FMASK values in [Mesa's AMD driver](https://fuchsia.googlesource.com/third_party/mesa/+/refs/tags/gitlab/mesa-22.3.6/src/gallium/drivers/radeonsi/si_compute_blit.c).

The native host allocates, uploads and owns the mask texture exactly as before.
Livery texture dimensions and render sample counts are not changed. Main-pass
MSAA remapping continues through the native `0xaa420` helper.

## Build guards

Inspected host SHA-256:
`7e8449fb50a181c24d5d9f75366aafbf4cede645ab9e90db20486703802fceaf`.
PE timestamp `0x6aca9dc0`; image size `0x349000`; x64 non-DLL image.

Hash region: RVA `0x100b70`, length `0x2d2`.
Original SHA-256:
`25c8a4c490b962917ded6ce25fbce7f2925caace8677948e8a4cf35b0e5c6e63`.
Patched SHA-256:
`f79b2e1c76b73dcb140176dd7e3eedcce12e702006785cb9854f927b28e90e02`.

Both edits share one page. Validate before writing, retain the original bytes,
flush the instruction cache, restore page protection, and roll back failures.
Repeated application accepts the exact corrected hash without another write.
Unknown builds/code are skipped. The global mod switch disables this correction.
Azimuth 0.9.5 retains normal adapter support but is not patched by this fix.

## Native verification

`development/tests/test_azimuth_livery.py` maps the user's original host locally and executes
the actual mask-helper machine code. Only allocation/upload/CRT boundaries are
mocked; its sample-count tests, descriptor recognition, mask calculation,
sample remapping and texture description execute natively. The original helper
rejects the exact two-sample view, and the corrected helper produces a separate
single-sample R8_UINT texture. Four/eight-sample output is compared byte-for-byte
with the original. Invalid view layouts, one-sample textures, texture arrays,
unknown helper bytes and changed headers are refused; RX protection is restored.

This test establishes the native mask path and ABI. The user confirmed that
wrapped cars load with stock livery settings using 0.2.24. In 0.2.25 the same
edits use the shared guarded writer in `azimuth_host.c`; the native test is
rerun to verify identical behavior.

## Azimuth 0.9.7 update — 0.2.30

The inspected 0.9.7 host retains the same missing 2x fragment-mask case.
Its helper moved to RVA `0x104320` (0x2d2 bytes); the recognition edit is at
`0x10436b` and fragment-bit selection at `0x1043dc`. Each build has separate
full-region SHA-256 guards in `adapters/azimuth_livery.c`. The shared host
writer selects 0.9.6 or 0.9.7 before verifying the relevant code.
Native helper tests reproduce the omission and check 2x, 4x and 8x mask
output against both hosts. The 4x/8x output remains identical to the native
baseline. The 0.9.7 identity and TLS contract are documented in
`AZIMUTH_SAVE_NOTES.md`.

## Azimuth 0.9.8 update — 0.2.62

The same omission remains in the inspected 0.9.8 host. Its helper is now
`0x1046c0:0x2d2`, with recognition at `0x10470b` and fragment-bit selection
at `0x10477c`. This build has separate original/patched region hashes and uses
the same two edits. The native fixture executes the current helper's actual
instructions, supplies its relocated dependencies and checks all 12 mask
cases, unchanged 4x/8x paths, code/header guards and protection rollback.
The host identity is recorded in `AZIMUTH_SAVE_NOTES.md`.

## Azimuth 0.9.9 — 0.2.64

The omitted two-sample case remains in the helper at RVA 0x13dc40. Its guarded
span is now 0x2ea bytes; the recognition and fragment-width edits retain their
relative offsets 0x4b and 0xbc. Region SHA-256 values:

- Original: `eca7bee6c480c3e8ab135bc3f63a4776abaa431c7b44ae995b918a918219f84b`.
- Patched: `7e23143f8f6abdeb9c575272e13ae6fdf3806ecac30eee0863a6c70994e95fa3`.

Native fixtures execute this new helper, with the renderer allocation boundary
supplied privately. They check two-sample fragment output, unchanged four/eight
sample output and both code-hash guards. No game GPU rendering runs in them.
