#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
#include <string_view>

namespace utils {

/// Stores a short (<= 8 byte) string inline, avoiding heap allocation.
///
/// Used as a drop-in replacement for `std::string` in `LogElement` when
/// the payload is known to be short (e.g. order IDs, symbols, status codes).
/// The stored string is always null-terminated.
class ShortString {
public:
    constexpr ShortString() noexcept : val{.integer = 0} {}

    constexpr ShortString(const ShortString &rhs) noexcept = default;
    constexpr ShortString(ShortString &&rhs) noexcept = default;

    constexpr ShortString &operator=(const ShortString &rhs) noexcept = default;
    constexpr ShortString &operator=(ShortString &&rhs) noexcept = default;

    [[nodiscard]] static constexpr ShortString shorten(std::string_view rhs) {
        ShortString result;
        auto len = rhs.size() < 8 ? rhs.size() : 8;
        __builtin_memcpy(result.val.chars.data(), rhs.data(), len);
        return result;
    }

    [[nodiscard]] static constexpr ShortString shorten(const std::string &rhs) {
        ShortString result;
        auto len = rhs.size() < 8 ? rhs.size() : 8;
        __builtin_memcpy(result.val.chars.data(), rhs.data(), len);
        return result;
    }

    /// Copies up to 8 bytes, stopping at the first null terminator.
    /// The byte loop compiles to unrolled mov instructions at -O2.
    [[nodiscard]] static constexpr ShortString shorten(const char *rhs) {
        ShortString result;
        for (size_t i = 0; i < 8 && rhs[i] != '\0'; ++i)
            result.val.chars[i] = rhs[i];
        return result;
    }

    /// Returns a pointer to the stored null-terminated data.
    [[nodiscard]] constexpr const char *data() const noexcept { return val.chars.data(); }

    /// Returns the length of the stored string (0–8).
    [[nodiscard]] constexpr size_t size() const noexcept {
        // val[7] may be non-null when the string is exactly 8 chars.
        for (size_t i = 0; i < 8; ++i)
            if (val.chars[i] == '\0')
                return i;
        return 8;
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return val.chars[0] == '\0'; }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return std::string_view(val.chars.data(), size());
    }

    [[nodiscard]] friend constexpr bool operator==(const ShortString &lhs,
                                                   const ShortString &rhs) noexcept {
        return lhs.val.integer == rhs.val.integer;
    }

    [[nodiscard]] friend constexpr bool operator!=(const ShortString &lhs,
                                                   const ShortString &rhs) noexcept {
        return lhs.val.integer != rhs.val.integer;
    }

private:
    union {
        std::array<char, 8> chars;
        std::uint64_t integer;
    } val;
};

inline std::ostream &operator<<(std::ostream &os, const ShortString &str) {
    return os << str.view();
}

}  // namespace utils