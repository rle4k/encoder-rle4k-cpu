#include <gtest/gtest.h>
#include <zlib.h>
#include <lzo/lzo1x.h>
#include <snappy.h>
#include <zstd.h>
#include <brotli/decode.h>

#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "rle4k_format_encoder.h"
#include "rle4k.h"
#include "test_helpers.h"

USING_SP2_NAMESPACE();

class FormatEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        initialize_global_buffer_manager(512, 64, 2);
        src_ = rle4k_test::make_pattern_bitmap(256, 32, 2);
    }
    std::unique_ptr<bitmap_info> src_;
};

TEST_F(FormatEncoderTest, FactorySupportsAllSix) {
    auto& fac = format_encoder_factory::get_instance();
    EXPECT_TRUE(fac.is_format_supported(raster_output_format_rle4k));
    EXPECT_TRUE(fac.is_format_supported(raster_output_format_gzip));
    EXPECT_TRUE(fac.is_format_supported(raster_output_format_lzo2));
    EXPECT_TRUE(fac.is_format_supported(raster_output_format_snappy));
    EXPECT_TRUE(fac.is_format_supported(raster_output_format_zstd));
    EXPECT_TRUE(fac.is_format_supported(raster_output_formaT_BROTLI));
    EXPECT_FALSE(fac.is_format_supported(0));
    auto fmts = fac.get_supported_formats();
    EXPECT_GE(fmts.size(), 6u);
}

TEST_F(FormatEncoderTest, MetadataAndClone) {
    rle4k_format_encoder e;
    EXPECT_STREQ(e.get_format_name(), "RLE4K");
    EXPECT_STREQ(e.get_file_extension(), ".rle4k");
    EXPECT_FALSE(e.supports_gpu());
    auto* c = e.clone();
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(c->get_supported_format(), raster_output_format_rle4k);
    delete c;
}

TEST_F(FormatEncoderTest, PackRle4kRoundTrip) {
    rle4k_format_encoder enc;
    encode_format_args args;
    auto packed = enc.process_pack(src_.get(), args);
    ASSERT_NE(packed, nullptr);
    EXPECT_GT(packed->data_length, 0u);
    EXPECT_EQ(packed->format, raster_output_format_rle4k);

    std::vector<uint8_t> dec(src_->data_length, 0);
    int rc = rle4k_decode_block(reinterpret_cast<const uint8_t*>(packed->data),
                                packed->data_length, dec.data(),
                                src_->width, src_->height, src_->get_stride_bytes());
    ASSERT_GE(rc, 0);
    EXPECT_EQ(std::memcmp(dec.data(), src_->data, src_->data_length), 0);
}

TEST_F(FormatEncoderTest, PackGzip) {
    gzip_format_encoder enc;
    encode_format_args args;
    args.gzip_level = 1;
    auto packed = enc.process_pack(src_.get(), args);
    ASSERT_NE(packed, nullptr);
    EXPECT_GT(packed->data_length, 0u);

    uLong dest_len = src_->data_length;
    std::vector<uint8_t> out(dest_len);
    int zrc = uncompress(out.data(), &dest_len,
                         reinterpret_cast<const Bytef*>(packed->data), packed->data_length);
    ASSERT_EQ(zrc, Z_OK);
    EXPECT_EQ(std::memcmp(out.data(), src_->data, src_->data_length), 0);
}

TEST_F(FormatEncoderTest, PackLzo2) {
    lzo2_format_encoder enc;
    encode_format_args args;
    args.lzo2_level = 1;
    auto packed = enc.process_pack(src_.get(), args);
    ASSERT_NE(packed, nullptr);

    std::vector<uint8_t> out(src_->data_length);
    lzo_uint out_len = out.size();
    int rc = lzo1x_decompress(reinterpret_cast<const unsigned char*>(packed->data),
                              packed->data_length, out.data(), &out_len, nullptr);
    ASSERT_EQ(rc, LZO_E_OK);
    EXPECT_EQ(out_len, src_->data_length);
    EXPECT_EQ(std::memcmp(out.data(), src_->data, src_->data_length), 0);
}

