#ifndef FH2_VOICE_SUBTITLES_H
#define FH2_VOICE_SUBTITLES_H
#include "extra_options.h"
/* Subtitles for spoken lines the game shows none for (Ben, Ashley, ANNA). The
   speech parameter constructors and SubtitleDataSource::Update are hooked by the
   machine code in voice_subtitles_code.h (source: tools/voice_subtitles); a voice
   event whose last path component is an id of the VoiceSubtitles.str table shows
   that text, for <id>~t seconds (the sample length) plus a margin. The display
   time follows the system clock, so it does not depend on the frame rate.

   The table is read once from <stringtables>\<archive>\VoiceSubtitles.str (loose)
   or from <stringtables>\<archive>.zip (a stored entry). Returns the number of
   lines installed; 0 leaves the game untouched. */
unsigned voice_subtitles_install(unsigned char *game_image, const wchar_t *stringtables,
                                 const wchar_t *archive);
#endif
