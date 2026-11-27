#include "fov.hpp"
#include "live.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "patch.hpp"
#include "pe.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstddef>

namespace {
using Getter = float (*)(void *);
struct Camera { const char *name; const char *key; uint32_t table; uint32_t target; bool chase; };
constexpr Camera cameras[] = {
    {"FollowLow", "FOVChase", 0x19eeb70, 0x1ed934, true},
    {"FollowHigh", "FOVFarChase", 0x19eedb0, 0x1ed934, true},
    {"Follow", "FOVChase", 0x19eeff0, 0x1ed934, true},
    {"Hood", "FOVHood", 0x19ef978, 0x4c3d40, false},
    {"BumperHigh", "FOVBumper", 0x19ef730, 0x4c3d40, false},
    {"Driver", "FOVDriver", 0x19ee660, 0x4c3d40, false},
};
Getter originals[6]{};
int degrees[6]{};
std::atomic<bool> reported[6]{};
constexpr float radians = 0.017453292519943295f;
template<class T> bool read(const void *p, T &v) {
    if (!core::pe::readable(p, sizeof(T))) return false;
    std::memcpy(&v,p,sizeof(T)); return true;
}
template<size_t I> float get(void *camera) {
    const float native = originals[I](camera);
    if (!std::isfinite(native) || native <= 0 || !camera) return native;
    auto *p = static_cast<uint8_t *>(camera);
    float source = 0;
    if constexpr (cameras[I].chase) {
        uint8_t *settings = nullptr;
        if (!read(p+0x180,settings) || !settings || !read(settings+0x1c,source)) return native;
    } else {
        if (!read(p+0x1c0,source)) return native;
    }
    if (!std::isfinite(source) || source <= 0) return native;
    const float baseline = cameras[I].chase ? source*radians : source;
    // Preserve the original split-screen multiplier, but do not override an unrelated native FOV path.
    const float ratio = native/baseline;
    if (!std::isfinite(ratio) || (std::fabs(ratio-1.0f)>0.01f && std::fabs(ratio-0.5f)>0.01f)) return native;
    const float requested = fm6live::enabled() ? fm6live::fov_half(cameras[I].key)*0.5f : float(degrees[I]);
    if (requested == 0) return native;
    const float result = requested*radians*ratio;
    if (!reported[I].exchange(true,std::memory_order_relaxed))
        core::log::write("FM6 FOV: %s native=%.2f deg source=%.2f deg result=%.2f deg", cameras[I].name, double(native/radians), double(baseline/radians), double(result/radians));
    return result;
}
constexpr Getter callbacks[]={get<0>,get<1>,get<2>,get<3>,get<4>,get<5>};
}
void install_fm6_fov(uint8_t *base,const wchar_t *ini_path) {
    constexpr char chase_sig[]="80 b9 88 01 00 00 00 48 8b 81 80 01 00 00 c5 fa 10 40 1c";
    constexpr char mounted_sig[]="40 53 48 83 ec 20 80 b9 08 02 00 00 00 48 8b d9";
    if (!core::pe::readable(base+0x1ed934,19) || !core::pe::readable(base+0x4c3d40,16) ||
        core::patch::match(base+0x1ed934,19,chase_sig,nullptr)<0 || core::patch::match(base+0x4c3d40,16,mounted_sig,nullptr)<0) {
        core::log::write("FM6 FOV: native getter signature mismatch; disabled"); return;
    }
    for (size_t i=0;i<6;++i) {
        const auto &c=cameras[i];
        int v=core::ini::get_int(ini_path,"FM6",c.key,0);
        if (!v && !fm6live::enabled()) continue;
        v = v > 0 ? std::clamp(v,50,67) : 0;
        auto *slot=base+c.table+0x68;
        Getter original=nullptr;
        if (!read(slot,original) || original!=reinterpret_cast<Getter>(base+c.target)) {
            core::log::write("FM6 FOV: %s slot mismatch; skipped",c.name); continue;
        }
        originals[i]=original; degrees[i]=v;
        if (!core::patch::write_code(slot,&callbacks[i],sizeof(Getter))) {
            core::log::write("FM6 FOV: %s patch failed",c.name); continue;
        }
        core::log::write("FM6 FOV: %s installed %d degrees (%s)",c.name,v,c.key);
    }
}
