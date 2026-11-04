/* Music file decoding: mp3 / flac / wav -> 44.1 kHz stereo S16, with a linear resampler.
   Ported from FH2_SpotifyMin's folder_core.c (GPL-3.0-or-later). */
#include "decoder.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

#define IN_CHUNK 1024
#define MAX_CH   8

enum { K_MP3 = 1, K_FLAC, K_WAV };

struct FolderTrack {
    int kind;
    drmp3 mp3;
    drflac *flac;
    drwav wav;
    unsigned channels, rate;
    double step, pos; /* linear resampler: input frames advanced per output frame */
    Frame prev;
    int primed;
    int16_t in[IN_CHUNK * MAX_CH];
};

static int ends_with_nocase(const wchar_t *p, const char *ext) {
    size_t n = wcslen(p), e = strlen(ext);
    if (n < e)
        return 0;
    for (size_t i = 0; i < e; ++i) {
        unsigned c = (unsigned)p[n - e + i];
        if (c >= 'A' && c <= 'Z')
            c += 32;
        if (c != (unsigned char)ext[i])
            return 0;
    }
    return 1;
}

FolderTrack *ft_open(const wchar_t *path) {
    FolderTrack *t = calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    if (ends_with_nocase(path, ".mp3")) {
        if (!drmp3_init_file_w(&t->mp3, path, NULL))
            goto fail;
        t->kind = K_MP3;
        t->channels = t->mp3.channels;
        t->rate = t->mp3.sampleRate;
    } else if (ends_with_nocase(path, ".flac")) {
        t->flac = drflac_open_file_w(path, NULL);
        if (!t->flac)
            goto fail;
        t->kind = K_FLAC;
        t->channels = t->flac->channels;
        t->rate = t->flac->sampleRate;
    } else if (ends_with_nocase(path, ".wav")) {
        if (!drwav_init_file_w(&t->wav, path, NULL))
            goto fail;
        t->kind = K_WAV;
        t->channels = t->wav.channels;
        t->rate = t->wav.sampleRate;
    } else
        goto fail;
    if (t->channels < 1 || t->channels > MAX_CH || t->rate < 6000 || t->rate > 192000) {
        ft_close(t);
        return NULL;
    }
    t->step = (double)t->rate / 44100.0;
    return t;
fail:
    free(t);
    return NULL;
}

void ft_close(FolderTrack *t) {
    if (!t)
        return;
    if (t->kind == K_MP3)
        drmp3_uninit(&t->mp3);
    else if (t->kind == K_FLAC)
        drflac_close(t->flac);
    else if (t->kind == K_WAV)
        drwav_uninit(&t->wav);
    free(t);
}

static short lerp16(short a, short b, double x) {
    double v = (double)a + ((double)b - (double)a) * x;
    return (short)(v < -32768.0 ? -32768.0 : (v > 32767.0 ? 32767.0 : v));
}

size_t ft_read(FolderTrack *t, Frame *out) {
    uint64_t got = 0;
    size_t n = 0;
    if (t->kind == K_MP3)
        got = drmp3_read_pcm_frames_s16(&t->mp3, IN_CHUNK, t->in);
    else if (t->kind == K_FLAC)
        got = drflac_read_pcm_frames_s16(t->flac, IN_CHUNK, t->in);
    else if (t->kind == K_WAV)
        got = drwav_read_pcm_frames_s16(&t->wav, IN_CHUNK, t->in);
    for (uint64_t i = 0; i < got; ++i) {
        const int16_t *s = t->in + i * t->channels;
        Frame f;
        f.l = s[0];
        f.r = t->channels > 1 ? s[1] : s[0];
        if (!t->primed) {
            t->prev = f;
            t->primed = 1;
            t->pos = 0.0;
            continue;
        }
        while (t->pos < 1.0 && n < FT_MAX_OUT) {
            out[n].l = lerp16(t->prev.l, f.l, t->pos);
            out[n].r = lerp16(t->prev.r, f.r, t->pos);
            ++n;
            t->pos += t->step;
        }
        t->pos -= 1.0;
        t->prev = f;
    }
    return n;
}
