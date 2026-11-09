# Azimuth v0.9.10 — Extra Options 0.2.79

Only the inspected 3,593,728-byte executable is accepted:
`78a5f635b3241918707b5acddeaa96bd59ccb3819490e9218ee152497ca73639`.
PE timestamp 0x6ad14833; image size 0x3d3000. Older headers and any changed file
are refused before loader-import or game patches. A version warning names 0.9.10.

The native timeBeginPeriod return is 0x78e90. LoadLibraryExW IAT is 0x2853c0;
the TLS-only function is 0xcba60 with its original prologue. Its call at 0xcbb06
returns to 0xcbb0c and retains the image context in RDI. The proxy restores the
IAT before initializing game patches. The original helper exports and TLS setup
are unchanged. The loader fixture executes this actual function with a private
mapped game and the unmodified helper, then verifies TLS index/thread, modified
guest bytes and restored import/page permissions.

Save comparison guards are now 0x182c49/0x51 and 0x183610/0x3e5. The optional
patch skips revision size (+0x4b), blob size (+0x13) and hash content (+0x378)
mismatch rejections. Recorded string lengths, marker syntax and normal writing
remain active. Both original/patched regions have complete SHA-256 guards.
The native comparison fixture exercises accepted/rejected hashes, sizes,
revisions, repeated application and protection-failure rollback. Missing-marker
repair uses data/titles/1A96545B/saves, as in 0.9.9.

The fragment-mask module and startup call were removed. No renderer compatibility
code is included in the new adapter. The obsolete fixture/notes are under
history/fragment-mask. GPU gameplay and Wine are not executed by these tests.

The new Motion Blur High/Ultra tables also write the game's photo-mode grid
density to 587/1170 before the TLS handoff. This caused the initially reported
photo_mode_sample_density refusal. The mod now recognizes these exact values;
Default restores the original layer choice and numeric choices override it.
Both photo sampling and Real-time Shadows remain available as overrides.
See PHOTO_SAMPLING_NOTES.md and SUN_SHADOW_NOTES.md for the guarded defaults.

The unmodified 4,608-byte TLS helper used by the current native fixture has
SHA-256 `d92ff5c173ec42c2f371d8d28a30c5535489f7f0dffa43363860cf1fe70ee695`.
It matches the installed helper after testing; the previous 0.9.9 helper hash
in historical notes does not identify this 0.9.10 file.
