# FM5 local design catalogue order

My Designs displays designs for the currently selected car first. Both groups
retain the native save-folder order. This does not change save files, filenames,
design data, thumbnails, or FH2.

The local `PopupLoadSaveLivery` collection-ready boundary (`vtable + 0x640`)
partitions the existing item pointers before the native ItemList binding.
The selected car comes from FM5's current UI car service (`0x3bfc80`, car record
getter at virtual `+0x100`, followed by its native CarOrdinal getter at virtual
`+0x180`). The record's `+0x250` field is the owned garage-entry ID and must
not be compared to a car model ID. Design ordinals come from the native header metadata
(`item + 0xe0`, metadata `+0xa4`); the header's native completion flag is at
`item + 0xa4`.

Headers load asynchronously. The popup waits for their existing native requests
before binding, without loading additional files or textures. Header completion
only marks pending work; ordering/binding runs on the page update. Failed reads
are terminal and remain among the other designs. A five-second fail-open guard
allows the native catalogue to display even if an unexpected request never
finishes. After binding, there is no list scanning or sorting per frame.
The popup's destruction releases temporary state and cancels pending binding.

Save/Overwrite mode keeps the synthetic New Design slot first and partitions
only the real designs that follow it. Folder names are never parsed or used
to infer a car ID, including for externally downloaded designs.
Vinyl-group catalogues and other game pages are not reordered.

`source/development/tests/test_fm5_designs.py` checks native patch bytes, grouping
and stability, delayed/failed headers, selection remapping, changing cars,
empty/no-match lists, Save/Overwrite ordering and cancellation against a
private mapping of the FM5 executable. It does not claim in-game validation.
