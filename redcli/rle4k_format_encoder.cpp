#include "rle4k_format_encoder.h"
#include <rle4k.h>
#include <zlib.h>
#include <lzo/lzo1x.h>
#include <snappy.h>
#include <zstd.h>
#include <brotli/encode.h>

USING_SP2_NAMESPACE();

/* ---- format_encoder base ---- */
std::unique_ptr<bitmap_info> format_encoder::process_pack(const bitmap_info* source, const encode_format_args& args) {
    validate_bitmap(source);
    raster_result_settings settings;
    settings.format = get_supported_format();
    settings.width  = source->width;
    settings.align_mode = 3; /* ALIGN064 */
    auto target = std::make_unique<bitmap_info>(settings, source->height);

    int result = internal_pack(source, target.get(), args);
    if (result < 0) throw raster_exception(result, "Encoding failed");

    target->source_length = source->data_length;
    if (target->extends) {
        target->extends->compression_ratio = (uint16_t)(
            (1.0 - (double)target->data_length / (double)source->data_length) * 10000);
    }
    /* Encoded payload only — no 8× retain; matches rle16cli EncodedBlock::shrink_to_fit. */
    target->shrink_to_fit();
    return target;
}

/* ---- rle4k ---- */
int rle4k_format_encoder::internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args&) {
    dst->data_length = 0;
    uint32_t stride = src->get_stride_bytes();
    uint32_t cap = dst->get_buffer_size();
    int len = rle4k_encode_block((const uint8_t*)src->data, src->height, src->width, (uint8_t*)dst->data, stride, cap);
    if (len == RLE4K_ERROR_TARGET_TOO_SMALL) {
        dst->resize();
        cap = dst->get_buffer_size();
        len = rle4k_encode_block((const uint8_t*)src->data, src->height, src->width, (uint8_t*)dst->data, stride, cap);
    }
    if (len < 0) return len;
    dst->data_length = (uint32_t)len;
    return len;
}

/* ---- gzip ---- */
int gzip_format_encoder::internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) {
    uLong src_size = src->data_length;
    uLong comp_size = dst->get_buffer_size();
    int level = args.gzip_level;
    if (level < 1) level = 1; if (level > 9) level = 9;
    int rc = compress2((Bytef*)dst->data, &comp_size, (const Bytef*)src->data, src_size, level);
    if (rc != Z_OK) return (int)dst->format * (-1000) + rc;
    dst->data_length = (uint32_t)comp_size;
    return (int)comp_size;
}

/* ---- lzo2 ---- */
/* LZO compress needs a wrkmem buffer and is NOT re-entrant on a shared one.
 * Multi-threaded encode (redcli -et N) previously raced on a process-wide
 * static, causing intermittent NG round-trips and ACCESS_VIOLATION (0xC0000005). */
static thread_local unsigned char lzo_wrkmem[LZO1X_999_MEM_COMPRESS];
int lzo2_format_encoder::internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) {
    lzo_uint out_len = dst->get_buffer_size();
    int level = args.lzo2_level;
    if (level < 1) level = 1; if (level > 999) level = 999;
    int rc;
    if (level == 1) {
        rc = lzo1x_1_compress((const unsigned char*)src->data, src->data_length,
                              (unsigned char*)dst->data, &out_len, lzo_wrkmem);
    } else {
        rc = lzo1x_999_compress_level((const unsigned char*)src->data, src->data_length,
                                      (unsigned char*)dst->data, &out_len, lzo_wrkmem,
                                      nullptr, 0, 0, level);
    }
    if (rc != LZO_E_OK) return (int)dst->format * (-1000) + rc;
    dst->data_length = (uint32_t)out_len;
    return (int)out_len;
}

/* ---- snappy ---- */
int snappy_format_encoder::internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args&) {
    size_t out_len = dst->get_buffer_size();
    snappy::RawCompress(src->data, src->data_length, dst->data, &out_len);
    dst->data_length = (uint32_t)out_len;
    return (int)out_len;
}

/* ---- zstd ---- */
int zstd_format_encoder::internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) {
    int level = args.zstd_level;
    if (level < 1) level = 1; if (level > 22) level = 22;
    size_t out_len = ZSTD_compress(dst->data, dst->get_buffer_size(), src->data, src->data_length, level);
    if (ZSTD_isError(out_len)) return (int)dst->format * (-1000) + (int)ZSTD_getErrorCode(out_len);
    dst->data_length = (uint32_t)out_len;
    return (int)out_len;
}

/* ---- brotli ---- */
int brotli_format_encoder::internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) {
    size_t out_len = dst->get_buffer_size();
    int level = args.brotli_level;
    int window = args.brotli_window;
    if (level < 0) level = 0; if (level > 11) level = 11;
    if (window < 10) window = 10; if (window > 24) window = 24;
    if (BrotliEncoderCompress(level, window, BROTLI_DEFAULT_MODE, src->data_length,
                              (const uint8_t*)src->data, &out_len, (uint8_t*)dst->data) != BROTLI_TRUE)
        return (int)dst->format * (-1000) - 1;
    dst->data_length = (uint32_t)out_len;
    return (int)out_len;
}

/* ---- format_encoder_factory ---- */
format_encoder_factory::format_encoder_factory() { initialize_default_processors(); }

format_encoder_factory& format_encoder_factory::get_instance() {
    static format_encoder_factory instance;
    return instance;
}

format_encoder* format_encoder_factory::get_encoder(int fmt) {
    auto it = processors.find(fmt);
    return it != processors.end() ? it->second.get() : nullptr;
}

void format_encoder_factory::register_processor(std::unique_ptr<format_encoder> proc) {
    if (proc) {
        for (int fmt : proc->get_supported_formats())
            processors[fmt] = std::unique_ptr<format_encoder>(proc->clone());
    }
}

bool format_encoder_factory::is_format_supported(int fmt) const {
    return processors.find(fmt) != processors.end();
}

std::vector<int> format_encoder_factory::get_supported_formats() const {
    std::vector<int> fmts; fmts.reserve(processors.size());
    for (auto& p : processors) fmts.push_back(p.first);
    return fmts;
}

void format_encoder_factory::initialize_default_processors() {
    register_processor(std::make_unique<rle4k_format_encoder>());
    register_processor(std::make_unique<gzip_format_encoder>());
    register_processor(std::make_unique<lzo2_format_encoder>());
    register_processor(std::make_unique<snappy_format_encoder>());
    register_processor(std::make_unique<zstd_format_encoder>());
    register_processor(std::make_unique<brotli_format_encoder>());
}
