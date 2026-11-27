// Log of the mod (fm_extra_options.log next to winmm.dll, recreated on every run). Thread safe.
#pragma once

namespace core::log {

// Creates (truncates) the log file. Without a successful open, write() does nothing.
bool open(const wchar_t *path);
void close();

// One line: "[   ms tid] text".
void write(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

} // namespace core::log
