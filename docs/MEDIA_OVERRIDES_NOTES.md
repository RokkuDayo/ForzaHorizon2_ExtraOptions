# Historical minizip experiments (removed in 0.2.220)

Use ASSET_OVERRIDES_NOTES.md for the replacement hash-based loader. The notes
below describe removed attempts and do not describe current support.


## 0.2.215: PGZP/minizip indexed loose texture overrides

Minizip resources bypass central ResolveFile: queued resource loader170ae40
uses the retained archive volume and virtual140 open by numeric index. Added
2ead978 hook (original11ed120), gated by minizip flag140 and loaded header count
138. Prefix48 is game:\media\...\GeoChunkN\. Map index to decimal .pb path,
resolve through existing prioritized native filesystem, and open returned
volume/file natively. File object supplies replacement's own length.

features/minizip_overrides.h is included by media_overrides.c. On first access,
cache numeric .pb filenames from matching loose mediaoverride/mediapc/active-mod
directories in compact bitsets keyed by prefix/count/media revision. No full
archive copy, decompression or per-missing-texture filesystem probe. Ordinary
ZIP, absent/invalid entries/prefixes, missing replacement fall back unchanged.
Candidate file open preserves native read flags and async marker. Cache reset
on test/media image reset; file additions require restart. Does not index .pb
files contained solely inside addon ZIPs or plugin private media directories.

Texture Test was initially GeoChunk6; user corrected9 and independently moved
its folder before testing. Verified actual archive9 entry3999 is zlib swatchbin
327763 bytes; replacement is327764 bytes (same GUID). Test_minizip.py runs
actual native queued loader170ae40, native file constructor, resolver/read and
buffering11df550 on actual user asset. Final host game: folder path bridge and
vanilla fallback are mocked. Ten checks pass. Media/ZIP regressions pass, PE
exports181/importsKernel32+NTDLL and LoadLibrary pass. No installed writes or
live visual test. Package outputs/extra-options-0.2.215-minizip-overrides.zip.
