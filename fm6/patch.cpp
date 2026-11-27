#include "patch.hpp"

#include "log.hpp"
#include "pe.hpp"

#include <windows.h>
#include <cstring>

namespace core::patch {

namespace {

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Calls f(form, length) for each "|"-separated form of hex; stops when f returns false.
template <typename F> void each_form(const char *hex, F f)
{
    for (const char *p = hex;;) {
        const char *bar = strchr(p, '|');
        size_t len = bar ? static_cast<size_t>(bar - p) : strlen(p);
        if (!f(p, len) || !bar) return;
        p = bar + 1;
    }
}

void to_hex(const uint8_t *p, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = d[p[i] >> 4];
        out[i * 2 + 1] = d[p[i] & 15];
    }
    out[n * 2] = 0;
}

} // namespace

bool parse(const char *hex, size_t len, Bytes &out, bool wildcards)
{
    out.size = 0;
    for (size_t i = 0; i < len;) {
        if (hex[i] == ' ') { i++; continue; }
        if (i + 1 >= len || out.size == k_max_bytes) return false;
        if (hex[i] == '?' && hex[i + 1] == '?') {
            if (!wildcards) return false;
            out.value[out.size] = 0;
            out.any[out.size++] = true;
        } else {
            int hi = hex_digit(hex[i]), lo = hex_digit(hex[i + 1]);
            if (hi < 0 || lo < 0) return false;
            out.value[out.size] = static_cast<uint8_t>(hi << 4 | lo);
            out.any[out.size++] = false;
        }
        i += 2;
    }
    return out.size > 0;
}

bool valid(const Site &s)
{
    if (!s.expect || !s.write) return false;
    Bytes w{};
    size_t wsize = 0;
    if (*s.write) {
        if (!parse(s.write, strlen(s.write), w, false)) return false;
        wsize = w.size;
    }
    bool ok = true;
    each_form(s.expect, [&](const char *form, size_t len) {
        Bytes e;
        ok = parse(form, len, e, true) && s.offset + wsize <= e.size;
        return ok;
    });
    return ok;
}

int match(const uint8_t *p, size_t avail, const char *expect, size_t *size)
{
    int found = -1, index = 0;
    each_form(expect, [&](const char *form, size_t len) {
        Bytes e;
        if (parse(form, len, e, true) && e.size <= avail) {
            size_t i = 0;
            while (i < e.size && (e.any[i] || p[i] == e.value[i])) i++;
            if (i == e.size) {
                found = index;
                if (size) *size = e.size;
                return false;
            }
        }
        index++;
        return true;
    });
    return found;
}

bool write_code(void *dst, const void *src, size_t n)
{
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(dst, src, n);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return true;
}

Result apply(uint8_t *base, size_t image_size, const Option &opt, const char *game)
{
    // every site is checked before the first write
    for (const Site &s : opt.sites) {
        if (!valid(s)) {
            log::write("%s: %s: site %#x is malformed in the table, option ignored", game, opt.key, s.rva);
            return Result::invalid;
        }
        uint8_t *p = base + s.rva;
        size_t avail = s.rva < image_size ? image_size - s.rva : 0;
        // a section ends its region (Azimuth protects each one): only what is readable there is compared
        if (avail > k_max_bytes) avail = k_max_bytes;
        avail = avail ? pe::readable_size(p, avail) : 0;
        if (!avail) {
            log::write("%s: %s: site %#x is outside the image or unreadable, option ignored", game, opt.key, s.rva);
            return Result::mismatch;
        }
        size_t size = 0;
        int form = match(p, avail, s.expect, &size);
        if (form < 0) {
            char have[k_max_bytes * 2 + 1];
            to_hex(p, 16 < avail ? 16 : avail, have);
            log::write("%s: %s: site %#x does not match (has %s), option ignored", game, opt.key, s.rva, have);
            return Result::mismatch;
        }
        if (form > 0) log::write("%s: %s: site %#x in alternative form %d", game, opt.key, s.rva, form);
    }
    if (opt.install && !opt.install(base, image_size)) {
        log::write("%s: %s: its code could not be installed, option ignored", game, opt.key);
        return Result::mismatch;
    }
    int written = 0;
    for (const Site &s : opt.sites) {
        if (!*s.write) continue;
        Bytes w;
        parse(s.write, strlen(s.write), w, false);
        if (!write_code(base + s.rva + s.offset, w.value, w.size)) {
            log::write("%s: %s: site %#x: VirtualProtect failed (error %lu)", game, opt.key, s.rva, GetLastError());
            return Result::mismatch;
        }
        written++;
    }
    log::write("%s: %s applied (%d sites written, %d checked)", game, opt.key, written, static_cast<int>(opt.sites.size()));
    return Result::applied;
}

} // namespace core::patch
