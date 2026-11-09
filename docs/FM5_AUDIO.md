# FM5 channel volume controls

The Audio page adds fourteen native mixer-group faders immediately after Master Game Volume. Values run from 0 (silence) to 100 (original authored mix). The existing master volume and music switches remain native. The list uses the native master slider template and a bounded, focus-scrolling viewport.

Controls: Music, Voiceover, Car, Tyres, Collisions, Cockpit sounds, Track ambience, Crowd, Garage ambience, World objects, Environment reverb, Wind, Interface sounds and ForzaVista effects.

Groups follow the authored hierarchy: Track ambience also affects Crowd. Environment reverb changes the return/reverberation layer, not the direct sounds. Car covers the car mixer group rather than promising a player/AI split. Audio settings use the native page Accept/Cancel/Reset callbacks and the shared FM5_ExtraOptions.dat version 4. Version 3 files are accepted with channel defaults of 100; existing camera, video and player selections are preserved.

## Native integration

Verified FM5 image only. MasterBank.bank.strings STDT contains a prefix trie: version byte, node/string/GUID counts, 8-byte nodes, string pool, GUID array. Nodes contain string offset and packed 24-bit child/GUID index plus 8-bit child count. The fourteen group identities are recorded in fm5_audio_channels.h.

The FMOD playback bus holds its native description at +0xB0. Its identity method at slot 3 (+0x18) returns the bus's own GUID; slot 2 (+0x10) is the parent routing GUID and must not be used for identity. This distinction was verified using the live mixer capture and native descriptor vtables. Live GBUS descriptions use vtable 10B8B98: the parent getter reads +0x198 and the identity getter reads +0x1A8. The generic native identity copy E2D5F0 also calls slot 3.

The four native final bus-gain submissions at DBA4B5, DBA6D7, DBD6B5 and DBD7E5 call CBD0E0. All retain the bus in RSI. A verified padding thunk passes it as the third argument, applies the selected multiplier after authored gain/automation, and calls the original submission on its original mixer thread. Nothing holds bus or channel pointers beyond a call. No FMOD calls run from slider handlers, and the audio path performs no allocation, file access, logging or memory scanning. Defaults skip identity queries until a channel is changed. A sticky activation flag also permits restoring 100 after Cancel or Reset.

The regular studio update's DD7CD0 call at DD9FD0 runs normally, then refreshes matching groups through native DBA3E0 when a slider revision changes. It finds existing global groups through native DBB670(system+0xB8, GUID, -1), the same cache lookup used by native DD80A0. It never creates absent groups or retains their handles. The old +0x338 traversal was incorrect: the live capture showed that this temporary format-change work queue is empty during steady playback. Unchanged updates perform no extra cache lookups; slider edits, Cancel and Reset trigger fourteen bounded lookups. Native errors are preserved, and original gain is recomputed from authored factors, so attenuation never compounds. Newly created groups receive attenuation through their normal gain submissions.

Audio property registry: native initializer 2CDA90, list head 245EE30; float descriptor factory 2CCBE0. Native page initialization at 2237218 snapshots channel values; accept/cancel/reset functor invokers preserve their callback-object ABI.

## Validation

Development test test_fm5_audio.py verifies all patch originals, transformed XML and ordering, native property ABI, every group at 0/25/75/100, the actual native DBA3E0 calculation and installed RSI-passing thunk while changing a steady group, wrong parent-GUID rejection, real captured GBUS identity interface and native cache lookup with an empty format-change queue, alternate returned GUID addresses, unchanged-update fast path, unrelated-group pass-through, default fast path, callback routing, persistence and migration. These are private-image tests and do not substitute for listening in-game.

The older monolithic test_fm5.py startup whitelist predates the always-on Video/Photo/Driver extensions and fails its all-disabled image-equality assertion with those extensions present. The camera-binding integration test initializes the full current startup transaction successfully. Its failure is not counted as a passing full regression.
