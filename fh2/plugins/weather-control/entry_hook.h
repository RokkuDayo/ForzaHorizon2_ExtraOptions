#ifndef TIMELOCK_ENTRY_HOOK_H
#define TIMELOCK_ENTRY_HOOK_H
#include <stddef.h>
#include <stdint.h>

// Redirects the game function at `rva` to `handler`. `prologue` (8-32 bytes,
// position-independent) must match the function's first bytes exactly.
// Returns a trampoline that runs the original function, or NULL with nothing changed.
void *entry_hook_install(unsigned char *image, size_t image_size, uint32_t rva,
                         const unsigned char *prologue, unsigned length, void *handler);

#endif
