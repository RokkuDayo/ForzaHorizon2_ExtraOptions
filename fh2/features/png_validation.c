#include "image.h"
#include "support.h"

enum PngChunkType {
    PNG_CHUNK_HEADER = 0x49484452u,
    PNG_CHUNK_PALETTE = 0x504c5445u,
    PNG_CHUNK_IMAGE_DATA = 0x49444154u,
    PNG_CHUNK_END = 0x49454e44u
};

static uint32_t read_big_endian_u32(const unsigned char *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) |
           bytes[3];
}

static uint32_t png_crc32(const unsigned char *data, uint32_t size) {
    static const uint32_t nibble_crc[] = {0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
                                          0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
                                          0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
                                          0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu};
    uint32_t crc = 0xffffffffu;
    while (size--) {
        crc ^= *data++;
        crc = (crc >> 4) ^ nibble_crc[crc & 15];
        crc = (crc >> 4) ^ nibble_crc[crc & 15];
    }
    return crc ^ 0xffffffffu;
}

const char *image_png_error(const unsigned char *data, uint32_t size) {
    static const unsigned char signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!data || size < 57 || size > PNG_MAX_FILE_SIZE || !memory_equal(data, signature, 8)) {
        return "wrong PNG signature or file size";
    }
    unsigned has_header = 0, has_palette = 0, has_image_bytes = 0, has_image_chunk = 0,
             image_chunks_ended = 0, color_type = 0, bit_depth = 0;
    unsigned char compression_header[2] = {0};
    unsigned compression_header_size = 0;
    uint32_t compressed_size = 0;
    uint32_t offset = 8;
    while (offset < size) {
        if (size - offset < 12) {
            return "invalid PNG container";
        }
        uint32_t chunk_size = read_big_endian_u32(data + offset),
                 chunk_type = read_big_endian_u32(data + offset + 4);
        if (chunk_size > size - offset - 12) {
            return "truncated PNG chunk";
        }
        if (png_crc32(data + offset + 4, chunk_size + 4) !=
            read_big_endian_u32(data + offset + 8 + chunk_size)) {
            return "PNG chunk checksum mismatch";
        }
        for (unsigned i = 4; i < 8; ++i) {
            unsigned char letter = data[offset + i];
            if (!((letter >= 'A' && letter <= 'Z') || (letter >= 'a' && letter <= 'z'))) {
                return "invalid PNG container";
            }
        }
        if (data[offset + 6] & 0x20) {
            return "invalid PNG container"; /* PNG's reserved type bit must be zero. */
        }
        const unsigned char *chunk_data = data + offset + 8;
        if (!has_header && chunk_type != PNG_CHUNK_HEADER) {
            return "invalid PNG container";
        }
        if (chunk_type == PNG_CHUNK_HEADER) { /* IHDR */
            if (has_header || chunk_size != 13) {
                return "invalid PNG container";
            }
            uint32_t width = read_big_endian_u32(chunk_data),
                     height = read_big_endian_u32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            if (!width || !height || width > 2048 || height > 2048 || chunk_data[10] ||
                chunk_data[11] || chunk_data[12] > 1) {
                return "invalid PNG container";
            }
            if (!((color_type == 0 && (bit_depth == 1 || bit_depth == 2 || bit_depth == 4 ||
                                       bit_depth == 8 || bit_depth == 16)) ||
                  (color_type == 2 && (bit_depth == 8 || bit_depth == 16)) ||
                  (color_type == 3 &&
                   (bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8)) ||
                  ((color_type == 4 || color_type == 6) && (bit_depth == 8 || bit_depth == 16)))) {
                return "invalid PNG container";
            }
            has_header = 1;
        } else if (chunk_type == PNG_CHUNK_PALETTE) { /* PLTE */
            if (has_palette || has_image_chunk || !chunk_size || chunk_size % 3 ||
                chunk_size > 768 || color_type == 0 || color_type == 4 ||
                (color_type == 3 && chunk_size / 3 > (1u << bit_depth))) {
                return "invalid PNG container";
            }
            has_palette = 1;
        } else if (chunk_type == PNG_CHUNK_IMAGE_DATA) { /* IDAT; empty IDAT chunks are legal. */
            if (image_chunks_ended || (color_type == 3 && !has_palette)) {
                return "invalid PNG container";
            }
            has_image_chunk = 1;
            has_image_bytes |= chunk_size != 0;
            for (unsigned i = 0; i < chunk_size && compression_header_size < 2; ++i) {
                compression_header[compression_header_size++] = chunk_data[i];
            }
            compressed_size += chunk_size;
        } else if (chunk_type == PNG_CHUNK_END) { /* IEND */
            if (chunk_size || !has_image_bytes || offset + 12 != size) {
                return "missing image data or unexpected bytes after IEND";
            }
            if (compressed_size < 6 || compression_header_size != 2 ||
                (compression_header[0] & 15) != 8 || (compression_header[0] >> 4) > 7 ||
                (compression_header[1] & 0x20) ||
                (((unsigned)compression_header[0] << 8) | compression_header[1]) % 31 != 0) {
                return "invalid PNG compression header";
            }
            return NULL;
        } else {
            if (!(data[offset + 4] & 0x20)) {
                return "unknown critical PNG chunk";
            }
            if (has_image_chunk) {
                image_chunks_ended = 1;
            }
        }
        offset += chunk_size + 12;
    }
    return "invalid PNG container";
}

int image_validate_png(const unsigned char *data, uint32_t size) {
    return image_png_error(data, size) == NULL;
}
