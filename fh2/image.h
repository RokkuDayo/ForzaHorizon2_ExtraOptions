#ifndef FH2_IMAGE_H
#define FH2_IMAGE_H
#include "extra_options.h"
#define PNG_MAX_FILE_SIZE (16u * 1024u * 1024u)
const char *image_png_error(const unsigned char *data, uint32_t size);
int image_validate_png(const unsigned char *data, uint32_t size);
unsigned char *image_decode_png(const unsigned char *png, DWORD size, DWORD *output_size,
                                UINT *width, UINT *height);
#endif
