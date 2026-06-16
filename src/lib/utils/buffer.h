#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace utils {

/**
 * @brief Fixed-capacity read/write buffer with front-pop semantics.
 *
 * Buffer stores bytes in a contiguous array. Writers append at the end
 * via writeBuf()/extend(), readers consume from the front via data()/consume().
 * On consume(), remaining data is memmove'd to the front so that data() always
 * points to the beginning of the array — no wrap-around arithmetic needed.
 *
 * @tparam SIZE Total capacity in bytes (compile-time constant, no heap).
 *
 * @warning Not thread-safe. Caller must serialize access.
 * @warning extend() does not bounds-check — caller must ensure writeSize() >= n.
 */
template <std::size_t SIZE>
class Buffer {
public:
    /// Number of unread bytes currently held.
    [[nodiscard]] std::size_t size() const noexcept { return read_buffer_pos_; }

    /// Number of bytes available for writing (capacity - size).
    [[nodiscard]] std::size_t writeSize() const noexcept { return SIZE - read_buffer_pos_; }

    /// Pointer to the start of readable data. Valid for [0, size()).
    [[nodiscard]] const char* data() const noexcept { return read_buffer_.data(); }

    /// Pointer to the first writable byte. Caller may write up to writeSize() bytes,
    /// then call extend(n) to commit.
    [[nodiscard]] char* writeBuf() noexcept { return read_buffer_.data() + read_buffer_pos_; }

    /**
     * @brief Discard @p consumed bytes from the front.
     *
     * Remaining bytes are shifted left so data() stays at offset 0.
     *
     * @pre consumed <= size()
     * @param consumed number of bytes to remove
     */
    void consume(std::size_t consumed) noexcept {
        assert(consumed <= read_buffer_pos_);

        std::memmove(read_buffer_.data(), read_buffer_.data() + consumed,
                     read_buffer_pos_ - consumed);
        read_buffer_pos_ -= consumed;
    }

    /**
     * @brief Mark @p added bytes as written.
     *
     * Call after writing to writeBuf().
     *
     * @pre added <= writeSize()
     * @param added number of bytes committed
     */
    void extend(std::size_t added) noexcept {
        assert(added <= writeSize());
        read_buffer_pos_ += added;
    }

    /// View of currently held data as a string_view.
    [[nodiscard]] std::string_view view() const noexcept {
        return std::string_view(read_buffer_.data(), read_buffer_pos_);
    }

    /// Discard all buffered data (size() becomes 0).
    void clear() noexcept { read_buffer_pos_ = 0; }

    /// Total capacity of the buffer.
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return SIZE; }

private:
    std::array<char, SIZE> read_buffer_{};
    std::size_t read_buffer_pos_{0};
};

}  // namespace utils