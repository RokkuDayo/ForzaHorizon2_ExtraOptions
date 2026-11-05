// Spotify through Spotify Soloist. Three threads: the watcher (starts soloist.exe, keeps its sound turned
// down in the volume mixer, notices when it exits), the recorder (capture.cpp: its sound -> the station)
// and the control connection (Soloist's WebSocket API: events in, commands out).
#include "spotify.hpp"

#include "capture.hpp"
#include "live.hpp"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

SpotifyOptions g_opt;
LogFn g_log;
std::wstring g_data;   // Soloist's data folder: its login, and ws.port
HANDLE g_proc = nullptr;
DWORD g_pid   = 0;
std::atomic<bool> g_running{false};
std::atomic<bool> g_playing{false};
std::atomic<bool> g_logged_in{false};
std::atomic<bool> g_active{false};      // Soloist is the active Spotify Connect device
std::atomic<bool> g_play_on_active{false};   // auto_activate asked for it: play once it's active
volatile bool g_stop_capture = false;
std::atomic<float> g_gain{1.0f};   // undoes the volume mixer setting on the recording

constexpr float kQuietLevel = 0.0001f;   // -80 dB on the speakers; the recording is turned back up 10000x

std::mutex g_np_mx;
std::wstring g_title, g_artist;
bool g_np_changed = false;

std::mutex g_ws_mx;   // one sender at a time (receiving runs alongside)
HINTERNET g_ws = nullptr;

void logf(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (g_log) g_log(line);
}

std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring unquoted(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s)
        if (c != L'"') out += c;
    return out;
}

// ---- Just enough JSON for Soloist's events ------------------------------------------------------------------

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    bool b   = false;
    double n = 0;
    std::string s;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json& operator[](const char* key) const {
        static const Json none;
        for (const auto& m : members)
            if (m.first == key) return m.second;
        return none;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : p_(text.c_str()), end_(text.c_str() + text.size()) {}
    bool parse(Json& out) { return value(out, 0); }

private:
    const char* p_;
    const char* end_;

    void ws() {
        while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r')) ++p_;
    }
    bool literal(const char* word) {
        size_t n = std::strlen(word);
        if (static_cast<size_t>(end_ - p_) < n || std::strncmp(p_, word, n) != 0) return false;
        p_ += n;
        return true;
    }
    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) out += static_cast<char>(0xC0 | (cp >> 6)), out += static_cast<char>(0x80 | (cp & 0x3F));
        else if (cp < 0x10000)
            out += static_cast<char>(0xE0 | (cp >> 12)), out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)),
                out += static_cast<char>(0x80 | (cp & 0x3F));
        else
            out += static_cast<char>(0xF0 | (cp >> 18)), out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    bool hex4(unsigned& v) {
        if (end_ - p_ < 4) return false;
        v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = *p_++;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (p_ >= end_ || *p_ != '"') return false;
        ++p_;
        while (p_ < end_ && *p_ != '"') {
            char c = *p_++;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p_ >= end_) return false;
            char e = *p_++;
            switch (e) {
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && end_ - p_ >= 6 && p_[0] == '\\' && p_[1] == 'u') {   // surrogate pair
                    p_ += 2;
                    unsigned lo = 0;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: out += e; break;   // \" \\ \/
            }
        }
        if (p_ >= end_) return false;
        ++p_;
        return true;
    }
    bool value(Json& out, int depth) {
        if (depth > 64) return false;
        ws();
        if (p_ >= end_) return false;
        char c = *p_;
        if (c == '{') {
            ++p_;
            out.type = Json::Object;
            ws();
            if (p_ < end_ && *p_ == '}') return ++p_, true;
            for (;;) {
                std::string key;
                ws();
                if (!string(key)) return false;
                ws();
                if (p_ >= end_ || *p_++ != ':') return false;
                out.members.emplace_back(std::move(key), Json{});
                if (!value(out.members.back().second, depth + 1)) return false;
                ws();
                if (p_ < end_ && *p_ == ',') {
                    ++p_;
                    continue;
                }
                return p_ < end_ && *p_++ == '}';
            }
        }
        if (c == '[') {
            ++p_;
            out.type = Json::Array;
            ws();
            if (p_ < end_ && *p_ == ']') return ++p_, true;
            for (;;) {
                out.items.emplace_back();
                if (!value(out.items.back(), depth + 1)) return false;
                ws();
                if (p_ < end_ && *p_ == ',') {
                    ++p_;
                    continue;
                }
                return p_ < end_ && *p_++ == ']';
            }
        }
        if (c == '"') {
            out.type = Json::String;
            return string(out.s);
        }
        if (literal("true")) return out.type = Json::Bool, out.b = true, true;
        if (literal("false")) return out.type = Json::Bool, true;
        if (literal("null")) return true;
        char* stop = nullptr;
        out.n = std::strtod(p_, &stop);
        if (stop == p_) return false;
        out.type = Json::Number;
        p_       = stop;
        return true;
    }
};

