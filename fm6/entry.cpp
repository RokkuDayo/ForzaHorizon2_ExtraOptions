#include <windows.h>
#include <cwchar>
#include "pe.hpp"
#include "log.hpp"
#include "live.hpp"
#include "fov.hpp"
#include "blur_amount.hpp"
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
    core::log::write("FM6 global integration candidate 1");
    initialized=true;
    fm6live::initialize(game,ini);
    install_fm6_blur_amount(game,ini);
    install_fm6_fov(game,ini);
    return 1;
}
