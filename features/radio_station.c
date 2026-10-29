#include "radio_station.h"
#include <mmsystem.h>
#include <mmreg.h>
#include <msacm.h>
#include "autodrive_game.h"
#include "logging.h"
#include "support.h"
#include "bindings.h"

/* Native "My Music" radio station.

   The game reads media\audio\RadioInfo_<lang>.xml through its CXML4Resource
   loader and builds one station per <RadioStation>. Each playlist entry names
   an FMOD Studio event plus a SoundName key; track playback (0x1018840) asks
   Studio::System::getSoundInfo(key) for the stream parameters, opens the sound
   with System::createSound(... | CREATESTREAM | NONBLOCKING), takes the FSB
   subsound once it is ready (0x1022180) and hands it to the event's programmer
   instrument. Everything therefore plays on the game's radio bus, with its
   volume, ducking, pause and station banner.

   Hooks (all verified byte-for-byte first; any mismatch leaves the game alone):
   - CXML4Resource read slot: after the file is read, RadioInfo text gets one
     extra <RadioStation> whose entries use FH2MUSIC_<n> sound keys.
   - getSoundInfo call in track playback: FH2MUSIC_<n> keys answer with the MP3
     file loaded in memory (FMOD_OPENMEMORY_POINT); other keys go to FMOD.
   - getSubSound call in the open poll: a plain MP3 stream has no subsounds, so
     our marked index returns the stream itself.
   - radio player update slot: the RadioNext/RadioRestart bindings (F7/F6)
     move the playing custom track's event timeline (next track / restart), on
     the radio's own thread.
   - RadioModule::IsStationUnlocked call sites (D-pad cycle, state machine, UI):
     the custom station always counts as unlocked, whatever the progression
     (and every station with [CustomRadio] UnlockAllStations=1).
   - The RadioStation binding (F9) on the game thread, from the local car
     update: calls the game's own RadioModule::SetStation(name) path used by
     RadioSetStation state nodes, which hands the name to the deferred audio
     manager.
   Settings: FH2_ExtraOptions.ini [CustomRadio]; everything is off by default. */
#define SOUND_KEY_PREFIX "FH2MUSIC_"
#define SILENCE_KEY "FH2MUSIC_SILENCE"

enum {
    XML_READ_SLOT = 0x2ec4fd0,
    XML_READ = 0x13de5a0,
    PLAYER_UPDATE_SLOT = 0x2e76248,
    PLAYER_UPDATE = 0x1025120,
    STATION_BANKS_SLOT = 0x2e76798,
    STATION_BANKS = 0x10136e0,
    SOUND_INFO_CALL = 0x10189a0,
    SOUND_INFO = 0x1a98920,
    SUBSOUND_CALL = 0x10221f2,
    SUBSOUND = 0x199f510,
    SOUND_INFO_CAVE = 0x1006fe0,
    SUBSOUND_CAVE = 0x1007c90,
    SET_TIMELINE = 0x1a9b040,
    SET_VOLUME = 0x1a9b2b0,
    ENTRY_LENGTH_MS = 3600000,
    NAME_PADDING = 96,
    RADIO_SYSTEM_GLOBAL = 0x365d410,
    RADIO_SYSTEM_VTABLE = 0x2e76818,
    STATION_VTABLE = 0x2e76780,
    PLAYLIST_VTABLE = 0x2e76718,
    PLAYLIST_NEXT_SLOT = 0x2e76740,
    PLAYLIST_NEXT = 0x1007db0,
    PLAYER_VTABLE = 0x2e76220,
    PLAYER_HOLDER_OFFSET = 0x30,
    STATION_SLOTS_OFFSET = 8,
    STATION_SLOT_SIZE = 0x20,
    STATION_SLOT_COUNT = 3,
    SLOT_CLOCK_OFFSET = 0x18,
    FREEROAM_PLAYLIST_OFFSET = 0x68,
    EVENT_PLAYLIST_OFFSET = 0x78,
    PLAYLIST_CURSOR_OFFSET = 0x68,
    HISTORY_FIND_CALL = 0x1009140,
    HISTORY_FIND = 0xfe7fd0,
    HISTORY_FIND_CAVE = 0xfe9e30,
    STATION_HISTORY_OFFSET = 0x160,
    GUARD_WINDOW_MS = 2000,
    GUARD_LIMIT = 6,
    GUARD_PAUSE_MS = 30000,
    PLACEHOLDER_LENGTH = 200,
    ENTRY_DISPLAY_OFFSET = 0x40,
    ENTRY_ARTIST_OFFSET = 0x60,
    PLAYER_START_OFFSET = 0x28,
    PLAYER_POSITION_OFFSET = 0x2c,
    END_MARGIN_MS = 400,
    BACK_WINDOW_MS = 3000,
    SET_STATION = 0x65b310,
    AUDIO_MANAGER = 0x365a298,
    STATION_UNLOCKED = 0x668810,
    UNLOCKED_CAVE = 0x1014950,
    CHANGE_STATION = 0x660e50,
    CHANGE_CAVE = 0x664e21,
    MODULE_MANAGER_GLOBAL = 0x34c0f40,
    MODULE_GETTER = 0x766c70,
    MODULE_WRAPPER_OFFSET = 0x60,
    MODULE_MAP_OFFSET = 0x68,
    WRAPPER_CURRENT_OFFSET = 0x38,
    MAP_NODE_NAME_OFFSET = 0x28,
    MAX_MAP_STATIONS = 32,
    GAME_ALLOC = 0x3229c0,
    GAME_FREE = 0x31c7b0,
    CAVE_SIZE = 16,
    STATION_NUMBER_OFFSET = 0x108,
    PLAYER_INSTANCE_OFFSET = 0x10,
    PLAYER_STATE_OFFSET = 0x20,
    PLAYER_STATE_PLAYING = 3,
    MAX_TRACKS = 2048,
    TEXT_CAPACITY = 256,
    FOLDER_CAPACITY = 1024,
    TAG_BUFFER_SIZE = 65536,
    MIN_TRACK_MS = 3000,
    MAX_FILE_SIZE = 256 * 1024 * 1024,
    BUFFER_SLOTS = 5,
    BUFFER_IDLE_MS = 15000,
    DECODE_CHUNK = 16384,
    PLAYER_SLOTS = 8,
    KEY_POLL_MS = 30,
    SUBSOUND_MARK = 0x46480000,
    SUBSOUND_MASK = 0xffff0000,
    SILENCE_INDEX = 0xffff,
    SILENCE_RATE = 22050,
    SILENCE_SAMPLES = SILENCE_RATE * 4 / 10,
    FMOD_OPENMEMORY_POINT = 0x10000000,
    FMOD_ACCURATETIME = 0x4000,
    EXINFO_SIZE = 0xd8
};

typedef unsigned char (*XmlReadFn)(void *, void *, void *, void *);
typedef void (*PlayerUpdateFn)(void *, float);
typedef void (*StationBanksFn)(void *, void *, void *);
typedef int (*SoundInfoFn)(void *, const char *, void *);
typedef int (*SubSoundFn)(void *, int, void **);
typedef int (*SetTimelineFn)(void *, int);
typedef int (*SetVolumeFn)(void *, float);
typedef void (*SetStationFn)(void *, const void *);
typedef unsigned char (*StationUnlockedFn)(void *, const void *);
typedef void (*ChangeStationFn)(void *, int);
typedef void (*ModuleGetterFn)(void *, GameShared *);
typedef void *(*GameAllocFn)(size_t);
typedef void (*GameFreeFn)(void *);
typedef SHORT(WINAPI *KeyStateFn)(int);
typedef HWND(WINAPI *ForegroundWindowFn)(void);
typedef DWORD(WINAPI *WindowProcessFn)(HWND, LPDWORD);
typedef BOOL(WINAPI *SetPointerFn)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);

typedef struct {
    wchar_t name[MAX_PATH];
    char title[TEXT_CAPACITY], artist[TEXT_CAPACITY];
    unsigned duration;
} Track;

typedef struct {
    void *data;
    unsigned track;
    uint64_t handed_at;  // last time it was served or still playing
    DWORD size;
    int handed;          // given to FMOD at least once
    int pcm;
} SoundBuffer;

typedef struct {
    void *player;
    unsigned track;
} PlayerTrack;

static unsigned char *image;
static XmlReadFn original_read;
static PlayerUpdateFn original_update;
static StationBanksFn original_banks;
static SoundInfoFn original_sound_info;
static SubSoundFn original_subsound;
static SetTimelineFn set_timeline;
static SetVolumeFn set_volume;
static float gain = 1.0f;
// Alternative playback modes kept off (they were for bisecting behaviour).
static const int single_entry = 0, zero_offset_always = 0, rename_entries = 0, shift_keys = 0;
// [CustomRadio] UnlockAllStations.
static int unlock_all;
static volatile LONG unlock_all_live;
// PcmDecode: decode MP3s with Windows' codec instead of FMOD's.
static int pcm_decode = 1;
static int station_enabled;
// Radio-thread playback state.
//  - per-track mode (default): the radio picks entry e by its own station clock
//    and is served file (e + shift); F7/F6 move `shift`.
//  - single-entry mode: one long entry, always served the file at `cursor`.
static unsigned cursor, served_track, served_entry, served_end;
static int shift, served_valid, served_done, served_position, served_moved;
static volatile LONG zero_offset_pending;
// The playlist entry last served (its SoundName text is the entry's first field).
static const void *served_entry_address;
// F7/F6 transitions: further presses wait for the next serve; a watchdog on the
// game thread re-tunes the station if no serve follows.
static volatile LONG serve_count, transition_serve;
static volatile LONG64 transition_at;
// Playlist next-pick hook (radio thread): the custom station always continues
// in folder order from the served entry, wrapping at both ends; F6 asks for -1.
// The step holds until the next serve: one transition runs GetNext more than
// once (the item actually played is not the first pick), so every pick until
// then must aim at the same entry.
typedef void *(*PlaylistNextFn)(void *, GameShared *, int);
static PlaylistNextFn original_next;
static volatile LONG next_step = 1;
static int wrap_logged;
// The station re-picks (0x1009090) while a pick is in its recently-played list
// (station+0x160); for the steered custom station that list is ignored, or a
// step back to a played track would loop forever on the radio thread.
typedef void **(*HistoryFindFn)(void **, void *, void *, void *);
static HistoryFindFn original_find;
// File position (ms) the playing custom track started from (its live offset or 0).
static int played_from;
static uint64_t guard_window, guard_until;
static unsigned guard_count;
static SetStationFn set_station;
static StationUnlockedFn original_unlocked;
static ChangeStationFn original_change;
static ModuleGetterFn module_getter;
static GameAllocFn game_alloc;
static GameFreeFn game_free;
static KeyStateFn key_state;
static ForegroundWindowFn foreground_window;
static WindowProcessFn window_process;
static SetPointerFn set_pointer;

static wchar_t ini_path[PATH_CAPACITY];
static wchar_t folder[FOLDER_CAPACITY];
static char station_name[TEXT_CAPACITY];
static const int skip_banks = 0;
static unsigned next_key, restart_key, select_key;
static char injected_name[TEXT_CAPACITY];
static volatile LONG unlock_hooked;
static const unsigned unlocked_calls[] = {0x660fd1, 0x78a092, 0x8fc8bf};
static const unsigned change_calls[] = {0x8fc659, 0xc9fad5};

// MSVC std::string as the game lays it out.
typedef struct {
    union {
        char inline_text[16];
        char *heap_text;
    };
    size_t size, capacity;
} GameString;

static SRWLOCK lock = SRWLOCK_INIT;
static Track *tracks;
static unsigned track_count;
static int scanned;
static volatile LONG station_number;
static SoundBuffer buffers[BUFFER_SLOTS];
static PlayerTrack players[PLAYER_SLOTS];
static unsigned char tag_buffer[TAG_BUFFER_SIZE];
static unsigned char silence[44 + SILENCE_SAMPLES * 2];
static char log_line[MAX_PATH * 3 + 128];

// --- text helpers ---------------------------------------------------------------

