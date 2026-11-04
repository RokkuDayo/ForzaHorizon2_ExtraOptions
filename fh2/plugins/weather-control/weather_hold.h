#ifndef TIMELOCK_WEATHER_HOLD_H
#define TIMELOCK_WEATHER_HOLD_H
#include <stddef.h>

#include "config.h"

// Holds free-roam weather at the chosen game weather option (Sunny, Wet,
// Rain, Storm) through the game's own WeatherManager::SetControl, the call
// behind the stateflow's SetWeatherControl. Only a weather manager the game
// has released to dynamic weather (or one we set) is ever touched.
// No hook of its own: clock_hold.c calls weather_hold_frame from its
// TimeOfDay::Update hook, on the world-update thread right after the
// weather manager's own update.
int weather_hold_init(unsigned char *image, size_t image_size, LogFn log);  // 1 when every guard matched
void weather_hold_frame(unsigned char *tod);

// Offline tests.
int weather_hold_verify(const unsigned char *image, size_t image_size, LogFn log);
typedef void (*WeatherSetControlFn)(unsigned char *weather, const unsigned char *control);
typedef void (*WeatherReleaseFn)(unsigned char *weather);
typedef int (*WeatherFindPresetFn)(unsigned char *weather, const void *name);
// Replaces the game functions and the world lookup (tests only).
void weather_hold_test_setup(WeatherSetControlFn set_control, WeatherReleaseFn release, WeatherFindPresetFn find_preset,
                             unsigned char *(*weather_of)(unsigned char *tod), LogFn log);
// The 0x3c-byte control block weather_hold builds for a mode (preset index given).
void weather_hold_build_control(int mode, int preset_index, unsigned char *control);
const char *weather_hold_preset_name(int mode);

#endif
