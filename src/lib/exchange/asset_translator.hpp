#pragma once

/// @file asset_translator.hpp
/// @brief Maps FIX symbol names to internal TickerId values.
///
/// Uses a bidirectional mapping built from the engine configuration at
/// startup. Lookups are O(n) in the number of tickers (n ≤ MAX_TICKERS = 8),
/// which is trivially fast and cache-friendly without hashing overhead.

#include <algorithm>
#include <array>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>

#include "lib/exchange/constants.h"
#include "lib/exchange/definitions.h"

namespace exchange {

/// Bidirectional mapping between FIX symbol strings and internal TickerId.
///
/// Thread-safe for concurrent reads after construction.  No mutations
/// occur after the constructor completes.
class AssetTranslator final {
public:
    AssetTranslator() = default;

    /// Builds the mapping from a list of ticker symbols.
    ///
    /// The index of each symbol in @p tickers becomes its TickerId.
    /// @pre tickers.size() <= MAX_TICKERS
    explicit AssetTranslator(const std::vector<std::string>& tickers) {
        if (tickers.size() > MAX_TICKERS) {
            throw std::invalid_argument(
                "Number of configured tickers is greater than the allowed capacity");
        }
        for (size_t i = 0; i < tickers.size(); ++i) {
            entries_[i] = tickers[i];
        }
        count_ = tickers.size();
    }

    /// Looks up a TickerId by FIX symbol name (case-sensitive).
    ///
    /// @return TickerId on success, std::nullopt if the symbol is unknown.
    [[nodiscard]] TickerId resolve(std::string_view symbol) const noexcept {
        for (size_t i = 0; i < count_; ++i) {
            if (entries_[i] == symbol) {
                return TickerId(i);
            }
        }
        return TickerId::INVALID;
    }

    /// Looks up a FIX symbol name by TickerId.
    ///
    /// @return symbol string_view, or std::nullopt if the ID is out of range.
    [[nodiscard]] std::optional<std::string_view> lookup(TickerId id) const noexcept {
        const auto raw = type_safe::get(id);
        if (raw >= count_) {
            return std::nullopt;
        }
        return entries_[raw];
    }

    /// Returns the number of registered tickers.
    [[nodiscard]] size_t size() const noexcept { return count_; }

private:
    // TODO: replace std::string with ShortString in order to improve performance
    std::array<std::string, MAX_TICKERS> entries_;
    size_t count_ = 0;
};

}  // namespace exchange