static size_t wide_length(const wchar_t *text) {
    size_t length = 0;
    while (text[length]) {
        ++length;
    }
    return length;
}

static int wide_append(wchar_t *output, size_t capacity, const wchar_t *text) {
    size_t length = wide_length(output);
    for (size_t index = 0;; ++index) {
        if (length >= capacity) {
            output[capacity - 1] = 0;
            return 0;
        }
        output[length++] = text[index];
        if (!text[index]) {
            return 1;
        }
    }
}

static int lower(int character) {
    return character >= 'A' && character <= 'Z' ? character + 'a' - 'A' : character;
}

static const char *find_text(const char *text, size_t length, const char *needle, int last) {
    size_t count = text_length(needle);
    const char *found = NULL;
    for (size_t index = 0; count && index + count <= length; ++index) {
        size_t matched = 0;
        while (matched < count && lower(text[index + matched]) == lower(needle[matched])) {
            ++matched;
        }
        if (matched == count) {
            found = text + index;
            if (!last) {
                return found;
            }
        }
    }
    return found;
}

static void log_text(const char *prefix, const wchar_t *text) {
    size_t length = text_length(prefix);
    memory_copy(log_line, prefix, length);
    int written = WideCharToMultiByte(CP_UTF8, 0, text, -1, log_line + length,
                                      (int)(sizeof(log_line) - length - 3), NULL, NULL);
    length += written > 0 ? (size_t)written - 1 : 0;
    memory_copy(log_line + length, "\r\n", 3);
    log_append(log_line);
}

// Growing output buffer for the generated XML.
typedef struct {
    char *data;
    size_t length, capacity;
    int failed;
} Output;

static void put(Output *out, const char *text, size_t length) {
    if (out->failed || out->length + length + 1 > out->capacity) {
        out->failed = 1;
        return;
    }
    memory_copy(out->data + out->length, text, length);
    out->length += length;
    out->data[out->length] = 0;
}

static void put_text(Output *out, const char *text) {
    put(out, text, text_length(text));
}

static void put_number(Output *out, unsigned value) {
    char digits[12];
    char *cursor = digits + sizeof(digits);
    do {
        *--cursor = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    put(out, cursor, (size_t)(digits + sizeof(digits) - cursor));
}

// The game's own RadioInfo files carry raw '&' in attributes ("Camo & Crooked"),
// so its parser does not decode entities: keep '&' and replace the characters
// that would end an attribute or tag.
static void put_escaped(Output *out, const char *text) {
    for (; *text; ++text) {
        char character = *text == '"' ? '\'' : *text == '<' ? '(' : *text == '>' ? ')' : *text;
        if ((unsigned char)character >= 32) {
            put(out, &character, 1);
        }
    }
}

// --- settings -----------------------------------------------------------------

static int read_settings(const wchar_t *directory) {
    if (!directory || !path_join(ini_path, directory, MOD_INI_NAME)) {
        return 0;
    }
    unlock_all = GetPrivateProfileIntW(L"CustomRadio", L"UnlockAllStations", 0, ini_path) == 1;
    InterlockedExchange(&unlock_all_live, unlock_all);
    station_enabled = GetPrivateProfileIntW(L"CustomRadio", L"Enabled", 0, ini_path) == 1;
    if (!station_enabled) {
        return 0;
    }
    wchar_t value[FOLDER_CAPACITY];
    GetPrivateProfileStringW(L"CustomRadio", L"Folder", L"Music", value, ARRAY_COUNT(value),
                             ini_path);
    if (!value[0]) {
        memory_copy(value, L"Music", sizeof(L"Music"));
    }
    int absolute = value[1] == L':' || (value[0] == L'\\' && value[1] == L'\\');
    folder[0] = 0;
    if (!absolute && (!wide_append(folder, ARRAY_COUNT(folder), directory) ||
                      (folder[0] && folder[wide_length(folder) - 1] != L'\\' &&
                       !wide_append(folder, ARRAY_COUNT(folder), L"\\")))) {
        return 0;
    }
    if (!wide_append(folder, ARRAY_COUNT(folder), value)) {
        return 0;
    }
    GetPrivateProfileStringW(L"CustomRadio", L"Name", L"", value, ARRAY_COUNT(value), ini_path);
    station_name[0] = 0;
    if (value[0]) {
        WideCharToMultiByte(CP_UTF8, 0, value, -1, station_name, sizeof(station_name), NULL,
                            NULL);
    }
    int volume = (int)GetPrivateProfileIntW(L"CustomRadio", L"Volume", 100, ini_path);
    volume = volume < 0 ? 0 : volume > 400 ? 400 : volume;
    gain = (float)volume / 100.0f;
    pcm_decode = GetPrivateProfileIntW(L"CustomRadio", L"PcmDecode", 1, ini_path) == 1;
    // [Bindings] RadioStation/RadioNext/RadioRestart (keyboard F9/F7/F6).
    select_key = binding_keyboard(BIND_RADIO_STATION);
    next_key = binding_keyboard(BIND_RADIO_NEXT);
    restart_key = binding_keyboard(BIND_RADIO_RESTART);
    return 1;
}

// A radio binding: its key, or its Xbox buttons on the local controller.
static int radio_binding_down(GameAction action, unsigned key) {
    void *controller;
    unsigned short buttons;
    return (key && key_state && (key_state((int)key) & 0x8000)) ||
           (binding_xbox(action) && autodrive_game_buttons(&controller, &buttons) == 0 &&
            binding_pad_down(action, buttons));
}

// --- ID3 tags and MP3 duration ------------------------------------------------

static unsigned big_endian(const unsigned char *bytes, unsigned count, int syncsafe) {
    unsigned value = 0;
    for (unsigned index = 0; index < count; ++index) {
        value = syncsafe ? value << 7 | (bytes[index] & 0x7f) : value << 8 | bytes[index];
    }
    return value;
}

// Decodes the first value of an ID3 text frame (encoding byte + payload) as UTF-8.
static void decode_text(const unsigned char *data, unsigned size, char *output) {
    wchar_t wide[TEXT_CAPACITY];
    unsigned count = 0;
    output[0] = 0;
    if (!size) {
        return;
    }
    unsigned encoding = data[0];
    ++data;
    --size;
    if (encoding == 3) {
        unsigned length = 0;
        while (length < size && data[length] && length < TEXT_CAPACITY - 1) {
            ++length;
        }
        memory_copy(output, data, length);
        output[length] = 0;
    } else {
        if (encoding == 1 || encoding == 2) {
            int big = encoding == 2;
            if (size >= 2 && ((data[0] == 0xfe && data[1] == 0xff) ||
                              (data[0] == 0xff && data[1] == 0xfe))) {
                big = data[0] == 0xfe;
                data += 2;
                size -= 2;
            }
            for (unsigned index = 0; index + 1 < size && count + 1 < TEXT_CAPACITY / 3;
                 index += 2) {
                wchar_t character = big ? (wchar_t)(data[index] << 8 | data[index + 1])
                                        : (wchar_t)(data[index] | data[index + 1] << 8);
                if (!character) {
                    break;
                }
                wide[count++] = character;
            }
        } else {
            for (unsigned index = 0; index < size && data[index] && count + 1 < TEXT_CAPACITY / 3;
                 ++index) {
                wide[count++] = data[index];
            }
        }
        wide[count] = 0;
        if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, output, TEXT_CAPACITY, NULL, NULL)) {
            output[0] = 0;
        }
    }
    size_t length = text_length(output);
    while (length && (output[length - 1] == ' ' || output[length - 1] == '\t')) {
        output[--length] = 0;
    }
}

static void read_id3v2(unsigned size, Track *track) {
    unsigned version = tag_buffer[3], flags = tag_buffer[5];
    unsigned end = 10 + big_endian(tag_buffer + 6, 4, 1), cursor = 10;
    if (version < 2 || version > 4 || flags & 0x80) {
        return;
    }
    if (end > size) {
        end = size;
    }
    if (version >= 3 && flags & 0x40 && end >= 14) {
        unsigned extended = big_endian(tag_buffer + 10, 4, version == 4);
        cursor += version == 4 ? extended : extended + 4;
    }
    unsigned header = version == 2 ? 6 : 10, id_size = version == 2 ? 3 : 4;
    while (cursor < end && end - cursor >= header && tag_buffer[cursor]) {
        const unsigned char *frame = tag_buffer + cursor;
        unsigned frame_size = version == 2   ? big_endian(frame + 3, 3, 0)
                              : version == 4 ? big_endian(frame + 4, 4, 1)
                                             : big_endian(frame + 4, 4, 0);
        if (frame_size > end - cursor - header) {
            break;
        }
        if (memory_equal(frame, version == 2 ? "TT2" : "TIT2", id_size)) {
            decode_text(frame + header, frame_size, track->title);
        } else if (memory_equal(frame, version == 2 ? "TP1" : "TPE1", id_size)) {
            decode_text(frame + header, frame_size, track->artist);
        }
        cursor += header + frame_size;
    }
}

static int read_at(HANDLE file, uint64_t offset, void *output, DWORD size, DWORD *read) {
    LARGE_INTEGER position;
    position.QuadPart = (LONGLONG)offset;
    *read = 0;
    return set_pointer && set_pointer(file, position, NULL, FILE_BEGIN) &&
           ReadFile(file, output, size, read, NULL);
}

static void read_id3v1(HANDLE file, uint64_t size, Track *track, int *present) {
    unsigned char tag[128], field[31];
    DWORD read;
    *present = 0;
    if (size < 128 || !read_at(file, size - 128, tag, sizeof(tag), &read) ||
        read != sizeof(tag) || !memory_equal(tag, "TAG", 3)) {
        return;
    }
    *present = 1;
    field[0] = 0;
    if (!track->title[0]) {
        memory_copy(field + 1, tag + 3, 30);
        decode_text(field, sizeof(field), track->title);
    }
    if (!track->artist[0]) {
        memory_copy(field + 1, tag + 33, 30);
        decode_text(field, sizeof(field), track->artist);
    }
}

static unsigned mp3_duration(HANDLE file, uint64_t start, uint64_t size, int id3v1) {
    static const unsigned short rates1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160,
                                            192, 224, 256, 320};
    static const unsigned short rates2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96,
                                            112, 128, 144, 160};
    static const unsigned frequencies[3][3] = {
        {44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};
    DWORD read;
    if (start >= size || !read_at(file, start, tag_buffer, 8192, &read) || read < 64) {
        return 0;
    }
    for (DWORD at = 0; at + 64 <= read; ++at) {
        const unsigned char *frame = tag_buffer + at;
        if (frame[0] != 0xff || (frame[1] & 0xe0) != 0xe0) {
            continue;
        }
        unsigned version = (frame[1] >> 3) & 3, layer = (frame[1] >> 1) & 3;
        unsigned bitrate_index = frame[2] >> 4, rate_index = (frame[2] >> 2) & 3;
        if (version == 1 || layer != 1 || !bitrate_index || bitrate_index == 15 ||
            rate_index == 3) {
            continue;
        }
        int mpeg1 = version == 3, mono = (frame[3] >> 6) == 3;
        unsigned frequency = frequencies[mpeg1 ? 0 : version == 2 ? 1 : 2][rate_index];
        unsigned samples = mpeg1 ? 1152 : 576;
        unsigned side = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
        const unsigned char *xing = frame + 4 + side;
        if (at + 4 + side + 16 <= read &&
            (memory_equal(xing, "Xing", 4) || memory_equal(xing, "Info", 4)) &&
            (big_endian(xing + 4, 4, 0) & 1)) {
            uint64_t frames = big_endian(xing + 8, 4, 0);
            return (unsigned)(frames * samples * 1000 / frequency);
        }
        const unsigned char *vbri = frame + 36;
        if (at + 36 + 18 <= read && memory_equal(vbri, "VBRI", 4)) {
            uint64_t frames = big_endian(vbri + 14, 4, 0);
            return (unsigned)(frames * samples * 1000 / frequency);
        }
        unsigned kbps = (mpeg1 ? rates1 : rates2)[bitrate_index];
        uint64_t bytes = size - start - at - (id3v1 ? 128 : 0);
        return (unsigned)(bytes * 8 / kbps);
    }
    return 0;
}

