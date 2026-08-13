#include <gtest/gtest.h>
#include <mutex>
#include <thread>
#include <vector>
#include <set>

#include "rle4k_stats.h"
#include "rle4k_block_pipeline.h"
#include "rle4k_bitmap.h"
#include "rle4k_buffer_mgr.h"
#include "test_helpers.h"

USING_SP2_NAMESPACE();

TEST(Stats, PerformanceTimer) {
    performance_timer t;
    t.start();
    /* busy-ish wait */
    volatile int x = 0;
    for (int i = 0; i < 100000; ++i) x += i;
    t.stop();
    EXPECT_GE(t.elapsed_us(), 0u);
    t.reset();
}

TEST(Stats, BlockPerformanceStats) {
    initialize_global_buffer_manager(64, 8, 1);
    bitmap_info bmp(64, 8, ALIGN008);
    bmp.extends->raster_elapsed_us = 10;
    bmp.extends->encode_elapsed_us = 20;
    bmp.extends->compression_ratio = 5000;
    bmp.source_length = 100;
    bmp.data_length = 40;

    block_performance_stats s(&bmp);
    EXPECT_EQ(s.rasterization_time_us, 10u);
    EXPECT_EQ(s.compression_time_us, 20u);
    EXPECT_EQ(s.original_size, 100u);
    EXPECT_EQ(s.compressed_size, 40u);
    s.reset();
    EXPECT_EQ(s.original_size, 0u);
}

TEST(BlockPipeline, PopAllRows) {
    rle4k_row_block_queue q(5);
    EXPECT_EQ(q.total(), 5);
    std::set<int> rows;
    for (;;) {
        int r = q.pop();
        if (r < 0) break;
        rows.insert(r);
    }
    EXPECT_EQ(rows.size(), 5u);
    EXPECT_EQ(q.pop(), -1);
}

TEST(BlockPipeline, ConcurrentPop) {
    constexpr int N = 200;
    rle4k_row_block_queue q(N);
    std::vector<int> collected;
    collected.reserve(N);
    std::mutex mu;

    auto worker = [&]() {
        for (;;) {
            int r = q.pop();
            if (r < 0) break;
            std::lock_guard<std::mutex> lock(mu);
            collected.push_back(r);
        }
    };

    std::thread t1(worker), t2(worker), t3(worker);
    t1.join(); t2.join(); t3.join();

    EXPECT_EQ(collected.size(), static_cast<size_t>(N));
    std::set<int> uniq(collected.begin(), collected.end());
    EXPECT_EQ(uniq.size(), static_cast<size_t>(N));
}

TEST(BlockPipeline, ZeroRows) {
    rle4k_row_block_queue q(0);
    EXPECT_EQ(q.pop(), -1);
}
