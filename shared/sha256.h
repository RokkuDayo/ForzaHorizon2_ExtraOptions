#ifndef FH2_SHA256_H
#define FH2_SHA256_H
#include "shared/extra_options.h"

typedef struct {
    uint32_t h[8];
    uint64_t length;
    unsigned char block[64];
    unsigned used;
} Sha256Context;

void sha256_initialize(Sha256Context *context);
void sha256_update(Sha256Context *context, const unsigned char *data, size_t size);
void sha256_finalize(Sha256Context *context, unsigned char digest[32]);
#endif
