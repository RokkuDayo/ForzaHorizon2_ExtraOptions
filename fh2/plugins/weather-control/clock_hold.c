// Holds the free-roam clock at the configured hour.
//
// The game keeps time of day in one object per loaded world (world+0x480,
// see research/time_of_day.md). Every frame the world update calls
// TimeOfDay::Update(tod, dt, out) at game+0x197b170, which does
//     if (!tod->stopped) tod->time += dt * tod->speed   (wrapping at 86400 s)
// and stops at tod->target when a timed transition is active.
// The stateflow drives it through behaviours only: SetTimeOfDay calls
// SetTime(tod, h*3600+m*60, 1) (game+0x197aec0) and StopTimeOfDay writes
// tod->stopped unless tod->stop_locked is set. Free roam ends with
// global.start_time (stopped = 0); races and events set their own time and
// stop the clock.
//
// We hook Update's entry. Before the original runs, if the lock is on and the
// clock is running freely (not stopped, no timed transition, no fixed-time or
// stop lock), we do what the stateflow's set_time_of_day + stop_time pair
// does: SetTime(tod, hour, 1) through the game's own function, then
// stopped = 1. Clocks the game stopped itself (races, cutscenes, car meets)
// are never touched. A clock we stopped is moved when the hour changes and
// released (stopped = 0) when the lock is turned off, but only while it is
// still exactly the value we set, so we never undo the game's own changes.
// Everything is checked against the expected bytes before anything is patched.

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clock_hold.h"
#include "entry_hook.h"
#include "weather_hold.h"

enum {
    TOD_UPDATE = 0x197b170,     // TimeOfDay::Update(tod, float dt, out)
    TOD_ADVANCE = 0x197b1ac,    // its "if (!stopped) time += dt * speed" block
    TOD_SET_TIME = 0x197aec0,   // SetTime(tod, float seconds, int remap)
    TOD_GET_TIME = 0x197a880,   // float GetTime(tod, int remap)
    STOP_BEHAVIOUR = 0x93ca07,  // States::StopTimeOfDay: honours stop_locked, writes stopped

    TOD_TIME = 0x10,            // float seconds since midnight (internal, after the remap curve)
    TOD_STOPPED = 0x24,         // byte: clock does not advance
    TOD_FIXED = 0x25,           // byte: debug fixed-time override; SetTime is ignored
    TOD_STOP_LOCKED = 0x27,     // byte: StopTimeOfDay is ignored
    TOD_HAS_TARGET = 0x28,      // byte: timed transition to a target time is running
};

// mov rax,rsp / mov [rax+8],rbx / mov [rax+0x10],rdi: position independent.
static const unsigned char update_prologue[11] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x78, 0x10};

// cmp byte [rcx+0x24],0 / vmovss xmm7,[86400] / jne / vmovss xmm2,[rcx+0x10] /
// vmulss xmm0,xmm1,[rcx+0x18] / vaddss / vcomiss / vmovss [rcx+0x10],xmm1
static const unsigned char update_advance[37] = {
    0x80, 0x79, 0x24, 0x00, 0xc5, 0xfa, 0x10, 0x3d, 0x1c, 0xbf, 0x97, 0x00, 0x75, 0x57, 0xc5, 0xfa, 0x10, 0x51, 0x10,
    0xc5, 0xf2, 0x59, 0x41, 0x18, 0xc5, 0xfa, 0x58, 0xca, 0xc5, 0xf8, 0x2f, 0xcf, 0xc5, 0xfa, 0x11, 0x49, 0x10};

// The whole SetTime: if (!fixed) { time = remap ? Remap(seconds) : seconds; } realtime = 0.
static const unsigned char set_time[61] = {
    0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x80, 0x79, 0x25, 0x00, 0x48, 0x8b, 0xd9, 0x75, 0x0a, 0x45, 0x85, 0xc0, 0x75,
    0x12, 0xc5, 0xfa, 0x11, 0x49, 0x10, 0xc6, 0x81, 0x61, 0x03, 0x00, 0x00, 0x00, 0x48, 0x83, 0xc4, 0x20, 0x5b, 0xc3,
    0xe8, 0xe5, 0xfe, 0xff, 0xff, 0xc6, 0x83, 0x61, 0x03, 0x00, 0x00, 0x00, 0xc5, 0xfa, 0x11, 0x43, 0x10, 0x48, 0x83,
    0xc4, 0x20, 0x5b, 0xc3};

// GetTime: remap ? InverseRemap(time) : time.
static const unsigned char get_time[20] = {0x85, 0xd2, 0x75, 0x06, 0xc5, 0xfa, 0x10, 0x41, 0x10, 0xc3,
                                           0xc5, 0xfa, 0x10, 0x49, 0x10, 0xe9, 0x4c, 0x01, 0x00, 0x00};

// StopTimeOfDay tail: tod = ...; if (!tod->stop_locked) tod->stopped = value.
static const unsigned char stop_behaviour_tail[9] = {0x80, 0x78, 0x27, 0x00, 0x75, 0x03, 0x88, 0x58, 0x24};

typedef unsigned char (*UpdateFn)(unsigned char *tod, float dt, void *out, void *unused);
typedef void (*SetTimeFn)(unsigned char *tod, float seconds, int remap);
typedef float (*GetTimeFn)(unsigned char *tod, int remap);