static void describe_track(const wchar_t *path, Track *track) {
    track->title[0] = track->artist[0] = 0;
    track->duration = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    LARGE_INTEGER size;
    DWORD read = 0;
    uint64_t audio = 0;
    int id3v1 = 0;
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
        read_at(file, 0, tag_buffer, sizeof(tag_buffer), &read)) {
        if (read >= 10 && memory_equal(tag_buffer, "ID3", 3)) {
            audio = 10 + (uint64_t)big_endian(tag_buffer + 6, 4, 1) + (tag_buffer[5] & 0x10 ? 10 : 0);
            read_id3v2(read, track);
        }
        read_id3v1(file, (uint64_t)size.QuadPart, track, &id3v1);
        track->duration = mp3_duration(file, audio, (uint64_t)size.QuadPart, id3v1);
    }
    CloseHandle(file);
    if (!track->title[0]) {
        WideCharToMultiByte(CP_UTF8, 0, track->name, -1, track->title, TEXT_CAPACITY, NULL, NULL);
        size_t length = text_length(track->title);
        if (length > 4 && track->title[length - 4] == '.') {
            track->title[length - 4] = 0;
        }
    }
}

static int track_path(unsigned index, wchar_t *path, size_t capacity) {
    path[0] = 0;
    return wide_append(path, capacity, folder) && wide_append(path, capacity, L"\\") &&
           wide_append(path, capacity, tracks[index].name);
}

static void scan_tracks(void) {
    static wchar_t search[FOLDER_CAPACITY + 8], path[FOLDER_CAPACITY + MAX_PATH + 2];
    scanned = 1;
    tracks = HeapAlloc(GetProcessHeap(), 0, MAX_TRACKS * sizeof(*tracks));
    search[0] = 0;
    if (!tracks || !wide_append(search, ARRAY_COUNT(search), folder) ||
        !wide_append(search, ARRAY_COUNT(search), L"\\*.mp3")) {
        return;
    }
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW(search, &entry);
    if (find == INVALID_HANDLE_VALUE) {
        log_text("RADIO: no .mp3 files in ", folder);
        return;
    }
    do {
        size_t length = wide_length(entry.cFileName);
        if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY || length >= MAX_PATH ||
            track_count >= MAX_TRACKS || entry.nFileSizeHigh ||
            entry.nFileSizeLow > MAX_FILE_SIZE) {
            continue;
        }
        Track *track = &tracks[track_count];
        memory_copy(track->name, entry.cFileName, (length + 1) * sizeof(wchar_t));
        if (!track_path(track_count, path, ARRAY_COUNT(path))) {
            continue;
        }
        describe_track(path, track);
        if (track->duration < MIN_TRACK_MS) {
            log_text("RADIO: skipped (unknown length): ", track->name);
            continue;
        }
        ++track_count;
    } while (FindNextFileW(find, &entry));
    FindClose(find);
    log_append_value("RADIO: playable MP3 files: ", (int)track_count);
}

// --- XML injection ------------------------------------------------------------

static unsigned highest_station_number(const char *text, size_t length) {
    unsigned highest = 0;
    const char *cursor = text, *end = text + length;
    while ((cursor = find_text(cursor, (size_t)(end - cursor), "Number=\"", 0)) != NULL) {
        cursor += 8;
        unsigned value = 0;
        while (cursor < end && *cursor >= '0' && *cursor <= '9' && value < 100000) {
            value = value * 10 + (unsigned)(*cursor++ - '0');
        }
        if (value > highest) {
            highest = value;
        }
    }
    return highest;
}

// Names are padded with spaces so the game's strings get heap buffers that can
// later be rewritten in place with the title of the file actually served.
static void put_name(Output *out, const char *text) {
    size_t mark = out->length;
    put_escaped(out, text);
    while (rename_entries && out->length - mark < NAME_PADDING && !out->failed) {
        put(out, " ", 1);
    }
}

static void put_entry(Output *out, unsigned key, unsigned length, const char *title,
                      const char *artist) {
    unsigned drop = length / 3 < 10000 ? length / 3 : 10000;
    put_text(out, "        <Entry EventName=\"/Master/Radio/Track\" SoundName=\"" SOUND_KEY_PREFIX);
    put_number(out, key);
    put_text(out, "\" SampleLength=\"");
    put_number(out, length);
    put_text(out, "\" DisplayName=\"");
    put_name(out, title);
    put_text(out, "\" Artist=\"");
    put_name(out, artist);
    put_text(out, "\" TrackStart=\"0\" DJDrop=\"");
    put_number(out, drop);
    put_text(out, "\" TrackDrop=\"");
    put_number(out, drop);
    put_text(out, "\" TrackBreakDown=\"");
    put_number(out, length / 2);
    put_text(out, "\" StingerStart=\"");
    put_number(out, length - 1500);
    put_text(out, "\" DJStart=\"");
    put_number(out, length - 1000);
    put_text(out, "\" End=\"");
    put_number(out, length);
    put_text(out, "\"/>\r\n");
}

static void put_tracks(Output *out, const char *type) {
    put_text(out, "      <Playlist Type=\"");
    put_text(out, type);
    put_text(out, "\">\r\n");
    if (single_entry) {
        put_entry(out, 0, ENTRY_LENGTH_MS, "", "");
    } else {
        for (unsigned index = 0; index < track_count; ++index) {
            put_entry(out, index, tracks[index].duration, tracks[index].title,
                      tracks[index].artist);
        }
    }
    put_text(out, "      </Playlist>\r\n");
}

static void put_stingers(Output *out, const char *type) {
    put_text(out, "      <Playlist Type=\"");
    put_text(out, type);
    put_text(out, "\">\r\n        <Entry EventName=\"/Master/Radio/Stinger\" SoundName=\"" SILENCE_KEY
                  "\" SampleLength=\"400\" StartNextTrack=\"200\" End=\"400\"/>\r\n"
                  "      </Playlist>\r\n");
}

static int build_station(Output *out, unsigned number, int polish) {
    put_text(out, "    <RadioStation Name=\"");
    size_t mark = out->length;
    put_escaped(out, station_name[0] ? station_name : polish ? "MOJA MUZYKA" : "MY MUSIC");
    if (!out->failed && out->length - mark < sizeof(injected_name)) {
        memory_copy(injected_name, out->data + mark, out->length - mark);
        injected_name[out->length - mark] = 0;
    }
    put_text(out, "\" Number=\"");
    put_number(out, number);
    put_text(out, "\" RecentlyPlayedListSize=\"-1\">\r\n");
    put_tracks(out, "FreeroamTracks");
    put_tracks(out, "EventTracks");
    put_stingers(out, "Stingers");
    put_stingers(out, "ShortStingers");
    put_text(out, "      <Playlist Type=\"DJ\">\r\n      </Playlist>\r\n    </RadioStation>\r\n");
    return !out->failed;
}

static const char *resource_name(const unsigned char *resource, size_t *length) {
    const char *name = (const char *)(resource + 0xb8);
    size_t capacity;
    if (!memory_read(resource + 0xc8, length, sizeof(*length)) ||
        !memory_read(resource + 0xd0, &capacity, sizeof(capacity)) || !*length ||
        *length > 4096 || capacity < *length) {
        return NULL;
    }
    if (capacity >= 16 && !memory_read(resource + 0xb8, &name, sizeof(name))) {
        return NULL;
    }
    return name;
}

static void inject_station(unsigned char *resource) {
    size_t name_length;
    const char *name = resource_name(resource, &name_length);
    if (!name || !find_text(name, name_length, "radioinfo", 0)) {
        return;
    }
    char **buffer = (char **)(resource + 0xe0);
    uint32_t *length = (uint32_t *)(resource + 0xe8);
    if (!*buffer || !*length || *length > 8u * 1024u * 1024u) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (!scanned) {
        scan_tracks();
    }
    ReleaseSRWLockExclusive(&lock);
    if (!track_count) {
        return;
    }
    const char *close = find_text(*buffer, *length, "</RadioStations>", 1);
    if (!close) {
        log_append("RADIO: RadioInfo layout not recognised; station not added.\r\n");
        return;
    }
    while (close > *buffer && (close[-1] == ' ' || close[-1] == '\t')) {
        --close;  // insert at the start of the closing tag's line
    }
    unsigned number = highest_station_number(*buffer, *length) + 1;
    Output station = {0};
    station.capacity = (size_t)track_count * 2 * (3 * TEXT_CAPACITY * 2 + 512) + 4096;
    station.data = HeapAlloc(GetProcessHeap(), 0, station.capacity);
    if (!station.data ||
        !build_station(&station, number, find_text(name, name_length, "_pl.", 0) != NULL)) {
        log_append("RADIO: station XML could not be built.\r\n");
        if (station.data) {
            HeapFree(GetProcessHeap(), 0, station.data);
        }
        return;
    }
    size_t before = (size_t)(close - *buffer), total = *length + station.length;
    char *replacement = game_alloc(total + 1);
    if (replacement) {
        memory_copy(replacement, *buffer, before);
        memory_copy(replacement + before, station.data, station.length);
        memory_copy(replacement + before + station.length, *buffer + before, *length - before);
        replacement[total] = 0;
        char *original = *buffer;
        *buffer = replacement;
        *length = (uint32_t)total;
        game_free(original);
        InterlockedExchange(&station_number, (LONG)number);
        log_append_value("RADIO: custom station added as number ", (int)number);
    } else {
        log_append("RADIO: station XML allocation failed.\r\n");
    }
    HeapFree(GetProcessHeap(), 0, station.data);
}

static unsigned char xml_read_hook(void *resource, void *a, void *b, void *c) {
    unsigned char result = original_read(resource, a, b, c);
    if (result) {
        inject_station(resource);
    }
    return result;
}

// --- FMOD sound hooks ---------------------------------------------------------

static void build_silence(void) {
    static const unsigned char header[44] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
        16, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0,
        'd', 'a', 't', 'a', 0, 0, 0, 0};
    uint32_t data = SILENCE_SAMPLES * 2, riff = data + 36, rate = SILENCE_RATE,
             bytes = SILENCE_RATE * 2;
    memory_copy(silence, header, sizeof(header));
    memory_copy(silence + 4, &riff, 4);
    memory_copy(silence + 24, &rate, 4);
    memory_copy(silence + 28, &bytes, 4);
    memory_copy(silence + 40, &data, 4);
}

// --- MP3 -> PCM -------------------------------------------------------------------
// The game's FMOD MP3 decoder mis-handles joint (mid/side) stereo frames, which
// most MP3s use: the result sounds hollow, with the centre (mids, vocals) lost.
// Windows' own MP3 ACM codec decodes the file to 16-bit PCM instead; FMOD then
// plays a plain stereo WAV from memory. PcmDecode=0 serves the MP3 as before.

typedef MMRESULT(ACMAPI *AcmOpenFn)(LPHACMSTREAM, HACMDRIVER, LPWAVEFORMATEX, LPWAVEFORMATEX,
                                    LPWAVEFILTER, DWORD_PTR, DWORD_PTR, DWORD);
typedef MMRESULT(ACMAPI *AcmSizeFn)(HACMSTREAM, DWORD, LPDWORD, DWORD);
typedef MMRESULT(ACMAPI *AcmHeaderFn)(HACMSTREAM, LPACMSTREAMHEADER, DWORD);
typedef MMRESULT(ACMAPI *AcmCloseFn)(HACMSTREAM, DWORD);

static AcmOpenFn acm_open;
static AcmSizeFn acm_size;
static AcmHeaderFn acm_prepare, acm_convert, acm_unprepare;
static AcmCloseFn acm_close;
static volatile LONG prefetch_request = -1;

