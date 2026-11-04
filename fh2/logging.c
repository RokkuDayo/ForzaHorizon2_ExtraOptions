#include "logging.h"
#include "features.h"
#include "settings.h"
#include "support.h"

static wchar_t runtime_log_path[PATH_CAPACITY];
static wchar_t startup_log_path[PATH_CAPACITY];

void log_reset(void) {
    runtime_log_path[0] = 0;
}

void log_set_directory(const wchar_t *directory) {
    if (!path_join(runtime_log_path, directory, MOD_LOG_NAME)) {
        runtime_log_path[0] = 0;
    }
}

void log_append(const char *message) {
    if (!runtime_log_path[0]) {
        return;
    }
    HANDLE file =
        CreateFileW(runtime_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD bytes_written;
    WriteFile(file, message, (DWORD)text_length(message), &bytes_written, NULL);
    CloseHandle(file);
}

static void write_text(HANDLE file, const char *text) {
    DWORD bytes_written;
    WriteFile(file, text, (DWORD)text_length(text), &bytes_written, NULL);
}

static void write_number(HANDLE file, int value) {
    char number[12];
    char *cursor = number + sizeof(number) - 1;
    unsigned magnitude = value < 0 ? 0u - (unsigned)value : (unsigned)value;
    *cursor = 0;
    do {
        *--cursor = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude);
    if (value < 0) {
        *--cursor = '-';
    }
    write_text(file, cursor);
}

void log_append_value(const char *label, int value) {
    if (!runtime_log_path[0]) {
        return;
    }
    HANDLE file =
        CreateFileW(runtime_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        write_text(file, label);
        write_number(file, value);
        write_text(file, "\r\n");
        CloseHandle(file);
    }
}

void log_start(const wchar_t *directory, const ModOptions *options) {
    if (!path_join(startup_log_path, directory, MOD_LOG_NAME)) {
        return;
    }
    HANDLE file = CreateFileW(startup_log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
#ifdef FH2_AZIMUTH
    write_text(file, MOD_NAME " " MOD_VERSION "\r\n");
#else
    write_text(file, MOD_NAME " " MOD_VERSION " - xg.dll hook\r\n");
#endif
    write_text(file, status_get());
    for (size_t index = 0; index < setting_count; ++index) {
        const SettingDefinition *definition = &setting_definitions[index];
        int value = settings_value(options, definition);
        write_text(file, "\r\n");
        write_text(file, definition->log_name);
        write_text(file, "=");
        write_number(file, value);
    }
    write_text(file, "\r\n");
    CloseHandle(file);
}
