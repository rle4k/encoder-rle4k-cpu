#pragma once

#include <memory>
#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>

#include "base/shared.h"
#include "cndefs.h"

BEGIN_SP2_NAMESPACE()

/* ---- error codes ---- */
constexpr int RASTER_ERROR_BASE         = -1;
constexpr int INVALID_BITMAP_SOURCE     = RASTER_ERROR_BASE - 12;
constexpr int INVALID_BITMAP_ZEROSIZE   = RASTER_ERROR_BASE - 7;
constexpr int INVALID_BITMAP_OVERSIZE   = RASTER_ERROR_BASE - 8;
constexpr int INVALID_MEMORY_BUFFER     = RASTER_ERROR_BASE - 10;
constexpr int SHOULD_NOT_BE_CALLED      = RASTER_ERROR_BASE - 9;
constexpr int OPEN_FILE_RO_FAILED       = RASTER_ERROR_BASE - 1;
constexpr int INVALID_BITMAP_HEADER     = RASTER_ERROR_BASE - 2;
constexpr int INVALID_BITMAP_DATA       = RASTER_ERROR_BASE - 3;
constexpr int FILE_NAME_IS_EMTPY        = RASTER_ERROR_BASE - 4;
constexpr int FILE_CREATE_FAILED        = RASTER_ERROR_BASE - 5;
constexpr int FILE_WRITE_FAILED         = RASTER_ERROR_BASE - 6;

/* ---- raster_output_format ---- */
enum raster_output_format : uint8_t {
    raster_output_format_none   = 0x0,
    raster_output_format_bitmap = 0x1,
    raster_output_format_rle4k  = 0x4,
    raster_output_format_gzip   = 0x7,
    raster_output_format_zstd   = 0x8,
    raster_output_format_snappy = 0xA,
    raster_output_format_lzo2   = 0xB,
    raster_output_formaT_BROTLI = 0xF,
};
using RasterOutputFormat = raster_output_format;

/* Compatibility macros */
#define RASTER_OUTPUT_FORMAT_RLE4K   raster_output_format_rle4k
#define RASTER_OUTPUT_FORMAT_GZIP    raster_output_format_gzip
#define RASTER_OUTPUT_FORMAT_ZSTD    raster_output_format_zstd
#define RASTER_OUTPUT_FORMAT_SNAPPY  raster_output_format_snappy
#define RASTER_OUTPUT_FORMAT_LZO2    raster_output_format_lzo2
#define RASTER_OUTPUT_FORMAT_BROTLI raster_output_formaT_BROTLI

/* ---- map_align_mode ---- */
enum map_align_mode : unsigned char {
    ALIGN008 = 0, ALIGN016 = 1, ALIGN032 = 2, ALIGN064 = 3,
    ALIGN128 = 4, ALIGN256 = 5, ALIGN512 = 6,
};
using MAP_ALIGN_MODE = map_align_mode;

constexpr uint32_t MAX_BITMAP_WIDTH  = 1024 * 1024;
constexpr uint32_t MAX_BITMAP_HEIGHT = 1024 * 1024;

/* ---- raster_result_settings ---- */
#pragma pack(4)
struct raster_result_settings {
    raster_output_format format     = raster_output_format_none;
    uint8_t  align_mode = 3;
    uint16_t align_bits = 0;
    unsigned int width   = 0;
    unsigned int height  = 0;
    double pixel_size_x  = 0.0;
    double pixel_size_y  = 0.0;

    inline void set_pixel_size(double px, double py) { pixel_size_x = px; pixel_size_y = py; }
    void normalize() {
        if (align_mode > 6) align_mode = 6;
        align_bits = (uint16_t)(8u << align_mode);
        if (abs(pixel_size_x) < 0.0001) pixel_size_x = 1;
        if (abs(pixel_size_y) < 0.0001) pixel_size_y = 1;
    }
};
using RasterResultSettings = raster_result_settings;
#pragma pack()

/* ---- extend_bitmap_info ---- */
#pragma pack(4)
struct extend_bitmap_info {
    uint16_t compression_ratio = 0;
    uint32_t raster_elapsed_us = 0;
    uint32_t encode_elapsed_us = 0;
private:
    uint32_t data_buffer_size  = 0;
public:
    uint32_t stride_bytes      = 0;

    extend_bitmap_info() = default;
    extend_bitmap_info(const extend_bitmap_info& other);
    extend_bitmap_info(const extend_bitmap_info* other);
    extend_bitmap_info& operator=(const extend_bitmap_info& other);
    inline void set_data_buffer_size(uint32_t sz) { data_buffer_size = sz; }
    inline uint32_t get_buffer_size() const { return data_buffer_size; }
};
using ExtendBitMapInfo = extend_bitmap_info;
#pragma pack()

/* ---- bitmap_info ---- */
#pragma pack(4)
struct bitmap_info {
    uint32_t index        = 0;
    uint32_t width        = 0;
    uint32_t height       = 0;
    uint32_t data_length  = 0;
    char*    data         = nullptr;
    uint32_t source_length = 0;
    raster_output_format format = raster_output_format_none;
    uint8_t  align_mode   = 3;
    extend_bitmap_info* extends = nullptr;

    inline uint32_t get_stride_bytes() const { return extends->stride_bytes; }
    inline uint32_t get_buffer_size() const {
        if (extends->get_buffer_size() > 0) return extends->get_buffer_size();
        return source_length > data_length ? source_length : data_length;
    }
    uint32_t get_align_bits();
    void resize();
    /* Reallocate buffer to data_length after encode (rle16cli shrink_to_fit parity). */
    void shrink_to_fit();

    bitmap_info(const char* header);
    static std::unique_ptr<bitmap_info> load_file(const char* file_name);
    void save(const std::string& file_name);
    bitmap_info(const raster_result_settings& settings, uint32_t block_height);
    bitmap_info(const bitmap_info& source);
    bitmap_info(const bitmap_info* source);
    bitmap_info(uint32_t width, uint32_t height, map_align_mode am = ALIGN008);
    bitmap_info(uint32_t w, uint32_t h, raster_output_format fmt,
                const std::vector<uint8_t>& data_vec, map_align_mode am = ALIGN008);
    bitmap_info(uint32_t w, uint32_t h, raster_output_format fmt,
                const char* data_ptr, uint32_t data_size, map_align_mode am = ALIGN008);
    bitmap_info* clone() const;
    std::unique_ptr<bitmap_info> deep_clone() const;
    ~bitmap_info();
};
using BitMapInfo = bitmap_info;
#pragma pack()

/* ---- raster_exception ---- */
class raster_exception : public std::exception {
    int error_code;
public:
    raster_exception(int code, const char* msg)
        : std::exception(msg), error_code(code) {}
    int get_error_code() const { return error_code; }
};

/* ---- validate ---- */
inline void validate_bitmap(const bitmap_info* source) {
    if (!source || !source->data || source->data_length == 0 || source->source_length == 0)
        throw raster_exception(INVALID_BITMAP_SOURCE, "Invalid bitmap source");
    if (source->width == 0 || source->height == 0)
        throw raster_exception(INVALID_BITMAP_ZEROSIZE, "Zero dimension");
    if (source->width > MAX_BITMAP_WIDTH || source->height > MAX_BITMAP_HEIGHT)
        throw raster_exception(INVALID_BITMAP_OVERSIZE, "Exceeds max size");
}

END_SP2_NAMESPACE()
