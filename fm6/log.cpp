#include "log.hpp"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace core::log {

namespace {
FILE *g_file;
ULONGLONG g_t0;
SRWLOCK g_lock = SRWLOCK_INIT;
} // namespace

bool open(const wchar_t *path)
{
    AcquireSRWLockExclusive(&g_lock);
    if (g_file) fclose(g_file);
    g_file = _wfopen(path, L"w");
    g_t0 = GetTickCount64();
    ReleaseSRWLockExclusive(&g_lock);
    return g_file != nullptr;
}

void close()
{
    AcquireSRWLockExclusive(&g_lock);
    if (g_file) fclose(g_file);
    g_file = nullptr;
    ReleaseSRWLockExclusive(&g_lock);
}

void write(const char *fmt, ...)
{
    AcquireSRWLockExclusive(&g_lock);
    if (g_file) {
        fprintf(g_file, "[%7llu ms t%05lu] ", GetTickCount64() - g_t0, GetCurrentThreadId());
        va_list ap;
        va_start(ap, fmt);
        vfprintf(g_file, fmt, ap);
        va_end(ap);
        fputc('\n', g_file);
        fflush(g_file);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace core::log
