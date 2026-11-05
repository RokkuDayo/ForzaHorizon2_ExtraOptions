// A live source: a music player outside the game whose sound the station plays (Spotify through Soloist, music apps).
// Each is a playlist in Audio Options; while it's chosen, the station plays what the player plays.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class LivePlayer {
public:
    virtual ~LivePlayer() = default;
    virtual const wchar_t* id() const   = 0;   // its playlist id in Audio Options ("xmr:youtube")
    virtual const wchar_t* name() const = 0;   // shown in Audio Options and on the radio ("YouTube Music")
    virtual bool listed()               = 0;   // offered in Audio Options
    virtual bool available()            = 0;   // can play right now
    // Not available: wait for it (a music app that isn't open yet) or give up and play the music folder (Soloist ended).
    virtual bool wait_when_missing() const = 0;
    virtual const char* missing_hint() const = 0;   // for the log and the radio display while it waits
    virtual bool playing() = 0;
    virtual void next()    = 0;
    virtual void pause()   = 0;
    virtual void resume()  = 0;
    // The song it reported last. False if nothing changed since the last call.
    virtual bool now_playing(std::wstring& title, std::wstring& artist) = 0;
    // The station is playing it (true) or not: lets it turn its own sound down only meanwhile.
    virtual void set_live(bool) {}
};

// The live players, in Audio Options order (live.cpp keeps the list; players add themselves at startup).
void live_register(LivePlayer* player);
// A player's recorded sound (44.1 kHz stereo S16; nullptr = silence): the station plays it while that player is chosen.
void live_audio(const LivePlayer* from, const int16_t* samples, size_t frames);

// The players registered, in Audio Options order.
std::vector<LivePlayer*> live_players();
