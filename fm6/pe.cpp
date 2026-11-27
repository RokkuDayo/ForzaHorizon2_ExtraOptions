#include "pe.hpp"

#include <cstdint>
#include <cstring>

namespace core::pe {

bool readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return false;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return static_cast<const uint8_t *>(p) + n <= static_cast<const uint8_t *>(mi.BaseAddress) + mi.RegionSize;
}

size_t readable_size(const void *p, size_t max)
{
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery(p, &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    if (mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    size_t left = static_cast<size_t>(static_cast<const uint8_t *>(mi.BaseAddress) + mi.RegionSize -
                                      static_cast<const uint8_t *>(p));
    return left < max ? left : max;
}

const IMAGE_NT_HEADERS64 *headers(const void *base)
{
    auto *b = static_cast<const uint8_t *>(base);
    auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(b);
    if (!b || !readable(b, sizeof *dos) || dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    if (dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return nullptr;
    auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS64 *>(b + dos->e_lfanew);
    if (!readable(nt, sizeof *nt) || nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
    return nt;
}

void **iat_slot(void *mod, const char *dll, const char *func)
{
    auto *b = static_cast<uint8_t *>(mod);
    const IMAGE_NT_HEADERS64 *nt = headers(b);
    if (!nt) return nullptr;
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(b + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp(reinterpret_cast<const char *>(b + d->Name), dll) != 0) continue;
        auto *names = reinterpret_cast<IMAGE_THUNK_DATA64 *>(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk
                                                                                         : d->FirstThunk));
        auto *slots = reinterpret_cast<IMAGE_THUNK_DATA64 *>(b + d->FirstThunk);
        for (size_t i = 0; names[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
            auto *ibn = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(b + names[i].u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char *>(ibn->Name), func) == 0)
                return reinterpret_cast<void **>(&slots[i].u1.Function);
        }
    }
    return nullptr;
}

} // namespace core::pe
