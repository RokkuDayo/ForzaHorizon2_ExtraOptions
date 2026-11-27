// PE helpers on mapped images: header check, readable ranges and IAT slots by import name.
#pragma once

#include <windows.h>
#include <cstddef>

namespace core::pe {

// [p, p + n) is committed, inside one region and readable.
bool readable(const void *p, size_t n);

// Bytes readable from p up to the end of its region, at most max (0 when p is not readable).
size_t readable_size(const void *p, size_t max);

// The PE64 headers of the image mapped at base, or null (no MZ/PE, not PE32+, or unreadable).
const IMAGE_NT_HEADERS64 *headers(const void *base);

// The IAT slot of dll!func (by name, case-insensitive dll) in the module mapped at mod, or null.
void **iat_slot(void *mod, const char *dll, const char *func);

} // namespace core::pe
