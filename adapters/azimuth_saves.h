#ifndef FH2_AZIMUTH_SAVES_H
#define FH2_AZIMUTH_SAVES_H
#include "extra_options.h"

/* "Saves in Documents" (FH2_ExtraOptions.ini [Saves] InDocuments, default 0;
   no in-game row, it applies at the next start). Called from DllMain, before
   Azimuth runs: with the option on, <Documents>\My Games\Forza Horizon
   2\Saves\1A96545B is filled once when it holds no files (from the game-folder
   saves <launcher>\data\titles\1A96545B\saves), then Azimuth's KERNEL32 file imports
   are patched so every path in or under the game-folder saves folder goes to
   the Documents folder. Off: no hooks and no copy. */
void saves_redirect_attach(const wchar_t *mod_directory);
/* The mod's log was (re)started: write the lines kept since DllMain. */
void saves_redirect_log_ready(void);
/* Rewrites a PATH_CAPACITY path in place when redirection is on and it is the
   game-folder saves folder or below it. 1 = rewritten. */
int saves_redirect_path(wchar_t *path);
/* The user's Documents folder (SHGetKnownFolderPath(FOLDERID_Documents), else
   %USERPROFILE%\Documents with *fallback = 1). 1 = found. */
int saves_documents_folder(wchar_t *output, size_t capacity, int *fallback);
#endif
