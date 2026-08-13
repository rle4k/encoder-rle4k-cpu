#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

#include "rle4k_bitmap.h"
#include "rle4k_buffer_mgr.h"
#include "test_helpers.h"

USING_SP2_NAMESPACE();

class BitmapTest : public ::testing::Test {
protected:
    void SetUp() override {
        initialize_global_buffer_manager(512, 64, 2);
    }
};

TEST_F(BitmapTest, ConstructBasic) {
    bitmap_info bmp(64, 32, ALIGN008);
    EXPECT_EQ(bmp.width, 64u);
    EXPECT_EQ(bmp.height, 32u);
    EXPECT_NE(bmp.data, nullptr);
    EXPECT_GT(bmp.data_length, 0u);
    EXPECT_NE(bmp.extends, nullptr);
    EXPECT_EQ(bmp.get_stride_bytes(), 8u);
}

TEST_F(BitmapTest, ConstructAligned) {
    bitmap_info bmp(100, 16, ALIGN064);
    EXPECT_EQ(bmp.get_align_bits(), 64u);
    EXPECT_EQ(bmp.get_stride_bytes(), ((100u + 63u) / 64u * 64u) / 8u);
}

TEST_F(BitmapTest, ZeroSizeThrows) {
    EXPECT_THROW(bitmap_info(0, 10, ALIGN008), raster_exception);
    EXPECT_THROW(bitmap_info(10, 0, ALIGN008), raster_exception);
}

TEST_F(BitmapTest, FromVectorAndClone) {
    std::vector<uint8_t> data(16, 0xA5);
    bitmap_info bmp(64, 2, raster_output_format_rle4k, data, ALIGN008);
    EXPECT_EQ(bmp.format, raster_output_format_rle4k);
    EXPECT_EQ(bmp.data_length, 16u);

    auto clone = bmp.deep_clone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->width, bmp.width);
    EXPECT_EQ(clone->height, bmp.height);
    EXPECT_EQ(std::memcmp(clone->data, bmp.data, bmp.data_length), 0);
}

TEST_F(BitmapTest, FromRawPointer) {
    char raw[32];
    std::memset(raw, 0x3C, sizeof(raw));
    bitmap_info bmp(128, 2, raster_output_format_gzip, raw, 32, ALIGN008);
    EXPECT_EQ(bmp.data_length, 32u);
    EXPECT_EQ(std::memcmp(bmp.data, raw, 32), 0);
}

TEST_F(BitmapTest, FromSettings) {
    raster_result_settings s;
    s.width = 256;
    s.align_mode = 3;
    s.format = raster_output_format_zstd;
    s.normalize();
    EXPECT_EQ(s.align_bits, 64u);
    bitmap_info bmp(s, 32);
    EXPECT_EQ(bmp.width, 256u);
    EXPECT_EQ(bmp.height, 32u);
    EXPECT_EQ(bmp.format, raster_output_format_zstd);
}

TEST_F(BitmapTest, ResizeGrowsBuffer) {
    bitmap_info bmp(64, 8, ALIGN008);
    uint32_t before = bmp.get_buffer_size();
    bmp.resize();
    EXPECT_GE(bmp.get_buffer_size(), before * 2);
}

TEST_F(BitmapTest, SaveAndLoadRoundTrip) {
    auto path = rle4k_test::temp_dir() / "bitmap_roundtrip.bin";
    {
        bitmap_info bmp(64, 4, ALIGN008);
        std::memset(bmp.data, 0x55, bmp.data_length);
        bmp.save(path.string());
    }
    auto loaded = bitmap_info::load_file(path.string().c_str());
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->width, 64u);
    EXPECT_EQ(loaded->height, 4u);
    EXPECT_EQ(static_cast<unsigned char>(loaded->data[0]), 0x55u);
}

TEST_F(BitmapTest, SaveEmptyNameThrows) {
    bitmap_info bmp(8, 8, ALIGN008);
    EXPECT_THROW(bmp.save(""), raster_exception);
}

TEST_F(BitmapTest, LoadMissingThrows) {
    EXPECT_THROW(bitmap_info::load_file("Z:/no/such/file_rle4k_unit.bin"), raster_exception);
}

TEST_F(BitmapTest, ValidateHelpers) {
    bitmap_info bmp(32, 8, ALIGN008);
    EXPECT_NO_THROW(validate_bitmap(&bmp));
    EXPECT_THROW(validate_bitmap(nullptr), raster_exception);
}

TEST_F(BitmapTest, EncodeFormatArgsDefaults) {
    encode_format_args a;
    EXPECT_EQ(a.gzip_level, 6);
    encode_format_args b(1.0, true, 0);
    EXPECT_TRUE(b.mirror);
    EXPECT_EQ(b.bg_polarity, 0);
}

TEST_F(BitmapTest, ExtendCopyAssign) {
    extend_bitmap_info a;
    a.compression_ratio = 42;
    a.set_data_buffer_size(100);
    a.stride_bytes = 16;
    extend_bitmap_info b(a);
    EXPECT_EQ(b.compression_ratio, 42);
    EXPECT_EQ(b.get_buffer_size(), 100u);
    extend_bitmap_info c;
    c = a;
    EXPECT_EQ(c.stride_bytes, 16u);
    extend_bitmap_info d(&a);
    EXPECT_EQ(d.compression_ratio, 42);
    extend_bitmap_info e(static_cast<const extend_bitmap_info*>(nullptr));
    EXPECT_EQ(e.compression_ratio, 0);
    c = c; /* self-assign */
    EXPECT_EQ(c.stride_bytes, 16u);
}

TEST_F(BitmapTest, CloneAndCopyCtors) {
    bitmap_info bmp(64, 4, ALIGN008);
    std::memset(bmp.data, 0xA5, bmp.data_length);
    bmp.data_length = bmp.source_length;

    bitmap_info* raw = bmp.clone();
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(std::memcmp(raw->data, bmp.data, bmp.data_length), 0);
    delete raw;

    bitmap_info copy(bmp);
    EXPECT_EQ(copy.width, bmp.width);
    EXPECT_EQ(copy.source_length, bmp.data_length);

    bitmap_info from_ptr(&bmp);
    EXPECT_EQ(from_ptr.width, bmp.width);
    EXPECT_EQ(from_ptr.source_length, bmp.data_length);
}

TEST_F(BitmapTest, AlignModeClampAndOversized) {
    bitmap_info bmp(16, 8, static_cast<map_align_mode>(7));
    EXPECT_EQ(bmp.get_align_bits(), 64u); /* align_mode>6 clamps to 3 */
    EXPECT_THROW(bitmap_info(MAX_BITMAP_WIDTH + 1, 8, ALIGN008), raster_exception);
    EXPECT_THROW(bitmap_info(8, MAX_BITMAP_HEIGHT + 1, ALIGN008), raster_exception);
}

TEST_F(BitmapTest, SettingsInvalidHeight) {
    raster_result_settings s;
    s.width = 64;
    s.align_mode = 0;
    s.format = raster_output_format_bitmap;
    s.normalize();
    EXPECT_THROW(bitmap_info(s, 0), raster_exception);
}
