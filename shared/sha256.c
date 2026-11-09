#include "shared/sha256.h"
#include "shared/support.h"

static uint32_t rotate_right(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform_block(Sha256Context *context) {
    static const uint32_t round_constants[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t schedule[64];
    for (unsigned i = 0; i < 16; ++i) {
        schedule[i] = ((uint32_t)context->block[i * 4] << 24) |
                      ((uint32_t)context->block[i * 4 + 1] << 16) |
                      ((uint32_t)context->block[i * 4 + 2] << 8) | context->block[i * 4 + 3];
    }
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t x = schedule[i - 15], y = schedule[i - 2];
        schedule[i] = schedule[i - 16] + (rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3)) +
                      schedule[i - 7] + (rotate_right(y, 17) ^ rotate_right(y, 19) ^ (y >> 10));
    }
    uint32_t a = context->h[0], b = context->h[1], c = context->h[2], d = context->h[3],
             e = context->h[4], f = context->h[5], g = context->h[6], h = context->h[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t t = h + (rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25)) +
                     ((e & f) ^ (~e & g)) + round_constants[i] + schedule[i];
        uint32_t u = (rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22)) +
                     ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t;
        d = c;
        c = b;
        b = a;
        a = t + u;
    }
    context->h[0] += a;
    context->h[1] += b;
    context->h[2] += c;
    context->h[3] += d;
    context->h[4] += e;
    context->h[5] += f;
    context->h[6] += g;
    context->h[7] += h;
}

void sha256_initialize(Sha256Context *context) {
    const uint32_t initial_state[] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memory_copy(context->h, initial_state, sizeof(initial_state));
    context->length = 0;
    context->used = 0;
}

void sha256_update(Sha256Context *context, const unsigned char *data, size_t size) {
    context->length += size;
    while (size) {
        size_t block_bytes = 64 - context->used;
        if (block_bytes > size) {
            block_bytes = size;
        }
        memory_copy(context->block + context->used, data, block_bytes);
        context->used += (unsigned)block_bytes;
        data += block_bytes;
        size -= block_bytes;
        if (context->used == 64) {
            sha256_transform_block(context);
            context->used = 0;
        }
    }
}

void sha256_finalize(Sha256Context *context, unsigned char digest[32]) {
    uint64_t bit_length = context->length * 8;
    context->block[context->used++] = 0x80;
    if (context->used > 56) {
        while (context->used < 64) {
            context->block[context->used++] = 0;
        }
        sha256_transform_block(context);
        context->used = 0;
    }
    while (context->used < 56) {
        context->block[context->used++] = 0;
    }
    for (unsigned i = 0; i < 8; ++i) {
        context->block[63 - i] = (unsigned char)(bit_length >> (i * 8));
    }
    sha256_transform_block(context);
    for (unsigned i = 0; i < 32; ++i) {
        digest[i] = (unsigned char)(context->h[i / 4] >> (24 - (i % 4) * 8));
    }
}
