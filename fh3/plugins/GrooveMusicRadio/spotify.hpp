// Spotify through Spotify Soloist, Spotify's own headless Spotify Connect player (needs Premium and a
// Soloist API key from developer.spotify.com/dashboard). The mod runs soloist.exe hidden if it's in the
// XboxMusicRadio folder; it shows up in the Spotify app as a device. Soloist only plays to a sound device,
// so the mod records its sound (and turns its own output far down in the volume mixer), and drives it
// through its local WebSocket API: now playing, next song, pause while the station is off.
#pragma once

#include "library.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

// Shown in Audio Options next to the folder playlists while Soloist runs.
inline constexpr wchar_t kSpotifyId[] = L"xmr:spotify";

struct SpotifyOptions {
    std::wstring dir;            // the folder with soloist.exe; its data (login), cache and log go in a "soloist" folder here
    std::wstring device;         // the device name in the Spotify app
    std::wstring api_key;        // the Soloist API key
    int volume          = 100;   // Soloist's starting volume (the Spotify app's slider)
    bool quiet_speakers = true;  // turn Soloist's own sound down to -80 dB so it's only heard on the radio
    bool auto_activate  = true;  // when the station switches to Spotify, move Spotify playback to Soloist and play
};

// Starts Soloist if soloist.exe is there and there's an API key, and adds Spotify to the station's live players.
// False (nothing started) otherwise.
bool spotify_start(const SpotifyOptions& opt, LogFn log);
// Soloist is running: the station can play it.
bool spotify_running();
// Soloist is playing (not paused or idle).
bool spotify_playing();
void spotify_next();
void spotify_pause();
void spotify_resume();
// The track Soloist reported last. False if nothing changed since the last call.
bool spotify_now_playing(std::wstring& title, std::wstring& artist);
