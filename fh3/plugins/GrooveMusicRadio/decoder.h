/* Music file decoding: mp3 / flac / wav -> 44.1 kHz stereo S16.
   Ported from FH2_SpotifyMin's folder_core (GPL-3.0-or-later). Decoders: dr_mp3, dr_flac, dr_wav
   (David Reid, public domain / MIT-0). */
#ifndef XMR_DECODER_H
#define XMR_DECODER_H
#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    short l, r;
} Frame; /* one stereo sample pair */

#define FT_MAX_OUT 8192 /* the output buffer passed to ft_read must hold at least this many frames */

typedef struct FolderTrack FolderTrack;
FolderTrack *ft_open(const wchar_t *path); /* NULL: unsupported/unreadable file */
size_t ft_read(FolderTrack *t, Frame *out); /* up to FT_MAX_OUT frames at 44100 Hz; 0 = end of track */
void ft_close(FolderTrack *t);

#ifdef __cplusplus
}
#endif
#endif