/* Regresses the shared-static lzo_wrkmem race under multi-threaded encode. */
TEST_F(FormatEncoderTest, PackLzo2Concurrent) {
    constexpr int kThreads = 8;
    constexpr int kIters = 40;
    std::atomic<int> fails{0};
    auto worker = [this, &fails, kIters]() {
        lzo2_format_encoder enc;
        encode_format_args args;
        args.lzo2_level = 1;
        for (int i = 0; i < kIters; ++i) {
            auto packed = enc.process_pack(src_.get(), args);
            if (!packed) {
                fails.fetch_add(1);
                return;
            }
            std::vector<uint8_t> out(src_->data_length);
            lzo_uint out_len = out.size();
            int rc = lzo1x_decompress(
                reinterpret_cast<const unsigned char*>(packed->data), packed->data_length,
                out.data(), &out_len, nullptr);
            if (rc != LZO_E_OK || out_len != src_->data_length ||
                std::memcmp(out.data(), src_->data, src_->data_length) != 0) {
                fails.fetch_add(1);
                return;
            }
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& th : threads) {
        th.join();
    }
    EXPECT_EQ(fails.load(), 0);
}

TEST_F(FormatEncoderTest, PackSnappy) {
    snappy_format_encoder enc;
    encode_format_args args;
    auto packed = enc.process_pack(src_.get(), args);
    ASSERT_NE(packed, nullptr);
    std::string out;
    ASSERT_TRUE(snappy::Uncompress(packed->data, packed->data_length, &out));
    EXPECT_EQ(out.size(), src_->data_length);
    EXPECT_EQ(std::memcmp(out.data(), src_->data, src_->data_length), 0);
}

TEST_F(FormatEncoderTest, PackZstd) {
    zstd_format_encoder enc;
    encode_format_args args;
    args.zstd_level = 1;
    auto packed = enc.process_pack(src_.get(), args);
    ASSERT_NE(packed, nullptr);
    std::vector<uint8_t> out(src_->data_length);
    size_t n = ZSTD_decompress(out.data(), out.size(), packed->data, packed->data_length);
    ASSERT_FALSE(ZSTD_isError(n));
    EXPECT_EQ(n, src_->data_length);
    EXPECT_EQ(std::memcmp(out.data(), src_->data, src_->data_length), 0);
}

TEST_F(FormatEncoderTest, PackBrotli) {
    brotli_format_encoder enc;
    encode_format_args args;
    args.brotli_level = 1;
    args.brotli_window = 16;
    auto packed = enc.process_pack(src_.get(), args);
    ASSERT_NE(packed, nullptr);
    std::vector<uint8_t> out(src_->data_length);
    size_t out_len = out.size();
    ASSERT_EQ(BrotliDecoderDecompress(packed->data_length,
                                      reinterpret_cast<const uint8_t*>(packed->data),
                                      &out_len, out.data()),
              BROTLI_DECODER_RESULT_SUCCESS);
    EXPECT_EQ(out_len, src_->data_length);
    EXPECT_EQ(std::memcmp(out.data(), src_->data, src_->data_length), 0);
}

TEST_F(FormatEncoderTest, FactoryGetEncoder) {
    auto* e = format_encoder_factory::get_instance().get_encoder(raster_output_format_rle4k);
    ASSERT_NE(e, nullptr);
    encode_format_args args;
    auto packed = e->process_pack(src_.get(), args);
    EXPECT_NE(packed, nullptr);
}

TEST_F(FormatEncoderTest, InvalidSourceThrows) {
    rle4k_format_encoder enc;
    encode_format_args args;
    EXPECT_THROW(enc.process_pack(nullptr, args), raster_exception);
}

TEST_F(FormatEncoderTest, LzoHighLevelPath) {
    lzo2_format_encoder enc;
    encode_format_args args;
    args.lzo2_level = 3;
    auto packed = enc.process_pack(src_.get(), args);
    ASSERT_NE(packed, nullptr);
    EXPECT_GT(packed->data_length, 0u);
}
