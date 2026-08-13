#include <gtest/gtest.h>
#include <thread>

#include "rle4k_buffer_mgr.h"

USING_SP2_NAMESPACE();

TEST(BufferMgr, SingletonAndInitialize) {
    auto& mgr = global_buffer_manager::get_instance();
    mgr.clear();
    mgr.initialize(512, 64, 4);
    uint32_t expected = global_buffer_manager::get_memory_buffer_size(512, 64);
    EXPECT_GT(expected, 0u);

    /* second init with smaller size is a no-op when already initialized larger */
    mgr.initialize(256, 32, 2);
}

TEST(BufferMgr, AllocFreeReuse) {
    auto& mgr = global_buffer_manager::get_instance();
    mgr.clear();
    mgr.initialize(256, 32, 2);

    auto* a = mgr.alloc(1024, false);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(a->buffer, nullptr);
    EXPECT_GE(a->buffer_size, 1024u);
    EXPECT_FALSE(a->big_buffer);

    char* ptr = a->buffer;
    mgr.free(ptr);

    auto* b = mgr.alloc(512, false);
    ASSERT_NE(b, nullptr);
    /* TLS cache should reuse the previous buffer when size fits */
    EXPECT_EQ(b->buffer, ptr);
    mgr.free(b->buffer);
}

TEST(BufferMgr, BigAlloc) {
    auto& mgr = global_buffer_manager::get_instance();
    auto* big = mgr.alloc(4096, true);
    ASSERT_NE(big, nullptr);
    EXPECT_TRUE(big->big_buffer);
    mgr.free(big->buffer);
}

TEST(BufferMgr, HelpersAndGetObject) {
    initialize_global_buffer_manager(128, 16, 1);
    auto* mb = mb_new_throw(256, false);
    ASSERT_NE(mb, nullptr);
    const memory_buffer* got = global_buffer_manager::get_instance().get_object(mb->buffer);
    EXPECT_EQ(got, mb);
    mb_free(mb->buffer);
}

TEST(BufferMgr, FreeNullIsSafe) {
    mb_free(nullptr);
    global_buffer_manager::get_instance().free(nullptr);
}

TEST(BufferMgr, SystemNewDelete) {
    auto* mb = memory_buffer::new_from_system(128, false);
    ASSERT_NE(mb, nullptr);
    EXPECT_EQ(mb->buffer_size, 128u);
    memory_buffer::delete_to_system(mb);
}

TEST(BufferMgr, FillTlsCacheAndOverflow) {
    auto& mgr = global_buffer_manager::get_instance();
    mgr.clear();
    mgr.initialize(64, 8, 1);

    std::vector<char*> ptrs;
    for (int i = 0; i < 8; ++i) {
        auto* mb = mgr.alloc(256, false);
        ASSERT_NE(mb, nullptr);
        ptrs.push_back(mb->buffer);
    }
    for (char* p : ptrs) mgr.free(p);

    /* Free more than cache capacity — extras deleted, still safe */
    std::vector<char*> more;
    for (int i = 0; i < 6; ++i) {
        auto* mb = mgr.alloc(128, true);
        ASSERT_NE(mb, nullptr);
        more.push_back(mb->buffer);
    }
    for (char* p : more) mgr.free(p);
}

TEST(BufferMgr, RaisePoolSizeOnDemand) {
    auto& mgr = global_buffer_manager::get_instance();
    mgr.clear();
    mgr.initialize(32, 4, 1);
    auto* small = mgr.alloc(64, false);
    ASSERT_NE(small, nullptr);
    mgr.free(small->buffer);
    auto* big = mgr.alloc(1024 * 64, false);
    ASSERT_NE(big, nullptr);
    EXPECT_GE(big->buffer_size, 1024u * 64u);
    mgr.free(big->buffer);
}

TEST(BufferMgr, PopReusableRejectsTooSmall) {
    auto& mgr = global_buffer_manager::get_instance();
    mgr.clear();
    mgr.initialize(32, 4, 1);
    auto* tiny = mgr.alloc(64, false);
    ASSERT_NE(tiny, nullptr);
    mgr.free(tiny->buffer);
    /* Cached buffer is too small for this request → deleted, fresh alloc */
    auto* wide = mgr.alloc(1024 * 32, false);
    ASSERT_NE(wide, nullptr);
    EXPECT_GE(wide->buffer_size, 1024u * 32u);
    mgr.free(wide->buffer);
}

TEST(BufferMgr, TlsDestructorOnThreadExit) {
    /* TLS cache destructor (delete_to_system) runs when the worker thread exits. */
    std::thread([] {
        auto& mgr = global_buffer_manager::get_instance();
        auto* a = mgr.alloc(256, false);
        auto* b = mgr.alloc(512, true);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        mgr.free(a->buffer);
        mgr.free(b->buffer);
    }).join();
}