static int load_acm(void) {
    static int loaded;
    if (!loaded) {
        loaded = 1;
        HMODULE acm = LoadLibraryExW(L"msacm32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (acm) {
            acm_open = (AcmOpenFn)(uintptr_t)GetProcAddress(acm, "acmStreamOpen");
            acm_size = (AcmSizeFn)(uintptr_t)GetProcAddress(acm, "acmStreamSize");
            acm_prepare = (AcmHeaderFn)(uintptr_t)GetProcAddress(acm, "acmStreamPrepareHeader");
            acm_convert = (AcmHeaderFn)(uintptr_t)GetProcAddress(acm, "acmStreamConvert");
            acm_unprepare = (AcmHeaderFn)(uintptr_t)GetProcAddress(acm, "acmStreamUnprepareHeader");
            acm_close = (AcmCloseFn)(uintptr_t)GetProcAddress(acm, "acmStreamClose");
        }
        if (!acm_open || !acm_size || !acm_prepare || !acm_convert || !acm_unprepare || !acm_close) {
            acm_open = NULL;
            log_append("RADIO: Windows MP3 decoder (msacm32) unavailable; MP3 served as is.\r\n");
        }
    }
    return acm_open != NULL;
}

// First MPEG-1/2/2.5 Layer III frame at or after `at`.
static int mp3_frame(const unsigned char *data, DWORD end, DWORD *at, unsigned *rate,
                     unsigned *channels, unsigned *kbps, int *mpeg1) {
    static const unsigned short rates1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160,
                                            192, 224, 256, 320};
    static const unsigned short rates2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96,
                                            112, 128, 144, 160};
    static const unsigned frequencies[3][3] = {
        {44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};
    for (DWORD i = *at; i + 4 <= end && i < *at + 65536; ++i) {
        const unsigned char *h = data + i;
        unsigned version = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3, bitrate = h[2] >> 4,
                 sampling = (h[2] >> 2) & 3;
        if (h[0] != 0xff || (h[1] & 0xe0) != 0xe0 || version == 1 || layer != 1 || !bitrate ||
            bitrate == 15 || sampling == 3) {
            continue;
        }
        *mpeg1 = version == 3;
        *rate = frequencies[*mpeg1 ? 0 : version == 2 ? 1 : 2][sampling];
        *channels = (h[3] >> 6) == 3 ? 1 : 2;
        *kbps = (*mpeg1 ? rates1 : rates2)[bitrate];
        *at = i;
        return 1;
    }
    return 0;
}

static void *grow(void *data, size_t used, size_t *capacity, size_t need) {
    if (need <= *capacity) {
        return data;
    }
    size_t bigger = *capacity * 2 > need ? *capacity * 2 : need;
    void *next = HeapAlloc(GetProcessHeap(), 0, bigger);
    if (next) {
        memory_copy(next, data, used);
        *capacity = bigger;
    }
    HeapFree(GetProcessHeap(), 0, data);
    return next;
}

// Decodes a whole MP3 file to a 16-bit PCM WAV image (header + samples).
static void *decode_mp3(const unsigned char *data, DWORD size, DWORD *output_size) {
    DWORD start = 0, end = size;
    unsigned rate, channels, kbps;
    int mpeg1;
    if (size >= 10 && memory_equal(data, "ID3", 3)) {
        start = 10 + big_endian(data + 6, 4, 1) + (data[5] & 0x10 ? 10 : 0);
    }
    if (size >= 128 && memory_equal(data + size - 128, "TAG", 3)) {
        end = size - 128;
    }
    if (start >= end || !load_acm() || !mp3_frame(data, end, &start, &rate, &channels, &kbps, &mpeg1)) {
        return NULL;
    }
    MPEGLAYER3WAVEFORMAT mp3;
    WAVEFORMATEX pcm;
    for (unsigned i = 0; i < sizeof(mp3); ++i) {
        ((unsigned char *)&mp3)[i] = 0;
    }
    mp3.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
    mp3.wfx.nChannels = (WORD)channels;
    mp3.wfx.nSamplesPerSec = rate;
    mp3.wfx.nAvgBytesPerSec = kbps * 125;
    mp3.wfx.nBlockAlign = 1;
    mp3.wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
    mp3.wID = MPEGLAYER3_ID_MPEG;
    mp3.fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
    mp3.nBlockSize = (WORD)((mpeg1 ? 144 : 72) * kbps * 1000 / rate);
    mp3.nFramesPerBlock = 1;
    mp3.nCodecDelay = 1393;
    pcm.wFormatTag = WAVE_FORMAT_PCM;
    pcm.nChannels = (WORD)channels;
    pcm.nSamplesPerSec = rate;
    pcm.nBlockAlign = (WORD)(channels * 2);
    pcm.nAvgBytesPerSec = rate * pcm.nBlockAlign;
    pcm.wBitsPerSample = 16;
    pcm.cbSize = 0;
    HACMSTREAM stream = NULL;
    if (acm_open(&stream, NULL, &mp3.wfx, &pcm, NULL, 0, 0, ACM_STREAMOPENF_NONREALTIME) &&
        acm_open(&stream, NULL, &mp3.wfx, &pcm, NULL, 0, 0, 0)) {
        log_append("RADIO: MP3 decoder refused the format; MP3 served as is.\r\n");
        return NULL;
    }
    DWORD chunk_out = 0;
    unsigned char *source = NULL, *target = NULL, *out = NULL;
    size_t used = 44, capacity = 0;
    int ok = 0;
    if (!acm_size(stream, DECODE_CHUNK, &chunk_out, ACM_STREAMSIZEF_SOURCE) && chunk_out) {
        source = HeapAlloc(GetProcessHeap(), 0, DECODE_CHUNK);
        target = HeapAlloc(GetProcessHeap(), 0, chunk_out);
        capacity = (size_t)((uint64_t)(end - start) * pcm.nAvgBytesPerSec / (kbps * 125)) +
                   (1 << 20);
        out = HeapAlloc(GetProcessHeap(), 0, capacity);
    }
    if (source && target && out) {
        ACMSTREAMHEADER header;
        for (unsigned i = 0; i < sizeof(header); ++i) {
            ((unsigned char *)&header)[i] = 0;
        }
        header.cbStruct = sizeof(header);
        header.pbSrc = source;
        header.cbSrcLength = DECODE_CHUNK;
        header.pbDst = target;
        header.cbDstLength = chunk_out;
        if (!acm_prepare(stream, &header, 0)) {
            DWORD at = start, carry = 0;
            ok = 1;
            for (int last = 0; ok && !last;) {
                DWORD take = end - at < DECODE_CHUNK - carry ? end - at : DECODE_CHUNK - carry;
                memory_copy(source + carry, data + at, take);
                at += take;
                last = at >= end;
                header.cbSrcLength = carry + take;
                header.cbSrcLengthUsed = header.cbDstLengthUsed = 0;
                if (acm_convert(stream, &header,
                                last ? ACM_STREAMCONVERTF_END : ACM_STREAMCONVERTF_BLOCKALIGN)) {
                    ok = 0;
                    break;
                }
                out = grow(out, used, &capacity, used + header.cbDstLengthUsed);
                if (!out) {
                    ok = 0;
                    break;
                }
                memory_copy(out + used, target, header.cbDstLengthUsed);
                used += header.cbDstLengthUsed;
                carry = header.cbSrcLength - header.cbSrcLengthUsed;
                if (carry && header.cbSrcLengthUsed) {
                    for (DWORD i = 0; i < carry; ++i) {
                        source[i] = source[header.cbSrcLengthUsed + i];
                    }
                } else if (carry >= DECODE_CHUNK) {
                    ok = 0;  // the decoder made no progress
                }
            }
            header.cbSrcLength = DECODE_CHUNK;
            header.cbDstLength = chunk_out;
            acm_unprepare(stream, &header, 0);
        }
    }
    acm_close(stream, 0);
    if (source) {
        HeapFree(GetProcessHeap(), 0, source);
    }
    if (target) {
        HeapFree(GetProcessHeap(), 0, target);
    }
    if (!ok || used <= 44 || used > 0xffffffffu) {
        if (out) {
            HeapFree(GetProcessHeap(), 0, out);
        }
        log_append("RADIO: MP3 decode failed; MP3 served as is.\r\n");
        return NULL;
    }
    static const unsigned char header[44] = {
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
        16, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16, 0,
        'd', 'a', 't', 'a', 0, 0, 0, 0};
    uint32_t bytes = (uint32_t)(used - 44), riff = bytes + 36, sample_rate = rate,
             byte_rate = pcm.nAvgBytesPerSec;
    uint16_t channel_count = (uint16_t)channels, align = pcm.nBlockAlign;
    memory_copy(out, header, sizeof(header));
    memory_copy(out + 4, &riff, 4);
    memory_copy(out + 22, &channel_count, 2);
    memory_copy(out + 24, &sample_rate, 4);
    memory_copy(out + 28, &byte_rate, 4);
    memory_copy(out + 32, &align, 2);
    memory_copy(out + 40, &bytes, 4);
    *output_size = (DWORD)used;
    return out;
}

static void *read_track_file(unsigned index, DWORD *size) {
    wchar_t path[FOLDER_CAPACITY + MAX_PATH + 2];
    if (!track_path(index, path, ARRAY_COUNT(path))) {
        return NULL;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        return NULL;
    }
    LARGE_INTEGER length;
    void *data = NULL;
    *size = 0;
    if (GetFileSizeEx(file, &length) && length.QuadPart > 0 && length.QuadPart <= MAX_FILE_SIZE) {
        data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart);
        if (data && (!ReadFile(file, data, (DWORD)length.QuadPart, size, NULL) ||
                     *size != (DWORD)length.QuadPart)) {
            HeapFree(GetProcessHeap(), 0, data);
            data = NULL;
        }
    }
    CloseHandle(file);
    return data;
}

// --- buffer cache (lock held by callers of the cache_* helpers) --------------------
// Buffers given to FMOD stay alive while their track plays and for 15 s after;
// prefetched ones FMOD never saw can be dropped at any time.

static SoundBuffer *cache_find(unsigned index) {
    for (unsigned i = 0; i < BUFFER_SLOTS; ++i) {
        if (buffers[i].data && buffers[i].track == index) {
            return &buffers[i];
        }
    }
    return NULL;
}

static SoundBuffer *cache_free_slot(unsigned keep) {
    uint64_t now = GetTickCount64();
    SoundBuffer *slot = NULL;
    for (unsigned i = 0; i < BUFFER_SLOTS && !slot; ++i) {
        if (!buffers[i].data) {
            slot = &buffers[i];
        }
    }
    for (unsigned i = 0; i < BUFFER_SLOTS && !slot; ++i) {
        if (!buffers[i].handed && buffers[i].track != keep) {
            slot = &buffers[i];
        }
    }
    for (unsigned i = 0; i < BUFFER_SLOTS && !slot; ++i) {
        if (buffers[i].track != keep && buffers[i].track != served_track &&
            now - buffers[i].handed_at > BUFFER_IDLE_MS) {
            slot = &buffers[i];
        }
    }
    if (slot && slot->data) {
        HeapFree(GetProcessHeap(), 0, slot->data);
        *slot = (SoundBuffer){0};
    }
    return slot;
}

// Copies a cached buffer out, marking it as given to FMOD when `hand` is set.
static void cache_take(SoundBuffer *slot, int hand, void **data, DWORD *size, int *pcm) {
    if (hand) {
        slot->handed = 1;
        slot->handed_at = GetTickCount64();
    }
    *data = slot->data;
    *size = slot->size;
    *pcm = slot->pcm;
}

