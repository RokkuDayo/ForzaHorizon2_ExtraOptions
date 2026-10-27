#ifndef FH2_AZIMUTH_PHOTOS_H
#define FH2_AZIMUTH_PHOTOS_H
#include "extra_options.h"

/* "Photos as JPG" (FH2_ExtraOptions.ini [Photos] ExportJpg, default 0). The
   game keeps each photo as a save container
   <saves>\player-*\Photo_NNNN_<yyyyMMddHHmmss UTC>\revisions\<rev>\data\image,
   which is already a JPEG. A low-priority thread lists those containers every
   3 s and copies the image of the newest committed revision to
   <Documents>\My Games\Forza Horizon 2\Photos\FH2 <yyyy-MM-dd HH-mm-ss>.jpg
   (local time). Reads the saves only (the redirected folder when [Saves]
   InDocuments is on); never overwrites or deletes an exported file. */
void photo_export_start(const wchar_t *settings_directory);
#endif
