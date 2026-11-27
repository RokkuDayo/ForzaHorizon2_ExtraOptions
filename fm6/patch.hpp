// Checked patches on the game image: every byte a patch relies on is compared before anything is written, and an
// option is applied all or nothing (one site that does not match leaves the whole option off).
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace core::patch {

// One site of an option.
//  expect: hex of the bytes at rva ("??" = any byte, spaces ignored); "|" separates alternative forms of the site
//          (e.g. the game's original code or the same code after Azimuth's own patch).
//  write:  hex written at rva + offset, inside the checked range of every form; empty = the site is only checked.
struct Site {
    uint32_t rva;
    const char *expect;
    uint32_t offset;
    const char *write;
};

// An option of the ini ([game] key=1 turns it on): its sites, applied together. label: its name in Azimuth's
// launcher (game settings, Extra Options section); null keeps it out of the launcher. install: code the option
// needs (hooks, strings built from the game's own), called once every site matched and before any site is
// written; it writes nothing unless it returns true, and false leaves the whole option off.
struct Option {
    const char *key;
    std::span<const Site> sites;
    const char *label = nullptr;
    bool (*install)(uint8_t *base, size_t image_size) = nullptr;
};

constexpr size_t k_max_bytes = 32;

struct Bytes {
    uint8_t value[k_max_bytes];
    bool any[k_max_bytes]; // "??"
    size_t size;
};

// Parses one form (no "|"). false when malformed, empty, longer than k_max_bytes, or with "??" and !wildcards.
bool parse(const char *hex, size_t len, Bytes &out, bool wildcards);

// The site is well formed: every form parses, write parses without wildcards and fits in every form.
bool valid(const Site &s);

// Index of the first form of expect that matches p (avail bytes readable), or -1. *size = size of that form.
int match(const uint8_t *p, size_t avail, const char *expect, size_t *size);

// Writes code: VirtualProtect, copy, restore, FlushInstructionCache.
bool write_code(void *dst, const void *src, size_t n);

enum class Result { applied, mismatch, invalid };

// Checks every site of opt in the image (base, image_size) and, if all match, writes them. Logs as
// "<game>: <key>: ...".
Result apply(uint8_t *base, size_t image_size, const Option &opt, const char *game);

} // namespace core::patch
