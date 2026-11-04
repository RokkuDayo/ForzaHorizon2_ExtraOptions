#include "patch_engine.h"
#include "patches_generated.h"
#include "sha256.h"
#include "support.h"

static unsigned char file_buffer[65536];

int verify_game_file(const wchar_t *executable_path) {
    HANDLE file = CreateFileW(executable_path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        status_set("REFUSED: cannot read the game executable for verification.");
        return 0;
    }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size) || size.QuadPart != GAME_FILE_SIZE) {
        CloseHandle(file);
        status_set("REFUSED: unsupported executable size.");
        return 0;
    }
    Sha256Context hash_context;
    sha256_initialize(&hash_context);
    uint32_t file_offset = 0;
    DWORD bytes_read;
    for (;;) {
        if (!ReadFile(file, file_buffer, sizeof(file_buffer), &bytes_read, 0)) {
            CloseHandle(file);
            status_set("REFUSED: executable verification read failed.");
            return 0;
        }
        if (!bytes_read) {
            break;
        }
        /* Replace only the known patch ranges with their original bytes before
           hashing. Any modification anywhere else still fails verification. */
        for (unsigned i = 0; i < FIXED_PATCH_COUNT; ++i) {
            const PatchDefinition *patch = &fixed_patches[i];
            uint32_t begin = patch->offset > file_offset ? patch->offset : file_offset;
            uint32_t end = patch->offset + patch->size < file_offset + bytes_read
                               ? patch->offset + patch->size
                               : file_offset + bytes_read;
            if (begin < end) {
                memory_copy(file_buffer + begin - file_offset,
                            patch->original + begin - patch->offset, end - begin);
            }
        }
        sha256_update(&hash_context, file_buffer, bytes_read);
        file_offset += bytes_read;
    }
    CloseHandle(file);
    unsigned char digest[32];
    sha256_finalize(&hash_context, digest);
    if (file_offset != GAME_FILE_SIZE || !memory_equal(digest, game_file_sha256, 32)) {
        status_set("REFUSED: executable differs from the supported FH2 build "
                   "outside the known "
                   "patch ranges.");
        return 0;
    }
    return 1;
}
