#pragma once
#include "support.h"

HANDLE video_redirect_open_file(LPCWSTR filename, DWORD access, DWORD sharing, DWORD disposition,
                                LPCREATEFILE2_EXTENDED_PARAMETERS options, int *handled);
