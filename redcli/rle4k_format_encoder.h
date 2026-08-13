#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <map>

#include "rle4k_bitmap.h"

BEGIN_SP2_NAMESPACE()

/* ---- format_encoder base class ---- */
class format_encoder {
public:
    virtual ~format_encoder() = default;
    virtual raster_output_format get_supported_format() const = 0;
    virtual const char* get_format_name() const = 0;
    virtual const char* get_file_extension() const = 0;
    virtual format_encoder* clone() const = 0;
    virtual bool supports_gpu() { return false; }
    std::vector<int> get_supported_formats() const { return { (int)get_supported_format() }; }
    virtual std::unique_ptr<bitmap_info> process_pack(const bitmap_info* source, const encode_format_args& args);
protected:
    virtual int internal_pack(const bitmap_info* source, bitmap_info* target, const encode_format_args& args) {
        throw raster_exception(SHOULD_NOT_BE_CALLED, "base internal_pack not callable.");
    }
};

/* ---- six encoder subclasses ---- */
class rle4k_format_encoder : public format_encoder {
public:
    raster_output_format get_supported_format() const override { return raster_output_format_rle4k; }
    const char* get_format_name() const override { return "RLE4K"; }
    const char* get_file_extension() const override { return ".rle4k"; }
    format_encoder* clone() const override { return new rle4k_format_encoder(*this); }
protected:
    int internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) override;
};

class gzip_format_encoder : public format_encoder {
public:
    raster_output_format get_supported_format() const override { return raster_output_format_gzip; }
    const char* get_format_name() const override { return "GZIP"; }
    const char* get_file_extension() const override { return ".gzip"; }
    format_encoder* clone() const override { return new gzip_format_encoder(*this); }
protected:
    int internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) override;
};

class lzo2_format_encoder : public format_encoder {
public:
    raster_output_format get_supported_format() const override { return raster_output_format_lzo2; }
    const char* get_format_name() const override { return "LZO2"; }
    const char* get_file_extension() const override { return ".lzo2"; }
    format_encoder* clone() const override { return new lzo2_format_encoder(*this); }
protected:
    int internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) override;
};

class snappy_format_encoder : public format_encoder {
public:
    raster_output_format get_supported_format() const override { return raster_output_format_snappy; }
    const char* get_format_name() const override { return "SNAPPY"; }
    const char* get_file_extension() const override { return ".snappy"; }
    format_encoder* clone() const override { return new snappy_format_encoder(*this); }
protected:
    int internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) override;
};

class zstd_format_encoder : public format_encoder {
public:
    raster_output_format get_supported_format() const override { return raster_output_format_zstd; }
    const char* get_format_name() const override { return "ZSTD"; }
    const char* get_file_extension() const override { return ".zstd"; }
    format_encoder* clone() const override { return new zstd_format_encoder(*this); }
protected:
    int internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) override;
};

class brotli_format_encoder : public format_encoder {
public:
    raster_output_format get_supported_format() const override { return raster_output_formaT_BROTLI; }
    const char* get_format_name() const override { return "BROTLI"; }
    const char* get_file_extension() const override { return ".brotli"; }
    format_encoder* clone() const override { return new brotli_format_encoder(*this); }
protected:
    int internal_pack(const bitmap_info* src, bitmap_info* dst, const encode_format_args& args) override;
};

/* ---- format_encoder_factory ---- */
class format_encoder_factory {
public:
    static format_encoder_factory& get_instance();
    format_encoder* get_encoder(int fmt);
    void register_processor(std::unique_ptr<format_encoder> proc);
    bool is_format_supported(int fmt) const;
    std::vector<int> get_supported_formats() const;
private:
    format_encoder_factory();
    format_encoder_factory(const format_encoder_factory&) = delete;
    format_encoder_factory& operator=(const format_encoder_factory&) = delete;
    void initialize_default_processors();
    std::map<int, std::unique_ptr<format_encoder>> processors;
};

END_SP2_NAMESPACE()
