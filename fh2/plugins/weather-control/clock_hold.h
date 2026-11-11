#ifndef TIMELOCK_CLOCK_HOLD_H
#define TIMELOCK_CLOCK_HOLD_H
#include <stddef.h>

#include "config.h"

// Holds a freely running clock without writing native stop flags.
// Returns 1 when installed.
int clock_hold_install(unsigned char *image, size_t image_size, LogFn log);

// The guard checks on their own (offline tests): 1 when every expected byte matches.
int clock_hold_verify(const unsigned char *image, size_t image_size, LogFn log);

#endif