// Reads (and decodes) a track into the cache. Everything that touches a slot
// happens under the lock, so the prefetch thread can never free a buffer that
// was just handed out.
static int cache_load(unsigned index, int hand, void **out, DWORD *out_size, int *out_pcm) {
    AcquireSRWLockExclusive(&lock);
    SoundBuffer *found = cache_find(index);
    if (found) {
        cache_take(found, hand, out, out_size, out_pcm);
    }
    ReleaseSRWLockExclusive(&lock);
    if (found) {
        return 1;
    }
    DWORD size = 0, pcm_size = 0;
    void *raw = read_track_file(index, &size), *data = raw;
    int pcm = 0;
    if (raw && pcm_decode) {
        void *decoded = decode_mp3(raw, size, &pcm_size);
        if (decoded) {
            HeapFree(GetProcessHeap(), 0, raw);
            data = decoded;
            size = pcm_size;
            pcm = 1;
        }
    }
    if (!data) {
        return 0;
    }
    AcquireSRWLockExclusive(&lock);
    SoundBuffer *slot = cache_find(index);
    if (slot) {
        HeapFree(GetProcessHeap(), 0, data);  // loaded meanwhile by the other thread
    } else if ((slot = cache_free_slot(index)) != NULL) {
        *slot = (SoundBuffer){data, index, GetTickCount64(), size, 0, pcm};
    } else {
        static int busy_logged;
        if (!busy_logged) {
            busy_logged = 1;  // once per session; it can recur on every track switch
            log_append("RADIO: all track buffers busy; one is kept outside the cache.\r\n");
        }
        static SoundBuffer loose;
        loose = (SoundBuffer){data, index, GetTickCount64(), size, 1, pcm};
        slot = &loose;
    }
    cache_take(slot, hand, out, out_size, out_pcm);
    ReleaseSRWLockExclusive(&lock);
    return 1;
}

static void cache_touch(unsigned index) {
    AcquireSRWLockExclusive(&lock);
    SoundBuffer *slot = cache_find(index);
    if (slot) {
        slot->handed_at = GetTickCount64();
    }
    ReleaseSRWLockExclusive(&lock);
}

// Loads the track into a buffer that stays alive while FMOD streams from it.
static void *load_track(unsigned index, DWORD *size) {
    void *data;
    int pcm;
    if (!cache_load(index, 1, &data, size, &pcm)) {
        return NULL;
    }
    log_text(pcm ? "RADIO: streaming (PCM) " : "RADIO: streaming (MP3) ", tracks[index].name);
    // Decode the following track in the background.
    InterlockedExchange(&prefetch_request, (LONG)((index + 1) % track_count));
    return data;
}

static DWORD WINAPI prefetch_thread(void *parameter) {
    (void)parameter;
    for (;;) {
        Sleep(200);
        LONG request = InterlockedExchange(&prefetch_request, -1);
        void *data;
        DWORD size;
        int pcm;
        if (request >= 0 && (unsigned)request < track_count) {
            cache_load((unsigned)request, 0, &data, &size, &pcm);
        }
    }
}

static int parse_key_index(const char *key, unsigned *index) {
    size_t prefix = sizeof(SOUND_KEY_PREFIX) - 1;
    if (!key || !memory_equal(key, SOUND_KEY_PREFIX, prefix)) {
        return 0;
    }
    if (text_equal_ignore_case(key, SILENCE_KEY)) {
        *index = SILENCE_INDEX;
        return 1;
    }
    unsigned value = 0;
    const char *digit = key + prefix;
    if (!*digit) {
        return 0;
    }
    for (; *digit; ++digit) {
        if (*digit < '0' || *digit > '9' || value > MAX_TRACKS) {
            return 0;
        }
        value = value * 10 + (unsigned)(*digit - '0');
    }
    *index = value;
    return value < track_count;
}

// Rewrites a game std::string in place, never reallocating (heap buffers only).
static void rewrite_game_string(unsigned char *value, const char *text) {
    GameString string;
    if (!memory_read(value, &string, sizeof(string)) || string.capacity < 16 ||
        string.size > string.capacity || !string.heap_text ||
        !memory_is_writable(string.heap_text, string.capacity + 1)) {
        return;
    }
    size_t length = text_length(text);
    if (length > string.capacity) {
        length = string.capacity;
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80) {
            --length;  // do not cut a UTF-8 sequence
        }
    }
    memory_copy(string.heap_text, text, length);
    string.heap_text[length] = 0;
    memory_copy(value + 0x10, &length, sizeof(length));
}

// The key the radio passes is the SSO text inside the playlist entry itself
// (SoundName is the entry's first field), so the entry can be renamed.
static void rename_entry(const char *key, const Track *track) {
    unsigned char *entry = (unsigned char *)key;
    size_t size, capacity;
    if (!memory_read(entry + 0x10, &size, sizeof(size)) ||
        !memory_read(entry + 0x18, &capacity, sizeof(capacity)) || capacity != 15 ||
        size != text_length(key)) {
        return;
    }
    rewrite_game_string(entry + ENTRY_DISPLAY_OFFSET, track->title);
    rewrite_game_string(entry + ENTRY_ARTIST_OFFSET, track->artist);
}

// Stops serving for a while if the radio keeps re-requesting (a failing or
// instantly ending stream would otherwise spin).
static int loop_guard(void) {
    uint64_t now = GetTickCount64();
    if (now < guard_until) {
        return 1;
    }
    if (now - guard_window > GUARD_WINDOW_MS) {
        guard_window = now;
        guard_count = 0;
    }
    if (++guard_count > GUARD_LIMIT) {
        guard_until = now + GUARD_PAUSE_MS;
        log_append("RADIO: loop guard: the radio re-requested the custom track too often; "
                   "serving paused for 30 s. Please send this log.\r\n");
        return 1;
    }
    return 0;
}

static int sound_info_hook(void *system, const char *key, void *info) {
    unsigned index;
    if (!parse_key_index(key, &index)) {
        return original_sound_info(system, key, info);
    }
    void *data;
    DWORD size;
    if (index == SILENCE_INDEX) {
        data = silence;
        size = sizeof(silence);
    } else {
        if (loop_guard()) {
            return 18;  // any FMOD error: the radio treats it as a failed item.
        }
        unsigned entry = index, file;
        if (single_entry) {
            // A request after the previous file reached its end means "next".
            if (served_valid && !served_done &&
                served_position + (int)BACK_WINDOW_MS >= (int)tracks[served_track].duration) {
                cursor = (served_track + 1) % track_count;
            }
            file = cursor % track_count;
        } else {
            int n = (int)track_count;
            file = (unsigned)((((int)entry + shift) % n + n) % n);
        }
        data = load_track(file, &size);
        if (!data) {
            log_append("RADIO: track could not be read; skipping.\r\n");
            cursor = (file + 1) % track_count;
            return 18;
        }
        // A transition first re-serves the playing entry; only the next entry
        // ends the step.
        int same = served_valid && !single_entry && entry == served_entry;
        LONG step = same ? 1 : InterlockedExchange(&next_step, 1);
        if (!same) {
            wrap_logged = 0;
        }
        if (step < 0 && !single_entry && served_valid &&
            entry != (served_entry + track_count - 1) % track_count) {
            log_append_value("RADIO: previous track missed; the radio served entry ", (int)entry);
        }
        served_track = file;
        served_entry = entry;
        served_entry_address = key;
        InterlockedIncrement(&serve_count);
        InterlockedExchange64(&transition_at, 0);
        served_end = single_entry ? ENTRY_LENGTH_MS : tracks[entry].duration;
        served_valid = 1;
        served_done = 0;
        served_position = 0;
        served_moved = single_entry || file != entry;
        InterlockedExchange(&zero_offset_pending, served_moved || zero_offset_always);
        if (rename_entries) {
            rename_entry(key, &tracks[file]);
        }
        index = file;
    }
    // FMOD_STUDIO_SOUND_INFO: name_or_data, mode, FMOD_CREATESOUNDEXINFO, subsoundindex.
    unsigned char *fields = info;
    uint32_t mode = FMOD_OPENMEMORY_POINT | FMOD_ACCURATETIME, exinfo_size = EXINFO_SIZE;
    int marker = (int)(SUBSOUND_MARK | index);
    for (unsigned offset = 0; offset < 0xf0; ++offset) {
        fields[offset] = 0;
    }
    memory_copy(fields, &data, sizeof(data));
    memory_copy(fields + 8, &mode, 4);
    memory_copy(fields + 0x10, &exinfo_size, 4);
    memory_copy(fields + 0x14, &size, 4);
    memory_copy(fields + 0xe8, &marker, 4);
    return 0;
}

// `subsound` points into the radio's 0x28-byte request: +0x10 is the output
// slot and +0x20 the player that owns the request.
static int subsound_hook(void *sound, int index, void **subsound) {
    void *player = NULL;
    memory_read((unsigned char *)subsound + 0x10, &player, sizeof(player));
    int ours = ((unsigned)index & SUBSOUND_MASK) == SUBSOUND_MARK;
    unsigned track = (unsigned)index & 0xffff;
    if (player) {
        PlayerTrack *free_slot = NULL;
        for (unsigned i = 0; i < PLAYER_SLOTS; ++i) {
            if (players[i].player == player) {
                free_slot = &players[i];
                break;
            }
            if (!players[i].player && !free_slot) {
                free_slot = &players[i];
            }
        }
        if (free_slot) {
            *free_slot = (PlayerTrack){ours && track != SILENCE_INDEX ? player : NULL, track};
        }
    }
    if (!ours) {
        return original_subsound(sound, index, subsound);
    }
    *subsound = sound;
    if (player && track != SILENCE_INDEX) {
        // The radio resumes an entry at its "live" offset. That offset belongs to
        // the entry, so a different file (or the long single entry) starts at 0.
        int start = 0, live = 0;
        void *instance = NULL;
        unsigned char *fields = player;
        memory_read(fields + PLAYER_START_OFFSET, &live, 4);
        if (InterlockedExchange(&zero_offset_pending, 0) &&
            memory_is_writable(fields + PLAYER_START_OFFSET, 4)) {
            memory_copy(fields + PLAYER_START_OFFSET, &start, 4);
            played_from = 0;
            log_append_value("RADIO: live start offset reset to 0; was ms ", live);
        } else {
            played_from = live;
            log_append_value("RADIO: playing from live offset ms ", live);
        }
        if (set_volume && gain != 1.0f &&
            memory_read(fields + PLAYER_INSTANCE_OFFSET, &instance, sizeof(instance)) && instance) {
            set_volume(instance, gain);
        }
    }
    return 0;
}

// --- hotkeys on the radio thread ---------------------------------------------

static int game_focused(void) {
    if (!foreground_window || !window_process) {
        return 0;
    }
    HWND window = foreground_window();
    DWORD process = 0;
    if (window) {
        window_process(window, &process);
    }
    return process == GetCurrentProcessId();
}

// Ends the current file: the radio then asks for the long entry again and is
// answered with the file at the (already moved) cursor.
static void finish_current(void *instance) {
    served_done = 1;
    set_timeline(instance, (int)served_end - 2000);
}

static int has_vtable(const void *object, unsigned rva) {
    void *table;
    return object && memory_read(object, &table, sizeof(table)) && table == image + rva;
}

// The custom station object, found in RadioSystem's station list by number.
static unsigned char *custom_station(void) {
    unsigned char *system, *begin, *end;
    LONG number = InterlockedCompareExchange(&station_number, 0, 0);
    if (!number || !memory_read(image + RADIO_SYSTEM_GLOBAL, &system, sizeof(system)) ||
        !has_vtable(system, RADIO_SYSTEM_VTABLE) ||
        !memory_read(system + 0x30, &begin, sizeof(begin)) ||
        !memory_read(system + 0x38, &end, sizeof(end)) || end < begin ||
        (size_t)(end - begin) > 64 * 16) {
        return NULL;
    }
    for (unsigned char *at = begin; at < end; at += 16) {
        unsigned char *station;
        int value;
        if (memory_read(at, &station, sizeof(station)) && has_vtable(station, STATION_VTABLE) &&
            memory_read(station + STATION_NUMBER_OFFSET, &value, 4) && value == number) {
            return station;
        }
    }
    return NULL;
}

