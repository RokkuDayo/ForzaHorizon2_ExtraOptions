#pragma once
#include "support.h"

// Private names keep native async file creation inside the translation layer's
// supported game: namespace. Registrations live for the process lifetime.
const char *media_files_register(const wchar_t *physical, int temporary);
HANDLE media_files_open(LPCWSTR filename, DWORD access, DWORD sharing, DWORD disposition,
                        LPCREATEFILE2_EXTENDED_PARAMETERS options, int *handled);
