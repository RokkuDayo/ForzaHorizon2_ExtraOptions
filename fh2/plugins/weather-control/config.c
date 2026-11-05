// Time Lock settings: TimeLock.ini beside the plugins folder.
//
//   [TimeLock]
//   Enabled=1
//   Hour=Noon          ; 0-23.99 hours, "18:30", or a preset name
//   [Presets]
//   Dawn=5:45 ...      ; optional overrides of the built-in presets
//
// The in-game Settings rows call config_set_*, which save Enabled and Hour
// back to the same file (Hour as a plain number).

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "config.h"

static wchar_t ini[MAX_PATH];
static LogFn log_message;
static volatile LONG enabled;
static volatile LONG hour_bits;  // float
static volatile LONG version;

static const struct {
    const wchar_t *name;
    const wchar_t *value;
} builtin_presets[] = {
    {L"Dawn", L"5:45"},
    {L"Noon", L"12:00"},
    {L"Sunset", L"17:50"},
    {L"Night", L"23:00"},
};

static void log_line(const char *text) {
    if (log_message) log_message(text);
}

static float bits_to_float(LONG bits) {
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static LONG float_to_bits(float value) {
    LONG bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

// Numbers only: "18", "18.5", "18:30". Returns 1 when valid (0 <= h < 24).
static int parse_number(const wchar_t *text, float *hours) {
    while (*text == L' ' || *text == L'\t') ++text;
    if (!*text) return 0;
    wchar_t *end = NULL;
    double value;
    const wchar_t *colon = wcschr(text, L':');
    if (colon) {
        long h = wcstol(text, &end, 10);
        if (end != colon) return 0;
        long m = wcstol(colon + 1, &end, 10);
        if (end == colon + 1 || m < 0 || m > 59) return 0;
        value = (double)h + (double)m / 60.0;
    } else {
        value = wcstod(text, &end);
        if (end == text) return 0;
    }
    while (*end == L' ' || *end == L'\t') ++end;
    if (*end && *end != L';') return 0;
    if (!(value >= 0.0 && value < 24.0)) return 0;
    *hours = (float)value;
    return 1;
}

int config_parse_hour(const wchar_t *text, const wchar_t *ini_path, float *hours) {
    if (parse_number(text, hours)) return 1;
    wchar_t name[64];
    size_t n = 0;
    while (*text == L' ' || *text == L'\t') ++text;
    while (text[n] && text[n] != L' ' && text[n] != L'\t' && text[n] != L';' && n + 1 < ARRAYSIZE(name)) {
        name[n] = text[n];
        ++n;
    }
    name[n] = 0;
    if (!n) return 0;
    const wchar_t *fallback = NULL;
    for (unsigned i = 0; i < ARRAYSIZE(builtin_presets); ++i)
        if (!_wcsicmp(name, builtin_presets[i].name)) fallback = builtin_presets[i].value;
    wchar_t value[64];
    GetPrivateProfileStringW(L"Presets", name, fallback ? fallback : L"", value, ARRAYSIZE(value), ini_path);
    return parse_number(value, hours);
}

void config_load(const wchar_t *ini_path, LogFn log) {
    log_message = log;
    wcsncpy(ini, ini_path, MAX_PATH - 1);
    ini[MAX_PATH - 1] = 0;
    char line[256];
    int on = GetPrivateProfileIntW(L"TimeLock", L"Enabled", 0, ini) != 0;
    wchar_t text[64];
    GetPrivateProfileStringW(L"TimeLock", L"Hour", L"Noon", text, ARRAYSIZE(text), ini);
    float hours = 12.0f;
    if (!config_parse_hour(text, ini, &hours)) {
        snprintf(line, sizeof line, "TIME LOCK: Hour=%ls in TimeLock.ini is not 0-23.99, HH:MM or a preset; using 12:00.\r\n", text);
        log_line(line);
        hours = 12.0f;
    }
    InterlockedExchange(&hour_bits, float_to_bits(hours));
    InterlockedExchange(&enabled, on);
    InterlockedIncrement(&version);
    int h = (int)hours, m = (int)((hours - (float)h) * 60.0f + 0.5f);
    if (m == 60) { ++h; m = 0; }
    snprintf(line, sizeof line, "TIME LOCK: settings loaded: Enabled=%d, Hour=%ls (%02d:%02d).\r\n", on, text, h % 24, m);
    log_line(line);
}

int config_enabled(void) { return enabled != 0; }
float config_hour(void) { return bits_to_float(hour_bits); }
long config_version(void) { return version; }

static void save(void) {
    wchar_t text[32];
    WritePrivateProfileStringW(L"TimeLock", L"Enabled", enabled ? L"1" : L"0", ini);
    swprintf(text, ARRAYSIZE(text), L"%.2f", (double)config_hour());
    WritePrivateProfileStringW(L"TimeLock", L"Hour", text, ini);
}

int config_set_enabled(int on) {
    on = on != 0;
    if (InterlockedExchange(&enabled, on) != on) {
        InterlockedIncrement(&version);
        save();
    }
    return 1;
}

int config_set_hour(float hours) {
    if (!(hours >= 0.0f && hours < 24.0f)) return 0;
    LONG bits = float_to_bits(hours);
    if (InterlockedExchange(&hour_bits, bits) != bits) {
        InterlockedIncrement(&version);
        save();
    }
    return 1;
}

// ---------------------------------------------------------------- weather

static volatile LONG weather_mode;  // WeatherMode

static const struct {
    const wchar_t *name;
    int mode;
} weather_names[] = {
    {L"Dynamic", WEATHER_DYNAMIC}, {L"Current", WEATHER_DYNAMIC}, {L"Sunny", WEATHER_SUNNY}, {L"Clear", WEATHER_SUNNY},
    {L"Wet", WEATHER_WET},         {L"Rain", WEATHER_RAIN},       {L"Storm", WEATHER_STORM},
};

const char *config_weather_name(int mode) {
    static const char *names[WEATHER_COUNT] = {"Dynamic", "Sunny", "Wet", "Rain", "Storm"};
    return mode >= 0 && mode < WEATHER_COUNT ? names[mode] : "?";
}

int config_parse_weather(const wchar_t *text, int *mode) {
    wchar_t name[32];
    size_t n = 0;
    while (*text == L' ' || *text == L'\t') ++text;
    while (text[n] && text[n] != L' ' && text[n] != L'\t' && text[n] != L';' && n + 1 < ARRAYSIZE(name)) {
        name[n] = text[n];
        ++n;
    }
    name[n] = 0;
    for (unsigned i = 0; i < ARRAYSIZE(weather_names); ++i)
        if (n && !_wcsicmp(name, weather_names[i].name)) {
            *mode = weather_names[i].mode;
            return 1;
        }
    return 0;
}

void config_load_weather(void) {
    wchar_t text[32];
    char line[200];
    GetPrivateProfileStringW(L"Weather", L"Mode", L"Dynamic", text, ARRAYSIZE(text), ini);
    int mode = WEATHER_DYNAMIC;
    if (!config_parse_weather(text, &mode)) {
        snprintf(line, sizeof line, "TIME LOCK: [Weather] Mode=%ls is not Dynamic, Sunny, Wet, Rain or Storm; using Dynamic.\r\n", text);
        log_line(line);
        mode = WEATHER_DYNAMIC;
    }
    InterlockedExchange(&weather_mode, mode);
    InterlockedIncrement(&version);
    snprintf(line, sizeof line, "TIME LOCK: weather setting loaded: Mode=%s.\r\n", config_weather_name(mode));
    log_line(line);
}

int config_weather(void) { return (int)weather_mode; }

int config_set_weather(int mode) {
    if (mode < 0 || mode >= WEATHER_COUNT) return 0;
    if (InterlockedExchange(&weather_mode, mode) != mode) {
        InterlockedIncrement(&version);
        wchar_t text[16];
        swprintf(text, ARRAYSIZE(text), L"%hs", config_weather_name(mode));
        WritePrivateProfileStringW(L"Weather", L"Mode", text, ini);
    }
    return 1;
}