// The station's live clock (ms, float) for the playlist item `holder`: the
// station advances it every tick and moves to the next item at SampleLength.
static float *station_clock(unsigned char *station, void *holder) {
    for (unsigned i = 0; i < STATION_SLOT_COUNT; ++i) {
        unsigned char *slot = station + STATION_SLOTS_OFFSET + i * STATION_SLOT_SIZE;
        void *item;
        if (memory_read(slot + 8, &item, sizeof(item)) && item == holder &&
            memory_is_writable(slot + SLOT_CLOCK_OFFSET, 4)) {
            return (float *)(slot + SLOT_CLOCK_OFFSET);
        }
    }
    // Fallback: the only running slot (byte +0x1c) is the one playing.
    float *running = NULL;
    for (unsigned i = 0; i < STATION_SLOT_COUNT; ++i) {
        unsigned char *slot = station + STATION_SLOTS_OFFSET + i * STATION_SLOT_SIZE;
        unsigned char active = 0;
        if (memory_read(slot + 0x1c, &active, 1) && active) {
            if (running || !memory_is_writable(slot + SLOT_CLOCK_OFFSET, 4)) {
                return NULL;
            }
            running = (float *)(slot + SLOT_CLOCK_OFFSET);
        }
    }
    if (running) {
        log_append("RADIO: station clock matched by running slot.\r\n");
    }
    return running;
}

typedef void (*ItemEntryFn)(void *, GameShared *);

// The playlist entry an item points to (item->vtable[2] returns it shared).
static const void *item_entry(unsigned char *items, int count, int index) {
    void *item, **table;
    uintptr_t method;
    if (index < 0 || index >= count || !memory_read(items + index * 16, &item, sizeof(item)) ||
        !item || !memory_read(item, &table, sizeof(table)) ||
        !memory_read(table + 2, &method, sizeof(method)) || method < (uintptr_t)image + 0x1000 ||
        method >= (uintptr_t)image + 0x1dcc000) {
        return NULL;
    }
    GameShared out = {0};
    ((ItemEntryFn)method)(item, &out);
    const void *entry = out.object;
    autodrive_game_release_shared(&out);
    return entry;
}

// Steps the playlist that is playing the served entry back, so that its next
// pick (cursor + 1, through the shuffle order when there is one) is the item
// played before it.
static int playlist_back(unsigned char *station) {
    static const unsigned offsets[] = {FREEROAM_PLAYLIST_OFFSET, EVENT_PLAYLIST_OFFSET};
    if (!served_entry_address) {
        return 0;
    }
    for (unsigned p = 0; p < ARRAY_COUNT(offsets); ++p) {
        unsigned char *list, *items, *items_end;
        int *order, *order_end, cursor;
        if (!memory_read(station + offsets[p], &list, sizeof(list)) ||
            !has_vtable(list, PLAYLIST_VTABLE) ||
            !memory_read(list + PLAYLIST_CURSOR_OFFSET, &cursor, 4) ||
            !memory_read(list + 0x40, &items, sizeof(items)) ||
            !memory_read(list + 0x48, &items_end, sizeof(items_end)) ||
            !memory_read(list + 8, &order, sizeof(order)) ||
            !memory_read(list + 0x10, &order_end, sizeof(order_end)) || items_end <= items ||
            cursor < 0) {
            continue;
        }
        int count = (int)((items_end - items) / 16);
        int ordered = order && order_end - order == count;
        // The playing item is at the cursor, or one before it when the station
        // already picked the next one.
        for (int back = 0; back < 2 && back < count; ++back) {
            int position = ((cursor - back) % count + count) % count, index = position;
            if (ordered && !memory_read(order + position, &index, 4)) {
                continue;
            }
            if (item_entry(items, count, index) == served_entry_address &&
                memory_is_writable(list + PLAYLIST_CURSOR_OFFSET, 4)) {
                int target = ((position - 2) % count + count) % count;
                memory_copy(list + PLAYLIST_CURSOR_OFFSET, &target, 4);
                log_append_value("RADIO: playlist cursor was ", cursor);
                log_append_value("RADIO: playlist cursor set to ", target);
                return 1;
            }
        }
    }
    return 0;
}

static int read_game_string(const void *value, char *output, size_t capacity);

// Entry index of a playlist entry: its SoundName (first field) is FH2MUSIC_<n>.
static int entry_index(const void *entry) {
    char key[32];
    unsigned index;
    if (!entry || !read_game_string(entry, key, sizeof(key)) || !parse_key_index(key, &index) ||
        index >= track_count) {
        return -1;
    }
    return (int)index;
}

// Playlist::GetNext(out, mode) for every playlist; only the custom station's
// track playlists are steered. Mode 0 picks items[cursor + 1], mode 1
// items[order[cursor + 1]], both wrapping; the cursor is set so that the pick
// is the wanted folder position.
static void *playlist_next_hook(void *list, GameShared *out, int mode) {
    unsigned char *station = served_valid && !single_entry ? custom_station() : NULL;
    unsigned char *mine[2] = {NULL, NULL}, *items, *items_end;
    int *order = NULL, *order_end = NULL;
    if (station) {
        memory_read(station + FREEROAM_PLAYLIST_OFFSET, &mine[0], sizeof(mine[0]));
        memory_read(station + EVENT_PLAYLIST_OFFSET, &mine[1], sizeof(mine[1]));
    }
    if (!station || (mode != 0 && mode != 1) || ((unsigned char *)list != mine[0] &&
                                                 (unsigned char *)list != mine[1]) ||
        !memory_read((unsigned char *)list + 0x40, &items, sizeof(items)) ||
        !memory_read((unsigned char *)list + 0x48, &items_end, sizeof(items_end)) ||
        items_end <= items || !memory_is_writable((unsigned char *)list + PLAYLIST_CURSOR_OFFSET, 4)) {
        return original_next(list, out, mode);
    }
    int count = (int)((items_end - items) / 16);
    int step = (int)InterlockedCompareExchange(&next_step, 0, 0);
    if (step == 0) {
        // A restart repeats the entry once; the serve that follows is the same
        // entry, so the pick itself ends the step.
        InterlockedCompareExchange(&next_step, 1, 0);
    }
    int current = (int)served_entry;
    int want = ((current + step) % count + count) % count;
    int position = -1;
    if (mode == 1) {
        if (memory_read((unsigned char *)list + 8, &order, sizeof(order)) &&
            memory_read((unsigned char *)list + 0x10, &order_end, sizeof(order_end)) && order &&
            order_end - order == count) {
            for (int p = 0; p < count && position < 0; ++p) {
                int index;
                if (memory_read(order + p, &index, 4) && index >= 0 && index < count &&
                    entry_index(item_entry(items, count, index)) == want) {
                    position = p;
                }
            }
        }
    } else if (entry_index(item_entry(items, count, want)) == want) {
        position = want;
    }
    if (position >= 0) {
        int cursor = position == 0 ? count - 1 : position - 1;
        memory_copy((unsigned char *)list + PLAYLIST_CURSOR_OFFSET, &cursor, 4);
        if (step < 0 && !wrap_logged) {
            log_append_value("RADIO: previous track: the radio is given entry ", want);
        }
        if (wrap_logged) {
            // already logged for this transition
        } else if (step > 0 && want == 0 && current == count - 1) {
            wrap_logged = 1;
            log_append("RADIO: playlist wrapped to first track.\r\n");
        } else if (step < 0 && want == count - 1 && current == 0) {
            wrap_logged = 1;
            log_append("RADIO: playlist wrapped to last track.\r\n");
        }
    } else {
        log_append("RADIO: next track not found in the playlist; the radio picks.\r\n");
    }
    return original_next(list, out, mode);
}

static void **history_find_hook(void **found, void *first, void *last, void *value) {
    unsigned char *station = served_valid && !single_entry ? custom_station() : NULL;
    void *head = NULL;
    original_find(found, first, last, value);
    if (station && memory_read(station + STATION_HISTORY_OFFSET, &head, sizeof(head)) &&
        head == last && *found != last) {
        *found = last;  // not "recently played": the folder order decides
    }
    return found;
}

static void player_update_hook(void *player, float dt) {
    static uint64_t last_poll;
    static int next_down, back_down;
    original_update(player, dt);
    if (!track_count) {
        return;
    }
    void *active = NULL;
    for (unsigned i = 0; i < PLAYER_SLOTS; ++i) {
        if (players[i].player == player && players[i].track < track_count) {
            active = player;
        }
    }
    void *instance = NULL;
    int state = 0, position = 0;
    if (active &&
        (!memory_read((unsigned char *)player + PLAYER_STATE_OFFSET, &state, 4) ||
         state != PLAYER_STATE_PLAYING ||
         !memory_read((unsigned char *)player + PLAYER_INSTANCE_OFFSET, &instance,
                      sizeof(instance)) ||
         !instance ||
         !memory_read((unsigned char *)player + PLAYER_POSITION_OFFSET, &position, 4))) {
        active = NULL;
    }
    if (active && served_valid) {
        static uint64_t last_touch;
        uint64_t moment = GetTickCount64();
        if (moment - last_touch > 2000) {
            last_touch = moment;
            cache_touch(served_track);
        }
    }
    if (active && served_valid && !served_done) {
        if (position > served_position) {
            served_position = position;
        }
        // Only a file that differs from the radio's entry can end before the
        // radio does; an unshifted file ends together with its own entry.
        if (served_moved &&
            position + (int)END_MARGIN_MS >= (int)tracks[served_track].duration) {
            cursor = (served_track + 1) % track_count;
            ++shift;
            finish_current(instance);
            log_append("RADIO: track finished; next from the folder.\r\n");
            return;
        }
    }
    uint64_t now = GetTickCount64();
    if (now - last_poll < KEY_POLL_MS) {
        return;
    }
    last_poll = now;
    int focused = game_focused();
    int next = focused && radio_binding_down(BIND_RADIO_NEXT, next_key);
    int back = focused && radio_binding_down(BIND_RADIO_RESTART, restart_key);
    int next_pressed = next && !next_down, back_pressed = back && !back_down;
    next_down = next;
    back_down = back;
    if (!active || !served_valid || (!next_pressed && !back_pressed)) {
        return;
    }
    LONG64 pending = InterlockedCompareExchange64(&transition_at, 0, 0);
    if (pending && (LONG64)now - pending < 4000) {
        log_append("RADIO: key ignored; the previous change is still in progress.\r\n");
        return;
    }
    if (shift_keys) {
        if (next_pressed) {
            cursor = (served_track + 1) % track_count;
            ++shift;
            finish_current(instance);
            log_append("RADIO: next track requested (shift).\r\n");
        } else if (position > (int)BACK_WINDOW_MS) {
            set_timeline(instance, 0);
            served_position = 0;
            log_append("RADIO: track restarted (shift).\r\n");
        } else {
            cursor = (served_track + track_count - 1) % track_count;
            --shift;
            finish_current(instance);
            log_append("RADIO: previous track requested (shift).\r\n");
        }
        return;
    }
    // Native route: move the station's own clock, so the radio itself picks the
    // next (or previous) entry at offset 0 and plays it as a normal transition.
    void *holder = NULL;
    unsigned char *station = custom_station();
    float *clock = NULL;
    if (station && has_vtable(player, PLAYER_VTABLE) &&
        memory_read((unsigned char *)player + PLAYER_HOLDER_OFFSET, &holder, sizeof(holder)) &&
        holder) {
        clock = station_clock(station, holder);
    }
    if (!clock) {
        log_append("RADIO: station clock not found; key ignored.\r\n");
        return;
    }
    // The F6 window counts from where this track started playing (a tuned-in
    // station joins mid-track). Restart: the radio resyncs a player to its
    // slot clock (live offset), so a seek alone does not hold; instead the
    // station moves on to the same entry (step 0), which plays from 0.
    int restart = back_pressed && position - played_from > (int)BACK_WINDOW_MS;
    if (restart && !original_next) {
        *clock = 0.0f;
        set_timeline(instance, 0);
        served_position = 0;
        log_append("RADIO: track restarted (station clock).\r\n");
        return;
    }
    if (restart) {
        InterlockedExchange(&next_step, 0);
        log_append_value("RADIO: track restart requested; it was at ms ", position);
    } else if (back_pressed && original_next) {
        InterlockedExchange(&next_step, -1);
    } else if (back_pressed && !playlist_back(station)) {
        log_append("RADIO: playlist position not found; F6 acts as next.\r\n");
    }
    served_done = 1;
    InterlockedExchange(&transition_serve, InterlockedCompareExchange(&serve_count, 0, 0));
    InterlockedExchange64(&transition_at, (LONG64)now ? (LONG64)now : 1);
    *clock = 1.0e9f;  // the station clamps it to SampleLength and moves on
    log_append(restart        ? "RADIO: the station plays the same entry again from 0.\r\n"
               : back_pressed ? "RADIO: previous track requested (station clock).\r\n"
                              : "RADIO: next track requested (station clock).\r\n");
}

