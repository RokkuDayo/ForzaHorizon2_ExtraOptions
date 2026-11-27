#include "hook.hpp"

#include "patch.hpp"

#include <windows.h>
#include <cstring>

namespace core::hook {

namespace {

void abs_jmp(uint8_t *out, const void *to)
{
    out[0] = 0xff;
    out[1] = 0x25;
    memset(out + 2, 0, 4);
    memcpy(out + 6, &to, 8);
}

} // namespace

bool install(uint8_t *at, const char *stolen_hex, void *target, void **tramp)
{
    patch::Bytes stolen;
    if (!patch::parse(stolen_hex, strlen(stolen_hex), stolen, false) || stolen.size < k_jmp_size) return false;
    if (memcmp(at, stolen.value, stolen.size) != 0) return false;

    auto *t = static_cast<uint8_t *>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!t) return false;
    memcpy(t, at, stolen.size);
    abs_jmp(t + stolen.size, at + stolen.size);
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    *tramp = t;

    uint8_t jmp[patch::k_max_bytes];
    memset(jmp, 0xcc, sizeof jmp); // leftover of the stolen instructions: never reached
    abs_jmp(jmp, target);
    return patch::write_code(at, jmp, stolen.size);
}

bool reachable(const void *from, const void *p, size_t n)
{
    auto f = reinterpret_cast<intptr_t>(from), a = reinterpret_cast<intptr_t>(p);
    intptr_t lo = a - f, hi = a + static_cast<intptr_t>(n ? n - 1 : 0) - f; // the last byte is p + n - 1
    return lo >= INT32_MIN && hi <= INT32_MAX;
}

void *alloc_near(const void *close_to, size_t size, unsigned long protect)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity, n = reinterpret_cast<uintptr_t>(close_to);
    const uintptr_t min_app = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t max_app = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    const uintptr_t span = 0x7ff00000; // a little under 2 GB on each side
    uintptr_t lo = n > min_app + span ? n - span : min_app, hi = n < max_app - span ? n + span : max_app;

    for (uintptr_t a = (lo + gran - 1) & ~(gran - 1); a + size <= hi;) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(reinterpret_cast<void *>(a), &mi, sizeof mi)) break;
        uintptr_t end = reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize;
        if (mi.State == MEM_FREE && a + size <= end && reachable(close_to, reinterpret_cast<void *>(a), size)) {
            void *p = VirtualAlloc(reinterpret_cast<void *>(a), size, MEM_RESERVE | MEM_COMMIT, protect);
            if (p) return p;
        }
        a = (end + gran - 1) & ~(gran - 1);
    }
    return nullptr;
}

void *alloc_reaching(const void *a, const void *b, size_t size, unsigned long protect)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t min_app = reinterpret_cast<uintptr_t>(si.lpMinimumApplicationAddress);
    const uintptr_t max_app = reinterpret_cast<uintptr_t>(si.lpMaximumApplicationAddress);
    auto try_at = [&](uintptr_t at) -> void * {
        auto *p = reinterpret_cast<void *>(at);
        if (!reachable(a, p, size) || !reachable(b, p, size)) return nullptr;
        return VirtualAlloc(p, size, MEM_RESERVE | MEM_COMMIT, protect);
    };

    // upward from b
    for (uintptr_t at = (reinterpret_cast<uintptr_t>(b) + gran) & ~(gran - 1); at + size <= max_app;) {
        if (!reachable(a, reinterpret_cast<void *>(at), size)) break;
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(reinterpret_cast<void *>(at), &mi, sizeof mi)) break;
        uintptr_t end = reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize;
        if (mi.State == MEM_FREE && at + size <= end)
            if (void *p = try_at(at)) return p;
        at = (end + gran - 1) & ~(gran - 1);
    }
    // downward from a
    for (uintptr_t top = reinterpret_cast<uintptr_t>(a) & ~(gran - 1); top > min_app + size;) {
        uintptr_t at = (top - size) & ~(gran - 1);
        if (!reachable(b, reinterpret_cast<void *>(at), size)) break;
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(reinterpret_cast<void *>(at), &mi, sizeof mi)) break;
        uintptr_t end = reinterpret_cast<uintptr_t>(mi.BaseAddress) + mi.RegionSize;
        if (mi.State == MEM_FREE && at + size <= end)
            if (void *p = try_at(at)) return p;
        top = reinterpret_cast<uintptr_t>(mi.AllocationBase ? mi.AllocationBase : mi.BaseAddress) & ~(gran - 1);
    }
    return nullptr;
}

} // namespace core::hook
