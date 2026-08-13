#pragma once

#include <cstdint>

/*
 * RLE4K public API (librle4k)
 *
 * Data model
 * - Input/Output "image blocks" are 1-bit-per-pixel, bit-packed buffers.
 * - Bit order inside each byte is LSB-first.
 * - `width` is measured in *bits* (pixels), not bytes.
 * - `stride` is measured in *bytes per row*. If 0, defaults to ceil(width/8).
 */

#define RLE4K_ERROR_NULL_POINTER         (-1)
#define RLE4K_ERROR_INVALID_DIMENSION    (-2)
#define RLE4K_ERROR_INVALID_STRIDE       (-3)
#define RLE4K_ERROR_INVALID_LENGTH       (-4)
#define RLE4K_ERROR_WIDTH_EXCEEDS_STRIDE (-5)
#define RLE4K_ERROR_TARGET_TOO_SMALL     (-100)
#define RLE4K_ERROR_EVALUATION_EXPIRED   (-200)

int rle4k_encode_block(const uint8_t* block, uint32_t height, uint32_t width, uint8_t* encoded, uint32_t stride, uint32_t target_length = 0);
int rle4k_decode_block(const uint8_t* encoded, uint32_t inlen, uint8_t* block, uint32_t width, uint32_t height, uint32_t stride = 0);
int rle4k_encode(const uint8_t* stream, uint32_t length, uint8_t* encoded);
int rle4k_decode(const uint8_t* encoded, uint32_t length, uint8_t* stream);
