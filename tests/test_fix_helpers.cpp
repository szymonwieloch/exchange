/// @file test_fix_helpers.cpp
/// @brief Unit tests for FIX message parsing helpers (fix_helpers.hpp).
///
/// Covers: toInternalSide, toRequestType, parseNewOrderSingle (success,
/// missing tags, invalid values, unknown symbol, optional price), and
/// parseOrderCancelRequest (success, missing tags, invalid values, unknown
/// symbol).

#include <gtest/gtest.h>

#include <cstdio>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

// Use pipe as SOH separator for readable test strings.
#define SOH_CHARACTER '|'
// Suppress deprecated warnings from fixpp (std::aligned_storage in C++23)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <fixpp/tag.h>
#include <fixpp/versions/v42.h>
#include <fixpp/visitor.h>
#pragma GCC diagnostic pop

#include "lib/exchange/definitions.h"
#include "lib/exchange/fix/fix_helpers.hpp"
#include "lib/exchange/request.h"

using exchange::Price;
using exchange::Quantity;
using exchange::Request;
using exchange::RequestType;
using exchange::Side;
using exchange::TickerId;
using exchange::UserId;

// ===================================================================
//  Test visit rules (checksum/length parsing required, but values
//  are not validated — we pre-compute correct values in helpers below)
// ===================================================================

struct TestRules : public Fixpp::VisitRules {
    using Overrides = OverrideSet<>;
    using Dictionary = Fixpp::v42::Spec::Dictionary;
    static constexpr bool ValidateChecksum = false;
    static constexpr bool ValidateLength = false;
    static constexpr bool StrictMode = false;
    static constexpr bool SkipUnknownTags = false;
};

// ===================================================================
//  FIX message construction helpers
// ===================================================================

/// Computes the FIX checksum (sum of all bytes modulo 256) for the portion
/// of the message up to (but not including) the "10=" tag.
[[nodiscard]] static std::string fixChecksum(std::string_view upToChecksum) noexcept {
    int sum = 0;
    for (char c : upToChecksum)
        sum += static_cast<unsigned char>(c);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%03d", sum % 256);
    return buf;
}

/// Builds a complete FIX 4.2 message string.
///
/// Produces: 8=FIX.4.2|9=<bodyLen>|35=<msgType>|<body>|10=<checksum>|
///
/// @param msgType  FIX MsgType, e.g. "D" for NewOrderSingle, "F" for Cancel.
/// @param body     Tag=Value pairs after MsgType, each delimited by '|'.
///                 Must NOT include a trailing '|' (it is added here).
/// @return A complete, well-formed FIX message string with the SOH
///         character replaced by '|'.
[[nodiscard]] static std::string buildFixMsg(std::string_view msgType,
                                             std::string_view body) noexcept {
    // BodyPart = "35=<msgType>|<body>|"
    std::string bodyPart = "35=";
    bodyPart += msgType;
    bodyPart += "|";
    bodyPart += body;
    bodyPart += "|";

    // The prefix: 8=FIX.4.2|9=<bodyLen>|
    std::string prefix = "8=FIX.4.2|9=";
    prefix += std::to_string(bodyPart.size());
    prefix += "|";

    // Checksum is computed over prefix + bodyPart (but NOT the "10=" trailer)
    std::string msg = prefix + bodyPart;
    msg += "10=";
    msg += fixChecksum(msg);
    msg += "|";
    return msg;
}

// ===================================================================
//  FIX message visitor helpers
// ===================================================================

/// Visitor that captures a parsed NewOrderSingle message and forwards it
/// to a callback.  Any other message type triggers a test failure.
template <typename Callback>
struct NewOrderSingleVisitor : public Fixpp::StaticVisitor<void> {
    Callback& cb;
    explicit NewOrderSingleVisitor(Callback& c) : cb(c) {}
    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::NewOrderSingle::Ref& msg) {
        cb(msg);
    }
    template <typename H, typename M>
    void operator()(H, M) {
        FAIL() << "Unexpected FIX message type visited";
    }
};

/// Visitor that captures a parsed OrderCancelRequest message and forwards it
/// to a callback.  Any other message type triggers a test failure.
template <typename Callback>
struct OrderCancelRequestVisitor : public Fixpp::StaticVisitor<void> {
    Callback& cb;
    explicit OrderCancelRequestVisitor(Callback& c) : cb(c) {}
    void operator()(const Fixpp::v42::Header::Ref& /*header*/,
                    const Fixpp::v42::Message::OrderCancelRequest::Ref& msg) {
        cb(msg);
    }
    template <typename H, typename M>
    void operator()(H, M) {
        FAIL() << "Unexpected FIX message type visited";
    }
};

/// Parses a raw FIX NewOrderSingle (MsgType=D) string and invokes @p cb with
/// the parsed message reference.  Fails the test if parsing fails or a
/// different message type is visited.
template <typename Callback>
static void withParsedNewOrderSingle(std::string_view raw, Callback&& cb) {
    NewOrderSingleVisitor<Callback> visitor{cb};
    std::string mutableCopy{raw};
    auto result = Fixpp::visit(mutableCopy.data(), mutableCopy.size(), visitor, TestRules{});
    ASSERT_TRUE(result.isOk()) << result.unwrapErr().asString();
}

