#include "rle4k_buffer_mgr.h"
#include <algorithm>

USING_SP2_NAMESPACE();

namespace {

/* Per-thread LIFO cache: all hot alloc/free stay unlocked.
 * Shared queues are omitted — partitioned workers do not share buffers. */
struct tls_buffer_cache {
    static constexpr size_t kMaxNormal = 4;
    static constexpr size_t kMaxBig = 2;

    std::vector<memory_buffer*> normal;
    std::vector<memory_buffer*> big;

    ~tls_buffer_cache() {
        for (auto* b : normal) memory_buffer::delete_to_system(b);
        for (auto* b : big) memory_buffer::delete_to_system(b);
    }

    static tls_buffer_cache& get() {
        static thread_local tls_buffer_cache cache;
        return cache;
    }

    static memory_buffer* pop_reusable(std::vector<memory_buffer*>& stack, uint32_t size) {
        while (!stack.empty()) {
            memory_buffer* buf = stack.back();
            stack.pop_back();
            if (buf->buffer_size >= size) return buf;
            memory_buffer::delete_to_system(buf);
        }
        return nullptr;
    }

    static void push_or_delete(std::vector<memory_buffer*>& stack, size_t cap, memory_buffer* obj) {
        if (stack.size() < cap) stack.push_back(obj);
        else memory_buffer::delete_to_system(obj);
    }
};

void atomic_raise_size(std::atomic<uint32_t>& tracked, uint32_t size) {
    uint32_t cur = tracked.load(std::memory_order_relaxed);
    while (size > cur &&
           !tracked.compare_exchange_weak(cur, size,
               std::memory_order_release, std::memory_order_relaxed)) {
    }
}

} // namespace

global_buffer_manager& global_buffer_manager::get_instance() {
    static global_buffer_manager inst;
    return inst;
}
global_buffer_manager::global_buffer_manager() {}
global_buffer_manager::~global_buffer_manager() { clear(); }

const memory_buffer* global_buffer_manager::get_object(const char* buffer) const {
    return memory_buffer::get_object(buffer);
}

memory_buffer* global_buffer_manager::normal_alloc(uint32_t size) {
    auto& tls = tls_buffer_cache::get();
    if (memory_buffer* buf = tls_buffer_cache::pop_reusable(tls.normal, size))
        return buf;

    const uint32_t pool_size = current_buffer_size.load(std::memory_order_acquire);
    const uint32_t alloc_size = (pool_size > size) ? pool_size : size;
    if (alloc_size > pool_size)
        atomic_raise_size(current_buffer_size, alloc_size);
    return memory_buffer::new_from_system(alloc_size);
}

memory_buffer* global_buffer_manager::big_alloc(uint32_t size) {
    auto& tls = tls_buffer_cache::get();
    if (memory_buffer* buf = tls_buffer_cache::pop_reusable(tls.big, size))
        return buf;
    return memory_buffer::new_from_system(size, true);
}

memory_buffer* global_buffer_manager::alloc(uint32_t size, bool big) {
    return big ? big_alloc(size) : normal_alloc(size);
}

void global_buffer_manager::free(char* buffer) {
    if (!buffer) return;
    auto* obj = reinterpret_cast<memory_buffer*>(buffer - sizeof(memory_buffer));
    auto& tls = tls_buffer_cache::get();
    if (obj->big_buffer)
        tls_buffer_cache::push_or_delete(tls.big, tls_buffer_cache::kMaxBig, obj);
    else
        tls_buffer_cache::push_or_delete(tls.normal, tls_buffer_cache::kMaxNormal, obj);
}

void global_buffer_manager::clear() {
    /* TLS caches are released when their owning threads exit.
     * initialize()/clear() run on the main thread between worker joins. */
    current_buffer_size.store(0, std::memory_order_release);
    initialized.store(false, std::memory_order_release);
}

void global_buffer_manager::initialize(uint32_t strip_width, uint32_t block_height, uint32_t /*block_count*/) {
    const uint32_t ts = get_memory_buffer_size(strip_width, block_height);
    if (initialized.load(std::memory_order_acquire) &&
        ts <= current_buffer_size.load(std::memory_order_acquire))
        return;
    current_buffer_size.store(ts, std::memory_order_release);
    initialized.store(true, std::memory_order_release);
}
