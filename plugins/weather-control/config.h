#ifndef TIMELOCK_CONFIG_H
#define TIMELOCK_CONFIG_H

typedef void (*LogFn)(const char *message);

// Shared settings. Written by the ini loader and the Settings rows (UI
// thread), read by the clock hook (game thread); every field is one aligned
// 32-bit value, and `version` changes after each edit.
void config_load(const wchar_t *ini_path, LogFn log);
int config_enabled(void);
float config_hour(void);  // 0 <= hour < 24
long config_version(void);

// Settings-row edits: apply and save to the ini. Return 0 when refused.
int config_set_enabled(int enabled);
int config_set_hour(float hour);

// Parses "18.5", "18:30" or a preset name (Dawn, Noon, Sunset, Night, or one
// from [Presets]) into hours. Returns 1 when valid.
int config_parse_hour(const wchar_t *text, const wchar_t *ini_path, float *hours);

// Weather: the game's own weather options (MainlandWeatherOptions order:
// Current, Sunny, Wet, Rain, Storm). Dynamic leaves the game's weather alone.
enum { WEATHER_DYNAMIC, WEATHER_SUNNY, WEATHER_WET, WEATHER_RAIN, WEATHER_STORM, WEATHER_COUNT };
void config_load_weather(void);        // [Weather] Mode=..., after config_load
int config_weather(void);
int config_set_weather(int mode);      // Settings row: apply and save. 0 when refused.
int config_parse_weather(const wchar_t *text, int *mode);  // names, plus aliases Current, Clear
const char *config_weather_name(int mode);

#endif
