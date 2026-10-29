#include "azimuth_host.h"
#include "sha256.h"
#include "support.h"

#define HOST_CODE_CAPACITY 1024u
#define HOST_SET_CAPACITY 2u

typedef struct {
    const HostPatchSet *set;
    unsigned char original[HOST_CODE_CAPACITY];
    DWORD old_protection;
    int needs_write, writable;
} HostCodeChange;

#ifdef FH2_TWEAKS_TEST
static int protection_failure = -1;
static int protection_calls;
#endif

static const struct {
    AzimuthBuild build;
    uint32_t timestamp, image_size;
} supported_hosts[] = {
    {AZIMUTH_101, 0x6adff280u, 0x9a6000u},
    {AZIMUTH_102, 0x6ae05dd1u, 0x9af000u},
    {AZIMUTH_103, 0x6ae0f17au, 0x9d8000u},
    {AZIMUTH_104, 0x6ae393ebu, 0x8a2000u},
};

static unsigned host_image_size(AzimuthBuild build) {
    for (unsigned i = 0; i < ARRAY_COUNT(supported_hosts); ++i) {
        if (supported_hosts[i].build == build) {
            return supported_hosts[i].image_size;
        }
    }
    return 0;
}

AzimuthBuild azimuth_host_build(const unsigned char *base) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    if (!base || !memory_read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew < 64 || dos.e_lfanew > 4096 ||
        !memory_read(base + dos.e_lfanew, &nt, sizeof(nt))) {
        return 0;
    }
    if (nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        (nt.FileHeader.Characteristics & IMAGE_FILE_DLL) ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return AZIMUTH_UNKNOWN;
    }
    for (unsigned i = 0; i < ARRAY_COUNT(supported_hosts); ++i) {
        if (nt.FileHeader.TimeDateStamp == supported_hosts[i].timestamp &&
            nt.OptionalHeader.SizeOfImage == supported_hosts[i].image_size) {
            return supported_hosts[i].build;
        }
    }
    return AZIMUTH_UNKNOWN;
}

static int prepare_change(unsigned char *base, unsigned image_size, const HostPatchSet *set,
                          HostCodeChange *change) {
    unsigned char digest[32];
    change->set = set;
    change->writable = 0;
    change->needs_write = 0;
    if (!set->size || set->size > sizeof(change->original) || !set->original_hash ||
        !set->patched_hash || !set->patches || !set->patch_count ||
        set->rva > image_size - set->size ||
        !memory_read(base + set->rva, change->original, set->size)) {
        return 0;
    }
    for (unsigned i = 0; i < set->patch_count; ++i) {
        const HostCodePatch *patch = &set->patches[i];
        if (!patch->size || patch->size > set->size || patch->offset > set->size - patch->size) {
            return 0;
        }
    }
    Sha256Context hash;
    sha256_initialize(&hash);
    sha256_update(&hash, change->original, set->size);
    sha256_finalize(&hash, digest);
    if (memory_equal(digest, set->patched_hash, sizeof(digest))) {
        return 1;
    }
    if (!memory_equal(digest, set->original_hash, sizeof(digest))) {
        return 0;
    }
    change->needs_write = 1;
    return 1;
}

static int make_writable(unsigned char *base, HostCodeChange *change) {
#ifdef FH2_TWEAKS_TEST
    if (protection_calls++ == protection_failure) {
        SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    }
#endif
    if (!VirtualProtect(base + change->set->rva, change->set->size, PAGE_EXECUTE_READWRITE,
                        &change->old_protection)) {
        return 0;
    }
    change->writable = 1;
    return 1;
}

static int restore_protections(unsigned char *base, HostCodeChange *changes, unsigned count) {
    int restored = 1;
    while (count) {
        HostCodeChange *change = &changes[--count];
        DWORD ignored;
        if (!change->writable) {
            continue;
        }
        if (VirtualProtect(base + change->set->rva, change->set->size, change->old_protection,
                           &ignored)) {
            change->writable = 0;
        } else {
            restored = 0;
        }
    }
    return restored;
}

static void rollback_changes(unsigned char *base, HostCodeChange *changes, unsigned count,
                             int code_written) {
    for (unsigned i = 0; code_written && i < count; ++i) {
        HostCodeChange *change = &changes[i];
        DWORD ignored;
        if (!change->needs_write) {
            continue;
        }
        unsigned char *code = base + change->set->rva;
        if (!change->writable) {
            change->writable =
                VirtualProtect(code, change->set->size, PAGE_EXECUTE_READWRITE, &ignored) != 0;
        }
        if (change->writable) {
            memory_copy(code, change->original, change->set->size);
            FlushInstructionCache(GetCurrentProcess(), code, change->set->size);
        }
    }
    restore_protections(base, changes, count);
}

int azimuth_patch_host_codes(unsigned char *base, const HostPatchSet *sets, unsigned count) {
    HostCodeChange changes[HOST_SET_CAPACITY];
    unsigned changed = 0;
    AzimuthBuild build = azimuth_host_build(base);
    if (!sets || !count || count > HOST_SET_CAPACITY || build == AZIMUTH_UNKNOWN) {
        return 0;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!prepare_change(base, host_image_size(build), &sets[i], &changes[i])) {
            return 0;
        }
        changed += changes[i].needs_write;
    }
    if (!changed) {
        return 2;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (changes[i].needs_write && !make_writable(base, &changes[i])) {
            rollback_changes(base, changes, count, 0);
            return -1;
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        const HostPatchSet *set = changes[i].set;
        if (!changes[i].needs_write) {
            continue;
        }
        for (unsigned j = 0; j < set->patch_count; ++j) {
            const HostCodePatch *patch = &set->patches[j];
            memory_copy(base + set->rva + patch->offset, patch->bytes, patch->size);
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        const HostPatchSet *set = changes[i].set;
        if (changes[i].needs_write &&
            !FlushInstructionCache(GetCurrentProcess(), base + set->rva, set->size)) {
            rollback_changes(base, changes, count, 1);
            return -1;
        }
    }
    if (!restore_protections(base, changes, count)) {
        rollback_changes(base, changes, count, 1);
        return -1;
    }
    return 1;
}

int azimuth_patch_host_code(unsigned char *base, const HostPatchSet *set) {
    return azimuth_patch_host_codes(base, set, 1);
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) void Fh2TestAzimuthHostProtectFailure(int index) {
    protection_failure = index;
    protection_calls = 0;
}
#endif