static unsigned char *game;
static LogFn log_message;
static UpdateFn original_update;

// Only the game thread that runs Update touches these.
static unsigned char *held_tod;   // clock we stopped, or NULL
static float held_time;           // its TOD_TIME right after we set it
static long applied_version = -1;
static unsigned char *last_tod;
static unsigned blocked_logs, hold_logs;  // capped: the game may stop and restart the clock often

static void log_line(const char *text) {
    if (log_message) log_message(text);
}

static void clock_text(char *out, size_t size, float seconds) {
    if (!(seconds >= 0.0f && seconds < 86400.0f)) { snprintf(out, size, "?"); return; }
    int total = (int)(seconds / 60.0f + 0.5f) % (24 * 60);
    snprintf(out, size, "%02d:%02d", total / 60, total % 60);
}

static float nominal_time(unsigned char *tod) {
    return ((GetTimeFn)(game + TOD_GET_TIME))(tod, 1);
}

static void hold(unsigned char *tod, int moving) {
    char line[256], before[16], after[16];
    float seconds = config_hour() * 3600.0f;
    if (seconds >= 86399.0f) seconds = 86399.0f;
    clock_text(before, sizeof before, nominal_time(tod));
    ((SetTimeFn)(game + TOD_SET_TIME))(tod, seconds, 1);
    tod[TOD_STOPPED] = 1;
    held_tod = tod;
    memcpy(&held_time, tod + TOD_TIME, sizeof held_time);
    clock_text(after, sizeof after, nominal_time(tod));
    snprintf(line, sizeof line,
             moving ? "TIME LOCK: hour changed; clock moved %s -> %s and kept stopped.\r\n"
                    : "TIME LOCK: free-roam clock was running at %s; set to %s and stopped.\r\n",
             before, after);
    if (++hold_logs <= 30) log_line(line);
}

static int still_ours(unsigned char *tod) {
    float now;
    memcpy(&now, tod + TOD_TIME, sizeof now);
    return tod == held_tod && tod[TOD_STOPPED] && !tod[TOD_HAS_TARGET] && now == held_time;
}

static unsigned char update_hook(unsigned char *tod, float dt, void *out, void *unused) {
    if (tod) {
        long version = config_version();
        int changed = version != applied_version;
        applied_version = version;
        if (tod != last_tod) {
            // A new world was loaded: forget the old clock, it no longer exists.
            last_tod = tod;
            held_tod = NULL;
        }
        if (config_enabled()) {
            if (tod[TOD_FIXED] || tod[TOD_STOP_LOCKED]) {
                if (blocked_logs < 3 && !tod[TOD_STOPPED]) {
                    ++blocked_logs;
                    log_line("TIME LOCK: the game has locked this clock itself (fixed-time or stop lock); leaving it alone.\r\n");
                }
            } else if (!tod[TOD_STOPPED] && !tod[TOD_HAS_TARGET]) {
                hold(tod, 0);  // free running: free roam
            } else if (changed && still_ours(tod)) {
                hold(tod, 1);
            }
        } else if (changed && still_ours(tod) && !tod[TOD_STOP_LOCKED]) {
            tod[TOD_STOPPED] = 0;  // what global.start_time would do
            held_tod = NULL;
            log_line("TIME LOCK: lock turned off; clock running again.\r\n");
        }
    }
    weather_hold_frame(tod);  // same thread, right after the world update ran the weather manager
    return original_update(tod, dt, out, unused);
}

static int matches(const unsigned char *image, size_t image_size, uint32_t rva, const unsigned char *bytes,
                   size_t length, const char *what, LogFn log) {
    if (rva + length > image_size || memcmp(image + rva, bytes, length)) {
        char line[200];
        snprintf(line, sizeof line, "TIME LOCK: unexpected game code at game+0x%x (%s); nothing patched.\r\n", rva, what);
        if (log) log(line);
        return 0;
    }
    return 1;
}

int clock_hold_verify(const unsigned char *image, size_t image_size, LogFn log) {
    return matches(image, image_size, TOD_UPDATE, update_prologue, sizeof update_prologue, "TimeOfDay::Update entry", log) &
           matches(image, image_size, TOD_ADVANCE, update_advance, sizeof update_advance, "TimeOfDay::Update advance", log) &
           matches(image, image_size, TOD_SET_TIME, set_time, sizeof set_time, "TimeOfDay::SetTime", log) &
           matches(image, image_size, TOD_GET_TIME, get_time, sizeof get_time, "TimeOfDay::GetTime", log) &
           matches(image, image_size, STOP_BEHAVIOUR + 25, stop_behaviour_tail, sizeof stop_behaviour_tail, "States::StopTimeOfDay", log);
}

int clock_hold_install(unsigned char *image, size_t image_size, LogFn log) {
    game = image;
    log_message = log;
    if (!clock_hold_verify(image, image_size, log)) return 0;
    original_update = (UpdateFn)entry_hook_install(image, image_size, TOD_UPDATE, update_prologue,
                                                   sizeof update_prologue, (void *)update_hook);
    if (!original_update) {
        log("TIME LOCK: could not hook TimeOfDay::Update (already hooked or busy); nothing patched.\r\n");
        return 0;
    }
    log("TIME LOCK: hooked TimeOfDay::Update at game+0x197b170.\r\n");
    return 1;
}
