// The live players' side of the station (live.hpp): their registry, and the ring their recording goes
// into while the Groove player plays them. A live player is a playlist with one song; its reader
// (LiveSource) hands the player the ring at the recording's own pace. When the app moves to another song
// the reader ends, so the player asks for the playlist again and shows the new title and artist (the
// app keeps playing: it is only paused once nothing has read it for a few seconds).
#include "live.hpp"

#include "reader.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace {

std::mutex players_lock;
std::vector<LivePlayer*> players;

// About 3 s of 44.1 kHz stereo; at most ~1 s is kept banked (past that, drop: a tiny skip now and then
// instead of a growing delay, since the recording runs on the sound card's clock).
constexpr size_t kRing = 44100 * 3, kMaxBanked = 44100;
std::mutex ring_lock;
std::condition_variable ring_ready;
Frame ring[kRing];
size_t ring_read, ring_count;
const LivePlayer* live;  // the player the ring is for, while its reader is open
std::atomic<unsigned> opened{0};  // bumped by each new reader: a pending pause is called off

std::mutex names_lock;
struct NowPlaying {
    const LivePlayer* player;
    std::wstring title, artist;  // what the app reported last
    std::wstring shown;          // the title in the track list the game has
};
std::vector<NowPlaying> names;

NowPlaying& entry(const LivePlayer* player) {
    for (NowPlaying& n : names)
        if (n.player == player) return n;
    names.push_back({player, L"", L"", L""});
    return names.back();
}

// Takes the app's latest report, if any. True if the game's track list shows another song.
bool poll(LivePlayer* player) {
    std::wstring title, artist;
    bool changed = player->now_playing(title, artist);
    std::lock_guard<std::mutex> lock(names_lock);
    NowPlaying& n = entry(player);
    if (changed) {
        n.title = title;
        n.artist = artist;
    }
    return !n.title.empty() && n.title != n.shown;
}

class LiveSource final : public PcmSource {
public:
    explicit LiveSource(LivePlayer* player) : player_(player) {
        ++opened;
        {
            std::lock_guard<std::mutex> lock(ring_lock);
            if (live != player) ring_read = ring_count = 0;  // the same player's sound carries on
            live = player;
        }
        player_->set_live(true);
        player_->resume();
    }
    ~LiveSource() override {
        // Another song of the same player usually opens right away; pause only if none does.
        unsigned at = opened;
        LivePlayer* player = player_;
        struct Pending {
            LivePlayer* player;
            unsigned at;
        };
        auto pending = new Pending{player, at};
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            auto pending = static_cast<Pending*>(p);
            Sleep(4000);
            if (opened == pending->at) {
                {
                    std::lock_guard<std::mutex> lock(ring_lock);
                    if (live == pending->player) live = nullptr;
                }
                pending->player->set_live(false);
                pending->player->pause();
            }
            delete pending;
            return 0;
        }, pending, 0, nullptr);
        if (t) CloseHandle(t);
        else delete pending;
    }
    // Blocks (on the player's reading thread) until a block of the recording is in, so the game's buffer
    // stays shallow and the radio stays close to live. Never ends.
    size_t read(Frame* out, size_t max) override {
        // A song the game's track list doesn't show yet: end this one, so it asks for the new name. Not
        // more than once every 2 s, should an app report back and forth.
        ULONGLONG now = GetTickCount64();
        if (now - checked_ > 250) {
            checked_ = now;
            if (poll(player_) && now - opened_at_ > 2000) return 0;
        }
        size_t want = std::min<size_t>(max, 2048);
        std::unique_lock<std::mutex> lock(ring_lock);
        if (!ring_ready.wait_for(lock, std::chrono::milliseconds(200), [&] { return ring_count >= want; })) {
            // Nothing recorded (the app is paused or closed): a little silence keeps the station alive.
            lock.unlock();
            Sleep(40);
            size_t n = 44100 / 25;
            std::fill(out, out + n, Frame{0, 0});
            return n;
        }
        for (size_t i = 0; i < want; ++i) out[i] = ring[(ring_read + i) % kRing];
        ring_read = (ring_read + want) % kRing;
        ring_count -= want;
        return want;
    }

private:
    LivePlayer* player_;
    ULONGLONG checked_ = 0, opened_at_ = GetTickCount64();
};

}  // namespace

void live_register(LivePlayer* player) {
    std::lock_guard<std::mutex> lock(players_lock);
    players.push_back(player);
}

std::vector<LivePlayer*> live_players() {
    std::lock_guard<std::mutex> lock(players_lock);
    return players;
}

void live_audio(const LivePlayer* from, const int16_t* samples, size_t frames) {
    std::lock_guard<std::mutex> lock(ring_lock);
    if (from != live) return;
    for (size_t i = 0; i < frames && ring_count < kMaxBanked; ++i) {
        Frame f = samples ? Frame{samples[2 * i], samples[2 * i + 1]} : Frame{0, 0};
        ring[(ring_read + ring_count) % kRing] = f;
        ++ring_count;
    }
    ring_ready.notify_one();
}

PcmSource* make_live_source(LivePlayer* player) { return new LiveSource(player); }

// For the track list the game is given: the app's current song, which it will then show.
bool live_now_playing(LivePlayer* player, std::wstring& title, std::wstring& artist) {
    poll(player);
    std::lock_guard<std::mutex> lock(names_lock);
    NowPlaying& n = entry(player);
    n.shown = n.title;
    title = n.title;
    artist = n.artist;
    return !title.empty();
}

// The song the app reported last, without taking a new report (for the log).
bool live_title(const LivePlayer* player, std::wstring& title, std::wstring& artist) {
    std::lock_guard<std::mutex> lock(names_lock);
    for (const NowPlaying& n : names)
        if (n.player == player) {
            title = n.title;
            artist = n.artist;
            return !title.empty();
        }
    return false;
}
