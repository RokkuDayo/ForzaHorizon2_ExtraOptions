#include <windows.h>
#include <cwchar>
#include "pe.hpp"
#include "log.hpp"
#include "live.hpp"
#include "fov.hpp"
#include "blur_amount.hpp"
#include "patch.hpp"
#include "ini.hpp"

namespace {
using core::patch::Site;
using core::patch::Option;
// FM6 frontend/content patches from Victor Alves (MIT; see LICENSE-Victor.txt).
constexpr Site k_skip_intro[] = {
    { 0xa06b33, "41 38 b4 24 d7 30 00 00 74 ??", 8, "66 90" },
    { 0xa06c4d, "41 38 b4 24 d7 30 00 00 74 ??", 8, "66 90" },
    { 0xa07e0c, "41 38 b4 24 d7 30 00 00 74 ??", 8, "66 90" },
    { 0xa08eb6, "44 38 b8 d7 30 00 00 75", 7, "eb" },
};

constexpr Site k_skip_presentation_video[] = {
    { 0xce1c55, "48 8d 4b 28 e8 32 d8 46 ff", 4, "0f 1f 44 00 00" },
    { 0xce1eb4, "85 c0 75 19", 2, "66 90" },
};

constexpr Site k_unlock_dlc[] = {
    { 0xa8ad80, "48 89 5c 24 10 56 57 41 56 48 83 ec 20 48 8d b9", 0, "b8 01 00 00 00 c3" }, // mov eax,1; ret
    { 0xa40f50,
      "48 8b 0d d9 dd 39 01 48 83 c2 08 e8 20 9e 04 00"
      "|48 89 d9 48 b8 ?? ?? ?? ?? ?? ?? ?? ?? ff d0 90"
      "|48 89 f9 48 b8 ?? ?? ?? ?? ?? ?? ?? ?? ff d0 90",
      0, "b0 01 0f 1f 84 00 00 00 00 00 66 0f 1f 44 00 00" }, // mov al,1; nop8; nop6
};
constexpr Option frontend_options[] = {
    {"SkipIntro", k_skip_intro},
    {"SkipPresentationVideo", k_skip_presentation_video},
    {"UnlockDLC", k_unlock_dlc},
};
}

extern "C" __declspec(dllexport) int FM6Initialize(unsigned title, unsigned char* game, const wchar_t* dir) {
    static bool initialized=false;
    if(initialized) return 1;
    if(title != 0x64f9397eu || !game || !dir) return 0;
    const auto* nt=core::pe::headers(game);
    if(!nt || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt->FileHeader.TimeDateStamp != 0x58afb1cd || nt->OptionalHeader.SizeOfImage != 0x1fc8000) return 0;
    wchar_t ini[MAX_PATH], log[MAX_PATH];
    if(wcslen(dir)+32>=MAX_PATH) return 0;
    wcscpy(ini,dir); wcscat(ini,L"\\FM6_ExtraOptions.ini");
    wcscpy(log,dir); wcscat(log,L"\\FM6_ExtraOptions.log");
    if(!core::log::open(log)) return 0;
    core::log::write("FM6 global integration candidate 2");
    initialized=true;
    for (const auto& option : frontend_options) {
        if (core::ini::get_int(ini, "FM6", option.key, 0) == 1)
            core::patch::apply(game, nt->OptionalHeader.SizeOfImage, option, "FM6");
    }
    fm6live::initialize(game,ini);
    install_fm6_blur_amount(game,ini);
    install_fm6_fov(game,ini);
    return 1;
}
