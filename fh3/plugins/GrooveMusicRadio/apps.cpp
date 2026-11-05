// Music apps as live sources. Each has a thread that, about four times a second:
//  - looks at Windows' media sessions (Windows.Media.Control, the media keys' API) for the app's: song, playing or
//    not, next / pause / play. Called through the raw WinRT interfaces (IDs and method order from the Windows SDK's
//    windows.media.control.h), as the MinGW headers don't have them. Apps without one (NetEase Cloud Music): the song
//    from the window title ("title - artist"), playing from the recorded sound, controls through the app's global
//    keyboard shortcuts.
//  - finds the app's process and records its whole process tree (capture.cpp): browsers and Chromium-based apps play
//    from helper processes that come and go.
//  - while the station plays it, keeps the app's own sound turned down in the volume mixer, and puts it back
//    afterwards. The game is ended rather than exiting, so a helper outside it (rundll32 running
//    XmrRestoreAfterGameW from this DLL) waits for the game to end and puts it back then; after anything worse, the
//    mod does it the next time it starts.
#include "apps.hpp"

#include "capture.hpp"

#include <windows.h>
#include <roapi.h>
#include <tlhelp32.h>
#include <winstring.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

bool contains(const std::wstring& s, const std::wstring& part) { return !part.empty() && s.find(part) != std::wstring::npos; }

// ---- Processes -----------------------------------------------------------------------------------------------------

struct Proc {
    DWORD parent;
    std::wstring exe;   // lower case
};
using ProcMap = std::unordered_map<DWORD, Proc>;

ProcMap processes() {
    ProcMap out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        out[pe.th32ProcessID] = {pe.th32ParentProcessID, lower(pe.szExeFile)};
    CloseHandle(snap);
    return out;
}

bool in_tree(const ProcMap& procs, DWORD pid, DWORD root) {
    for (int hops = 0; hops < 32 && pid; ++hops) {
        if (pid == root) return true;
        auto it = procs.find(pid);
        if (it == procs.end() || it->second.parent == pid) return false;
        pid = it->second.parent;
    }
    return false;
}

// The top of `pid`'s chain of parents running the same program: the app itself, not one of its helpers.
DWORD app_root(const ProcMap& procs, DWORD pid) {
    auto it = procs.find(pid);
    if (it == procs.end()) return 0;
    const std::wstring exe = it->second.exe;
    for (int hops = 0; hops < 32; ++hops) {
        auto up = procs.find(it->second.parent);
        if (up == procs.end() || up->second.exe != exe || up->first == pid) break;
        pid = up->first;
        it  = up;
    }
    return pid;
}


// The browser a media session's app id points at ("MSEdge", "Chrome._crx_..."), if it's one.
std::wstring browser_of(const std::wstring& app_id) {
    std::wstring a = lower(app_id);
    if (contains(a, L"msedge")) return L"msedge.exe";
    if (contains(a, L"chrome")) return L"chrome.exe";
    if (contains(a, L"brave")) return L"brave.exe";
    if (contains(a, L"vivaldi")) return L"vivaldi.exe";
    if (contains(a, L"opera")) return L"opera.exe";
    if (contains(a, L"firefox") || a == L"308046b0af4a39cb") return L"firefox.exe";
    return L"";
}

// ---- Keyboard shortcuts ("Ctrl+Alt+Right") ---------------------------------------------------------------------------

struct Chord {
    std::vector<WORD> mods;
    WORD key = 0;
};

Chord parse_chord(const std::wstring& text) {
    Chord c;
    size_t start = 0;
    while (start <= text.size()) {
        size_t plus = text.find(L'+', start);
        std::wstring part = lower(text.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start));
        start = plus == std::wstring::npos ? text.size() + 1 : plus + 1;
        if (part.empty()) continue;
        if (part == L"ctrl" || part == L"control") c.mods.push_back(VK_CONTROL);
        else if (part == L"alt") c.mods.push_back(VK_MENU);
        else if (part == L"shift") c.mods.push_back(VK_SHIFT);
        else if (part == L"win") c.mods.push_back(VK_LWIN);
        else if (part.size() == 1 && std::iswalnum(part[0])) c.key = static_cast<WORD>(std::towupper(part[0]));
        else if (part[0] == L'f' && part.size() <= 3 && std::iswdigit(part[1])) c.key = static_cast<WORD>(VK_F1 + _wtoi(part.c_str() + 1) - 1);
        else if (part == L"left") c.key = VK_LEFT;
        else if (part == L"right") c.key = VK_RIGHT;
        else if (part == L"up") c.key = VK_UP;
        else if (part == L"down") c.key = VK_DOWN;
        else if (part == L"space") c.key = VK_SPACE;
        else if (part == L"home") c.key = VK_HOME;
        else if (part == L"end") c.key = VK_END;
        else if (part == L"pageup") c.key = VK_PRIOR;
        else if (part == L"pagedown") c.key = VK_NEXT;
    }
    return c;
}

