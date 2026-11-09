> Historical experiment. Removed entirely from Extra Options v0.3.0.

# Hash asset overrides

See ASSETS.md for `assets.json`, decimal/hex SourceHash keys, the fixed `assets/`
folder and load-order priority. Streamed world textures are supported as complete
swatchbins through TrackTexture11's native async queue. Other types are supported
when the native filesystem receives a hash-named request of the matching type;
this does not imply all bulk streamed asset formats have a hash override path.

Research anchors: native TrackTexture11 Update 0x1391040, vtable+0x28 at
0x2ec2060; native file queue call at0x139140b -> 0x13d51e0; native texture record
array [manager+fb60,manager+fb68), stride56, hash0/dimensions8/mips14/allocation16/
format20/quality-offsets24. Texture1970's real record from anthem_00.pvs offset
3381852 is validated against reconstructed full swatchbin payload5597184 bytes.
The replacement sets first native quality byte offset to zero and mip count to totalMips, preserving the
complete allocation and using exactly one quality group. Later >0 quality requests
remain at one, zero unload stays zero. Input chunk buffers are suppressed only
for mapped/compatible textures; queue uses a private mounted folder and index-1.
Thread-local queue context is cleared for unmapped/nested updates and restored.
Native jobs retain their callbacks/counters/resource ownership. No GPU hook.

Old minizip index hooks, tracing, chunk-memory adapters and Texture11 upload
interception from215-219 are removed; those experiments are historical.

0.2.221: Corrected the packed quality word: low28 bits byte offset, high4 bits
mip count. 220 incorrectly zeroed both. Native1388bd0(totalMips-summedCounts)
selected firstMip12 instead of0. New native regression reproduces the bad value
and verifies the replacement quality word c0000000 selects mip0.
