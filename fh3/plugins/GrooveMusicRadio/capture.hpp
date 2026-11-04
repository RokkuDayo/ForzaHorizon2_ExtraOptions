// Records one program's sound (WASAPI process loopback, Windows 10 2004+): Soloist plays to a sound
// device only, so its audio reaches the station this way.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// 44.1 kHz stereo S16, interleaved; `frames` stereo pairs. nullptr = that many frames of silence.
using CaptureSink = std::function<void(const int16_t* samples, size_t frames)>;
using CaptureLog  = std::function<void(const char*)>;

// Records `pid` and its child processes on a thread of its own until stop is set, times `*gain` (to undo
// set_process_output_volume; read for every packet). False if Windows can't.
bool capture_process(DWORD pid, const std::atomic<float>* gain, CaptureSink sink, CaptureLog log, const volatile bool* stop);
// Sets `pid`'s volume on the default output device (the volume mixer's slider, 0..1). The recording gets the
// same volume (a muted program records as silence), so a program is turned far down instead of muted.
// True once a session was set; a program only has one after it first plays, so call again until it's true.
bool set_process_output_volume(DWORD pid, float level);

// The volume mixer's entries on the default output device.
struct OutputSession {
    DWORD pid;
    bool active;   // playing right now
    float level;
    bool muted;
};
std::vector<OutputSession> list_output_sessions();
// Sets level and mute on every entry whose process `which` accepts. Returns how many.
int set_output_volume(const std::function<bool(DWORD pid)>& which, float level, bool muted);