/// Parses a raw FIX OrderCancelRequest (MsgType=F) string and invokes @p cb
/// with the parsed message reference.
template <typename Callback>
static void withParsedOrderCancelRequest(std::string_view raw, Callback&& cb) {
    OrderCancelRequestVisitor<Callback> visitor{cb};
    std::string mutableCopy{raw};
    auto result = Fixpp::visit(mutableCopy.data(), mutableCopy.size(), visitor, TestRules{});
    ASSERT_TRUE(result.isOk()) << result.unwrapErr().asString();
}

/// Helper: constructs a default translator populated from POPULAR_TICKERS.
//  (Removed — AssetTranslator has been extracted from the fix_helpers API.
//   Symbol resolution is a TODO stub that always returns TickerId::INVALID.)

// ===================================================================
//  toInternalSide
// ===================================================================

TEST(ToInternalSideTest, Buy) {
    EXPECT_EQ(exchange::fix::details::toInternalSide('1'), Side::BUY);
}

TEST(ToInternalSideTest, Sell) {
    EXPECT_EQ(exchange::fix::details::toInternalSide('2'), Side::SELL);
}

TEST(ToInternalSideTest, InvalidChars) {
    // Common invalid/miscellaneous Side values in FIX 4.2
    EXPECT_EQ(exchange::fix::details::toInternalSide('0'), Side::INVALID);
    EXPECT_EQ(exchange::fix::details::toInternalSide('3'), Side::INVALID);  // Buy minus
    EXPECT_EQ(exchange::fix::details::toInternalSide('4'), Side::INVALID);  // Sell plus
    EXPECT_EQ(exchange::fix::details::toInternalSide('5'), Side::INVALID);  // Sell short
    EXPECT_EQ(exchange::fix::details::toInternalSide('A'), Side::INVALID);
    EXPECT_EQ(exchange::fix::details::toInternalSide('\0'), Side::INVALID);
    EXPECT_EQ(exchange::fix::details::toInternalSide('9'), Side::INVALID);
}

// ===================================================================
//  toRequestType
// ===================================================================

TEST(ToRequestTypeTest, Market) {
    EXPECT_EQ(exchange::fix::details::toRequestType('1'), RequestType::NEW);
}

TEST(ToRequestTypeTest, Limit) {
    EXPECT_EQ(exchange::fix::details::toRequestType('2'), RequestType::NEW);
}

TEST(ToRequestTypeTest, InvalidChars) {
    EXPECT_EQ(exchange::fix::details::toRequestType('0'), RequestType::INVALID);
    EXPECT_EQ(exchange::fix::details::toRequestType('3'), RequestType::INVALID);  // Stop
    EXPECT_EQ(exchange::fix::details::toRequestType('4'), RequestType::INVALID);  // Stop-Limit
    EXPECT_EQ(exchange::fix::details::toRequestType('A'), RequestType::INVALID);
    EXPECT_EQ(exchange::fix::details::toRequestType('\0'), RequestType::INVALID);
}

// ===================================================================
//  parseNewOrderSingle — success cases
//
//  Note: AssetTranslator has been removed; symbol resolution is currently
//  a TODO stub that always returns TickerId::INVALID.  All parse calls
//  will fail with "Unknown symbol" until symbol resolution is re-wired.
// ===================================================================

TEST(ParseNewOrderSingleTest, ValidMarketOrderWithoutPrice) {
    // Market order: OrdType=1, Side=Buy, Symbol=AAPL, Qty=100, no Price
    auto raw = buildFixMsg("D", "11=ord001|21=1|55=AAPL|54=1|60=20240101-00:00:00|40=1|38=100");
    const UserId user{42};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

TEST(ParseNewOrderSingleTest, ValidLimitOrderWithPrice) {
    // Limit order: OrdType=2, Side=Sell, Symbol=MSFT, Qty=50, Price=125.50
    auto raw =
        buildFixMsg("D", "11=ord002|21=1|55=MSFT|54=2|60=20240101-00:00:00|40=2|38=50|44=125.50");
    const UserId user{7};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

TEST(ParseNewOrderSingleTest, BuyLimitOrder) {
    auto raw =
        buildFixMsg("D", "11=ord003|21=1|55=AAPL|54=1|60=20240101-00:00:00|40=2|38=200|44=99.00");
    const UserId user{100};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

TEST(ParseNewOrderSingleTest, LargeQuantityAndPrice) {
    auto raw = buildFixMsg(
        "D", "11=ord004|21=1|55=AAPL|54=1|60=20240101-00:00:00|40=2|38=999999|44=99999.99");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

// ===================================================================
//  parseNewOrderSingle — error cases
// ===================================================================

TEST(ParseNewOrderSingleTest, MissingSymbol) {
    // No Symbol (55) tag at all
    auto raw = buildFixMsg("D", "11=ord005|21=1|54=1|60=20240101-00:00:00|40=1|38=100");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Required tag missing: Symbol (55)");
    });
}

TEST(ParseNewOrderSingleTest, UnknownSymbol) {
    auto raw = buildFixMsg("D", "11=ord006|21=1|55=ZZZZ|54=1|60=20240101-00:00:00|40=1|38=100");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

TEST(ParseNewOrderSingleTest, MissingSide) {
    // No Side (54) tag
    auto raw = buildFixMsg("D", "11=ord007|21=1|55=AAPL|60=20240101-00:00:00|40=1|38=100");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");  // symbol check fails first
    });
}

TEST(ParseNewOrderSingleTest, InvalidSide) {
    auto raw = buildFixMsg("D", "11=ord008|21=1|55=AAPL|54=9|60=20240101-00:00:00|40=1|38=100");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");  // symbol check fails first
    });
}

