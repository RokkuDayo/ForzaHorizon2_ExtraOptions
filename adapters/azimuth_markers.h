#ifndef FH2_AZIMUTH_MARKERS_H
#define FH2_AZIMUTH_MARKERS_H
#include "extra_options.h"
void azimuth_repair_missing_markers(unsigned char *host);
/* Startup-only, allowlisted title scope. Existing commits and blobs are retained. */
void azimuth_repair_title_markers(unsigned char *host, unsigned title_id,
                                 void (*write_log)(const char *));
#endif
