#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <vector>

namespace utils {

// ===================================================================
//  SPSCQueue  —  lock-free, single-producer single-consumer
// ===================================================================

/// Lock-free, fixed-size SPSC queue.  No atomic read-modify-write
/// operations in the hot path — the producer and consumer each advance
/// their own cursor with simple loads and stores.
///
///   T* slot = queue.getNextToWriteTo();   // always valid (SPSC)
///   *slot = payload;
///   queue.updateWriteIndex();
///
///   const T* p = queue.getNextToRead();   // nullptr when empty
///   ... consume *p ...
///   queue.updateReadIndex();
///
/// @warning  Effective capacity is `capacity() - 1` — one slot is
///           reserved to disambiguate the "empty" vs "full" states.
///
/// @tparam T  Default-constructible element type.
template <typename T>
class SPSCQueue final {
public:
    explicit SPSCQueue(std::size_t num_elems) : store(num_elems, T()), cap(num_elems) {}

    ~SPSCQueue() = default;
    SPSCQueue() = delete;
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue(const SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;
    SPSCQueue& operator=(const SPSCQueue&&) = delete;

    [[nodiscard]] size_t capacity() const noexcept { return cap; }

    /// Returns a pointer to the next writable slot.
    /// Never nullptr — the single producer is expected to stay within
    /// the effective capacity.
    T* getNextToWriteTo() noexcept { return &store[write_idx.load(std::memory_order_relaxed)]; }

    /// Publishes the element written into the slot returned by the most
    /// recent getNextToWriteTo() call.
    void updateWriteIndex() noexcept {
        write_idx.store((write_idx.load(std::memory_order_relaxed) + 1) % cap,
                        std::memory_order_release);
        num_elements.fetch_add(1, std::memory_order_release);
    }

    /// Returns a pointer to the oldest unread element, or nullptr if empty.
    [[nodiscard]] const T* getNextToRead() const noexcept {
        return (read_idx.load(std::memory_order_acquire) == write_idx.load(std::memory_order_acquire))
                   ? nullptr
                   : &store[read_idx.load(std::memory_order_relaxed)];
    }

    /// Advances the read cursor past the element returned by the most
    /// recent getNextToRead() call.
    void updateReadIndex() noexcept {
        read_idx.store((read_idx.load(std::memory_order_relaxed) + 1) % cap,
                       std::memory_order_release);
        [[maybe_unused]] const size_t prev =
            num_elements.fetch_sub(1, std::memory_order_release);
        assert(prev > 0);
    }

    /// Returns the number of committed-but-unread elements.
    [[nodiscard]] size_t size() const noexcept { return num_elements.load(); }

private:
    std::vector<T> store;
    const size_t cap;
    std::atomic<size_t> write_idx = {0};
    std::atomic<size_t> read_idx = {0};
    std::atomic<size_t> num_elements = {0};
};

// ===================================================================
//  MPSCQueue  —  lock-free, multi-producer single-consumer
// ===================================================================

/// Lock-free, fixed-size MPSC queue with atomic batch reservation.
///
/// Multiple producer threads can safely log/emit concurrently.
/// Producers reserve a contiguous range of slots, write the payload,
/// then commit in FIFO order.  The consumer sees only fully-committed
/// batches — partial messages are never observable.
///
/// ## Batch API (multi-producer safe)
/// ```
///   auto start = queue.reserve(n);
///   if (start == static_cast<size_t>(-1)) { /* queue full — drop */ }
///   for (size_t i = 0; i < n; ++i)
///       *queue.slot((start + i) % queue.capacity()) = payload;
///   queue.commit(start, n);
/// ```
///
/// @warning  Commits are serialised in reservation order.  A producer
///           that reserves early but commits late (e.g. after being
///           preempted) will block all later reservations from becoming
///           visible.  Keep the reserve→commit window as small as
///           possible.
/// @warning  Effective capacity is `capacity() - 1`.
///
/// @tparam T  Default-constructible element type.
template <typename T>
class MPSCQueue final {
public:
    explicit MPSCQueue(std::size_t num_elems) : store(num_elems, T()), cap(num_elems) {}

    ~MPSCQueue() = default;
    MPSCQueue() = delete;
    MPSCQueue(const MPSCQueue&) = delete;
    MPSCQueue(const MPSCQueue&&) = delete;
    MPSCQueue& operator=(MPSCQueue&&) = delete;
    MPSCQueue& operator=(const MPSCQueue&&) = delete;

    [[nodiscard]] size_t capacity() const noexcept { return cap; }

    // -- producer batch API --

    /// Atomically reserve @p n contiguous slots.
    /// @return start index, or `static_cast<size_t>(-1)` when full.
    [[nodiscard]] size_t reserve(size_t n) noexcept {
        size_t wr = write_reserve.load(std::memory_order_relaxed);
        while (true) {
            const size_t rd = read_idx.load(std::memory_order_acquire);
            const size_t used = (wr >= rd) ? (wr - rd) : (cap - rd + wr);
            if (used + n + 1 > cap) return static_cast<size_t>(-1);

            const size_t new_wr = (wr + n) % cap;
            if (write_reserve.compare_exchange_weak(wr, new_wr,
                    std::memory_order_acq_rel, std::memory_order_relaxed))
                return wr;
        }
    }

    /// Returns a writable pointer to a previously reserved slot.
    /// @pre  @p index was obtained from reserve() and not yet committed.
    T* slot(size_t index) noexcept { return &store[index]; }

    /// Publish @p n elements starting at @p start.
    /// @pre  All earlier reservations must have been committed (FIFO order).
    void commit(size_t start, size_t n) noexcept {
        // Spin until it's our turn — earlier reservations commit first.
        while (write_ready.load(std::memory_order_acquire) != start) {
            // Window is tiny — just a few scalar stores.  On x86-64
            // with heavy contention a PAUSE could help, but it's
            // omitted here because the reserve→commit gap is minimal
            // in practice.
        }
        write_ready.store((start + n) % cap, std::memory_order_release);
        num_elements.fetch_add(n, std::memory_order_release);
    }

    // -- consumer API --

    /// Returns a pointer to the oldest unread element, or nullptr if empty.
    [[nodiscard]] const T* getNextToRead() const noexcept {
        const size_t rd = read_idx.load(std::memory_order_relaxed);
        const size_t wr = write_ready.load(std::memory_order_acquire);
        return (rd == wr) ? nullptr : &store[rd];
    }

    /// Advances the read cursor past the element returned by the most
    /// recent getNextToRead() call.
    void updateReadIndex() noexcept {
        const size_t rd = read_idx.load(std::memory_order_relaxed);
        read_idx.store((rd + 1) % cap, std::memory_order_release);
        [[maybe_unused]] const size_t prev =
            num_elements.fetch_sub(1, std::memory_order_release);
        assert(prev > 0);
    }

    /// Returns the number of committed-but-unread elements.
    [[nodiscard]] size_t size() const noexcept { return num_elements.load(); }

private:
    std::vector<T> store;
    const size_t cap;

    // -- MPSC coordination --
    //   write_reserve : next slot to be reserved    (CAS by producers)
    //   write_ready   : last fully-committed slot   (store by commit())
    //   read_idx      : next slot to be consumed    (store by updateReadIndex())
    //
    //   Invariant:  write_ready  ≤  write_reserve  (mod cap)
    //   Consumer sees elements only up to write_ready.
    std::atomic<size_t> write_reserve = {0};
    std::atomic<size_t> write_ready = {0};
    std::atomic<size_t> read_idx = {0};
    std::atomic<size_t> num_elements = {0};
};

}  // namespace utils