TEST(ParseNewOrderSingleTest, MissingOrdType) {
    auto raw = buildFixMsg("D", "11=ord009|21=1|55=AAPL|54=1|60=20240101-00:00:00|38=100");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");  // symbol check fails first
    });
}

TEST(ParseNewOrderSingleTest, InvalidOrdType) {
    // OrdType=3 is Stop, which is unsupported
    auto raw = buildFixMsg("D", "11=ord010|21=1|55=AAPL|54=1|60=20240101-00:00:00|40=3|38=100");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");  // symbol check fails first
    });
}

TEST(ParseNewOrderSingleTest, MissingOrderQty) {
    auto raw = buildFixMsg("D", "11=ord011|21=1|55=AAPL|54=1|60=20240101-00:00:00|40=1");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");  // symbol check fails first
    });
}

TEST(ParseNewOrderSingleTest, UnknownSymbolEmptyTranslator) {
    auto raw = buildFixMsg("D", "11=ord012|21=1|55=ZZZZ|54=1|60=20240101-00:00:00|40=1|38=100");
    const UserId user{1};

    withParsedNewOrderSingle(raw, [&](const auto& order) {
        auto result = exchange::fix::details::parseNewOrderSingle(order, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

// ===================================================================
//  parseOrderCancelRequest — success cases
//
//  Note: AssetTranslator has been removed; symbol resolution is currently
//  a TODO stub that always returns TickerId::INVALID.  All parse calls
//  will fail with "Unknown symbol" until symbol resolution is re-wired.
// ===================================================================

TEST(ParseOrderCancelRequestTest, ValidCancelBuy) {
    auto raw = buildFixMsg("F", "41=orig123|11=cancel001|55=AAPL|54=1|60=20240101-00:00:00");
    const UserId user{99};

    withParsedOrderCancelRequest(raw, [&](const auto& cancel) {
        auto result = exchange::fix::details::parseOrderCancelRequest(cancel, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

TEST(ParseOrderCancelRequestTest, ValidCancelSell) {
    auto raw = buildFixMsg("F", "41=orig456|11=cancel002|55=MSFT|54=2|60=20240101-00:00:00");
    const UserId user{3};

    withParsedOrderCancelRequest(raw, [&](const auto& cancel) {
        auto result = exchange::fix::details::parseOrderCancelRequest(cancel, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

// ===================================================================
//  parseOrderCancelRequest — error cases
// ===================================================================

TEST(ParseOrderCancelRequestTest, MissingSymbol) {
    auto raw = buildFixMsg("F", "41=orig789|11=cancel003|54=1|60=20240101-00:00:00");
    const UserId user{1};

    withParsedOrderCancelRequest(raw, [&](const auto& cancel) {
        auto result = exchange::fix::details::parseOrderCancelRequest(cancel, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Required tag missing: Symbol (55)");
    });
}

TEST(ParseOrderCancelRequestTest, UnknownSymbol) {
    auto raw = buildFixMsg("F", "41=orig000|11=cancel004|55=ZZZZ|54=1|60=20240101-00:00:00");
    const UserId user{1};

    withParsedOrderCancelRequest(raw, [&](const auto& cancel) {
        auto result = exchange::fix::details::parseOrderCancelRequest(cancel, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");
    });
}

TEST(ParseOrderCancelRequestTest, MissingSide) {
    auto raw = buildFixMsg("F", "41=orig111|11=cancel005|55=AAPL|60=20240101-00:00:00");
    const UserId user{1};

    withParsedOrderCancelRequest(raw, [&](const auto& cancel) {
        auto result = exchange::fix::details::parseOrderCancelRequest(cancel, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");  // symbol check fails first
    });
}

TEST(ParseOrderCancelRequestTest, InvalidSide) {
    auto raw = buildFixMsg("F", "41=orig222|11=cancel006|55=AAPL|54=X|60=20240101-00:00:00");
    const UserId user{1};

    withParsedOrderCancelRequest(raw, [&](const auto& cancel) {
        auto result = exchange::fix::details::parseOrderCancelRequest(cancel, user);
        ASSERT_FALSE(result.has_value());
        EXPECT_STREQ(result.error(), "Unknown symbol");  // symbol check fails first
    });
}
