// Guarded entry hooks for game functions (copied from FH2 LiveTune).
//
// The first 8 bytes of the function become `jmp rel32` to a stub placed next to
// the game image; the stub jumps to our handler, and a trampoline runs the
// displaced prologue then continues after it. The prologue must be exactly the
// expected bytes and position-independent (callers pass only such prologues).
// The 8-byte store happens with every other thread suspended and none of them
// inside the prologue, on a 16-byte aligned entry, so no thread ever runs a
// half-written instruction.

#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <string.h>

#include "entry_hook.h"

static void write_abs_jump(unsigned char *at, const void *target) {
    at[0] = 0xff; at[1] = 0x25;          // jmp qword ptr [rip+0]
    memset(at + 2, 0, 4);
    memcpy(at + 6, &target, 8);
}

static void *near_executable(unsigned char *image, size_t image_size, size_t size) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uintptr_t step = info.dwAllocationGranularity;
    uintptr_t start = ((uintptr_t)image + image_size + step - 1) & ~(step - 1);
    for (uintptr_t at = start; at < start + 0x40000000u; at += step) {
        void *memory = VirtualAlloc((void *)at, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (memory) return memory;
    }
    return NULL;
}

static HANDLE suspended[4096];

// Suspends every other thread; refuses (and resumes them) if any is executing
// inside [from, to). Returns the number suspended, or -1.
static int suspend_others(uintptr_t from, uintptr_t to) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return -1;
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    int count = 0, busy = 0;
    THREADENTRY32 entry = {sizeof entry};
    for (BOOL more = Thread32First(snapshot, &entry); more && count < (int)ARRAYSIZE(suspended);
         more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != pid || entry.th32ThreadID == self) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
        if (!thread) continue;
        if (SuspendThread(thread) == (DWORD)-1) { CloseHandle(thread); continue; }
        CONTEXT c;
        memset(&c, 0, sizeof c);
        c.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(thread, &c) && c.Rip >= from && c.Rip < to) busy = 1;
        suspended[count++] = thread;
    }
    CloseHandle(snapshot);
    if (busy) {
        while (count--) { ResumeThread(suspended[count]); CloseHandle(suspended[count]); }
        return -1;
    }
    return count;
}

void *entry_hook_install(unsigned char *image, size_t image_size, uint32_t rva,
                         const unsigned char *prologue, unsigned length, void *handler) {
    unsigned char *entry = image + rva;
    if (rva + length >= image_size || length < 8 || length > 32 || ((uintptr_t)entry & 15) ||
        memcmp(entry, prologue, length))
        return NULL;
    unsigned char *stub = near_executable(image, image_size, 64);
    if (!stub) return NULL;
    // [0..13] jump to the handler, [16..] trampoline: displaced prologue, then back.
    write_abs_jump(stub, handler);
    unsigned char *trampoline = stub + 16;
    memcpy(trampoline, prologue, length);
    write_abs_jump(trampoline + length, entry + length);
    FlushInstructionCache(GetCurrentProcess(), stub, 64);

    // New first 8 bytes: jmp rel32 to the stub, then the original bytes 5..7.
    int32_t rel = (int32_t)((intptr_t)stub - (intptr_t)(entry + 5));
    unsigned char patched[8];
    patched[0] = 0xe9;
    memcpy(patched + 1, &rel, 4);
    memcpy(patched + 5, entry + 5, 3);
    uint64_t value;
    memcpy(&value, patched, 8);

    DWORD old;
    if (!VirtualProtect(entry, 8, PAGE_EXECUTE_READWRITE, &old)) return NULL;
    int count = -1;
    for (int tries = 0; tries < 20 && count < 0; ++tries) {
        count = suspend_others((uintptr_t)entry, (uintptr_t)(entry + length));
        if (count < 0) Sleep(1);
    }
    if (count < 0) {
        VirtualProtect(entry, 8, old, &old);
        return NULL;
    }
    *(volatile uint64_t *)entry = value;
    FlushInstructionCache(GetCurrentProcess(), entry, 8);
    while (count-- > 0) { ResumeThread(suspended[count]); CloseHandle(suspended[count]); }
    VirtualProtect(entry, 8, old, &old);
    return trampoline;
}
