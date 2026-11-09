> Historical experiment. Removed entirely from Extra Options v0.3.0.

# Hash asset overrides — Extra Options 0.2.221

0.2.221 fixes the black replacement texture: the packed native quality-group
field now retains its mip count while using byte offset zero. The same test
texture and assets.json can stay in place; only replace winmm.dll.

Place assets.json directly inside an enabled mod folder. Put the replacement
files in that mod's assets folder. No mod.json entry, database entry, addon-id,
ZIP, minizip name, numeric PB entry, or matching replacement GUID is required.
The existing loadorder.txt and dependency rules apply. Later mods win.

Example:

    mods/Texture Test/
      assets.json
      assets/
        road.swatchbin

assets.json:

    {
      "format_version": 1,
      "assets": {
        "4082296683": "road.swatchbin"
      }
    }

The key can instead be "0xF352E76B". Both spellings mean the same unsigned
32-bit ID. Use ONE spelling per mapping. File paths are relative to assets/;
subfolders are allowed. Absolute paths and parent-directory traversal are
rejected. An assets-only mod folder is supported. Keep your INI/settings DAT.
Restart the game after installing or changing mappings.

World textures
--------------
Use the texture's SourceHash from AssetManifest.xml in tracks/anthem/bin.zip.
4082296683 is road_tarmac_medium_diff_4dca83a4-7f2a-4c6d-8ccd-17ec3d3ecf27.
It is texture index 1970 in the original track. The native PVS texture descriptor
contains that same hash. The loader resolves it before the native quality/chunk
selection and queues the replacement as a normal asynchronous loose file.
Native surface creation, binding, cancellation and unloading remain in charge.

Supply a COMPLETE tiled swatchbin, including the full mip chain. Currently it
must match the original dimensions, depth, texture format/tiling and total mip
allocation. Missing files and incompatible streamed swatches are logged and
retain the original. A whole overridden texture loads as one native quality
group; it does not stream separate mip fragments. Thus it can use more resident
memory at a distance than the original streamed version.

The old GeoChunk9/3999.pb is only the 512/256 mip fragment of this 2048 texture.
Simply renaming it to .swatchbin is insufficient. The old minizip index/GUID
upload experiments and tracing were removed from the DLL.

Other asset types
-----------------
The shared resolver also redirects hash-named native file requests such as
_0x12345678.materialbin when their extension matches the replacement. It opens
those files through the native folder/async file system too. This is NOT a
universal override for every asset referenced by a numeric value: packed model,
shader and material indices are not automatically texture SourceHashes. Direct
bulk loaders for other streamed asset types still need their own logical lookup
integration. No support for those untraced paths is claimed by this build.

Logging / validation
--------------------
Search FH2_ExtraOptions.log for ASSETS. Registration names the normalized hash
and load-order index; streamed texture use/rejection is reported once per
catalog generation. Invalid JSON disables its containing mod, consistent with
other addon JSONs. Duplicate IDs within one JSON are rejected; conflicts
between mods use later-mod priority. Files are indexed at startup/media revision;
no per-frame disk scans or full-payload cache is introduced.

The DLL passed private native-image checks (including the real resolver,
bundle parser and TrackTexture update/queue construction) and existing media/
addon ZIP regression tests. Live rendering remains unverified until tested in
the game; no installed files or running game memory were changed here.

Optional visual test
--------------------
texture-hash-test.zip contains a complete magenta BC7 texture for this hash.
Extract it beside Azimuth.exe; it adds assets.json and assets/road_test.swatchbin
to mods/Texture Test. Existing media/mod.json files can stay. Ensure Texture Test
is enabled in loadorder.txt. All mips are magenta, so the affected surface should
be obvious at any distance. This is a test texture, not your finished road art.
To stop the test, remove its assets.json or disable the mod and restart.