bool send_command(const char* command) {
    char msg[128];
    std::snprintf(msg, sizeof msg, "{\"type\":\"command\",\"command\":\"%s\"}", command);
    std::lock_guard<std::mutex> lk(g_ws_mx);
    return g_ws && WinHttpWebSocketSend(g_ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, msg, static_cast<DWORD>(std::strlen(msg))) ==
                       ERROR_SUCCESS;
}

// ---- Events ----------------------------------------------------------------------------------------------------

// Entity: the name at decorations.identity.name (or name), artists at decorations.creators[].entity....
void set_track(const Json& item) {
    if (item.type != Json::Object) return;
    const Json& deco = item["decorations"];
    std::string title = deco["identity"]["name"].s;
    if (title.empty()) title = item["name"].s;
    std::string artist;
    for (const Json& c : deco["creators"].items) {
        const std::string& a = c["entity"]["decorations"]["identity"]["name"].s;
        if (a.empty()) continue;
        if (!artist.empty()) artist += ", ";
        artist += a;
    }
    if (title.empty()) return;
    std::lock_guard<std::mutex> lk(g_np_mx);
    std::wstring t = widen(title), a = widen(artist);
    if (t == g_title && a == g_artist) return;
    g_title      = t;
    g_artist     = a;
    g_np_changed = true;
}

void set_status(const std::string& status) {
    if (!status.empty()) g_playing = status == "playing" || status == "buffering";
}

void on_event(const std::string& text) {
    Json ev;
    if (!JsonParser(text).parse(ev) || ev.type != Json::Object) return;
    const std::string& type = ev["type"].s;
    if (type == "auth_state") {
        static int last = -1;
        int logged_in   = ev["logged_in"].b ? 1 : 0;
        g_logged_in     = logged_in != 0;
        g_active        = ev["is_active"].b;
        if (logged_in != last)
            logf(logged_in ? "Soloist: signed in to Spotify" : "Soloist: waiting: in the Spotify app, play on the device \"%s\"",
                 narrow(unquoted(g_opt.device)).c_str());
        last = logged_in;
    } else if (type == "playback_state") {
        set_status(ev["status"].s);
        set_track(ev["item"]);
    } else if (type == "track_changed") {
        set_track(ev["item"]);
    } else if (type == "playback_changed") {
        set_status(ev["status"].s);
    } else if (type == "device_changed") {
        g_active = ev["is_active"].b;
        if (g_active && g_play_on_active.exchange(false)) send_command("play");
    } else if (type == "error") {
        logf("Soloist: %s", text.c_str());
    }
}

// The station just switched to Spotify (or Soloist only now signed in / connected meanwhile): take Spotify playback
// over from the phone or PC app, so there's no picking the device by hand. Once per time the station switches to it,
// so moving playback elsewhere while listening sticks.
// False: not yet (not signed in, or no control connection); try again.
bool try_activate() {
    if (!g_logged_in) return false;   // not signed in yet: the first time, the device has to be picked in the Spotify app
    if (g_active) return true;        // already playing here (or paused here: the station resumes it)
    g_play_on_active = true;
    if (!send_command("activate")) return g_play_on_active = false, false;
    logf("Soloist: moving Spotify playback to \"%s\"", narrow(unquoted(g_opt.device)).c_str());
    return true;
}

