#include <gtest/gtest.h>

#include <cstdint>

#include "lib/utils/mem.h"

using utils::MemPool;

// ── Trivial payload for basic tests ──────────────────────────────
struct Payload {
    int value = 0;
};

// ── Type that tracks constructor / destructor calls ─────────────
struct Tracked {
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static inline int alive = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static inline int ctor_calls = 0;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static inline int dtor_calls = 0;

    int id;

    explicit Tracked(int id = 0) : id(id) {
        ++alive;
        ++ctor_calls;
    }
    ~Tracked() {
        --alive;
        ++dtor_calls;
    }

    Tracked(const Tracked &) = delete;
    Tracked &operator=(const Tracked &) = delete;
    Tracked(Tracked &&) = delete;
    Tracked &operator=(Tracked &&) = delete;

    static void reset() noexcept {
        alive = 0;
        ctor_calls = 0;
        dtor_calls = 0;
    }
};

// ── Construction ────────────────────────────────────────────────
TEST(MemPoolTest, ConstructDoesNotThrow) {
    EXPECT_NO_THROW(MemPool<int>(16));
}

// ── Allocate / Deallocate ───────────────────────────────────────
TEST(MemPoolTest, AllocateReturnsNonNull) {
    MemPool<Payload> pool(4);
    auto *ptr = pool.allocate(42);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->value, 42);
}

TEST(MemPoolTest, AllocateMultipleReturnsDistinctPointers) {
    MemPool<Payload> pool(4);
    auto *obj1 = pool.allocate(1);
    auto *obj2 = pool.allocate(2);
    auto *obj3 = pool.allocate(3);

    ASSERT_NE(obj1, nullptr);
    ASSERT_NE(obj2, nullptr);
    ASSERT_NE(obj3, nullptr);

    EXPECT_NE(obj1, obj2);
    EXPECT_NE(obj2, obj3);
    EXPECT_NE(obj1, obj3);

    EXPECT_EQ(obj1->value, 1);
    EXPECT_EQ(obj2->value, 2);
    EXPECT_EQ(obj3->value, 3);
}

TEST(MemPoolTest, DeallocateAllowsReuse) {
    MemPool<Payload> pool(2);
    auto *first = pool.allocate(10);
    pool.deallocate(first);

    // LIFO stack: the just-freed slot (index 0) is reused immediately.
    auto *second = pool.allocate(20);
    EXPECT_EQ(first, second);
    EXPECT_EQ(second->value, 20);

    // Deallocate both; allocate again reuses the most recently freed slot.
    pool.deallocate(second);
    auto *third = pool.allocate(30);
    EXPECT_EQ(second, third);
    EXPECT_EQ(third->value, 30);
}

// ── LIFO reuse ──────────────────────────────────────────────────
TEST(MemPoolTest, LIFOReuseAfterPartialFree) {
    MemPool<Payload> pool(5);

    auto *obj1 = pool.allocate(1);
    auto *obj2 = pool.allocate(2);
    auto *obj3 = pool.allocate(3);

    // Free obj1 then obj2; LIFO stack means obj2's slot is on top.
    pool.deallocate(obj1);
    pool.deallocate(obj2);

    auto *obj4 = pool.allocate(4);
    auto *obj5 = pool.allocate(5);

    // obj4 reuses obj2's slot (most recently freed), obj5 reuses obj1's slot.
    EXPECT_EQ(obj4, obj2);
    EXPECT_EQ(obj5, obj1);

    EXPECT_EQ(obj4->value, 4);
    EXPECT_EQ(obj5->value, 5);

    // obj3 should still hold its original value.
    EXPECT_EQ(obj3->value, 3);

    // All allocated pointers should be distinct from each other.
    EXPECT_NE(obj4, obj3);
    EXPECT_NE(obj5, obj3);
    EXPECT_NE(obj4, obj5);
}

// ── Exhaustion ──────────────────────────────────────────────────
TEST(MemPoolTest, ExhaustPoolReturnsNull) {
    MemPool<Payload> pool(3);
    auto *obj1 = pool.allocate(1);
    auto *obj2 = pool.allocate(2);
    auto *obj3 = pool.allocate(3);

    ASSERT_NE(obj1, nullptr);
    ASSERT_NE(obj2, nullptr);
    ASSERT_NE(obj3, nullptr);

    // Pool is now full — next allocate should return nullptr.
    auto *obj4 = pool.allocate(4);
    EXPECT_EQ(obj4, nullptr);

    // After freeing one slot, allocate succeeds again.
    pool.deallocate(obj2);
    auto *obj5 = pool.allocate(5);
    ASSERT_NE(obj5, nullptr);
    EXPECT_EQ(obj5->value, 5);
}

// ── Default-constructible requirement ───────────────────────────
TEST(MemPoolTest, WorksWithTrivialType) {
    MemPool<std::int64_t> pool(8);
    auto *ptr = pool.allocate(12345LL);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 12345LL);
}

