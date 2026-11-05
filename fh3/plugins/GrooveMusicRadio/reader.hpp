// The Media Foundation source reader the Groove player gets for our songs. Azimuth's own
// MFCreateSourceReaderFromURL only reaches the game's files, and Windows' Media Foundation doesn't start
// under it, so the plugin hands the player an IMFSourceReader of its own over a PCM source: a decoded
// music file, or a music app's recorded sound.
//
// The player (0x196fb20, 0x1990820) asks GetNativeMediaType for the stream's format (none: it uses
// 44.1 kHz 16-bit stereo PCM), sets that as the current type, then calls ReadSample(any stream) until
// it reports the end, takes each sample's ConvertToContiguousBuffer and copies the Lock()ed bytes.
#pragma once

#include "decoder.h"

#include <windows.h>

#include <cstddef>

// 44.1 kHz stereo S16 frames.
class PcmSource {
public:
    virtual ~PcmSource() = default;
    // Up to `max` frames (max >= FT_MAX_OUT). 0 = the end of the song.
    virtual size_t read(Frame* out, size_t max) = 0;
};

// An IMFSourceReader (as void*, for the game) over `source`, which it owns. Never null.
void* make_source_reader(PcmSource* source);
