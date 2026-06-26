/// @file fix_helpers.hpp
/// @brief FIX message parsing helpers for converting wire-format messages into
///        internal Request objects.
///
/// All functions in this header are inline, noexcept, and allocation-free in
/// the success path — designed for the hot path of the FIX session dispatcher.

#pragma once

#include <expected>
#include <string>

// Suppress deprecated warnings from fixpp (std::aligned_storage in C++23)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <fixpp/tag.h>
#include <fixpp/versions/v42.h>
#pragma GCC diagnostic pop

#include "lib/exchange/request.h"
#include "tickers_gperf.hpp"

namespace exchange::fix::details {

/// Converts a FIX Side tag value (tag 54) to the internal Side enum.
///
/// FIX 4.2 defines Side as a single character:
///   - '1' = Buy
///   - '2' = Sell
///   - '3' = Buy minus, '4' = Sell plus, '5' = Sell short, etc.
///
/// Only Buy and Sell are currently supported; any other value yields
/// Side::INVALID, which causes the caller to reject the message.
///
/// @param c  The raw character from FIX tag 54.
/// @return Side::BUY, Side::SELL, or Side::INVALID.
[[nodiscard]] constexpr Side toInternalSide(char c) noexcept {
    switch (c) {
        case '1':
            return Side::BUY;
        case '2':
            return Side::SELL;
        default:
            return Side::INVALID;
    }
}

/// Converts a FIX OrdType tag value (tag 40) to the internal RequestType enum.
///
/// FIX 4.2 defines OrdType as a single character:
///   - '1' = Market
///   - '2' = Limit
///
/// Both Market ('1') and Limit ('2') are currently mapped to
/// RequestType::NEW.  Unsupported values (Stop, Stop-Limit, etc.) yield
/// RequestType::INVALID.
///
/// @param c  The raw character from FIX tag 40.
/// @return RequestType::NEW or RequestType::INVALID.
[[nodiscard]] constexpr RequestType toRequestType(char c) noexcept {
    switch (c) {
        case '1':
            return RequestType::NEW;
        case '2':
            return RequestType::NEW;
        default:
            return RequestType::INVALID;
    }
}

/// Parses and validates a FIX NewOrderSingle (MsgType=D) message.
///
/// Extracts and validates every required tag, converting FIX wire values into
/// the internal Request representation.  Validation order is:
///   1. Symbol (55)  — must be present and known to the AssetTranslator
///   2. Side (54)    — must be present and either '1' (Buy) or '2' (Sell)
///   3. OrdType (40) — must be present and either '1' (Market) or '2' (Limit)
///   4. Price (44)   — optional; if absent, Price::INVALID is assigned
///   5. OrderQty (38)— must be present
///
/// @param order       The parsed FIX NewOrderSingle message reference (read-only view).
/// @param user_id     The pre-resolved user identifier.  The caller derives this
///                    from ClOrdID via a hash function — see onNewOrderSingle().
/// @param translator  The asset translator for resolving Symbol → TickerId.
///                    Must be fully populated before any calls.
/// @return A valid Request on success, or a `const char*` error description on failure.
/// @note  No heap allocations in the success path.  The error string literals
///        live in .rodata and are safe to log or send to clients.
/// @pre   translator must be fully initialized with all known ticker symbols.
[[nodiscard]] inline std::expected<Request, const char*> parseNewOrderSingle(
    const Fixpp::v42::Message::NewOrderSingle::Ref& order, UserId user_id) noexcept {
    // --- Validate Symbol ---
    std::string symbol;
    if (!Fixpp::tryGet<Fixpp::Tag::Symbol>(order, symbol)) {
        return std::unexpected<const char*>("Required tag missing: Symbol (55)");
    }
    const auto* ticker_entry = TickersHash::in_word_set(symbol.c_str(), symbol.size());
    const TickerId ticker = ticker_entry ? TickerId{ticker_entry->id} : TickerId::INVALID;
    if (ticker == TickerId::INVALID) {
        return std::unexpected<const char*>("Unknown symbol");
    }

    // --- Validate Side ---
    char side_char = 0;
    if (!Fixpp::tryGet<Fixpp::Tag::Side>(order, side_char)) {
        return std::unexpected<const char*>("Required tag missing: Side (54)");
    }
    const Side side = toInternalSide(side_char);
    if (side == Side::INVALID) {
        return std::unexpected<const char*>("Invalid Side value");
    }

    // --- Validate OrdType ---
    char ord_type = 0;
    if (!Fixpp::tryGet<Fixpp::Tag::OrdType>(order, ord_type)) {
        return std::unexpected<const char*>("Required tag missing: OrdType (40)");
    }
    const RequestType req_type = toRequestType(ord_type);
    if (req_type == RequestType::INVALID) {
        return std::unexpected<const char*>("Unsupported OrderType");
    }

    // --- Extract Price (optional) ---
    double price_val = 0.0;
    const bool has_price = Fixpp::tryGet<Fixpp::Tag::Price>(order, price_val);

    // --- Validate OrderQty ---
    double qty_val = 0.0;
    if (!Fixpp::tryGet<Fixpp::Tag::OrderQty>(order, qty_val)) {
        return std::unexpected<const char*>("Required tag missing: OrderQty (38)");
    }

    return Request{
        .type = req_type,
        .user_id = user_id,
        .ticker_id = ticker,
        .order_id = OrderId::INVALID,
        .side = side,
        .price = has_price ? Price{static_cast<uint64_t>(price_val * 100)} : Price::INVALID,
        .qty = Quantity{static_cast<uint32_t>(qty_val)},
    };
}

/// Parses and validates a FIX OrderCancelRequest (MsgType=F) message.
///
/// Extracts and validates the tags required for a cancel request:
///   1. Symbol (55) — must be present and known to the AssetTranslator
///   2. Side (54)   — must be present and either '1' (Buy) or '2' (Sell)
///
/// The resulting Request has:
///   - type    = RequestType::CANCEL
///   - price   = Price::INVALID   (cancels don't carry a price)
///   - qty     = Quantity::INVALID (cancels don't carry a quantity)
///
/// @param cancel      The parsed FIX OrderCancelRequest message reference.
/// @param user_id     The pre-resolved user identifier (derived from ClOrdID by
///                    the caller — see onOrderCancelRequest()).
/// @param translator  The asset translator for resolving Symbol → TickerId.
///                    Must be fully populated before any calls.
/// @return A valid Request on success, or a `const char*` error description on failure.
/// @note  No heap allocations.  Error literals are static .rodata strings.
[[nodiscard]] inline std::expected<Request, const char*> parseOrderCancelRequest(
    const Fixpp::v42::Message::OrderCancelRequest::Ref& cancel, UserId user_id) noexcept {
    // --- Validate Symbol ---
    std::string symbol;
    if (!Fixpp::tryGet<Fixpp::Tag::Symbol>(cancel, symbol)) {
        return std::unexpected<const char*>("Required tag missing: Symbol (55)");
    }
    const auto* ticker_entry = TickersHash::in_word_set(symbol.c_str(), symbol.size());
    const TickerId ticker = ticker_entry ? TickerId{ticker_entry->id} : TickerId::INVALID;
    if (ticker == TickerId::INVALID) {
        return std::unexpected<const char*>("Unknown symbol");
    }

    // --- Validate Side ---
    char side_char = 0;
    if (!Fixpp::tryGet<Fixpp::Tag::Side>(cancel, side_char)) {
        return std::unexpected<const char*>("Required tag missing: Side (54)");
    }
    const Side side = toInternalSide(side_char);
    if (side == Side::INVALID) {
        return std::unexpected<const char*>("Invalid Side value");
    }

    return Request{
        .type = RequestType::CANCEL,
        .user_id = user_id,
        .ticker_id = ticker,
        .order_id = OrderId::INVALID,
        .side = side,
        .price = Price::INVALID,
        .qty = Quantity::INVALID,
    };
}

}  // namespace exchange::fix::details