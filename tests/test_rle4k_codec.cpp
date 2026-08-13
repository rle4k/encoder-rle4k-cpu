#include <gtest/gtest.h>
#include <vector>
#include <cstring>

#include "rle4k.h"
#include "test_helpers.h"

USING_SP2_NAMESPACE();

namespace {

std::vector<uint8_t> encode_then_decode_block(const uint8_t* src, uint32_t h, uint32_t w, uint32_t stride) {
    /* Worst-case RLE can expand; size generously. */
    std::vector<uint8_t> enc(static_cast<size_t>(stride) * h * 8 + 4096);
    int elen = rle4k_encode_block(src, h, w, enc.data(), stride, static_cast<uint32_t>(enc.size()));
    EXPECT_GT(elen, 0) << "encode failed: " << elen;
    if (elen <= 0) return {};
    enc.resize(static_cast<size_t>(elen));

    std::vector<uint8_t> dec(static_cast<size_t>(stride) * h, 0);
    int dlen = rle4k_decode_block(enc.data(), static_cast<uint32_t>(enc.size()),
                                  dec.data(), w, h, stride);
    EXPECT_GE(dlen, 0) << "decode failed: " << dlen;
    return dec;
}

} // namespace

TEST(Rle4kCodec, NullPointerErrors) {
    uint8_t buf[8] = {};
    EXPECT_EQ(rle4k_encode_block(nullptr, 1, 8, buf, 1, 8), RLE4K_ERROR_NULL_POINTER);
    EXPECT_EQ(rle4k_encode_block(buf, 1, 8, nullptr, 1, 8), RLE4K_ERROR_NULL_POINTER);
    EXPECT_EQ(rle4k_decode_block(nullptr, 1, buf, 8, 1, 1), -1);
    EXPECT_EQ(rle4k_decode_block(buf, 1, nullptr, 8, 1, 1), -1);
    EXPECT_EQ(rle4k_encode(nullptr, 1, buf), RLE4K_ERROR_NULL_POINTER);
    EXPECT_EQ(rle4k_decode(nullptr, 1, buf), RLE4K_ERROR_NULL_POINTER);
}

TEST(Rle4kCodec, InvalidDimensionErrors) {
    uint8_t buf[32] = {};
    EXPECT_EQ(rle4k_encode_block(buf, 0, 8, buf, 1, 32), RLE4K_ERROR_INVALID_DIMENSION);
    EXPECT_EQ(rle4k_encode_block(buf, 1, 0, buf, 1, 32), RLE4K_ERROR_INVALID_DIMENSION);
    EXPECT_EQ(rle4k_encode(buf, 0, buf), RLE4K_ERROR_INVALID_LENGTH);
    EXPECT_EQ(rle4k_decode_block(buf, 0, buf, 8, 1, 1), -2);
}

TEST(Rle4kCodec, RoundTripPatterns) {
    const uint32_t widths[] = { 8, 16, 64, 256, 512 };
    const uint32_t heights[] = { 1, 2, 8, 32 };
    for (uint32_t w : widths) {
        for (uint32_t h : heights) {
            for (int pat = 0; pat <= 5; ++pat) {
                auto bmp = rle4k_test::make_pattern_bitmap(w, h, pat);
                uint32_t stride = bmp->get_stride_bytes();
                auto dec = encode_then_decode_block(
                    reinterpret_cast<const uint8_t*>(bmp->data), h, w, stride);
                ASSERT_EQ(dec.size(), static_cast<size_t>(stride) * h)
                    << "w=" << w << " h=" << h << " pat=" << pat;
                EXPECT_EQ(std::memcmp(dec.data(), bmp->data, dec.size()), 0)
                    << "round-trip mismatch w=" << w << " h=" << h << " pat=" << pat;
            }
        }
    }
}

