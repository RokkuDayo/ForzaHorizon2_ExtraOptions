// The music library. A background thread scans the music folder whenever the
// game asks for its playlists, so files added while the game runs show up the
// next time the playlist list opens. Songs are only ever added to the song
// table, never removed, so a song id the game holds stays valid.
#include "library.hpp"

#include <windows.h>
#include <shobjidl.h>
#include <propsys.h>

#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cwctype>
#include <mutex>
#include <unordered_map>

#ifndef SORT_DIGITSASNUMBERS
#define SORT_DIGITSASNUMBERS 0x00000008
#endif

namespace {

LibraryOptions g_opt;
LogFn g_log;

std::mutex g_mx;
std::condition_variable g_cv;
uint32_t g_wanted = 0;   // newest generation asked for
uint32_t g_done   = 0;   // newest generation finished

std::vector<Song> g_songs;                        // append-only
std::unordered_map<std::wstring, uint32_t> g_index;   // lower-case path -> song
std::vector<Playlist> g_lists;

// PKEY_Title and PKEY_Music_Artist (propkey.h), spelled out so no GUID library is needed.
const PROPERTYKEY kTitle  = {{0xF29F85E0, 0x4FF9, 0x1068, {0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9}}, 2};
const PROPERTYKEY kArtist = {{0x56A3372E, 0xCE9C, 0x11D2, {0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6}}, 2};

// What the radio's decoders (dr_mp3, dr_flac, dr_wav) play.
const wchar_t* const kAudio[] = {L".mp3", L".flac", L".wav"};

std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

void logf(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (g_log) g_log(line);
}

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool is_audio(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = lower(name.substr(dot));
    for (const wchar_t* a : kAudio)
        if (ext == a) return true;
    return false;
}

// "Track 2" before "Track 10", ignoring case, as Explorer sorts.
bool natural_less(const std::wstring& a, const std::wstring& b) {
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), static_cast<int>(a.size()),
                           b.c_str(), static_cast<int>(b.size()), nullptr, nullptr, 0) == CSTR_LESS_THAN;
}

struct FileEntry {
    std::wstring path;
    uint64_t mtime;
};

void list_files(const std::wstring& dir, bool recurse, std::vector<FileEntry>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> subdirs;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (recurse) subdirs.push_back(dir + L"\\" + name);
        } else if (is_audio(name)) {
            uint64_t t = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
            out.push_back({dir + L"\\" + name, t});
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(subdirs.begin(), subdirs.end(), natural_less);
    for (const auto& d : subdirs) list_files(d, true, out);
}

std::vector<std::wstring> list_dirs(const std::wstring& dir) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), natural_less);
    return out;
}

std::wstring prop_text(IPropertyStore* ps, const PROPERTYKEY& key) {
    PROPVARIANT v;
    PropVariantInit(&v);
    std::wstring out;
    if (SUCCEEDED(ps->GetValue(key, &v))) {
        if (v.vt == VT_LPWSTR && v.pwszVal) {
            out = v.pwszVal;
        } else if (v.vt == (VT_VECTOR | VT_LPWSTR)) {
            for (ULONG i = 0; i < v.calpwstr.cElems; i++) {
                if (!v.calpwstr.pElems[i]) continue;
                if (!out.empty()) out += L"; ";
                out += v.calpwstr.pElems[i];
            }
        }
    }
    PropVariantClear(&v);
    return out;
}

void read_tags(const std::wstring& path, std::wstring& title, std::wstring& artist) {
    IPropertyStore* ps = nullptr;
    if (FAILED(SHGetPropertyStoreFromParsingName(path.c_str(), nullptr, GPS_DEFAULT, IID_IPropertyStore, reinterpret_cast<void**>(&ps))) || !ps)
        return;
    title  = prop_text(ps, kTitle);
    artist = prop_text(ps, kArtist);
    ps->Release();
}

// No tags: "Artist - Title.mp3", or "01 - Title.mp3", or just the name.
void from_file_name(const std::wstring& path, std::wstring& title, std::wstring& artist) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring stem = path.substr(slash == std::wstring::npos ? 0 : slash + 1);
    size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos) stem.resize(dot);
    size_t digits = 0;
    while (digits < stem.size() && std::iswdigit(stem[digits])) digits++;
    if (digits > 0 && digits <= 3 && digits < stem.size()) {   // a track number in front
        size_t i = digits;
        while (i < stem.size() && (stem[i] == L' ' || stem[i] == L'.' || stem[i] == L'-' || stem[i] == L'_')) i++;
        if (i > digits && i < stem.size()) stem = stem.substr(i);
    }
    size_t sep = stem.find(L" - ");
    if (sep != std::wstring::npos && artist.empty()) {
        artist = stem.substr(0, sep);
        stem   = stem.substr(sep + 3);
    }
    if (title.empty()) title = stem;
}

std::wstring folder_name(const std::wstring& dir) {
    std::wstring d = dir;
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    size_t slash = d.find_last_of(L"\\/");
    return slash == std::wstring::npos ? d : d.substr(slash + 1);
}

