#include <gtest/gtest.h>

#include <array>
#include <initializer_list>
#include <thread>

#include "lib/utils/queue.h"

using utils::LFQueue;

namespace {

/// Produce a sequence of values into the queue.
void produceValues(LFQueue<int>& queue, std::initializer_list<int> values) {
    for (int val : values) {
        *queue.getNextToWriteTo() = val;
        queue.updateWriteIndex();
    }
}

/// Consume and verify a sequence of expected values from the queue.
void consumeAndExpect(LFQueue<int>& queue, std::initializer_list<int> expected) {
    for (int exp : expected) {
        const int* val = queue.getNextToRead();
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, exp);
        queue.updateReadIndex();
    }
}

}  // namespace

// ── Trivial payload for tests ────────────────────────────────────
struct Payload {
    int value = 0;
};

// ── Construction ─────────────────────────────────────────────────
TEST(LFQueueTest, ConstructDoesNotThrow) {
    EXPECT_NO_THROW(LFQueue<int>(16));
}

TEST(LFQueueTest, ConstructWithZeroSizeIsAllowed) {
    // The queue itself doesn't validate size > 0 — modulo by 0 would
    // be UB at runtime, but construction alone must not crash.
    EXPECT_NO_THROW(LFQueue<int>(0));
}

// ── Single-element produce / consume ─────────────────────────────
TEST(LFQueueTest, ProduceConsumeSingle) {
    LFQueue<int> queue(4);
    *queue.getNextToWriteTo() = 42;
    queue.updateWriteIndex();

    EXPECT_EQ(queue.size(), 1);

    const int* val = queue.getNextToRead();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 42);

    queue.updateReadIndex();
    EXPECT_EQ(queue.size(), 0);
}

// ── Empty queue behaviour ────────────────────────────────────────
TEST(LFQueueTest, ReadFromEmptyReturnsNull) {
    LFQueue<int> queue(4);
    EXPECT_EQ(queue.getNextToRead(), nullptr);
    EXPECT_EQ(queue.size(), 0);
}

TEST(LFQueueTest, ReadAfterDrainReturnsNull) {
    LFQueue<int> queue(4);
    *queue.getNextToWriteTo() = 1;
    queue.updateWriteIndex();

    const int* val = queue.getNextToRead();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 1);
    queue.updateReadIndex();

    EXPECT_EQ(queue.getNextToRead(), nullptr);
    EXPECT_EQ(queue.size(), 0);
}

// ── Multiple elements (FIFO order) ───────────────────────────────
TEST(LFQueueTest, FifoOrderMultipleElements) {
    LFQueue<int> queue(8);
    for (int i = 0; i < 5; ++i) {
        *queue.getNextToWriteTo() = i * 10;
        queue.updateWriteIndex();
    }
    EXPECT_EQ(queue.size(), 5);

    for (int i = 0; i < 5; ++i) {
        const int* val = queue.getNextToRead();
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, i * 10);
        queue.updateReadIndex();
    }
    EXPECT_EQ(queue.size(), 0);
}

// ── Wrap-around ──────────────────────────────────────────────────
TEST(LFQueueTest, WrapAroundProducerIndex) {
    // Effective capacity is physical size - 1 (indices coincide when
    // empty, so we can't distinguish empty from full).
    LFQueue<int> queue(5);

    // Fill 3 slots (indices: write=3, read=0)
    for (int i = 0; i < 3; ++i) {
        *queue.getNextToWriteTo() = i;
        queue.updateWriteIndex();
    }

    // Drain two elements (indices: write=3, read=2)
    for (int i = 0; i < 2; ++i) {
        const int* val = queue.getNextToRead();
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, i);
        queue.updateReadIndex();
    }

    // Produce 3 more — write index wraps around past slot 4 to slots 0,1,2
    *queue.getNextToWriteTo() = 100;
    queue.updateWriteIndex();
    *queue.getNextToWriteTo() = 200;
    queue.updateWriteIndex();
    *queue.getNextToWriteTo() = 300;
    queue.updateWriteIndex();

    EXPECT_EQ(queue.size(), 4);

    // Drain remaining: should be 2, 100, 200, 300
    consumeAndExpect(queue, {2, 100, 200, 300});
    EXPECT_EQ(queue.size(), 0);
}

TEST(LFQueueTest, WrapAroundFullCycle) {
    // Physical size 4, effective capacity 3 (N-1).
    LFQueue<int> queue(4);

    // Fill, drain, fill, drain — forces multiple wrap-arounds
    for (int cycle = 0; cycle < 5; ++cycle) {
        produceValues(queue, {cycle * 10, cycle * 10 + 1, cycle * 10 + 2});
        EXPECT_EQ(queue.size(), 3);
        consumeAndExpect(queue, {cycle * 10, cycle * 10 + 1, cycle * 10 + 2});
        EXPECT_EQ(queue.size(), 0);
    }
}

// ── Non-trivial type ─────────────────────────────────────────────
TEST(LFQueueTest, WorksWithStructPayload) {
    LFQueue<Payload> queue(2);

    *queue.getNextToWriteTo() = Payload{10};
    queue.updateWriteIndex();

    const Payload* ptr = queue.getNextToRead();
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->value, 10);
    queue.updateReadIndex();

    EXPECT_EQ(queue.getNextToRead(), nullptr);
}

// ── SPSC thread test ─────────────────────────────────────────────
TEST(LFQueueTest, SingleProducerSingleConsumerThreaded) {
    constexpr int kNumItems = 10'000;
    LFQueue<int> queue(kNumItems);

    std::atomic<bool> producerDone{false};

    std::thread producer([&]() {
        for (int i = 0; i < kNumItems; ++i) {
            *queue.getNextToWriteTo() = i;
            queue.updateWriteIndex();
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        int consumed = 0;
        while (consumed < kNumItems) {
            const int* val = queue.getNextToRead();
            if (val == nullptr) {
                // Busy-wait — acceptable for a test harness
                continue;
            }
            EXPECT_EQ(*val, consumed);
            queue.updateReadIndex();
            ++consumed;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(queue.size(), 0);
    EXPECT_TRUE(producerDone.load());
}
