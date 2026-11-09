# Local gamerpic - 0.2.18

All addresses below are RVAs in the supported FH2 executable. The new option
is Profile.CustomGamerpic, default 1. Only the converter's relocated vtable
pointer is changed; no new fixed executable patch range is added.

## Original picture route

GamerPicConverter's Convert slot at 0x2da6a48 points to 0xb9a0d0. It takes
the ordinary six converter arguments and returns an owned Ptr<ImageSource>
through initially uninitialized result storage.

Its input is UserProfileDataSource (vtable 0x2daf548), with a retained
CUserProfile at offset 0x78 (vtable 0x2eb1288). The local profile retains its
Windows.Xbox.System.User at offset 0x50. Remote/Drivatar cache profiles do not
have that local User pointer. The override checks those layouts with guarded
reads and passes unrelated/remote/null inputs to the unchanged converter.

The original converter reaches GamerPicCacheModule through 0x556640. Its
pending fetch route includes 0x55b210, 0x54e9d0 and 0x555ed0, eventually
calling Windows.Xbox.System.User.GetGamerPicAsync. The callbacks at 0x543d30
and 0x5597d0 extract the returned encoded bytes and call WriteableBitmap's
image submission at 0x16eb510 with resource priority 0x60000.

Azimuth 0.9.5's picture operation entries at 0x23210/0x23270 create
Azimuth.OfflinePicture failures with HRESULT 0x800704cf. The override avoids
that operation altogether. It leaves both translation layers' services intact.

## Decoding and native texture submission

The executable's WIC pixel-format table at 0x2e820e0 contains empty GUIDs,
as does the default format GUID at 0x1f9d108. The original memory decoder
at 0x112e4a0 cannot match an ordinary OS decoder's real PNG pixel format.
The local override therefore does not feed an encoded PNG or an assumed DDS
format into that route and does not replace the global game's WIC factory.

On the first ready local-player request, the DLL loads the system
WindowsCodecs.dll, calls WICCreateImagingFactory_Proxy, decodes frame zero,
converts it to 32-bit RGBA and copies its pixels. COM objects and the DLL
handle are released before returning. This work runs outside DllMain.
The Windows codec and native raw texture path were exercised in the tests;
Linux/Wine/Proton codec availability has not been tested.

CreateBitmap at 0x535000 constructs the stock WriteableBitmap. Wrap at
0x535180 returns the stock UI image proxy, retaining the bitmap at offset
0xb8. AllocateBuffer at 0x165e600 creates an AVUI pixel buffer; its offset
0x28 is the storage pointer and offset 0x30 is its size. The bitmap owns that
buffer through its existing offset 0x80 member for the rest of the session.

The texture request at 0x1105510 receives that buffer, own=0, and an options
record with empty name, priority 0x60000, format=3, width and height. Its
native source-kind-2 dispatch at 0x1104c39 selects the raw image method
0x10c6f00 when format is nonzero. Format 3 maps to DXGI_FORMAT_R8G8B8A8_UNORM.
That method computes the row/slice pitch and creates a 2D texture and shader
resource view. Bitmap update at 0x16e2dc0 retains the stock asynchronous
rendering/update behavior. A one-row PNG repeats the row once, because the
native loader otherwise creates a 1D texture for height=1.

The cached UI image has one owned reference for the module's lifetime. Each
converter result receives its own additional reference. The bitmap retains
its pixels while the asynchronous renderer consumes them. The mod is pinned
before installing this callback, as with the earlier runtime callbacks.

## File and fallback behavior

Startup resolves Gamerpic.png from the adapter's settings directory.
Mezcal uses the game Mount folder; Azimuth uses the loaded helper DLL's folder,
normally data/bin. Configuration and logging use that same directory.

The file is loaded once per session after the UI manager, texture manager,
job executor and graphics device exist. Before readiness, the original
converter runs without consuming the load attempt. Missing/unreadable files,
failed decoding or allocation failures leave the original behavior in place.
File validation checks the signature, chunk structure/types/order, CRCs,
dimensions, legal bit-depth/color combinations and zlib header. The size limit
is 16 MiB and each dimension is limited to 2048. Common PNG modes and alpha
are tested. No account data or save blobs are modified.

## Tests and remaining limit

tests/test_gamerpic.py runs real OS PNG decoding, the game's native bitmap/buffer/
wrapper allocation, raw-image dispatch at 0x1104a60 and pixel upload preparation
at 0x10c6f00 in a relocated private game image. The GPU endpoint and renderer
queues are mocked, recording the descriptor, format, pitches and actual bytes.
Tests include 512-square RGBA, RGB, grayscale, palette and partial alpha;
missing/corrupt/oversized images; readiness; disabled restoration; local/remote
routing; argument forwarding; references; Mount and data/bin paths; and unwind data.
The native Azimuth helper-loader test additionally inspects the release DLL's
initialized gamerpic path and verifies that conflicting Mount settings are ignored.

These tests verify the handoff and native texture construction, not an actual
GPU draw or translation-layer UI. The final image appearance and every menu
using it still require in-game testing. No sample image is installed for the user.


## Previous path in 0.2.14 through 0.2.17

These versions resolved Gamerpic.png from the guest game directory supplied
to initialize, alongside FH2_ExtraOptions.ini. Mezcal derived that directory
from the main FH2 executable; Azimuth resolved its launcher game_folder to
Mount. Both adapters read Mount/Gamerpic.png while the Azimuth helper stayed
in data/bin. Version 0.2.18 changes the Azimuth location as described above.

## Wine report and read diagnostics — 0.2.66

The reported unreadable/invalid message followed a successful CreateFileW and
preceded image_decode_png. It covered GetFileSizeEx, allocation, ReadFile and the
PNG container validator, so it does not establish a path lookup or WIC failure.
Valid dimensions/file size alone do not establish intact PNG chunks and CRCs.

read_picture_file consumes the full bounded input, preserving the actual Win32
error and byte count on failure. A successful short transfer is followed by
another read; zero progress becomes EOF. Diagnostics separate each stage and
include PNG signature/checksum/compression/container reasons.
This follows the [ReadFile API contract](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile),
which reports actual bytes transferred, and preserves the
[PNG container rules](https://www.w3.org/TR/png/).

Private fixtures simulate seven-byte transfers, read failure and early EOF while
retaining real filesystem/WIC/native image operations. The normal 512-square,
RGB, grayscale, indexed and partial-alpha images still decode correctly. WSL
access was denied on this host and no Wine runtime was available, so these tests
cannot confirm the reported Linux issue. A new GAMERPIC log from the affected
Wine installation is needed to identify its exact failing stage and reason.
