// Music apps on this PC as live sources: YouTube Music (its web player in a browser), NetEase Cloud Music and
// QQ Music (their desktop apps). The mod records the app's sound for the station and, while the station plays it,
// turns the app's own sound down. Song names and controls come from Windows' media controls when the app reports
// to them (the media keys' API), otherwise from its window title and its global keyboard shortcuts.
#pragma once

#include "library.hpp"
#include "live.hpp"

#include <string>
#include <vector>

struct AppOptions {
    std::wstring id;                    // playlist id ("xmr:netease")
    std::wstring name;                  // "NetEase Cloud Music"
    std::wstring section;               // its ini section ("netease"); the note to put its sound back goes there too
    std::vector<std::wstring> exes;     // the programs that play it, lower case ("cloudmusic.exe"); a browser list for YouTube
    std::vector<std::wstring> app_ids;  // pieces of its media session's app id, lower case ("cloudmusic")
    bool browser          = false;      // played in a browser: any browser's media session may be it
    bool always_listed    = false;      // offered in Audio Options even when it isn't installed or running
    std::wstring install_path;          // where the app usually is, to offer it when it's installed but not running
    std::wstring play_pause_key;        // its global shortcut ("Ctrl+Alt+P"), for when it has no media session
    std::wstring next_key;              // ("Ctrl+Alt+Right")
    std::string missing_hint;           // "start NetEase Cloud Music and play something"
    bool quiet_speakers = true;
    bool trace          = false;
    std::wstring ini;
};

// Makes the player, registers it with the station and starts its thread.
void app_start(const AppOptions& opt, LogFn log);
// Starts the helper that, when the game ends, puts back any app sound the mod turned down (once per game).
void apps_start_restore_watcher(const std::wstring& ini, LogFn log);
