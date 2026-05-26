#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <vector>

namespace utils {

/// Lock-free, fixed-size, single-producer single-consumer (SPSC) queue.
///
/// All backing storage is pre-allocated once in the constructor via
/// std::vector — no heap allocations occur in the hot path.
///
/// The producer calls getNextToWriteTo() to obtain a pointer to the next
/// free slot, writes the payload, then calls updateWriteIndex() to publish
/// it.  The consumer calls getNextToRead() which returns nullptr when the
/// queue is empty, otherwise a pointer to the oldest unread element; after
/// consuming it, updateReadIndex() advances the read cursor.
///
/// @warning  This queue is **SPSC only**.  Using more than one producer or
///           more than one consumer concurrently is undefined behaviour.
///           The index updates are not atomic read-modify-write operations,
///           so concurrent writers (or concurrent readers) will corrupt
///           the cursors.
/// @warning  The queue cannot distinguish between "empty" and "full" using
///           only the read/write indices, so the effective capacity is
///           `num_elems - 1`.  Filling the queue to its physical capacity
///           causes the indices to coincide, making the queue appear empty.
/// @warning  getNextToWriteTo() returns a pointer into internal storage.
///           The pointer remains valid until the next call to
///           updateWriteIndex() from the same producer wraps around and
///           overwrites the same slot.  Do not hold on to the pointer
///           across a production cycle.
///
/// @tparam T  The element type stored in the queue.  Must be
///            default-constructible (required by the std::vector backing
///            store).
template <typename T>
class LFQueue final {
public:
    /// Pre-allocates storage for @p num_elems default-constructed elements.
    ///
    /// @param num_elems  Fixed capacity of the queue.  Must be > 0.
    explicit LFQueue(std::size_t num_elems) : store(num_elems, T()) {}

    // -- non-copyable, non-movable ----------------------------------
    ~LFQueue() = default;
    LFQueue() = delete;
    LFQueue(const LFQueue&) = delete;
    LFQueue(const LFQueue&&) = delete;
    LFQueue& operator=(const LFQueue&) = delete;
    LFQueue& operator=(const LFQueue&&) = delete;

    /// Returns a pointer to the next writable slot.
    ///
    /// The caller must write a valid T into the pointed-to location before
    /// calling updateWriteIndex().  The pointer is only valid until the
    /// next call to updateWriteIndex() (or until the next wrap-around).
    ///
    /// @return  Pointer to the next free slot.  Never nullptr.
    T* getNextToWriteTo() noexcept { return &store[next_write_index]; }

    /// Publishes the element written into the slot returned by the most
    /// recent getNextToWriteTo() call and advances the write cursor.
    ///
    /// Must only be called by the single producer thread.
    void updateWriteIndex() noexcept {
        next_write_index = (next_write_index + 1) % store.size();
        num_elements++;
    }

    /// Returns a pointer to the oldest unread element, or nullptr if the
    /// queue is empty.
    ///
    /// The pointer remains valid until the next call to updateReadIndex().
    ///
    /// @return  Pointer to the next readable element, or nullptr if empty.
    [[nodiscard]] const T* getNextToRead() const noexcept {
        return (next_read_index == next_write_index) ? nullptr : &store[next_read_index];
    }

    /// Advances the read cursor past the element returned by the most
    /// recent getNextToRead() call.
    ///
    /// Must only be called by the single consumer thread.
    /// @pre  The queue must not be empty (enforced by assert).
    void updateReadIndex() noexcept {
        next_read_index = (next_read_index + 1) % store.size();
        assert(num_elements != 0);
        num_elements--;
    }

    /// Returns the number of elements currently in the queue.
    ///
    /// In the SPSC case this is an accurate snapshot; with multiple
    /// threads the result may be stale by the time it is observed.
    [[nodiscard]] size_t size() const noexcept { return num_elements.load(); }

private:
    std::vector<T> store;
    std::atomic<size_t> next_write_index = {0};
    std::atomic<size_t> next_read_index = {0};
    std::atomic<size_t> num_elements = {0};
};

}  // namespace utils