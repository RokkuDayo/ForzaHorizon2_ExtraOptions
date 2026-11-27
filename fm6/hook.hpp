// Detour with a trampoline on game code.
#pragma once

#include <cstdint>

namespace core::hook {

// Size of the absolute jmp written at the hooked address (ff 25 00000000 + 8-byte target).
constexpr size_t k_jmp_size = 14;

// stolen_hex: the whole instructions at `at` that the jmp overwrites (at least k_jmp_size bytes, none relative to
// RIP, checked byte by byte). They are copied to a trampoline that jumps back to at + their size; then `at` jumps
// to target. *tramp = the trampoline (calls the original function). false (nothing written) when the bytes do not
// match or the trampoline cannot be allocated.
bool install(uint8_t *at, const char *stolen_hex, void *target, void **tramp);

// size bytes of committed memory (protection protect) whose whole range is within a rel32 (+-2 GB) of close_to, or
// null.
void *alloc_near(const void *close_to, size_t size, unsigned long protect);

// size bytes of committed memory (protection protect) reachable with a rel32 from both a and b (a <= b, e.g. the
// first and last site of a game that will point into it), searched outward from them; or null.
void *alloc_reaching(const void *a, const void *b, size_t size, unsigned long protect);

// Every byte of [p, p + n) can be reached with a rel32 from the end of an instruction at from.
bool reachable(const void *from, const void *p, size_t n);

} // namespace core::hook