TEST(Rle4kCodec, RoundTripNonByteAlignedWidth) {
    /* Non-multiple-of-8 widths: solid fill patterns; compare active bits only. */
    for (uint32_t w : { 7u, 15u, 127u }) {
        for (int pat : { 0, 1 }) {
            auto bmp = rle4k_test::make_pattern_bitmap(w, 8, pat);
            uint32_t stride = bmp->get_stride_bytes();
            auto dec = encode_then_decode_block(
                reinterpret_cast<const uint8_t*>(bmp->data), 8, w, stride);
            ASSERT_EQ(dec.size(), static_cast<size_t>(stride) * 8u);
            const uint32_t full_bytes = w / 8;
            const uint32_t rem_bits = w % 8;
            const uint8_t rem_mask = rem_bits ? static_cast<uint8_t>((1u << rem_bits) - 1u) : 0;
            for (uint32_t y = 0; y < 8; ++y) {
                const uint8_t* a = reinterpret_cast<const uint8_t*>(bmp->data) + y * stride;
                const uint8_t* b = dec.data() + y * stride;
                if (full_bytes)
                    EXPECT_EQ(std::memcmp(a, b, full_bytes), 0) << "w=" << w << " y=" << y;
                if (rem_bits)
                    EXPECT_EQ((a[full_bytes] ^ b[full_bytes]) & rem_mask, 0)
                        << "w=" << w << " y=" << y << " pat=" << pat;
            }
        }
    }
}

TEST(Rle4kCodec, StreamEncodeDecode) {
    auto bmp = rle4k_test::make_pattern_bitmap(128, 16, 2);
    uint32_t len = bmp->data_length;
    std::vector<uint8_t> enc(len * 8 + 4096);
    int elen = rle4k_encode(reinterpret_cast<const uint8_t*>(bmp->data), len, enc.data());
    ASSERT_GT(elen, 0);
    std::vector<uint8_t> dec(len, 0);
    int dlen = rle4k_decode(enc.data(), static_cast<uint32_t>(elen), dec.data());
    ASSERT_GE(dlen, 0);
    EXPECT_EQ(std::memcmp(dec.data(), bmp->data, len), 0);
}

TEST(Rle4kCodec, TargetTooSmallThenOk) {
    auto bmp = rle4k_test::make_pattern_bitmap(256, 8, 5);
    uint32_t stride = bmp->get_stride_bytes();
    std::vector<uint8_t> tiny(4);
    int rc = rle4k_encode_block(reinterpret_cast<const uint8_t*>(bmp->data),
                                bmp->height, bmp->width, tiny.data(), stride,
                                static_cast<uint32_t>(tiny.size()));
    EXPECT_EQ(rc, RLE4K_ERROR_TARGET_TOO_SMALL);

    std::vector<uint8_t> big(stride * bmp->height * 8 + 4096);
    rc = rle4k_encode_block(reinterpret_cast<const uint8_t*>(bmp->data),
                            bmp->height, bmp->width, big.data(), stride,
                            static_cast<uint32_t>(big.size()));
    EXPECT_GT(rc, 0);
}

TEST(Rle4kCodec, ZeroStrideDefaults) {
    auto bmp = rle4k_test::make_pattern_bitmap(64, 4, 3);
    uint32_t stride = bmp->get_stride_bytes();
    std::vector<uint8_t> enc(stride * bmp->height * 8 + 4096);
    int elen = rle4k_encode_block(reinterpret_cast<const uint8_t*>(bmp->data),
                                  bmp->height, bmp->width, enc.data(), 0,
                                  static_cast<uint32_t>(enc.size()));
    ASSERT_GT(elen, 0);
    std::vector<uint8_t> dec(stride * bmp->height, 0);
    int dlen = rle4k_decode_block(enc.data(), static_cast<uint32_t>(elen),
                                  dec.data(), bmp->width, bmp->height, 0);
    ASSERT_GE(dlen, 0);
    EXPECT_EQ(std::memcmp(dec.data(), bmp->data, dec.size()), 0);
}

TEST(Rle4kCodec, WidthExceedsStride) {
    uint8_t src[8] = {};
    uint8_t enc[64] = {};
    /* width 64 bits needs stride >= 8; pass stride=1 */
    EXPECT_EQ(rle4k_encode_block(src, 1, 64, enc, 1, 64), RLE4K_ERROR_WIDTH_EXCEEDS_STRIDE);
}

TEST(Rle4kCodec, LongRunsAndSparse) {
    /* Long solid runs exercise long-run encoding paths. */
    for (uint32_t w : { 512u, 1024u, 2048u }) {
        for (int pat : { 0, 1, 3, 4 }) {
            auto bmp = rle4k_test::make_pattern_bitmap(w, 64, pat);
            uint32_t stride = bmp->get_stride_bytes();
            auto dec = encode_then_decode_block(
                reinterpret_cast<const uint8_t*>(bmp->data), 64, w, stride);
            ASSERT_EQ(dec.size(), static_cast<size_t>(stride) * 64u);
            EXPECT_EQ(std::memcmp(dec.data(), bmp->data, dec.size()), 0)
                << "w=" << w << " pat=" << pat;
        }
    }
}