TEST(MemPoolTest, DeallocateThenAllocateMultipleTimes) {
    MemPool<Payload> pool(2);
    for (int i = 0; i < 5; ++i) {
        auto *ptr = pool.allocate(i);
        ASSERT_NE(ptr, nullptr);
        EXPECT_EQ(ptr->value, i);
        pool.deallocate(ptr);
    }
}

// ── Constructor / Destructor tracking ───────────────────────────
TEST(MemPoolTest, ConstructorCalledOnAllocate) {
    Tracked::reset();
    {
        MemPool<Tracked> pool(4);
        EXPECT_EQ(Tracked::ctor_calls, 0);  // no default construction

        auto *t1 = pool.allocate(10);
        EXPECT_EQ(Tracked::ctor_calls, 1);
        EXPECT_EQ(Tracked::alive, 1);
        EXPECT_EQ(t1->id, 10);

        auto *t2 = pool.allocate(20);
        EXPECT_EQ(Tracked::ctor_calls, 2);
        EXPECT_EQ(Tracked::alive, 2);
        EXPECT_EQ(t2->id, 20);
    }
    // Pool destroyed — destructors called for remaining live objects
    EXPECT_EQ(Tracked::dtor_calls, 2);
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(MemPoolTest, DestructorCalledOnDeallocate) {
    Tracked::reset();
    {
        MemPool<Tracked> pool(4);
        auto *t1 = pool.allocate(1);
        auto *t2 = pool.allocate(2);
        EXPECT_EQ(Tracked::ctor_calls, 2);
        EXPECT_EQ(Tracked::alive, 2);

        pool.deallocate(t1);
        EXPECT_EQ(Tracked::dtor_calls, 1);
        EXPECT_EQ(Tracked::alive, 1);

        pool.deallocate(t2);
        EXPECT_EQ(Tracked::dtor_calls, 2);
        EXPECT_EQ(Tracked::alive, 0);
    }
    // No extra dtor calls from pool destruction (all already freed)
    EXPECT_EQ(Tracked::dtor_calls, 2);
}

TEST(MemPoolTest, DestructorCalledOnPoolDestruction) {
    Tracked::reset();
    {
        MemPool<Tracked> pool(5);
        (void)pool.allocate(1);
        (void)pool.allocate(2);
        (void)pool.allocate(3);
        // Only deallocate one; two remain alive.
        auto *t4 = pool.allocate(4);
        EXPECT_EQ(Tracked::alive, 4);
        pool.deallocate(t4);
        EXPECT_EQ(Tracked::alive, 3);
    }
    // Pool destructor cleans up the remaining 3
    EXPECT_EQ(Tracked::dtor_calls, 4);  // 1 explicit + 3 from pool dtor
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(MemPoolTest, ConstructorDestructorOnReuse) {
    Tracked::reset();
    {
        MemPool<Tracked> pool(2);
        auto *t1 = pool.allocate(100);
        EXPECT_EQ(Tracked::ctor_calls, 1);
        EXPECT_EQ(t1->id, 100);

        pool.deallocate(t1);
        EXPECT_EQ(Tracked::dtor_calls, 1);

        // Reuse the same slot — new constructor call
        auto *t2 = pool.allocate(200);
        EXPECT_EQ(Tracked::ctor_calls, 2);
        EXPECT_EQ(t2->id, 200);

        pool.deallocate(t2);
        EXPECT_EQ(Tracked::dtor_calls, 2);
    }
    EXPECT_EQ(Tracked::alive, 0);
}

TEST(MemPoolTest, NoDefaultConstruction) {
    Tracked::reset();
    MemPool<Tracked> pool(8);
    // Creating the pool should not call any constructors
    EXPECT_EQ(Tracked::ctor_calls, 0);
    EXPECT_EQ(Tracked::alive, 0);
}

// ── Allocated count / capacity ─────────────────────────────────
TEST(MemPoolTest, AllocatedCountTracksInUseObjects) {
    MemPool<Payload> pool(5);
    EXPECT_EQ(pool.allocated_count(), 0);
    EXPECT_EQ(pool.capacity(), 5);

    auto *obj1 = pool.allocate(1);
    EXPECT_EQ(pool.allocated_count(), 1);

    auto *obj2 = pool.allocate(2);
    EXPECT_EQ(pool.allocated_count(), 2);

    pool.deallocate(obj1);
    EXPECT_EQ(pool.allocated_count(), 1);

    pool.deallocate(obj2);
    EXPECT_EQ(pool.allocated_count(), 0);
}

TEST(MemPoolTest, AllocatedCountReflectsFullPool) {
    MemPool<Payload> pool(3);
    (void)pool.allocate(1);
    (void)pool.allocate(2);
    (void)pool.allocate(3);
    EXPECT_EQ(pool.allocated_count(), 3);
    EXPECT_EQ(pool.allocated_count(), pool.capacity());

    auto *extra = pool.allocate(4);
    EXPECT_EQ(extra, nullptr);
    EXPECT_EQ(pool.allocated_count(), 3);  // count unchanged on failure
}
