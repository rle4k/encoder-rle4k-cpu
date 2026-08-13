#pragma once

#include <cstdint>
#include <chrono>
#include "cndefs.h"
#include "rle4k_bitmap.h"

BEGIN_SP2_NAMESPACE()

struct block_performance_stats {
    uint64_t rasterization_time_us = 0;
    uint64_t compression_time_us   = 0;
    size_t   original_size         = 0;
    size_t   compressed_size       = 0;
    double   compression_ratio     = 0.0;

    void reset() { memset(this, 0, sizeof(*this)); }
    explicit block_performance_stats(const bitmap_info* bmp) {
        rasterization_time_us = bmp->extends->raster_elapsed_us;
        compression_time_us   = bmp->extends->encode_elapsed_us;
        compression_ratio     = bmp->extends->compression_ratio;
        original_size         = bmp->source_length;
        compressed_size       = bmp->data_length;
    }
};

class performance_timer {
    using clock = std::chrono::high_resolution_clock;
    clock::time_point start_time;
    clock::time_point end_time;
    bool running = false;
public:
    void start() { start_time = clock::now(); running = true; }
    void stop()  { if (running) { end_time = clock::now(); running = false; } }
    uint32_t elapsed_us() const {
        auto now = running ? clock::now() : end_time;
        return (uint32_t)std::chrono::duration_cast<std::chrono::microseconds>(now - start_time).count();
    }
    void reset() { running = false; }
};

END_SP2_NAMESPACE()
