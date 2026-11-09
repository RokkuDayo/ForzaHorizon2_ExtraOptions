# Parental checks and save import - added in 0.2.12

## Native parental check paths

CGamerProfile vtable RVA 0x2eb1128 has multiplayer/content status getters at
offsets 0x100/0x108. These call shared tiny getters at 0xa66030/0xa65e60 that
read fields 0x44/0x48. The getters are compiler-shared with unrelated classes,
so those generic getter functions are left intact.

PrivilegeStatusEquals in stateflow (0x8fc3c0) obtains the current profile and
checks these two getters. The reflected enum (0xedc20) defines Allowed=4,
ParentalRestricted=2, NotRetrieved=5, Errored=6, and the other statuses.

The CGamerProfile requests at 0x1249c50 and 0x1249e90 issue WinRT privilege
checks 254 (multiplayer) and 247 (user-created content). Their prompt/retry
wrappers include 0x124c3d0/0x124c3e0/0x124c3f0/0x124c400; binding/reloading
the user in 0x124b5b0 also invokes the central requests. Azimuth's store
classification explicitly describes its check response as Restricted.

Two ten-byte startup memory patches replace the request entries with a leaf
store of Allowed to the matching profile field and a return. This covers
direct calls and all the stock request wrappers. It avoids async checks and
their later overwrites, adds no callback/vtable hook, and preserves the shared
getters and all profile fields outside the selected status. These requests
normally return void. The early return occurs before any stack adjustment;
the original unwind metadata remains valid for the untouched function body.
Byte/hash checks and the original patch transaction still apply to both sites.

tests/test_parental.py executes the original no-new-request path and both patched
requests for all eight statuses and prompt/no-prompt callers, checks canaries,
and restores the original entries. The user confirmed the override works in-game.

## Save evidence

Azimuth 0.9.5 save update at 0x1362a0 checks a 0x10000000 byte quota and genuine
disk capacity. None of those checks are patched. The source save is 52,104,427
bytes, below the 268,435,456-byte quota. Logs instead reject imported User_1
revisions where BinarySaveState is 4856 bytes but the marker records 4 bytes.
Native current revisions' hashes match ordinary SHA-256 of raw blob bytes.

Commit writer 0x12e320 and parser 0x12ec20 use the header
`Azimuth complete save revision v2`, followed by `blobs N`, then one line per
blob: `SHA256 SIZE NAME`. Names are UTF-8: native conversions 0x55e80/0x560f0
use Windows code page 65001. The blob data live in revisions/16-hex-digit-id/
data/, and the commit file is a sibling of data/. Completed revisions are
published only after writing the blobs and marker. The converter preserves
checksums and reports failed writes; it does not suppress validation.

The converter creates fresh revision 0000000000000001 per container and
renames the one User_* container to User_1 for the current Azimuth account 1.
It copies all other names and bytes, including two unusual existing Unicode
blob names and one empty container. It does not rewrite identities or values
inside game blobs. tests/test_save_import.py independently verifies all output
manifests and byte equality. The user subsequently confirmed the 0.2.12 changes
and imported save work in-game. Native save commits were not exercised by the
automated tests. Keep the source Mezcal save and the Azimuth storage backup.

## Save commit generation

The old checksum/file-size bypass has been removed. Both games retain native
Azimuth save validation. `Compatibility.GenerateMissingSaveCommits` generates
missing markers from actual blob data without modifying existing markers or saves.
See [AZIMUTH_SAVE_NOTES.md](AZIMUTH_SAVE_NOTES.md).
