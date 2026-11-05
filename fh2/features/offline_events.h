#ifndef FH2_OFFLINE_EVENTS_H
#define FH2_OFFLINE_EVENTS_H
#include "extra_options.h"
extern PatchDefinition offline_events_hook_patches[38];
size_t offline_results_xaml(const char *text,size_t length,char *out,size_t capacity);
void offline_events_prepare_hooks(unsigned char *image);
void offline_events_tick(void);
int offline_events_transform(void *resource);
void offline_events_resource_parsed(int success);
#endif
