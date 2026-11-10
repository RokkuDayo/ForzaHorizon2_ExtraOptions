#include "shared/support.h"

static char status_text[512] = "Not initialized";

void memory_copy(void *destination, const void *source, size_t size) {
    volatile unsigned char *destination_bytes = (volatile unsigned char *)destination;
    const unsigned char *source_bytes = (const unsigned char *)source;
    while (size--) {
        *destination_bytes++ = *source_bytes++;
    }
}

int memory_equal(const void *left, const void *right, size_t size) {
    const unsigned char *left_bytes = left, *right_bytes = right;
    while (size--) {
        if (*left_bytes++ != *right_bytes++) {
            return 0;
        }
    }
    return 1;
}

size_t text_length(const char *text) {
    size_t length = 0;
    while (text[length]) {
        ++length;
    }
    return length;
}

void status_set(const char *message) {
    size_t length = text_length(message);
    if (length >= sizeof(status_text)) {
        length = sizeof(status_text) - 1;
    }
    memory_copy(status_text, message, length);
    status_text[length] = 0;
}

void status_append(const char *message) {
    size_t length = text_length(status_text), append_size = text_length(message);
    if (append_size >= sizeof(status_text) - length) {
        append_size = sizeof(status_text) - length - 1;
    }
    memory_copy(status_text + length, message, append_size);
    status_text[length + append_size] = 0;
}

int text_equal_ignore_case(const char *left, const char *right) {
    while (*left && *right) {
        char left_char = *left++, right_char = *right++;
        if (left_char >= 'A' && left_char <= 'Z') {
            left_char += 'a' - 'A';
        }
        if (right_char >= 'A' && right_char <= 'Z') {
            right_char += 'a' - 'A';
        }
        if (left_char != right_char) {
            return 0;
        }
    }
    return *left == *right;
}

const char *text_find(const char *text, size_t length, const char *needle) {
    size_t needle_length = text_length(needle);
    if (needle_length > length) {
        return NULL;
    }
    if (!needle_length) {
        return text;
    }
    for (size_t offset = 0; offset <= length - needle_length; ++offset) {
        if (text[offset] == needle[0] && memory_equal(text + offset, needle, needle_length)) {
            return text + offset;
        }
    }
    return NULL;
}

const char *text_find_unique(const char *text, size_t length, const char *needle) {
    if (!*needle) {
        return NULL;
    }
    const char *found = text_find(text, length, needle);
    if (!found || text_find(found + 1, length - (size_t)(found + 1 - text), needle)) {
        return NULL;
    }
    return found;
}

static int memory_read_fault(EXCEPTION_POINTERS *exception) {
    const EXCEPTION_RECORD *record = exception->ExceptionRecord;
    DWORD code = record->ExceptionCode;
    if (code == EXCEPTION_GUARD_PAGE && record->NumberParameters >= 2) {
        void *fault_address = (void *)record->ExceptionInformation[1];
        MEMORY_BASIC_INFORMATION region;
        DWORD previous;
        if (VirtualQuery(fault_address, &region, sizeof(region)) == sizeof(region)) {
            VirtualProtect(fault_address, 1, region.Protect | PAGE_GUARD, &previous);
        }
    }
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
                   code == EXCEPTION_GUARD_PAGE
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

int memory_read(const void *address, void *out, size_t size) {
    if (!address || !out || size > UINTPTR_MAX - (uintptr_t)address) {
        return 0;
    }
    __try {
        memory_copy(out, address, size);
        return 1;
    } __except (memory_read_fault(GetExceptionInformation())) {
        return 0;
    }
}

int memory_read_pointer(const void *address, void **value) {
    return memory_read(address, value, sizeof(*value)) && *value;
}

int float_is_finite(float value) {
    uint32_t bits;
    memory_copy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000) != 0x7f800000;
}

int memory_is_writable(const void *address, size_t size) {
    uintptr_t start = (uintptr_t)address, end = start + size;
    if (!start || end < start) {
        return 0;
    }
    while (start < end) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery((void *)start, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
            return 0;
        }
        DWORD protection = info.Protect & 0xff;
        if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
            protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) {
            return 0;
        }
        uintptr_t next = (uintptr_t)info.BaseAddress + info.RegionSize;
        if (next <= start) {
            return 0;
        }
        start = next;
    }
    return 1;
}

const char *status_get(void) {
    return status_text;
}

int path_join(wchar_t *output, const wchar_t *directory, const wchar_t *name) {
    size_t length = 0;
    while (directory[length]) {
        if (length >= 32760) {
            return 0;
        }
        output[length] = directory[length];
        ++length;
    }
    if (length && output[length - 1] != L'\\' && output[length - 1] != L'/') {
        output[length++] = L'\\';
    }
    for (size_t name_index = 0;; ++name_index) {
        if (length >= 32767) {
            return 0;
        }
        output[length++] = name[name_index];
        if (!name[name_index]) {
            break;
        }
    }
    return 1;
}

#ifdef FH2_TWEAKS_TEST
__declspec(dllexport) int Fh2TestMemoryRead(const void *address, void *output, size_t size) {
    return memory_read(address, output, size);
}

__declspec(dllexport) uint64_t Fh2TestMemoryBenchmark(const void *address, unsigned count,
                                                      int process_api) {
    uint64_t sum = 0, value = 0;
    for (unsigned index = 0; index < count; ++index) {
        SIZE_T copied;
        int read = process_api ? ReadProcessMemory(GetCurrentProcess(), address, &value,
                                                   sizeof(value), &copied)
                               : memory_read(address, &value, sizeof(value));
        if (read) {
            sum += value;
        }
    }
    return sum;
}
#endif
