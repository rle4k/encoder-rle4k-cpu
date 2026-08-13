#pragma once
/*
 * RLE4K row-block work queue for parallel strip processing.
 *
 * Workers pop row indices and run the full per-block pipeline:
 *   rasterize -> encode (timed) -> decode (round-trip check)
 *
 * The queue coordinates block distribution across threads; encode and
 * decode benchmarks run back-to-back on each block before the worker pops
 * the next row.
 */

#include <atomic>

class rle4k_row_block_queue {
public:
    explicit rle4k_row_block_queue(int total_rows) : total_(total_rows), next_(0) {}

    /* Next row index, or -1 when all rows have been issued. */
    int pop() {
        int row = next_.fetch_add(1, std::memory_order_relaxed);
        return row < total_ ? row : -1;
    }

    int total() const { return total_; }

private:
    int total_;
    std::atomic<int> next_;
};
