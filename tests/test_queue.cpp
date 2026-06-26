#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <initializer_list>
#include <thread>
#include <vector>

#include "lib/utils/queue.h"

using utils::MPSCQueue;
using utils::SPSCQueue;

// ===================================================================
//  SPSCQueue tests  (single-producer single-consumer)
// ===================================================================

namespace {

void produceValues(SPSCQueue<int>& queue, std::initializer_list<int> values) {
    for (int val : values) {
        *queue.getNextToWriteTo() = val;
        queue.updateWriteIndex();
    }
}

void consumeAndExpect(SPSCQueue<int>& queue, std::initializer_list<int> expected) {
    for (int exp : expected) {
        const int* val = queue.getNextToRead();
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, exp);
        queue.updateReadIndex();
    }
}

}  // namespace

struct Payload {
    int value = 0;
};

// ── Construction ─────────────────────────────────────────────────
TEST(SPSCQueueTest, ConstructDoesNotThrow) {
    EXPECT_NO_THROW(SPSCQueue<int>(16));
}

TEST(SPSCQueueTest, ConstructWithZeroSizeIsAllowed) {
    EXPECT_NO_THROW(SPSCQueue<int>(0));
}

// ── Single-element produce / consume ─────────────────────────────
TEST(SPSCQueueTest, ProduceConsumeSingle) {
    SPSCQueue<int> queue(4);
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
TEST(SPSCQueueTest, ReadFromEmptyReturnsNull) {
    SPSCQueue<int> queue(4);
    EXPECT_EQ(queue.getNextToRead(), nullptr);
    EXPECT_EQ(queue.size(), 0);
}

TEST(SPSCQueueTest, ReadAfterDrainReturnsNull) {
    SPSCQueue<int> queue(4);
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
TEST(SPSCQueueTest, FifoOrderMultipleElements) {
    SPSCQueue<int> queue(8);
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
TEST(SPSCQueueTest, WrapAroundProducerIndex) {
    SPSCQueue<int> queue(5);

    for (int i = 0; i < 3; ++i) {
        *queue.getNextToWriteTo() = i;
        queue.updateWriteIndex();
    }

    for (int i = 0; i < 2; ++i) {
        const int* val = queue.getNextToRead();
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, i);
        queue.updateReadIndex();
    }

    *queue.getNextToWriteTo() = 100;
    queue.updateWriteIndex();
    *queue.getNextToWriteTo() = 200;
    queue.updateWriteIndex();
    *queue.getNextToWriteTo() = 300;
    queue.updateWriteIndex();

    EXPECT_EQ(queue.size(), 4);

    consumeAndExpect(queue, {2, 100, 200, 300});
    EXPECT_EQ(queue.size(), 0);
}

TEST(SPSCQueueTest, WrapAroundFullCycle) {
    SPSCQueue<int> queue(4);

    for (int cycle = 0; cycle < 5; ++cycle) {
        produceValues(queue, {cycle * 10, cycle * 10 + 1, cycle * 10 + 2});
        EXPECT_EQ(queue.size(), 3);
        consumeAndExpect(queue, {cycle * 10, cycle * 10 + 1, cycle * 10 + 2});
        EXPECT_EQ(queue.size(), 0);
    }
}

// ── Non-trivial type ─────────────────────────────────────────────
TEST(SPSCQueueTest, WorksWithStructPayload) {
    SPSCQueue<Payload> queue(2);

    *queue.getNextToWriteTo() = Payload{10};
    queue.updateWriteIndex();

    const Payload* ptr = queue.getNextToRead();
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->value, 10);
    queue.updateReadIndex();

    EXPECT_EQ(queue.getNextToRead(), nullptr);
}

// ── SPSC thread test ─────────────────────────────────────────────
TEST(SPSCQueueTest, SingleProducerSingleConsumerThreaded) {
    constexpr int kNumItems = 10'000;
    // +1 for the sentinel slot used to disambiguate empty vs full
    SPSCQueue<int> queue(kNumItems + 1);

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
                std::this_thread::yield();  // let the producer run
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

// ===================================================================
//  MPSCQueue tests  (multi-producer single-consumer)
// ===================================================================

TEST(MPSCQueueTest, ConstructDoesNotThrow) {
    EXPECT_NO_THROW(MPSCQueue<int>(16));
}

TEST(MPSCQueueTest, Capacity) {
    MPSCQueue<int> q(8);
    EXPECT_EQ(q.capacity(), 8);
}

TEST(MPSCQueueTest, ReserveCommitSingle) {
    MPSCQueue<int> q(8);
    const auto start = q.reserve(1);
    ASSERT_NE(start, static_cast<size_t>(-1));
    *q.slot(start) = 42;
    q.commit(start, 1);

    EXPECT_EQ(q.size(), 1);

    const int* val = q.getNextToRead();
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, 42);
    q.updateReadIndex();
    EXPECT_EQ(q.size(), 0);
}

TEST(MPSCQueueTest, ReserveCommitBatch) {
    MPSCQueue<int> q(8);
    constexpr size_t n = 3;
    const auto start = q.reserve(n);
    ASSERT_NE(start, static_cast<size_t>(-1));

    const auto cap = q.capacity();
    for (size_t i = 0; i < n; ++i)
        *q.slot((start + i) % cap) = static_cast<int>(i * 10);
    q.commit(start, n);

    EXPECT_EQ(q.size(), n);

    for (size_t i = 0; i < n; ++i) {
        const int* val = q.getNextToRead();
        ASSERT_NE(val, nullptr);
        EXPECT_EQ(*val, static_cast<int>(i * 10));
        q.updateReadIndex();
    }
    EXPECT_EQ(q.size(), 0);
}

TEST(MPSCQueueTest, ReserveFullReturnsSentinel) {
    MPSCQueue<int> q(4);  // effective capacity = 3

    for (int i = 0; i < 3; ++i) {
        const auto s = q.reserve(1);
        ASSERT_NE(s, static_cast<size_t>(-1));
        *q.slot(s) = i;
        q.commit(s, 1);
    }
    EXPECT_EQ(q.size(), 3);

    EXPECT_EQ(q.reserve(1), static_cast<size_t>(-1));
}

TEST(MPSCQueueTest, ReadFromEmptyReturnsNull) {
    MPSCQueue<int> q(4);
    EXPECT_EQ(q.getNextToRead(), nullptr);
    EXPECT_EQ(q.size(), 0);
}

TEST(MPSCQueueTest, BatchWrapAround) {
    MPSCQueue<int> q(5);  // effective capacity = 4

    // Write 3
    {
        const auto s = q.reserve(3);
        ASSERT_NE(s, static_cast<size_t>(-1));
        const auto cap = q.capacity();
        *q.slot((s + 0) % cap) = 10;
        *q.slot((s + 1) % cap) = 20;
        *q.slot((s + 2) % cap) = 30;
        q.commit(s, 3);
    }

    // Consume 2
    for (int expected : {10, 20}) {
        const int* v = q.getNextToRead();
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(*v, expected);
        q.updateReadIndex();
    }

    // Write 3 more — wraps around
    {
        const auto s = q.reserve(3);
        ASSERT_NE(s, static_cast<size_t>(-1));
        const auto cap = q.capacity();
        *q.slot((s + 0) % cap) = 100;
        *q.slot((s + 1) % cap) = 200;
        *q.slot((s + 2) % cap) = 300;
        q.commit(s, 3);
    }

    EXPECT_EQ(q.size(), 4);

    for (int expected : {30, 100, 200, 300}) {
        const int* v = q.getNextToRead();
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(*v, expected);
        q.updateReadIndex();
    }
    EXPECT_EQ(q.size(), 0);
}

TEST(MPSCQueueTest, MultiProducerSingleElement) {
    constexpr int kNumProducers = 4;
    constexpr int kItemsPerProducer = 250;
    constexpr int kTotalItems = kNumProducers * kItemsPerProducer;

    MPSCQueue<int> q(kTotalItems * 2);

    std::atomic<int> produced{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < kNumProducers; ++p) {
        producers.emplace_back([&, p]() {
            for (int i = 0; i < kItemsPerProducer; ++i) {
                const auto start = q.reserve(1);
                if (start == static_cast<size_t>(-1)) {
                    --i;  // retry
                    continue;
                }
                *q.slot(start) = p * 1000 + i;
                q.commit(start, 1);
                produced.fetch_add(1, std::memory_order_release);
            }
        });
    }

    std::atomic<int> consumed{0};

    std::thread consumer([&]() {
        int last = 0;
        while (last < kTotalItems) {
            const int* val = q.getNextToRead();
            if (val == nullptr)
                continue;
            EXPECT_GE(*val, 0);
            EXPECT_LT(*val, kNumProducers * 1000);
            q.updateReadIndex();
            ++last;
        }
        consumed.store(last, std::memory_order_release);
    });

    for (auto& t : producers)
        t.join();
    consumer.join();

    EXPECT_EQ(produced.load(), kTotalItems);
    EXPECT_EQ(consumed.load(), kTotalItems);
    EXPECT_EQ(q.size(), 0);
}

TEST(MPSCQueueTest, BatchMultiProducerNoInterleaving) {
    // Each producer writes [pid, seq, pid] as a batch.
    // The consumer verifies that batches are never interleaved.
    constexpr int kNumProducers = 4;
    constexpr int kMessagesPerProducer = 200;
    constexpr size_t kBatchSize = 3;

    constexpr size_t kTotalElements = kNumProducers * kMessagesPerProducer * kBatchSize;
    MPSCQueue<int> q(kTotalElements * 2);

    std::vector<std::thread> producers;
    for (int p = 0; p < kNumProducers; ++p) {
        producers.emplace_back([&, p]() {
            for (int seq = 0; seq < kMessagesPerProducer; ++seq) {
                const auto start = q.reserve(kBatchSize);
                if (start == static_cast<size_t>(-1)) {
                    --seq;
                    continue;
                }
                const auto cap = q.capacity();
                *q.slot((start + 0) % cap) = p;
                *q.slot((start + 1) % cap) = seq;
                *q.slot((start + 2) % cap) = p;
                q.commit(start, kBatchSize);
            }
        });
    }

    std::atomic<int> messagesRead{0};
    constexpr int kTotalMessages = kNumProducers * kMessagesPerProducer;

    std::thread consumer([&]() {
        while (messagesRead.load(std::memory_order_acquire) < kTotalMessages) {
            const int* v0 = q.getNextToRead();
            if (v0 == nullptr)
                continue;

            const int pid1 = *v0;
            q.updateReadIndex();

            const int* v1 = q.getNextToRead();
            if (v1 == nullptr)
                continue;  // batch not fully committed yet
            const int seq = *v1;
            q.updateReadIndex();

            const int* v2 = q.getNextToRead();
            ASSERT_NE(v2, nullptr) << "batch should be fully committed";
            const int pid2 = *v2;
            q.updateReadIndex();

            EXPECT_EQ(pid1, pid2) << "interleaving detected! pid mismatch";
            EXPECT_GE(pid1, 0);
            EXPECT_LT(pid1, kNumProducers);
            EXPECT_GE(seq, 0);
            EXPECT_LT(seq, kMessagesPerProducer);

            messagesRead.fetch_add(1, std::memory_order_release);
        }
    });

    for (auto& t : producers)
        t.join();
    consumer.join();

    EXPECT_EQ(messagesRead.load(), kTotalMessages);
    EXPECT_EQ(q.size(), 0);
}