// A global shortcut goes to the program that registered it, whichever window has the keyboard.
void press(const Chord& c) {
    if (!c.key) return;
    std::vector<INPUT> in;
    auto key = [&](WORD vk, bool up) {
        INPUT i{};
        i.type       = INPUT_KEYBOARD;
        i.ki.wVk     = vk;
        i.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0) | (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN || vk == VK_HOME ||
                                                      vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT
                                                      ? KEYEVENTF_EXTENDEDKEY
                                                      : 0);
        in.push_back(i);
    };
    for (WORD m : c.mods) key(m, false);
    key(c.key, false);
    key(c.key, true);
    for (auto it = c.mods.rbegin(); it != c.mods.rend(); ++it) key(*it, true);
    SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
}

// The title of `root`'s main window: its largest visible top-level window with a title.
std::wstring window_title(DWORD root) {
    struct Find {
        DWORD pid;
        std::wstring title;
        LONG area = -1;
    } f{root, L"", -1};
    EnumWindows(
        [](HWND h, LPARAM p) -> BOOL {
            auto* f   = reinterpret_cast<Find*>(p);
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid != f->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
            wchar_t t[512];
            int n = GetWindowTextW(h, t, 512);
            RECT r{};
            GetWindowRect(h, &r);
            LONG area = (r.right - r.left) * (r.bottom - r.top);
            if (n > 0 && area > f->area) {
                f->title = std::wstring(t, static_cast<size_t>(n));
                f->area  = area;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&f));
    return f.title;
}

// ---- Raw WinRT ----------------------------------------------------------------------------------------------------

// Calls method `slot` of a COM / WinRT interface (IUnknown: 0-2, IInspectable: 3-5, the interface's own from 6).
template <typename... A>
HRESULT vcall(void* obj, int slot, A... args) {
    auto vt = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<HRESULT(STDMETHODCALLTYPE*)(void*, A...)>(vt[slot])(obj, args...);
}
void release(void* obj) {
    if (obj) (*reinterpret_cast<ULONG(STDMETHODCALLTYPE***)(void*)>(obj))[2](obj);
}

const IID kIID_IAsyncInfo     = {0x00000036, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID kIID_ManagerStatics = {0x2050C4EE, 0x11A0, 0x57DE, {0xAE, 0xD7, 0xC9, 0x7C, 0x70, 0x33, 0x82, 0x45}};
const wchar_t kManagerClass[] = L"Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager";

// Method slots, from windows.media.control.h.
enum {
    kStaticsRequestAsync       = 6,
    kManagerGetSessions        = 7,
    kVectorGetAt               = 6,
    kVectorSize                = 7,
    kSessionAppId              = 6,
    kSessionGetMediaPropsAsync = 7,
    kSessionGetPlaybackInfo    = 9,
    kSessionTryPlay            = 10,
    kSessionTryPause           = 11,
    kSessionTrySkipNext        = 16,
    kPropsTitle                = 6,
    kPropsArtist               = 9,
    kInfoPlaybackStatus        = 7,
    kAsyncGetResults           = 8,   // IAsyncOperation<T>
    kAsyncInfoStatus           = 7,   // IAsyncInfo
};
constexpr int kStatusPlaying = 4;     // GlobalSystemMediaTransportControlsSessionPlaybackStatus

std::wstring take(HSTRING h) {
    UINT32 n         = 0;
    const wchar_t* p = WindowsGetStringRawBuffer(h, &n);
    std::wstring s(p ? p : L"", n);
    if (h) WindowsDeleteString(h);
    return s;
}

// Waits for an IAsyncOperation<T> (up to `ms`) and returns its result; releases the operation.
void* await(void* op, DWORD ms) {
    if (!op) return nullptr;
    void* info   = nullptr;
    void* result = nullptr;
    if (SUCCEEDED(vcall(op, 0, &kIID_IAsyncInfo, &info)) && info) {
        int status   = 0;
        ULONGLONG t0 = GetTickCount64();
        while (SUCCEEDED(vcall(info, kAsyncInfoStatus, &status)) && status == 0 && GetTickCount64() - t0 < ms) Sleep(5);
        if (status == 1) vcall(op, kAsyncGetResults, &result);
        release(info);
    }
    release(op);
    return result;
}

void* session_manager() {
    HSTRING cls   = nullptr;
    void* statics = nullptr;
    void* op      = nullptr;
    if (FAILED(WindowsCreateString(kManagerClass, static_cast<UINT32>(wcslen(kManagerClass)), &cls))) return nullptr;
    HRESULT hr = RoGetActivationFactory(cls, kIID_ManagerStatics, &statics);
    WindowsDeleteString(cls);
    if (FAILED(hr) || !statics) return nullptr;
    if (FAILED(vcall(statics, kStaticsRequestAsync, &op))) op = nullptr;
    release(statics);
    return await(op, 5000);
}

// ---- Putting an app's sound back -----------------------------------------------------------------------------------

constexpr float kQuietLevel = 0.0001f;   // -80 dB on the speakers; the recording is turned back up 10000x

void note_quiet(const std::wstring& ini, const std::wstring& section, const std::wstring& exe, float level, bool muted) {
    WritePrivateProfileStringW(section.c_str(), L"restore_app", exe.c_str(), ini.c_str());
    WritePrivateProfileStringW(section.c_str(), L"restore_level", std::to_wstring(level).c_str(), ini.c_str());
    WritePrivateProfileStringW(section.c_str(), L"restore_muted", muted ? L"1" : L"0", ini.c_str());
}

void clear_quiet_note(const std::wstring& ini, const std::wstring& section) {
    WritePrivateProfileStringW(section.c_str(), L"restore_app", nullptr, ini.c_str());
    WritePrivateProfileStringW(section.c_str(), L"restore_level", nullptr, ini.c_str());
    WritePrivateProfileStringW(section.c_str(), L"restore_muted", nullptr, ini.c_str());
}

enum class Restore { NothingNoted, Restored, NotRunning };

// Puts an app the ini notes as turned down (under `section`) back to its noted volume, if it's running, and clears
// the note.
Restore restore_from_note(const std::wstring& ini, const std::wstring& section, std::wstring* exe_out) {
    wchar_t exe[64] = {}, level[32] = {}, muted[8] = {};
    GetPrivateProfileStringW(section.c_str(), L"restore_app", L"", exe, 64, ini.c_str());
    if (!exe[0]) return Restore::NothingNoted;
    GetPrivateProfileStringW(section.c_str(), L"restore_level", L"1", level, 32, ini.c_str());
    GetPrivateProfileStringW(section.c_str(), L"restore_muted", L"0", muted, 8, ini.c_str());
    std::wstring name = lower(exe);
    if (exe_out) *exe_out = name;
    ProcMap procs = processes();
    int n         = set_output_volume(
        [&](DWORD pid) {
            auto it = procs.find(pid);
            return it != procs.end() && it->second.exe == name;
        },
        static_cast<float>(_wtof(level)), muted[0] == L'1');
    if (n == 0) return Restore::NotRunning;
    clear_quiet_note(ini, section);
    return Restore::Restored;
}

// Every section of the ini with a note.
void restore_all_notes(const std::wstring& ini) {
    std::vector<wchar_t> names(4096);
    DWORD n = GetPrivateProfileSectionNamesW(names.data(), static_cast<DWORD>(names.size()), ini.c_str());
    for (const wchar_t* s = names.data(); s < names.data() + n && *s; s += wcslen(s) + 1) restore_from_note(ini, s, nullptr);
}

// ---- The player --------------------------------------------------------------------------------------------------------

class AppPlayer final : public LivePlayer {
public:
    AppPlayer(const AppOptions& opt, LogFn log)
        : opt_(opt), log_(std::move(log)), play_pause_(parse_chord(opt.play_pause_key)), next_(parse_chord(opt.next_key)) {}

    const wchar_t* id() const override { return opt_.id.c_str(); }
    const wchar_t* name() const override { return opt_.name.c_str(); }
    bool listed() override {
        if (opt_.always_listed || found_) return true;
        if (!opt_.install_path.empty() && GetFileAttributesW(opt_.install_path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        ProcMap procs = processes();
        return std::any_of(procs.begin(), procs.end(), [&](const auto& p) { return wants_exe(p.second.exe); });
    }
    bool available() override { return found_; }
    bool wait_when_missing() const override { return true; }
    const char* missing_hint() const override { return opt_.missing_hint.c_str(); }
    bool playing() override {
        if (!found_) return false;
        if (has_session_) return session_playing_;
        return GetTickCount64() - last_sound_.load() < 1500;   // no media session: going by the recorded sound
    }
    void next() override { cmd_ = kCmdNext; }
    void pause() override { cmd_ = kCmdPause; }
    void resume() override { cmd_ = kCmdPlay; }
    bool now_playing(std::wstring& title, std::wstring& artist) override {
        std::lock_guard<std::mutex> lk(np_mx_);
        if (!np_changed_) return false;
        np_changed_ = false;
        title       = title_;
        artist      = artist_;
        return true;
    }
    void set_live(bool live) override { live_ = live; }

    void start() {
        if (HANDLE t = CreateThread(nullptr, 0, [](LPVOID self) -> DWORD { static_cast<AppPlayer*>(self)->run(); return 0; }, this, 0, nullptr))
            CloseHandle(t);
    }

private:
    enum { kCmdNone, kCmdNext, kCmdPause, kCmdPlay };

    AppOptions opt_;
    LogFn log_;
    Chord play_pause_, next_;

    std::atomic<bool> found_{false}, has_session_{false}, session_playing_{false}, live_{false};
    std::atomic<int> cmd_{kCmdNone};
    std::atomic<float> gain_{1.0f};
    std::atomic<ULONGLONG> last_sound_{0};

    std::mutex np_mx_;
    std::wstring title_, artist_;
    bool np_changed_ = false;

    DWORD root_ = 0;   // the app's process being recorded
    std::wstring root_exe_;
    bool quiet_         = false;
    float saved_level_  = 1.0f;
    bool saved_muted_   = false;

    void logf(const char* fmt, ...) {
        char body[900];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(body, sizeof body, fmt, ap);
        va_end(ap);
        if (log_) log_((narrow(opt_.name) + ": " + body).c_str());
    }

    bool wants_exe(const std::wstring& exe) const {
        return std::any_of(opt_.exes.begin(), opt_.exes.end(), [&](const std::wstring& e) { return exe == e; });
    }

    // How sure we are that a media session is this app's: its own app id 3, a browser (YouTube Music in any tab) 1.
    int score(const std::wstring& app_id) const {
        std::wstring a = lower(app_id);
        for (const auto& part : opt_.app_ids)
            if (contains(a, part)) return 3;
        if (opt_.browser && !browser_of(app_id).empty() && wants_exe(browser_of(app_id))) return 1;
        return 0;
    }

    void set_song(const std::wstring& title, const std::wstring& artist) {
        std::lock_guard<std::mutex> lk(np_mx_);
        if (title.empty() || (title == title_ && artist == artist_)) return;
        title_      = title;
        artist_     = artist;
        np_changed_ = true;
    }

    // The app's process to record: the browser of its media session, else one of its programs (one playing sound first).
    DWORD find_app(const ProcMap& procs, const std::wstring& session_app) {
        std::wstring browser = opt_.browser ? browser_of(session_app) : L"";
        auto ok              = [&](const std::wstring& exe) { return browser.empty() ? wants_exe(exe) : exe == browser; };
        DWORD best = 0;
        bool best_active = false;
        for (const auto& s : list_output_sessions()) {
            auto it = procs.find(s.pid);
            if (it == procs.end() || !ok(it->second.exe)) continue;
            if (!best || (s.active && !best_active)) {
                best        = s.pid;
                best_active = s.active;
            }
        }
        if (best) return app_root(procs, best);
        if (opt_.browser && browser.empty()) return 0;   // which browser, then? wait for the media session
        for (const auto& [pid, p] : procs)               // no sound yet: the app itself
            if (ok(p.exe)) return app_root(procs, pid);
        return 0;
    }

    // Puts the recorded app's sound back as it was.
    void unquiet() {
        if (!quiet_) return;
        gain_         = 1.0f;
        ProcMap procs = processes();
        DWORD root    = root_;
        set_output_volume([&](DWORD pid) { return in_tree(procs, pid, root); }, saved_level_, saved_muted_);
        quiet_ = false;
        clear_quiet_note(opt_.ini, opt_.section);
        logf("%s's own sound is back", narrow(root_exe_).c_str());
    }

    // While live: turn the app down (saving its level first), and keep new sound sessions of it down too.
    void manage_volume(const ProcMap& procs) {
        if (!(live_ && opt_.quiet_speakers && root_)) {
            unquiet();
            return;
        }
        DWORD root = root_;
        auto mine  = [&](DWORD pid) { return in_tree(procs, pid, root); };
        if (!quiet_) {
            bool any = false;
            for (const auto& s : list_output_sessions())
                if (mine(s.pid) && s.level > kQuietLevel * 2) {   // not one the mod left turned down
                    saved_level_ = s.level;
                    saved_muted_ = s.muted;
                    any          = true;
                    break;
                }
            if (!any) return;   // it has no sound session yet
            note_quiet(opt_.ini, opt_.section, root_exe_, saved_level_, saved_muted_);
            set_output_volume(mine, kQuietLevel, false);
            Sleep(300);   // let the louder audio already recorded drain first
            gain_  = 1.0f / kQuietLevel;
            quiet_ = true;
            logf("%s's own sound is turned down to -80 dB while the radio plays it", narrow(root_exe_).c_str());
            return;
        }
        for (const auto& s : list_output_sessions())   // a helper process that started since
            if (mine(s.pid) && (s.level > kQuietLevel * 2 || s.muted))
                set_output_volume([&](DWORD pid) { return pid == s.pid; }, kQuietLevel, false);
    }

    void run() {
        RoInitialize(RO_INIT_MULTITHREADED);
        void* manager = session_manager();
        if (!manager) logf("Windows' media sessions aren't available: going by the window title");
        void* session = nullptr;
        std::wstring session_app;
        volatile bool* stop_capture = nullptr;
        ULONGLONG last_refresh = 0, last_find = 0;
        bool restored = false;
        for (;;) {
            Sleep(250);
            ULONGLONG now = GetTickCount64();

            if (int cmd = cmd_.exchange(kCmdNone)) {
                if (session) {
                    void* op = nullptr;
                    int slot = cmd == kCmdNext ? kSessionTrySkipNext : cmd == kCmdPause ? kSessionTryPause : kSessionTryPlay;
                    if (SUCCEEDED(vcall(session, slot, &op))) release(op);   // fire and forget
                } else if (cmd == kCmdNext) {
                    press(next_);
                } else if ((cmd == kCmdPause) == playing()) {   // a play/pause toggle: only when it changes something
                    press(play_pause_);
                }
                last_refresh = 0;
            }
            if (now - last_refresh < 1000) continue;
            last_refresh = now;

            // Its media session: the best match, preferring one that plays.
            void* best = nullptr;
            std::wstring best_app, all;
            int best_score    = 0;
            bool best_playing = false;
            void* sessions    = nullptr;
            if (manager && SUCCEEDED(vcall(manager, kManagerGetSessions, &sessions)) && sessions) {
                UINT32 n = 0;
                vcall(sessions, kVectorSize, &n);
                for (UINT32 i = 0; i < n && i < 32; ++i) {
                    void* s = nullptr;
                    if (FAILED(vcall(sessions, kVectorGetAt, i, &s)) || !s) continue;
                    HSTRING h = nullptr;
                    vcall(s, kSessionAppId, &h);
                    std::wstring app = take(h);
                    void* info       = nullptr;
                    int status       = 0;
                    if (SUCCEEDED(vcall(s, kSessionGetPlaybackInfo, &info)) && info) {
                        vcall(info, kInfoPlaybackStatus, &status);
                        release(info);
                    }
                    bool playing = status == kStatusPlaying;
                    all += (all.empty() ? L"" : L", ") + app + L" (" + std::to_wstring(status) + L")";
                    int sc = score(app);
                    if (sc > 0 && (sc > best_score || (sc == best_score && playing && !best_playing))) {
                        release(best);
                        best         = s;
                        best_app     = app;
                        best_score   = sc;
                        best_playing = playing;
                    } else {
                        release(s);
                    }
                }
                release(sessions);
            }
            static std::mutex trace_mx;
            static std::wstring last_all;
            if (opt_.trace) {
                std::lock_guard<std::mutex> lk(trace_mx);   // all players see the same list: log it once
                if (all != last_all) log_(("trace: media sessions: " + (all.empty() ? std::string("(none)") : narrow(all))).c_str());
                last_all = all;
            }
            if (best_app != session_app) {
                if (!best_app.empty()) logf("following the media session of \"%s\"", narrow(best_app).c_str());
                else if (!session_app.empty()) logf("its media session is gone");
            }
            release(session);
            session          = best;
            session_app      = best_app;
            has_session_     = session != nullptr;
            session_playing_ = best_playing;

            // The app's process (looked for every few seconds, and whenever it's gone).
            ProcMap procs = processes();
            if (!restored) {   // after a crash: put back what the mod left turned down, once the app is running
                std::wstring exe;
                Restore r = restore_from_note(opt_.ini, opt_.section, &exe);
                if (r != Restore::NotRunning) restored = true;
                if (r == Restore::Restored) logf("put %s's own sound back (the game closed while it was turned down)", narrow(exe).c_str());
            }
            bool root_gone = root_ && procs.find(root_) == procs.end();
            if (root_gone || !root_ || now - last_find > 5000) {
                last_find  = now;
                DWORD root = find_app(procs, session_app);
                if (root_gone && !root) {
                    unquiet();
                    root_ = 0;
                    logf("closed");
                }
                if (root && root != root_) {
                    unquiet();
                    root_     = root;
                    root_exe_ = procs[root].exe;
                    if (stop_capture) *stop_capture = true;   // the old recording ends; its flag is left behind
                    stop_capture = new bool(false);
                    if (capture_process(
                            root, &gain_,
                            [this](const int16_t* s, size_t frames) {
                                if (s)
                                    for (size_t i = 0; i < frames * 2; i += 64)   // any sound: it's playing
                                        if (s[i] > 64 || s[i] < -64) {
                                            last_sound_ = GetTickCount64();
                                            break;
                                        }
                                live_audio(this, s, frames);
                            },
                            log_, stop_capture))
                        logf("recording %s (process %lu)", narrow(root_exe_).c_str(), root);
                }
            }
            found_ = session != nullptr || (!opt_.browser && root_ != 0);

            // The song: from the media session, else the window title ("title - artist").
            if (session) {
                void* op    = nullptr;
                void* props = SUCCEEDED(vcall(session, kSessionGetMediaPropsAsync, &op)) ? await(op, 1000) : nullptr;
                if (props) {
                    HSTRING t = nullptr, a = nullptr;
                    vcall(props, kPropsTitle, &t);
                    vcall(props, kPropsArtist, &a);
                    std::wstring title = take(t), artist = take(a);
                    release(props);
                    set_song(title, artist);
                }
            } else if (root_) {
                std::wstring t = window_title(root_);
                size_t dash    = t.rfind(L" - ");
                if (dash != std::wstring::npos) set_song(t.substr(0, dash), t.substr(dash + 3));
            }
            manage_volume(procs);
        }
    }
};

} // namespace

void app_start(const AppOptions& opt, LogFn log) {
    auto* p = new AppPlayer(opt, std::move(log));   // lives as long as the game
    live_register(p);
    p->start();
}

// The game can end without any of its code running (it's ended, not exited), so a helper outside it puts the apps
// back: rundll32 running XmrRestoreAfterGameW from this DLL, which waits for the game to end.
void apps_start_restore_watcher(const std::wstring& ini, LogFn log) {
    HMODULE self = nullptr;
    wchar_t dll[MAX_PATH] = {}, sys[MAX_PATH] = {};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&apps_start_restore_watcher), &self) ||
        !GetModuleFileNameW(self, dll, MAX_PATH) || !GetSystemDirectoryW(sys, MAX_PATH)) {
        log("music apps: couldn't start the volume watcher");
        return;
    }
    std::wstring exe = std::wstring(sys) + L"\\rundll32.exe";
    std::wstring cmd = L"\"" + exe + L"\" \"" + dll + L"\",XmrRestoreAfterGame " + std::to_wstring(GetCurrentProcessId()) + L" " + ini;
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    // Out of any job the game is in, or it would be ended with the game.
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, nullptr, nullptr, &si, &pi);
    if (!ok) ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) {
        log(("music apps: couldn't start the volume watcher (error " + std::to_string(GetLastError()) + ")").c_str());
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

// rundll32 "XboxMusicRadio.dll",XmrRestoreAfterGame <game process id> <ini path>: waits for the game to end, then puts
// back every app the ini notes as turned down (one that isn't running then gets it back the next time the mod starts).
extern "C" __declspec(dllexport) void CALLBACK XmrRestoreAfterGameW(HWND, HINSTANCE, LPWSTR args, int) {
    wchar_t* rest = nullptr;
    DWORD pid     = std::wcstoul(args, &rest, 10);
    while (rest && *rest == L' ') ++rest;
    if (!pid || !rest || !*rest) return;
    std::wstring ini = rest;
    HANDLE game      = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!game) return;
    WaitForSingleObject(game, INFINITE);
    CloseHandle(game);
    restore_all_notes(ini);
}
