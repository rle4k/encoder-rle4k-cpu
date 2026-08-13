#pragma once

#include <vector>
#include <memory>
#include <atomic>

#include "rle4k_bitmap.h"

BEGIN_SP2_NAMESPACE()

#pragma pack(4)
struct memory_buffer {
private:
    memory_buffer() {}
public:
    ~memory_buffer() { buffer = nullptr; buffer_size = 0; }
    static const memory_buffer* get_object(const char* d) {
        return reinterpret_cast<const memory_buffer*>(d - sizeof(memory_buffer));
    }
    static void delete_to_system(memory_buffer* mb) {
        if (mb) { try { delete[] reinterpret_cast<char*>(mb); } catch (...) {} }
    }
    static memory_buffer* new_from_system(uint32_t size, bool big = false) {
        try {
            auto* base = new char[sizeof(memory_buffer) + size];
            auto* ptr  = reinterpret_cast<memory_buffer*>(base);
            ptr->buffer      = base + sizeof(memory_buffer);
            ptr->big_buffer  = big;
            ptr->buffer_size = size;
            return ptr;
        } catch (...) { return nullptr; }
    }
    uint32_t buffer_size = 0;
    bool     big_buffer  = false;
    char*    buffer      = nullptr;
};
using MemoryBuffer = memory_buffer;

/* TLS-backed pool: alloc/free never take a global lock.
 * initialize() only publishes the preferred block size for workers. */
class global_buffer_manager {
    std::atomic<uint32_t> current_buffer_size{0};
    std::atomic<bool> initialized{false};

    global_buffer_manager();
public:
    global_buffer_manager(const global_buffer_manager&) = delete;
    global_buffer_manager& operator=(const global_buffer_manager&) = delete;
    static global_buffer_manager& get_instance();
    ~global_buffer_manager();

    memory_buffer* alloc(uint32_t size, bool big = false);
    void free(char* buffer);
    void clear();
    void initialize(uint32_t strip_width, uint32_t block_height, uint32_t block_count);
    const memory_buffer* get_object(const char* buffer) const;
    static uint32_t get_memory_buffer_size(uint32_t strip_width, uint32_t block_height) {
        return ((strip_width + 511) / 512) * 512 / 8 * block_height;
    }
private:
    memory_buffer* big_alloc(uint32_t size);
    memory_buffer* normal_alloc(uint32_t size);
};
using GlobalBufferManager = global_buffer_manager;

/* Helper functions */
inline void initialize_global_buffer_manager(uint32_t sw, uint32_t bh, uint32_t bc) {
    global_buffer_manager::get_instance().initialize(sw, bh, bc);
}
inline memory_buffer* mb_alloc(uint32_t sz) { return global_buffer_manager::get_instance().alloc(sz, false); }
inline memory_buffer* mb_alloc_big(uint32_t sz) { return global_buffer_manager::get_instance().alloc(sz, true); }
inline void mb_free(char* buf) { if (buf) global_buffer_manager::get_instance().free(buf); }

inline memory_buffer* mb_new_throw(uint32_t sz, bool big = false) {
    auto* b = big ? mb_alloc_big(sz) : mb_alloc(sz);
    if (!b) throw raster_exception(INVALID_MEMORY_BUFFER, "OOM");
    return b;
}

/* Compatibility macros */
#define MB_NEW(sz) mb_alloc(sz)
#define MB_NEW_THROW(...) mb_new_throw(__VA_ARGS__)
#define MB_DELETE(p) mb_free(p)

#pragma pack()
END_SP2_NAMESPACE()
