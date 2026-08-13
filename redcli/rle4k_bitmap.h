#pragma once

#include <bitmap_info.h>

BEGIN_SP2_NAMESPACE()

/* ---- encode_format_args (tool / codec params; not part of libraster) ---- */
#pragma pack(4)
struct encode_format_args {
    double  strip_width   = 0.0;
    bool    mirror        = false;
    uint8_t bg_polarity   = 1;

    int gzip_level    = 6;
    int zstd_level    = 3;
    int lzo2_level    = 1;
    int snappy_level  = 0;
    int brotli_level  = 6;
    int brotli_window = 22;

    encode_format_args() = default;
    encode_format_args(double sw, bool m, uint8_t bgp)
        : strip_width(sw), mirror(m), bg_polarity(bgp) {}
};
#pragma pack()

END_SP2_NAMESPACE()