// Index of the song at `f`, adding it or refreshing its tags if the file changed.
uint32_t song_for(const FileEntry& f, int& read) {
    std::wstring key = lower(f.path);
    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_index.find(key);
        if (it != g_index.end() && g_songs[it->second].mtime == f.mtime) return it->second;
    }
    Song s;
    s.path  = f.path;
    s.mtime = f.mtime;
    if (g_opt.read_tags) {
        read_tags(f.path, s.title, s.artist);
        read++;
    }
    if (s.title.empty() || s.artist.empty()) {
        std::wstring t, a = s.artist;
        from_file_name(f.path, t, a);
        if (s.title.empty()) s.title = t;
        s.artist = a;
    }
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_index.find(key);
    if (it != g_index.end()) {
        g_songs[it->second] = std::move(s);
        return it->second;
    }
    g_songs.push_back(std::move(s));
    uint32_t idx = static_cast<uint32_t>(g_songs.size() - 1);
    g_index.emplace(std::move(key), idx);
    return idx;
}

void scan() {
    DWORD t0 = GetTickCount();
    const std::wstring& root = g_opt.folder;
    std::vector<Playlist> lists;
    int read = 0;
    size_t songs = 0;

    auto add = [&](const std::wstring& rel, const std::wstring& name, const std::vector<FileEntry>& files) {
        if (files.empty()) return;
        Playlist p;
        p.id   = L"xmr:" + (rel.empty() ? std::wstring(L"/") : rel);
        if (p.id.size() > 240) p.id.resize(240);   // the game keeps 250 characters
        p.name = name;
        for (const auto& f : files) p.songs.push_back(song_for(f, read));
        songs += p.songs.size();
        lists.push_back(std::move(p));
    };

    std::vector<FileEntry> loose;   // files right in the music folder: a playlist named after it
    list_files(root, false, loose);
    std::sort(loose.begin(), loose.end(), [](const FileEntry& a, const FileEntry& b) { return natural_less(a.path, b.path); });
    add(L"", folder_name(root), loose);
    for (const auto& d : list_dirs(root)) {
        std::vector<FileEntry> files;
        list_files(root + L"\\" + d, true, files);
        add(d, d, files);
    }

    if (lists.size() > 1) {   // every song, first in the list: what the station plays until a playlist is chosen
        Playlist all;
        all.id   = kAllMusicId;
        all.name = L"All music";
        for (const auto& p : lists) all.songs.insert(all.songs.end(), p.songs.begin(), p.songs.end());
        lists.insert(lists.begin(), std::move(all));
    }
    size_t count = lists.size();
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_lists = std::move(lists);
    }
    logf("scanned %s: %zu playlist(s), %zu song(s), %d tag read(s), %lu ms", narrow(root).c_str(), count, songs, read,
         GetTickCount() - t0);
}

DWORD WINAPI scan_thread(LPVOID) {
    // The shell's property handlers (tags) are COM objects.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        uint32_t target;
        {
            std::unique_lock<std::mutex> lk(g_mx);
            g_cv.wait(lk, [] { return g_wanted > g_done; });
            target = g_wanted;
        }
        scan();
        {
            std::lock_guard<std::mutex> lk(g_mx);
            g_done = target;
        }
        g_cv.notify_all();
    }
}

} // namespace

void library_start(const LibraryOptions& opt, LogFn log) {
    g_opt = opt;
    g_log = std::move(log);
    library_rescan();
    if (HANDLE t = CreateThread(nullptr, 0, scan_thread, nullptr, 0, nullptr)) CloseHandle(t);
}

uint32_t library_rescan() {
    uint32_t gen;
    {
        // Requests that arrive while a scan runs share the one after it: the
        // thread takes the newest generation when it starts a scan.
        std::lock_guard<std::mutex> lk(g_mx);
        gen = ++g_wanted;
    }
    g_cv.notify_all();
    return gen;
}

uint32_t library_done() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_done;
}

std::vector<Playlist> library_playlists() {
    std::lock_guard<std::mutex> lk(g_mx);
    return g_lists;
}

bool library_playlist(const std::wstring& id, Playlist& out) {
    std::lock_guard<std::mutex> lk(g_mx);
    for (const auto& p : g_lists)
        if (p.id == id) {
            out = p;
            return true;
        }
    return false;
}

bool library_song(uint32_t index, Song& out) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (index >= g_songs.size()) return false;
    out = g_songs[index];
    return true;
}

std::wstring library_summary() {
    std::lock_guard<std::mutex> lk(g_mx);
    size_t songs = 0;
    for (const auto& p : g_lists) songs += p.songs.size();
    wchar_t buf[512];
    std::swprintf(buf, 512, L"%zu playlist(s), %zu song(s) in %ls (scan %u of %u done)", g_lists.size(), songs, g_opt.folder.c_str(), g_done,
                  g_wanted);
    return buf;
}