TEST(Rle4kCodec, DecodeEmptyLength) {
    uint8_t buf[8] = {};
    EXPECT_EQ(rle4k_decode(buf, 0, buf), RLE4K_ERROR_INVALID_LENGTH);
    EXPECT_EQ(rle4k_decode_block(buf, 0, buf, 8, 1, 1), -2);
}

TEST(Rle4kCodec, DecodeBadStartFlag) {
    uint8_t enc[4] = { 0x40, 0x40, 0x00, 0x00 };
    uint8_t out[16] = {};
    EXPECT_EQ(rle4k_decode_block(enc, 4, out, 8, 1, 1), -3);
}

TEST(Rle4kCodec, DecodeWidthExceedsStride) {
    uint8_t enc[8] = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    uint8_t out[8] = {};
    EXPECT_EQ(rle4k_decode_block(enc, 8, out, 64, 1, 1), -3);
}

TEST(Rle4kCodec, MidLengthRunsAndAlternatingBytes) {
    /* Mid-length (≥64) and alternating bytes exercise PackSingleRun + byte LUT. */
    for (uint32_t w : { 72u, 96u, 128u, 192u, 320u, 640u }) {
        auto bmp = rle4k_test::make_pattern_bitmap(w, 16, 5);
        uint32_t stride = bmp->get_stride_bytes();
        auto dec = encode_then_decode_block(
            reinterpret_cast<const uint8_t*>(bmp->data), 16, w, stride);
        ASSERT_EQ(dec.size(), static_cast<size_t>(stride) * 16u);
        EXPECT_EQ(std::memcmp(dec.data(), bmp->data, dec.size()), 0) << "w=" << w;
    }
}

TEST(Rle4kCodec, AlternatingBitRows) {
    auto bmp = rle4k_test::make_bitmap(256, 8, 0);
    uint32_t stride = bmp->get_stride_bytes();
    for (uint32_t y = 0; y < 8; ++y) {
        auto* row = reinterpret_cast<uint8_t*>(bmp->data) + y * stride;
        for (uint32_t x = 0; x < 256; ++x) {
            if (((x + y) & 1) != 0)
                row[x / 8] |= static_cast<uint8_t>(1u << (x % 8));
        }
    }
    auto dec = encode_then_decode_block(
        reinterpret_cast<const uint8_t*>(bmp->data), 8, 256, stride);
    ASSERT_EQ(dec.size(), static_cast<size_t>(stride) * 8u);
    EXPECT_EQ(std::memcmp(dec.data(), bmp->data, dec.size()), 0);
}

TEST(Rle4kCodec, BlockEndMarker) {
    /* Encode a small block then append 0xC0 — decoder should stop cleanly. */
    auto bmp = rle4k_test::make_pattern_bitmap(64, 2, 1);
    uint32_t stride = bmp->get_stride_bytes();
    std::vector<uint8_t> enc(stride * 2 * 8 + 64);
    int elen = rle4k_encode_block(reinterpret_cast<const uint8_t*>(bmp->data),
                                  2, 64, enc.data(), stride,
                                  static_cast<uint32_t>(enc.size()));
    ASSERT_GT(elen, 0);
    enc.resize(static_cast<size_t>(elen) + 1);
    enc[static_cast<size_t>(elen)] = 0xC0;
    std::vector<uint8_t> dec(stride * 2, 0);
    int dlen = rle4k_decode_block(enc.data(), static_cast<uint32_t>(enc.size()),
                                  dec.data(), 64, 2, stride);
    EXPECT_GE(dlen, 0);
}

TEST(Rle4kCodec, AllByteValuesOneRow) {
    /* One row with every byte 0..255 to stress ByteRunsLUT paths. */
    std::vector<uint8_t> src(256);
    for (int i = 0; i < 256; ++i) src[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    std::vector<uint8_t> enc(src.size() * 16 + 4096);
    int elen = rle4k_encode_block(src.data(), 1, 256 * 8, enc.data(), 256,
                                  static_cast<uint32_t>(enc.size()));
    ASSERT_GT(elen, 0);
    std::vector<uint8_t> dec(256, 0);
    int dlen = rle4k_decode_block(enc.data(), static_cast<uint32_t>(elen),
                                  dec.data(), 256 * 8, 1, 256);
    ASSERT_GE(dlen, 0);
    EXPECT_EQ(std::memcmp(dec.data(), src.data(), 256), 0);
}