// ---- The control connection --------------------------------------------------------------------------------------

// Soloist picks a free port (--ws 127.0.0.1:0) and writes it to ws.port in its data folder.
int read_port() {
    HANDLE h = CreateFileW((g_data + L"\\ws.port").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;
    char buf[32] = {};
    DWORD got    = 0;
    ReadFile(h, buf, sizeof buf - 1, &got, nullptr);
    CloseHandle(h);
    int port = std::atoi(buf);
    return port > 0 && port < 65536 ? port : 0;
}

HINTERNET ws_connect(HINTERNET session, int port) {
    HINTERNET con = WinHttpConnect(session, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
    if (!con) return nullptr;
    HINTERNET req = WinHttpOpenRequest(con, L"GET", L"/", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    HINTERNET ws  = nullptr;
    if (req && WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) &&
        WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr))
        ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);   // the WebSocket handle keeps the connection
    return ws;
}

DWORD WINAPI control_thread(LPVOID) {
    HINTERNET session = WinHttpOpen(L"XboxMusicRadio", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    bool told_fail    = false;
    std::vector<char> buf(64 * 1024);
    while (g_running && session) {
        int port     = read_port();
        HINTERNET ws = port ? ws_connect(session, port) : nullptr;
        if (!ws) {
            if (port && !told_fail) logf("Soloist: couldn't connect to its control port %d yet; retrying", port);
            told_fail = told_fail || port;
            Sleep(500);
            continue;
        }
        logf("Soloist: control connection on port %d", port);
        told_fail = false;
        {
            std::lock_guard<std::mutex> lk(g_ws_mx);
            g_ws = ws;
        }
        std::string msg;
        for (;;) {
            DWORD got = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            if (WinHttpWebSocketReceive(ws, buf.data(), static_cast<DWORD>(buf.size()), &got, &type) != ERROR_SUCCESS) break;
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
            msg.append(buf.data(), got);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                on_event(msg);
                msg.clear();
            } else if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                msg.clear();
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_ws_mx);
            g_ws = nullptr;
        }
        WinHttpCloseHandle(ws);
        if (g_running) logf("Soloist: control connection closed; reconnecting");
        Sleep(500);
    }
    if (session) WinHttpCloseHandle(session);
    return 0;
}

// ---- The watcher ------------------------------------------------------------------------------------------------------

