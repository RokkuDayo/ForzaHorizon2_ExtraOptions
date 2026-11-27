// Reading of the options ini (motorsport_extraoptions.ini, one section per game).
#pragma once

namespace core::ini {

// Integer value of [section] key in the file at path; def when the file, the section or the key is missing.
int get_int(const wchar_t *path, const char *section, const char *key, int def);

// Writes [section] key=value, keeping the rest of the file (comments included); creates what is missing.
bool set_int(const wchar_t *path, const char *section, const char *key, int value);

} // namespace core::ini