// Reads a game std::string into `output` (NUL-terminated); 0 if unreadable.
static int read_game_string(const void *value, char *output, size_t capacity) {
    GameString text;
    if (!value || !memory_read(value, &text, sizeof(text)) || text.size >= capacity ||
        text.capacity < text.size) {
        return 0;
    }
    const char *source = text.capacity >= 16 ? text.heap_text : text.inline_text;
    if (!memory_read(source, output, text.size)) {
        return 0;
    }
    output[text.size] = 0;
    return 1;
}

// 0x668810 is effectively RadioModule::IsStationLocked(name): ChangeStation
// skips names it returns true for, and the HUD radio banner (0x78a070) is only
// shown when it returns false. UnlockAllStations makes it false for every name.
static unsigned char unlocked_hook(void *module, const void *name) {
    unsigned char locked = original_unlocked(module, name);
    char text[TEXT_CAPACITY];
    if (!locked) {
        return 0;
    }
    if (InterlockedCompareExchange(&unlock_all_live, 0, 0)) {
        return 0;
    }
    if (InterlockedCompareExchange(&station_number, 0, 0) && injected_name[0] &&
        read_game_string(name, text, sizeof(text)) && text_equal_ignore_case(text, injected_name)) {
        return 0;
    }
    return locked;
}

// --- RadioModule (game thread): station map, current name, D-pad cycle ----------

typedef struct {
    char names[MAX_MAP_STATIONS][64];
    unsigned char locked[MAX_MAP_STATIONS];
    unsigned count;
} StationMap;

// In-order walk of the RadioModule station map (MSVC tree, name at node+0x28).
static int read_station_map(unsigned char *module, StationMap *map) {
    unsigned char *head, *node, *child, *parent;
    unsigned char nil = 1;
    map->count = 0;
    if (!memory_read(module + MODULE_MAP_OFFSET, &head, sizeof(head)) || !head ||
        !memory_read(head, &node, sizeof(node))) {
        return 0;
    }
    for (unsigned guard = 0; node && node != head && guard < 256; ++guard) {
        if (!memory_read(node + 0x19, &nil, 1) || nil) {
            break;
        }
        if (map->count < MAX_MAP_STATIONS) {
            unsigned i = map->count;
            if (!read_game_string(node + MAP_NODE_NAME_OFFSET, map->names[i], sizeof(map->names[i]))) {
                map->names[i][0] = 0;
            }
            map->locked[i] = original_unlocked ? original_unlocked(module, node + MAP_NODE_NAME_OFFSET) : 0;
            ++map->count;
        }
        // successor
        if (!memory_read(node + 0x10, &child, sizeof(child)) || !child ||
            !memory_read(child + 0x19, &nil, 1)) {
            return 0;
        }
        if (!nil) {
            node = child;
            while (memory_read(node, &child, sizeof(child)) && child &&
                   memory_read(child + 0x19, &nil, 1) && !nil) {
                node = child;
            }
        } else {
            if (!memory_read(node + 8, &parent, sizeof(parent))) {
                return 0;
            }
            while (parent && memory_read(parent + 0x19, &nil, 1) && !nil) {
                if (!memory_read(parent + 0x10, &child, sizeof(child)) || child != node) {
                    break;
                }
                node = parent;
                if (!memory_read(node + 8, &parent, sizeof(parent))) {
                    return 0;
                }
            }
            node = parent;
        }
    }
    return 1;
}

static int current_station_name(unsigned char *module, char *output, size_t capacity) {
    unsigned char *wrapper;
    return memory_read(module + MODULE_WRAPPER_OFFSET, &wrapper, sizeof(wrapper)) && wrapper &&
           read_game_string(wrapper + WRAPPER_CURRENT_OFFSET, output, capacity);
}

static void select_station(const char *text);

static int map_index(const StationMap *map, const char *text) {
    for (unsigned i = 0; i < map->count; ++i) {
        if (text_equal_ignore_case(map->names[i], text)) {
            return (int)i;
        }
    }
    return -1;
}

// Wraps RadioModule::ChangeStation (D-pad). If the custom station is missing
// from the module's map, it is spliced into the cycle after the last and before
// the first selectable station; otherwise the game's own cycle is used as is.
static void change_station_hook(void *module, int forward) {
    static StationMap map;
    char current[64];
    if (!set_station || !InterlockedCompareExchange(&station_number, 0, 0) || !injected_name[0] ||
        !read_station_map(module, &map) || map_index(&map, injected_name) >= 0 ||
        !current_station_name(module, current, sizeof(current))) {
        original_change(module, forward);
        return;
    }
    int first = -1, last = -1;
    for (unsigned i = 0; i < map.count; ++i) {
        if (!map.names[i][0]) {
            first = -1;  // unreadable map: leave cycling to the game
            break;
        }
        if (!map.locked[i] || InterlockedCompareExchange(&unlock_all_live, 0, 0)) {
            if (first < 0) {
                first = (int)i;
            }
            last = (int)i;
        }
    }
    if (first < 0) {
        original_change(module, forward);
        return;
    }
    if (text_equal_ignore_case(current, injected_name)) {
        select_station(map.names[forward ? first : last]);
        log_append("RADIO: D-pad left the custom station.\r\n");
        return;
    }
    int at = map_index(&map, current);
    if ((forward && at == last) || (!forward && at == first)) {
        select_station(injected_name);
        log_append("RADIO: D-pad reached the custom station.\r\n");
        return;
    }
    original_change(module, forward);
}

// Logs what the radio sees, once after load and on every station change.
static void radio_diagnostics(void) {
    static int listed;
    static char last[64];
    static uint64_t last_poll;
    uint64_t now = GetTickCount64();
    if (!module_getter || !InterlockedCompareExchange(&station_number, 0, 0) ||
        now - last_poll < 500) {
        return;
    }
    last_poll = now;
    void *manager = NULL;
    GameShared shared = {0};
    if (!memory_read(image + MODULE_MANAGER_GLOBAL, &manager, sizeof(manager)) || !manager) {
        return;
    }
    module_getter(manager, &shared);
    unsigned char *module = shared.object;
    if (module) {
        char current[64];
        if (!listed) {
            static StationMap map;
            unsigned char *system, *begin, *end;
            listed = 1;
            if (memory_read(image + RADIO_SYSTEM_GLOBAL, &system, sizeof(system)) &&
                has_vtable(system, RADIO_SYSTEM_VTABLE) &&
                memory_read(system + 0x30, &begin, sizeof(begin)) &&
                memory_read(system + 0x38, &end, sizeof(end)) && end >= begin) {
                log_append_value("RADIO: radio system stations: ", (int)((end - begin) / 16));
                for (unsigned char *at = begin; at < end && at < begin + 32 * 16; at += 16) {
                    unsigned char *station;
                    int number = 0;
                    char name[64];
                    if (memory_read(at, &station, sizeof(station)) &&
                        has_vtable(station, STATION_VTABLE) &&
                        memory_read(station + STATION_NUMBER_OFFSET, &number, 4) &&
                        read_game_string(station + 0xe0, name, sizeof(name))) {
                        log_append("RADIO:   station ");
                        log_append(name);
                        log_append_value(" number ", number);
                    }
                }
            }
            if (read_station_map(module, &map)) {
                log_append_value("RADIO: D-pad cycle (RadioModule map) stations: ", (int)map.count);
                for (unsigned i = 0; i < map.count; ++i) {
                    log_append("RADIO:   cycle ");
                    log_append(map.names[i]);
                    log_append(map.locked[i] ? " (locked by progression)\r\n" : "\r\n");
                }
                log_append(map_index(&map, injected_name) >= 0
                               ? "RADIO: custom station is in the D-pad cycle.\r\n"
                               : "RADIO: custom station is NOT in the D-pad cycle; it is spliced in "
                                 "after the last station.\r\n");
            } else {
                log_append("RADIO: D-pad cycle map not readable.\r\n");
            }
        }
        if (current_station_name(module, current, sizeof(current)) &&
            !text_equal_ignore_case(current, last)) {
            memory_copy(last, current, sizeof(last));
            log_append("RADIO: current station is now '");
            log_append(current);
            unsigned char *wrapper = NULL;
            int locked = original_unlocked &&
                         memory_read(module + MODULE_WRAPPER_OFFSET, &wrapper, sizeof(wrapper)) &&
                         wrapper && original_unlocked(module, wrapper + WRAPPER_CURRENT_OFFSET);
            log_append(locked ? "' (game marks it locked)\r\n" : "' (not locked)\r\n");
        }
    }
    autodrive_game_release_shared(&shared);
}

static void select_station(const char *text) {
    void *audio = NULL;
    if (!set_station || !memory_read(image + AUDIO_MANAGER, &audio, sizeof(audio)) || !audio) {
        return;
    }
    GameString name;
    size_t length = text_length(text);
    char *heap = NULL;
    for (unsigned i = 0; i < sizeof(name); ++i) {
        ((unsigned char *)&name)[i] = 0;
    }
    if (length < 16) {
        memory_copy(name.inline_text, text, length + 1);
        name.capacity = 15;
    } else {
        heap = HeapAlloc(GetProcessHeap(), 0, length + 1);
        if (!heap) {
            return;
        }
        memory_copy(heap, text, length + 1);
        name.heap_text = heap;
        name.capacity = length;
    }
    name.size = length;
    // Same call the RadioSetStation state node makes; it copies the name.
    set_station(NULL, &name);
    if (heap) {
        HeapFree(GetProcessHeap(), 0, heap);
    }
}

// Game thread, local car update: The RadioStation binding (F9) tunes the car radio to the custom station.
// A change that produced no new serve within 3 s: tune the station again, which
// gives the radio a fresh player at the live position.
static void transition_watchdog(void) {
    LONG64 at = InterlockedCompareExchange64(&transition_at, 0, 0);
    if (!at || (LONG64)GetTickCount64() - at < 3000 ||
        InterlockedCompareExchange(&serve_count, 0, 0) !=
            InterlockedCompareExchange(&transition_serve, 0, 0)) {
        return;
    }
    InterlockedExchange64(&transition_at, 0);
    void *manager = NULL;
    GameShared shared = {0};
    char current[64];
    int ours = 0;
    if (module_getter && memory_read(image + MODULE_MANAGER_GLOBAL, &manager, sizeof(manager)) &&
        manager) {
        module_getter(manager, &shared);
        ours = shared.object && current_station_name(shared.object, current, sizeof(current)) &&
               text_equal_ignore_case(current, injected_name);
        autodrive_game_release_shared(&shared);
    }
    if (ours) {
        select_station(injected_name);
        log_append("RADIO: watchdog: no track followed the change; station tuned again.\r\n");
    } else {
        log_append("RADIO: watchdog: change abandoned (another station is playing).\r\n");
    }
}

void radio_station_game_tick(void) {
    static uint64_t last_poll;
    static int select_down;
    radio_diagnostics();
    transition_watchdog();
    if (!set_station || (!select_key && !binding_xbox(BIND_RADIO_STATION)) ||
        !InterlockedCompareExchange(&station_number, 0, 0) || !injected_name[0]) {
        return;
    }
    uint64_t now = GetTickCount64();
    if (now - last_poll < KEY_POLL_MS) {
        return;
    }
    last_poll = now;
    int down = game_focused() && radio_binding_down(BIND_RADIO_STATION, select_key);
    int pressed = down && !select_down;
    select_down = down;
    if (!pressed) {
        return;
    }
    select_station(injected_name);
    log_append("RADIO: custom station selected (RadioStation binding).\r\n");
}

