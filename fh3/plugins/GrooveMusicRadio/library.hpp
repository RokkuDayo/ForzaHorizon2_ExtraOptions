// The music library: a folder whose subfolders are playlists.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct Song {
    std::wstring path;     // full path, handed to the game as the song's URL
    std::wstring title;    // tag, else the file name ("Artist - Title" is split)
    std::wstring artist;   // tag, else from the file name, else empty
    uint64_t     mtime = 0;
};

struct Playlist {
    std::wstring id;       // stable across launches: the game saves the default playlist's id
    std::wstring name;
    std::vector<uint32_t> songs;   // indices into the song table
};

// With more than one playlist, the first one is every song together.
inline constexpr wchar_t kAllMusicId[] = L"xmr:*";

struct LibraryOptions {
    std::wstring folder;
    bool read_tags = true;
};

using LogFn = std::function<void(const char*)>;

void library_start(const LibraryOptions& opt, LogFn log);
// Ask for a fresh scan; returns the generation that will include it.
uint32_t library_rescan();
// The generation of the newest finished scan (0 = none yet).
uint32_t library_done();

// Copies out of the current snapshot, under the library's lock.
std::vector<Playlist> library_playlists();
bool library_playlist(const std::wstring& id, Playlist& out);
bool library_song(uint32_t index, Song& out);
std::wstring library_summary();
