# FH2 local design catalogue order

My Designs groups designs for the currently selected car first, preserving
the native folder order within both groups. Save/Overwrite mode keeps the
synthetic New Design tile first. Folder names do not determine car identity.
No files, thumbnails, design payloads, or saved associations are changed.

The port follows the working FM5 lifecycle with FH2's verified native layout:

- `PopupLoadSaveLivery` vtable `0x2e28478`: initialize `+0x598`, update
  `+0x4c8`, collection ready `+0x648`, deleting destructor `+0`.
- `SavedFileLivery` vtable `0x2e28398`: completion notification `+0xa0`.
  Native `0xdfcb30` sets `item + 0xac` on success, applies the header through
  `+0xa8`, then notifies through `+0xa0` on both success and failure.
- Header metadata is at `item + 0xf0`; its `+0xa4` field is Data_Car.Id.
- Selected UI car service `0xced1c0`, virtual `+0x100`, then choices virtual
  `+0x188` returns CarUniqueId. Its first integer is the model ordinal,
  as used by native `0xbab540` for its Data_Car.Id query. Choices `+0x260`
  is an owned garage-entry ID and must not be used for header matching.
- Collection owner `page + 0x490`, storage `owner + 0x50`, count
  `storage + 0x30`, pointer array `storage + 0x28` then `array + 0x28`.
- Save mode compares `page + 0x588` and `page + 0x590`. The remembered
  selection is at `page + 0x6a8`.

The existing native asynchronous header requests supply metadata. Ordering
and binding wait for completion and run on the UI update, not inside the
completion notification. Failed headers remain with the other designs;
a five-second fail-open guard prevents a missing callback blocking the page.
There is no scanning or sorting per frame once the list is bound. Destruction
cancels pending work. Vinyl-group catalogues retain their native behavior.

`source/development/tests/test_fh2_designs.py` checks the native vtable bytes
and CarUniqueId getter in a private executable mapping, stable partitioning,
asynchronous and failed headers, synthetic tiles, selection preservation,
payload associations, and close cancellation. This is not an in-game test.