DWORD WINAPI watch_thread(LPVOID) {
    // Turn Soloist's own sound down as soon as it has any (it gets a mixer entry when it first plays),
    // then keep it there. The recording is turned back up only once the mixer has the new level.
    bool quiet = false;
    while (WaitForSingleObject(g_proc, quiet ? 2000 : 100) == WAIT_TIMEOUT) {
        if (!g_opt.quiet_speakers) continue;
        if (set_process_output_volume(g_pid, kQuietLevel) && !quiet) {
            Sleep(300);   // let the louder audio already recorded drain first
            g_gain = 1.0f / kQuietLevel;
            quiet  = true;
            logf("Soloist: its own sound is turned down to -80 dB in the volume mixer; it plays on the radio");
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(g_proc, &code);
    g_running      = false;
    g_playing      = false;
    g_stop_capture = true;
    if (code == 10)
        logf("Soloist: this soloist.exe has expired (builds last 90 days): download the new one from "
             "developer.spotify.com/documentation/soloist and put it in the GrooveMusicRadio mod folder");
    else
        logf("Soloist: exited (code %lu); see XboxMusicRadio\\soloist\\soloist.log", code);
    return 0;
}

} // namespace

namespace {

// Spotify as one of the station's live players (live.hpp).
class SpotifyPlayer final : public LivePlayer {
public:
    const wchar_t* id() const override { return kSpotifyId; }
    const wchar_t* name() const override { return L"Spotify"; }
    bool listed() override { return spotify_running(); }
    bool available() override { return spotify_running(); }
    bool wait_when_missing() const override { return false; }   // Soloist ended: play the music folder instead
    const char* missing_hint() const override { return "Soloist isn't running"; }
    bool playing() override { return spotify_playing(); }
    void next() override { spotify_next(); }
    void pause() override { spotify_pause(); }
    void resume() override { spotify_resume(); }
    bool now_playing(std::wstring& title, std::wstring& artist) override { return spotify_now_playing(title, artist); }
    void set_live(bool live) override {
        if (live && !was_live_) pending_ = g_opt.auto_activate;
        if (!live) pending_ = false;
        if (pending_ && try_activate()) pending_ = false;
        was_live_ = live;
    }

private:
    bool was_live_ = false;   // set_live is called from the station's player thread only
    bool pending_  = false;
};
SpotifyPlayer g_player;

} // namespace

bool spotify_start(const SpotifyOptions& opt, LogFn log) {
    g_opt   = opt;
    g_log   = std::move(log);
    std::wstring exe = g_opt.dir + L"\\soloist.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    std::wstring key = unquoted(g_opt.api_key), device = unquoted(g_opt.device);
    if (key.empty()) {
        logf("Soloist: soloist.exe is there but there's no API key: set api_key= under [spotify] in GrooveMusicRadio.ini "
             "(make one at developer.spotify.com/dashboard, \"Spotify Soloist API Key\")");
        return false;
    }
    if (device.empty()) device = L"FH3 Radio";

    std::wstring root = g_opt.dir + L"\\soloist";
    g_data            = root + L"\\data";
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW(g_data.c_str(), nullptr);
    DeleteFileW((g_data + L"\\ws.port").c_str());   // last session's port

    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE logf_h = CreateFileW((root + L"\\soloist.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, nullptr);
    HANDLE nul    = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    // A job object, never closed: Soloist ends with the game.
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li);
    }

    int volume = g_opt.volume < 0 ? 0 : g_opt.volume > 100 ? 100 : g_opt.volume;
    std::wstring cmd = L"\"soloist.exe\" -n \"" + device + L"\" -k \"" + key + L"\" -D \"" + g_data + L"\" -C \"" + root +
                       L"\\cache\" -z 500 -w 127.0.0.1:0 -i " + std::to_wstring(volume);
    STARTUPINFOW si{};
    si.cb          = sizeof si;
    si.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdInput   = nul;
    si.hStdOutput  = logf_h;
    si.hStdError   = logf_h;
    PROCESS_INFORMATION pi{};
    BOOL ok   = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                               g_opt.dir.c_str(), &si, &pi);
    DWORD err = GetLastError();
    if (logf_h != INVALID_HANDLE_VALUE) CloseHandle(logf_h);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) {
        logf("Soloist: couldn't start soloist.exe (error %lu)", err);
        return false;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);   // before it runs, so anything it starts is in the job too
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    g_proc    = pi.hProcess;
    g_pid     = pi.dwProcessId;
    g_running = true;
    logf("Soloist: started; in the Spotify app, play on the device \"%s\" (log: XboxMusicRadio\\soloist\\soloist.log)",
         narrow(device).c_str());

    live_register(&g_player);
    auto audio = [](const int16_t* samples, size_t frames) { live_audio(&g_player, samples, frames); };
    if (!capture_process(g_pid, &g_gain, audio, g_log, &g_stop_capture)) logf("Soloist: couldn't start recording it");
    if (HANDLE t = CreateThread(nullptr, 0, watch_thread, nullptr, 0, nullptr)) CloseHandle(t);
    if (HANDLE t = CreateThread(nullptr, 0, control_thread, nullptr, 0, nullptr)) CloseHandle(t);
    return true;
}

bool spotify_running() { return g_running; }
bool spotify_playing() { return g_running && g_playing; }
void spotify_next() { send_command("skip_next"); }
void spotify_pause() { send_command("pause"); }
void spotify_resume() { send_command("play"); }

bool spotify_now_playing(std::wstring& title, std::wstring& artist) {
    std::lock_guard<std::mutex> lk(g_np_mx);
    if (!g_np_changed) return false;
    g_np_changed = false;
    title        = g_title;
    artist       = g_artist;
    return true;
}
