#ifndef FH2_OFFLINE_EVENT_DIVISION_H
#define FH2_OFFLINE_EVENT_DIVISION_H
#include "extra_options.h"
void offline_event_division_initialize(unsigned char *image);
void offline_event_division_add(void *page, int enabled);
int offline_event_division_selected(void);
#endif
