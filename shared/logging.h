#ifndef FH2_LOGGING_H
#define FH2_LOGGING_H
#include "shared/extra_options.h"
void log_reset(void);
void log_set_named(const wchar_t *directory,const wchar_t *name);
void log_set_directory(const wchar_t *directory);
void log_append(const char *message);
void log_append_value(const char *label, int value);
void log_start(const wchar_t *directory, const ModOptions *options);
#endif
