#ifndef FH2_OFFLINE_EVENT_SETUP_H
#define FH2_OFFLINE_EVENT_SETUP_H
#include "extra_options.h"

typedef struct {
    unsigned drivatars, grid_order;
    int stock_only, traffic, street;
} OfflineEventSetup;

void offline_event_setup_initialize(unsigned char *image);
void offline_event_setup_add(void *page, int enabled);
OfflineEventSetup offline_event_setup_selected(void);
#endif