static void station_banks_hook(void *station, void *a, void *b) {
    int number = 0;
    LONG custom = InterlockedCompareExchange(&station_number, 0, 0);
    if (custom && memory_read((unsigned char *)station + STATION_NUMBER_OFFSET, &number, 4) &&
        number == custom) {
        log_append("RADIO: FMOD bank loads skipped for the custom station.\r\n");
        return;
    }
    original_banks(station, a, b);
}

// --- patch installation -------------------------------------------------------

static int slot_matches(unsigned rva, unsigned expected) {
    uintptr_t value;
    return memory_read(image + rva, &value, sizeof(value)) &&
           value == (uintptr_t)(image + expected);
}

static int call_matches(unsigned rva, unsigned expected) {
    unsigned char bytes[5];
    int32_t displacement;
    if (!memory_read(image + rva, bytes, sizeof(bytes)) || bytes[0] != 0xe8) {
        return 0;
    }
    memory_copy(&displacement, bytes + 1, 4);
    return image + rva + 5 + displacement == image + expected;
}

static int cave_free_size(unsigned rva, unsigned size) {
    unsigned char bytes[CAVE_SIZE];
    if (size > CAVE_SIZE || !memory_read(image + rva, bytes, size)) {
        return 0;
    }
    for (unsigned i = 0; i < size; ++i) {
        if (bytes[i] != 0xcc) {
            return 0;
        }
    }
    return 1;
}

static int cave_free(unsigned rva) {
    unsigned char bytes[CAVE_SIZE];
    if (!memory_read(image + rva, bytes, sizeof(bytes))) {
        return 0;
    }
    for (unsigned i = 0; i < CAVE_SIZE; ++i) {
        if (bytes[i] != 0xcc) {
            return 0;
        }
    }
    return 1;
}

static int write_bytes(unsigned rva, const void *data, size_t size) {
    DWORD previous, ignored;
    if (!VirtualProtect(image + rva, size, PAGE_EXECUTE_READWRITE, &previous)) {
        return 0;
    }
    memory_copy(image + rva, data, size);
    VirtualProtect(image + rva, size, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), image + rva, size);
    return 1;
}

static int write_slot(unsigned rva, void *callback) {
    uintptr_t value = (uintptr_t)callback;
    return write_bytes(rva, &value, sizeof(value));
}

// Routes a rel32 call through a 12-byte absolute jump in an int3 cave.
static int redirect_call(unsigned call_rva, unsigned cave_rva, void *callback) {
    unsigned char thunk[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
    uintptr_t target = (uintptr_t)callback;
    memory_copy(thunk + 2, &target, sizeof(target));
    int32_t displacement = (int32_t)(cave_rva - (call_rva + 5));
    unsigned char call[5] = {0xe8, 0, 0, 0, 0};
    memory_copy(call + 1, &displacement, 4);
    return write_bytes(cave_rva, thunk, sizeof(thunk)) && write_bytes(call_rva, call, sizeof(call));
}

static void load_input(void) {
    HMODULE user32 = LoadLibraryExW(L"user32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (user32) {
        key_state = (KeyStateFn)(uintptr_t)GetProcAddress(user32, "GetAsyncKeyState");
        foreground_window =
            (ForegroundWindowFn)(uintptr_t)GetProcAddress(user32, "GetForegroundWindow");
        window_process =
            (WindowProcessFn)(uintptr_t)GetProcAddress(user32, "GetWindowThreadProcessId");
    }
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    set_pointer = kernel ? (SetPointerFn)(uintptr_t)GetProcAddress(kernel, "SetFilePointerEx")
                         : NULL;
}

// Optional extras; each is verified on its own so the station itself still works
// if either does not match.
static int install_lock_hook(void) {
    original_unlocked = (StationUnlockedFn)(image + STATION_UNLOCKED);
    int calls = cave_free(UNLOCKED_CAVE);
    for (unsigned i = 0; i < ARRAY_COUNT(unlocked_calls) && calls; ++i) {
        calls = call_matches(unlocked_calls[i], STATION_UNLOCKED);
    }
    if (calls) {
        unsigned char thunk[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
        uintptr_t target = (uintptr_t)unlocked_hook;
        memory_copy(thunk + 2, &target, sizeof(target));
        calls = write_bytes(UNLOCKED_CAVE, thunk, sizeof(thunk));
        for (unsigned i = 0; i < ARRAY_COUNT(unlocked_calls) && calls; ++i) {
            int32_t displacement = (int32_t)(UNLOCKED_CAVE - (unlocked_calls[i] + 5));
            unsigned char call[5] = {0xe8, 0, 0, 0, 0};
            memory_copy(call + 1, &displacement, 4);
            calls = write_bytes(unlocked_calls[i], call, sizeof(call));
        }
    }
    InterlockedExchange(&unlock_hooked, calls);
    log_append(!calls      ? "RADIO: lock check did not match; stations follow progression.\r\n"
               : unlock_all ? "RADIO: all radio stations unlocked (UnlockAllStations=1).\r\n"
                            : "RADIO: station lock hook ready (UnlockAllStations=0).\r\n");
    return calls;
}

static void install_unlock_and_select(void) {
    int change = cave_free_size(CHANGE_CAVE, 12);
    for (unsigned i = 0; i < ARRAY_COUNT(change_calls) && change; ++i) {
        change = call_matches(change_calls[i], CHANGE_STATION);
    }
    static const unsigned char change_prologue[8] = {0x48, 0x8b, 0xc4, 0x57, 0x48, 0x83, 0xec, 0x70};
    static const unsigned char getter_prologue[10] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x30,
                                                      0x48, 0x8b, 0x49, 0x08};
    unsigned char probe[10];
    change = change && memory_read(image + CHANGE_STATION, probe, 8) &&
             memory_equal(probe, change_prologue, 8);
    if (memory_read(image + MODULE_GETTER, probe, 10) && memory_equal(probe, getter_prologue, 10)) {
        module_getter = (ModuleGetterFn)(image + MODULE_GETTER);
    }
    if (change) {
        original_change = (ChangeStationFn)(image + CHANGE_STATION);
        unsigned char thunk[12] = {0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xe0};
        uintptr_t target = (uintptr_t)change_station_hook;
        memory_copy(thunk + 2, &target, sizeof(target));
        change = write_bytes(CHANGE_CAVE, thunk, sizeof(thunk));
        for (unsigned i = 0; i < ARRAY_COUNT(change_calls) && change; ++i) {
            int32_t offset = (int32_t)(CHANGE_CAVE - (change_calls[i] + 5));
            unsigned char call[5] = {0xe8, 0, 0, 0, 0};
            memory_copy(call + 1, &offset, 4);
            change = write_bytes(change_calls[i], call, sizeof(call));
        }
    }
    log_append(change ? "RADIO: D-pad cycle hook installed.\r\n"
                      : "RADIO: D-pad cycle hook did not match; native cycle only.\r\n");
    // push rbx; sub rsp,40h; mov rbx,[rip+audio manager]; test rbx,rbx
    static const unsigned char prologue[9] = {0x40, 0x53, 0x48, 0x83, 0xec, 0x40, 0x48, 0x8b, 0x1d};
    unsigned char bytes[13];
    int32_t displacement = 0;
    if (memory_read(image + SET_STATION, bytes, sizeof(bytes)) &&
        memory_equal(bytes, prologue, sizeof(prologue))) {
        memory_copy(&displacement, bytes + 9, 4);
    }
    if (displacement && image + SET_STATION + 13 + displacement == image + AUDIO_MANAGER) {
        set_station = (SetStationFn)(image + SET_STATION);
        log_append("RADIO: the RadioStation binding tunes straight to the custom station.\r\n");
    } else {
        log_append("RADIO: station select path did not match; the RadioStation binding is disabled.\r\n");
    }
}

void radio_station_install(unsigned char *game_image, const wchar_t *directory) {
    image = game_image;
    if (!read_settings(directory)) {
        // The custom station is off: the station lock hook only for
        // UnlockAllStations=1; nothing at all otherwise.
        if (unlock_all) {
            install_lock_hook();
        }
        return;
    }
    if (!slot_matches(XML_READ_SLOT, XML_READ) || !slot_matches(PLAYER_UPDATE_SLOT, PLAYER_UPDATE) ||
        !call_matches(SOUND_INFO_CALL, SOUND_INFO) || !call_matches(SUBSOUND_CALL, SUBSOUND) ||
        !cave_free(SOUND_INFO_CAVE) || !cave_free(SUBSOUND_CAVE) ||
        (skip_banks && !slot_matches(STATION_BANKS_SLOT, STATION_BANKS))) {
        log_append("RADIO: game code did not match; custom station disabled.\r\n");
        return;
    }
    load_input();
    if (!set_pointer) {
        log_append("RADIO: SetFilePointerEx unavailable; custom station disabled.\r\n");
        return;
    }
    build_silence();
    original_read = (XmlReadFn)(image + XML_READ);
    original_update = (PlayerUpdateFn)(image + PLAYER_UPDATE);
    original_banks = (StationBanksFn)(image + STATION_BANKS);
    original_sound_info = (SoundInfoFn)(image + SOUND_INFO);
    original_subsound = (SubSoundFn)(image + SUBSOUND);
    set_timeline = (SetTimelineFn)(image + SET_TIMELINE);
    static const unsigned char volume_prologue[13] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x57, 0x48,
                                                      0x81, 0xec, 0x50, 0x01, 0x00, 0x00};
    unsigned char volume_bytes[13];
    if (memory_read(image + SET_VOLUME, volume_bytes, sizeof(volume_bytes)) &&
        memory_equal(volume_bytes, volume_prologue, sizeof(volume_prologue))) {
        set_volume = (SetVolumeFn)(image + SET_VOLUME);
    } else {
        log_append("RADIO: event volume call did not match; Volume setting ignored.\r\n");
    }
    game_alloc = (GameAllocFn)(image + GAME_ALLOC);
    game_free = (GameFreeFn)(image + GAME_FREE);
    // Sound hooks first: they only answer FH2MUSIC_ keys, which exist only once the
    // XML hook has added the station.
    if (!redirect_call(SOUND_INFO_CALL, SOUND_INFO_CAVE, (void *)sound_info_hook) ||
        !redirect_call(SUBSOUND_CALL, SUBSOUND_CAVE, (void *)subsound_hook) ||
        !write_slot(PLAYER_UPDATE_SLOT, (void *)player_update_hook) ||
        (skip_banks && !write_slot(STATION_BANKS_SLOT, (void *)station_banks_hook)) ||
        !write_slot(XML_READ_SLOT, (void *)xml_read_hook)) {
        log_append("RADIO: a hook could not be written; custom station may be partial.\r\n");
        return;
    }
    install_lock_hook();
    install_unlock_and_select();
    if (!single_entry && slot_matches(PLAYLIST_NEXT_SLOT, PLAYLIST_NEXT)) {
        original_next = (PlaylistNextFn)(image + PLAYLIST_NEXT);
        if (!write_slot(PLAYLIST_NEXT_SLOT, (void *)playlist_next_hook)) {
            original_next = NULL;
        }
    }
    if (original_next && call_matches(HISTORY_FIND_CALL, HISTORY_FIND) &&
        cave_free(HISTORY_FIND_CAVE)) {
        original_find = (HistoryFindFn)(image + HISTORY_FIND);
        if (!redirect_call(HISTORY_FIND_CALL, HISTORY_FIND_CAVE, (void *)history_find_hook)) {
            original_find = NULL;
        }
    }
    if (original_next && !original_find) {
        log_append("RADIO: recently-played check did not match; F6 may not step back.\r\n");
    }
    log_append(original_next ? "RADIO: folder order with wrap-around installed.\r\n"
                             : "RADIO: playlist next-pick did not match; the radio orders tracks.\r\n");
    if (pcm_decode) {
        HANDLE worker = CreateThread(NULL, 0, prefetch_thread, NULL, 0, NULL);
        if (worker) {
            CloseHandle(worker);
        }
        log_append("RADIO: MP3s are decoded to PCM by Windows (PcmDecode=1).\r\n");
    }
    log_text("RADIO: custom station hooks installed; folder ", folder);
}
