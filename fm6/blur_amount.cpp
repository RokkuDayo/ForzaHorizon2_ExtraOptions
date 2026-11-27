#include "blur_amount.hpp"
#include "live.hpp"
#include <cstddef>
#include "hook.hpp"
#include "patch.hpp"
#include "pe.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <windows.h>

namespace {
// MS x64 ABI: four pointer arguments, a stack float, and a stack pointer.
// The caller does not consume a return value. Preserve every original argument.
using Prepare = void (*)(void *, void *, void *, void *, float, void *);
Prepare original_prepare = nullptr;
float multiplier = 1.0f;
std::atomic<unsigned> calls{0};

void prepare_hook(void *self, void *camera, void *view, void *projection,
                  float aspect, void *context) {
    original_prepare(self, camera, view, projection, aspect, context);
    auto *p = static_cast<uint8_t *>(self);
    // Native routine writes float4 at self+0xa10; RDEF VelocityParams1
    // is at 0x830 relative to the constant buffer base self+0x1e0.
    // Only its Z lane (native source self+0xb84) is scaled.
    float before, after;
    std::memcpy(&before, p + 0xa18, sizeof before);
    after = before;
    fm6live::refresh();
    const float scaled = before * (fm6live::enabled() ? fm6live::intensity()/100.0f : multiplier);
    if (std::isfinite(before) && std::isfinite(scaled)) {
        after = scaled;
        std::memcpy(p + 0xa18, &after, sizeof after);
    }
    const unsigned n = calls.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 16 || n % 600 == 0) {
        float source;
        uint32_t samples;
        std::memcpy(&source, p + 0xb84, sizeof source);
        std::memcpy(&samples, p + 0xb90, sizeof samples);
        core::log::write("FM6: VelocityParams1.z #%u enabled=%u samples=%u source=%.6f original=%.6f result=%.6f",
                         n, unsigned(p[0xb80]), unsigned(samples), double(source), double(before), double(after));
    }
}
}

void install_fm6_blur_amount(uint8_t *base, const wchar_t *ini_path) {
    const int percent = std::clamp(core::ini::get_int(ini_path, "FM6", "MotionBlurIntensity", 100), 0, 400);
    if (percent == 100 && !fm6live::enabled()) {
        core::log::write("FM6: MotionBlurIntensity=100: native renderer untouched");
        return;
    }
    constexpr uint32_t context_rva = 0x19dd9d;
    constexpr uint32_t site_rva = 0x19ddb7;
    constexpr uint32_t target_rva = 0x3fd9a4;
    constexpr uint32_t pack_rva = 0x3fda25;
    constexpr char context_expected[] = "48 89 44 24 28 4c 8d 4c 24 40 4c 8d 45 80 c5 7a 11 54 24 20 48 8b d7 48 8b cb e8 e8 fb 25 00";
    constexpr char target_expected[] = "48 8b c4 48 89 58 08 48 89 70 10 48 89 78 18 55 41 54 41 55 41 56 41 57";
    constexpr char pack_expected[] = "c4 e3 79 21 83 84 0b 00 00 20 c4 e3 79 21 83 88 0b 00 00 30 c5 f8 29 83 10 0a 00 00";
    auto matches = [base](uint32_t rva, size_t size, const char *expected) {
        return core::pe::readable(base + rva, size) && core::patch::match(base + rva, size, expected, nullptr) >= 0;
    };
    if (!matches(context_rva, 31, context_expected) || !matches(target_rva, 24, target_expected) || !matches(pack_rva, 28, pack_expected)) {
        core::log::write("FM6: MotionBlurIntensity: runtime signature mismatch, disabled");
        return;
    }
    auto *site = base + site_rva;
    auto *stub = static_cast<uint8_t *>(core::hook::alloc_near(site + 5, 14, PAGE_EXECUTE_READWRITE));
    if (!stub) {
        core::log::write("FM6: MotionBlurIntensity: allocation failed");
        return;
    }
    uint8_t jump[14] = {0xff, 0x25, 0, 0, 0, 0};
    auto hook = &prepare_hook;
    static_assert(sizeof hook == 8);
    std::memcpy(jump + 6, &hook, 8);
    std::memcpy(stub, jump, sizeof jump);
    FlushInstructionCache(GetCurrentProcess(), stub, sizeof jump);
    const auto delta = reinterpret_cast<intptr_t>(stub) - reinterpret_cast<intptr_t>(site + 5);
    if (delta < std::numeric_limits<int32_t>::min() || delta > std::numeric_limits<int32_t>::max()) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return;
    }
    const int32_t rel = static_cast<int32_t>(delta);
    uint8_t call[5] = {0xe8, 0, 0, 0, 0};
    std::memcpy(call + 1, &rel, 4);
    original_prepare = reinterpret_cast<Prepare>(base + target_rva);
    multiplier = percent / 100.0f;
    if (!core::patch::write_code(site, call, sizeof call)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        core::log::write("FM6: MotionBlurIntensity: runtime patch write failed");
        return;
    }
    core::log::write("FM6: MotionBlurIntensity=%d: v0.3 VelocityParams1.z runtime hook installed", percent);
}
