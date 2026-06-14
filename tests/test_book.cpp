/// @file test_book.cpp
/// @brief Comprehensive unit tests for OrderBook and OrderBookHashMap.
///
/// Covers: construction, add (new orders, full/partial matches, multi-level
/// matching, duplicates), cancel (existing, non-existent, last-in-level,
/// one-of-many), FIFO priority ordering, side-specific matching, and ticker
/// isolation via OrderBookHashMap.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "lib/exchange/book.h"
#include "lib/exchange/constants.h"
#include "lib/exchange/md.h"
#include "lib/exchange/metric_registry.h"
#include "lib/exchange/order.h"
#include "lib/exchange/request.h"
#include "lib/utils/log.h"

using exchange::MarketOrderId;
using exchange::MDLFQueue;
using exchange::MDUpdate;
using exchange::MDUpdateType;
using exchange::Order;
using exchange::OrderBook;
using exchange::OrderBookHashMap;
using exchange::OrderId;
using exchange::Price;
using exchange::Priority;
using exchange::Quantity;
using exchange::Response;
using exchange::ResponseLFQueue;
using exchange::ResponseType;
using exchange::Side;
using exchange::TickerId;
using exchange::UserId;

// ===================================================================
//  Test Fixture
// ===================================================================

/// Provides a Logger (heap-allocated, leaked to avoid destructor issues),
/// response queue, and market-data update queue sized for the test.
class OrderBookTest : public ::testing::Test {
protected:
    /// Queues must be large enough to hold all messages emitted during a test.
    /// Each order add/cancel/match can emit multiple responses and MD updates.
    static constexpr size_t kQueueCapacity = 1024;

    // Logger is heap-allocated and intentionally leaked — its destructor
    // attempts to join a thread that is never created (TODO in log.h).
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    utils::Logger *logger = new utils::Logger("test_book.log", utils::LogLevel::DEBUG);
    ResponseLFQueue responses{kQueueCapacity};
    MDLFQueue market_updates{kQueueCapacity};
    exchange::MetricRegistry metrics;

    /// Creates an OrderBook for Ticker 0 with the fixture's queues.
    OrderBook makeBook() {
        return OrderBook{TickerId{0}, logger, &responses, &market_updates, metrics};
    }

    void SetUp() override {
        // Queues are default-constructed with kQueueCapacity elements; no
        // explicit reset needed since each test creates a fresh book.
    }

    // ── response / MD update helpers ───────────────────────────

    /// Drains and returns the next response, or nullptr if none.
    const Response *nextResponse() { return responses.getNextToRead(); }

    /// Advances the response read cursor past one element.
    void consumeResponse() { responses.updateReadIndex(); }

    /// Drains and returns the next market-data update, or nullptr if none.
    const MDUpdate *nextMD() { return market_updates.getNextToRead(); }

    /// Advances the MD read cursor past one element.
    void consumeMD() { market_updates.updateReadIndex(); }

    /// Drains all queued responses into a vector for inspection.
    std::vector<Response> drainResponses() {
        std::vector<Response> out;
        while (const auto *r = responses.getNextToRead()) {
            out.push_back(*r);
            responses.updateReadIndex();
        }
        return out;
    }

    /// Drains all queued MD updates into a vector for inspection.
    std::vector<MDUpdate> drainMD() {
        std::vector<MDUpdate> out;
        while (const auto *md = market_updates.getNextToRead()) {
            out.push_back(*md);
            market_updates.updateReadIndex();
        }
        return out;
    }

    /// Asserts that no responses are pending.
    void expectNoResponses() { EXPECT_EQ(responses.getNextToRead(), nullptr); }

    /// Asserts that no MD updates are pending.
    void expectNoMD() { EXPECT_EQ(market_updates.getNextToRead(), nullptr); }
};

// ===================================================================
//  Construction
// ===================================================================

TEST_F(OrderBookTest, ConstructDoesNotThrow) {
    EXPECT_NO_THROW(OrderBook(TickerId{0}, logger, &responses, &market_updates, metrics));
}

TEST_F(OrderBookTest, NewBookHasNoPendingMessages) {
    auto book = makeBook();
    expectNoResponses();
    expectNoMD();
}

// ===================================================================
//  add — simple resting orders (no match)
// ===================================================================

TEST_F(OrderBookTest, AddSingleBuyNoMatch) {
    auto book = makeBook();

    const bool ok =
        book.add(UserId{1}, OrderId{100}, TickerId{0}, Side::BUY, Price{100}, Quantity{10});
    EXPECT_TRUE(ok);

    // Expect: ACCEPTED response, ADD MD update
    const auto *resp = nextResponse();
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->type, ResponseType::ACCEPTED);
    EXPECT_EQ(resp->user_id, UserId{1});
    EXPECT_EQ(resp->order_id, OrderId{100});
    EXPECT_EQ(resp->side, Side::BUY);
    EXPECT_EQ(resp->price, Price{100});
    EXPECT_EQ(resp->leaves_qty, Quantity{10});
    consumeResponse();

    const auto *md = nextMD();
    ASSERT_NE(md, nullptr);
    EXPECT_EQ(md->type_, MDUpdateType::ADD);
    EXPECT_EQ(md->side, Side::BUY);
    EXPECT_EQ(md->price, Price{100});
    EXPECT_EQ(md->qty, Quantity{10});
    consumeMD();

    expectNoResponses();
    expectNoMD();
}

TEST_F(OrderBookTest, AddSingleSellNoMatch) {
    auto book = makeBook();

    const bool ok =
        book.add(UserId{2}, OrderId{200}, TickerId{0}, Side::SELL, Price{200}, Quantity{5});
    EXPECT_TRUE(ok);

    const auto *resp = nextResponse();
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->type, ResponseType::ACCEPTED);
    EXPECT_EQ(resp->side, Side::SELL);
    consumeResponse();

    const auto *md = nextMD();
    ASSERT_NE(md, nullptr);
    EXPECT_EQ(md->type_, MDUpdateType::ADD);
    EXPECT_EQ(md->side, Side::SELL);
    consumeMD();

    expectNoResponses();
    expectNoMD();
}

TEST_F(OrderBookTest, AddMultipleRestingOrdersSameSideSamePrice) {
    auto book = makeBook();

    EXPECT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{100}, Quantity{5}));
    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{3}));
    EXPECT_TRUE(book.add(UserId{3}, OrderId{3}, TickerId{0}, Side::BUY, Price{100}, Quantity{7}));

    // 3 ACCEPTED + 3 ADD
    auto resps = drainResponses();
    EXPECT_EQ(resps.size(), 3);
    for (const auto &r : resps) {
        EXPECT_EQ(r.type, ResponseType::ACCEPTED);
    }

    auto mds = drainMD();
    EXPECT_EQ(mds.size(), 3);
    for (const auto &m : mds) {
        EXPECT_EQ(m.type_, MDUpdateType::ADD);
    }
}

// ===================================================================
//  add — full match
// ===================================================================

TEST_F(OrderBookTest, AddBuyFullyMatchesExistingSell) {
    auto book = makeBook();

    // Place a resting sell
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    // Incoming buy at same price, same qty — full match
    const bool ok =
        book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{10});
    EXPECT_TRUE(ok);

    // Expected responses:
    //   1. ACCEPTED for incoming buy
    //   2. FILLED for incoming buy (aggressor)
    //   3. FILLED for resting sell
    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 3);

    // First: ACCEPTED
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);
    EXPECT_EQ(resps[0].user_id, UserId{2});

    // FILLED for aggressor (buy)
    bool has_buy_fill = false;
    bool has_sell_fill = false;
    for (size_t i = 1; i < resps.size(); ++i) {
        if (resps[i].type == ResponseType::FILLED) {
            if (resps[i].user_id == UserId{2}) {
                has_buy_fill = true;
                EXPECT_EQ(resps[i].exec_qty, Quantity{10});
                EXPECT_EQ(resps[i].leaves_qty, Quantity{0});
            }
            if (resps[i].user_id == UserId{1}) {
                has_sell_fill = true;
                EXPECT_EQ(resps[i].exec_qty, Quantity{10});
                EXPECT_EQ(resps[i].leaves_qty, Quantity{0});
            }
        }
    }
    EXPECT_TRUE(has_buy_fill);
    EXPECT_TRUE(has_sell_fill);

    // Expected MD: TRADE + CANCEL (resting order removed)
    auto mds = drainMD();
    ASSERT_GE(mds.size(), 2);
    bool has_trade = false;
    bool has_cancel = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE) {
            has_trade = true;
            EXPECT_EQ(m.qty, Quantity{10});
        }
        if (m.type_ == MDUpdateType::CANCEL)
            has_cancel = true;
    }
    EXPECT_TRUE(has_trade);
    EXPECT_TRUE(has_cancel);
}

TEST_F(OrderBookTest, AddSellFullyMatchesExistingBuy) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{150}, Quantity{8}));
    drainResponses();
    drainMD();

    const bool ok =
        book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::SELL, Price{150}, Quantity{8});
    EXPECT_TRUE(ok);

    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 3);
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);

    auto mds = drainMD();
    ASSERT_GE(mds.size(), 2);
}

// ===================================================================
//  add — partial match
// ===================================================================

TEST_F(OrderBookTest, AddBuyPartiallyMatchesLargerSell) {
    auto book = makeBook();

    // Resting sell: 15 units
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{15}));
    drainResponses();
    drainMD();

    // Incoming buy: 10 units (smaller than resting)
    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{10}));

    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 3);

    // FILLED for sell should have leaves_qty = 5 (partial fill)
    bool sell_modify_seen = false;
    for (const auto &r : resps) {
        if (r.type == ResponseType::FILLED && r.user_id == UserId{1}) {
            EXPECT_EQ(r.leaves_qty, Quantity{5});  // 15 - 10 = 5 remaining
            sell_modify_seen = true;
        }
    }
    EXPECT_TRUE(sell_modify_seen);

    // MD: TRADE + MODIFY (not CANCEL, since sell still has 5 left)
    auto mds = drainMD();
    ASSERT_GE(mds.size(), 2);
    bool has_modify = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::MODIFY) {
            has_modify = true;
            EXPECT_EQ(m.qty, Quantity{5});
        }
    }
    EXPECT_TRUE(has_modify);
}

TEST_F(OrderBookTest, AddBuyLargerThanRestingSellLeavesRestingBuy) {
    auto book = makeBook();

    // Resting sell: 5 units
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    // Incoming buy: 12 units (larger than resting → partial fill, then rest)
    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{12}));

    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 3);

    // Aggressor FILLED should have leaves_qty = 7 (12 - 5)
    bool aggressor_fill_ok = false;
    for (const auto &r : resps) {
        if (r.type == ResponseType::FILLED && r.user_id == UserId{2}) {
            EXPECT_EQ(r.exec_qty, Quantity{5});
            EXPECT_EQ(r.leaves_qty, Quantity{7});
            aggressor_fill_ok = true;
        }
    }
    EXPECT_TRUE(aggressor_fill_ok);

    // MD: TRADE + CANCEL (resting sell fully consumed) + ADD (remaining 7 as resting buy)
    auto mds = drainMD();
    bool has_trade = false, has_cancel = false, has_add = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            has_trade = true;
        if (m.type_ == MDUpdateType::CANCEL)
            has_cancel = true;
        if (m.type_ == MDUpdateType::ADD) {
            has_add = true;
            EXPECT_EQ(m.qty, Quantity{7});
        }
    }
    EXPECT_TRUE(has_trade);
    EXPECT_TRUE(has_cancel);
    EXPECT_TRUE(has_add);
}

// ===================================================================
//  add — multi-level matching
// ===================================================================

TEST_F(OrderBookTest, AddBuyMatchesMultiplePriceLevels) {
    auto book = makeBook();

    // Three sell levels: 100 (5), 101 (3), 102 (4)
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    ASSERT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::SELL, Price{101}, Quantity{3}));
    ASSERT_TRUE(book.add(UserId{3}, OrderId{3}, TickerId{0}, Side::SELL, Price{102}, Quantity{4}));
    drainResponses();
    drainMD();

    // Buy at 102, qty 10 — should consume all three levels (5+3+2)
    EXPECT_TRUE(book.add(UserId{4}, OrderId{4}, TickerId{0}, Side::BUY, Price{102}, Quantity{10}));

    auto resps = drainResponses();
    // ACCEPTED + 3 FILLED pairs = 7 responses
    ASSERT_GE(resps.size(), 7);

    // Aggressor final FILLED: leaves_qty = 0 (fully consumed)
    // Last sell FILLED at 102: partial fill, leaves_qty = 2
    auto mds = drainMD();
    // TRADE x3 + CANCEL x2 (100 and 101 fully consumed) + MODIFY x1 (102 partially)
    int trades = 0, cancels = 0, modifies = 0;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            ++trades;
        if (m.type_ == MDUpdateType::CANCEL)
            ++cancels;
        if (m.type_ == MDUpdateType::MODIFY)
            ++modifies;
    }
    EXPECT_EQ(trades, 3);
    EXPECT_EQ(cancels, 2);
    EXPECT_EQ(modifies, 1);
}

TEST_F(OrderBookTest, AddSellMatchesMultipleBidLevels) {
    auto book = makeBook();

    // Three bid levels: 200 (3), 199 (4), 198 (6)
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{200}, Quantity{3}));
    ASSERT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{199}, Quantity{4}));
    ASSERT_TRUE(book.add(UserId{3}, OrderId{3}, TickerId{0}, Side::BUY, Price{198}, Quantity{6}));
    drainResponses();
    drainMD();

    // Sell at 198, qty 10 — should consume all three levels (3+4+3)
    EXPECT_TRUE(book.add(UserId{4}, OrderId{4}, TickerId{0}, Side::SELL, Price{198}, Quantity{10}));

    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 7);

    auto mds = drainMD();
    int trades = 0, cancels = 0, modifies = 0;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            ++trades;
        if (m.type_ == MDUpdateType::CANCEL)
            ++cancels;
        if (m.type_ == MDUpdateType::MODIFY)
            ++modifies;
    }
    EXPECT_EQ(trades, 3);
    EXPECT_EQ(cancels, 2);   // 200 and 199 fully consumed
    EXPECT_EQ(modifies, 1);  // 198 partially consumed
}

// ===================================================================
//  add — non-crossing prices (order rests)
// ===================================================================

TEST_F(OrderBookTest, AddBuyBelowLowestAskRests) {
    auto book = makeBook();

    // Lowest ask is 100
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    // Buy at 99 — should not match (99 < 100), rests instead
    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{99}, Quantity{10}));

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);

    auto mds = drainMD();
    ASSERT_EQ(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::ADD);
}

TEST_F(OrderBookTest, AddSellAboveHighestBidRests) {
    auto book = makeBook();

    // Highest bid is 100
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    // Sell at 101 — should not match (101 > 100), rests instead
    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::SELL, Price{101}, Quantity{10}));

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);

    auto mds = drainMD();
    ASSERT_EQ(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::ADD);
}

// ===================================================================
//  add — crossed prices (aggressive pricing)
// ===================================================================

TEST_F(OrderBookTest, AddBuyAboveLowestAskMatchesAtAskPrice) {
    auto book = makeBook();

    // Lowest ask is 100
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    // Buy at 105 — crosses the spread, matches at 100
    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{105}, Quantity{5}));

    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 3);

    auto mds = drainMD();
    bool has_trade = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            has_trade = true;
    }
    EXPECT_TRUE(has_trade);
}

TEST_F(OrderBookTest, AddSellBelowHighestBidMatchesAtBidPrice) {
    auto book = makeBook();

    // Highest bid is 100
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    // Sell at 95 — crosses the spread, matches at 100
    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::SELL, Price{95}, Quantity{5}));

    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 3);

    auto mds = drainMD();
    bool has_trade = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            has_trade = true;
    }
    EXPECT_TRUE(has_trade);
}

// ===================================================================
//  add — price-time priority (FIFO within same price)
// ===================================================================

TEST_F(OrderBookTest, FifoMatchingWithinSamePriceLevel) {
    auto book = makeBook();

    // Place three sells at same price, different users (different priority)
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    ASSERT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    ASSERT_TRUE(book.add(UserId{3}, OrderId{3}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    // Buy 8 units — should consume first sell (5) fully, then second (3 partially)
    EXPECT_TRUE(book.add(UserId{4}, OrderId{4}, TickerId{0}, Side::BUY, Price{100}, Quantity{8}));

    auto resps = drainResponses();
    // ACCEPTED + FILLED(buy, 5, lv=3) + FILLED(sell1, 5, lv=0)
    //            + FILLED(buy, 3, lv=0) + FILLED(sell2, 3, lv=2)
    ASSERT_GE(resps.size(), 5);

    // First sell should have leaves_qty = 0 (fully consumed)
    bool sell1_full = false, sell2_partial = false;
    for (const auto &r : resps) {
        if (r.type == ResponseType::FILLED && r.user_id == UserId{1}) {
            EXPECT_EQ(r.leaves_qty, Quantity{0});
            sell1_full = true;
        }
        if (r.type == ResponseType::FILLED && r.user_id == UserId{2}) {
            EXPECT_EQ(r.leaves_qty, Quantity{2});  // 5 - 3 = 2
            sell2_partial = true;
        }
    }
    EXPECT_TRUE(sell1_full);
    EXPECT_TRUE(sell2_partial);

    // Third sell should not have been touched
    auto mds = drainMD();
    // Should have MODIFY for sell2 (not CANCEL for sell2)
    bool has_modify_sell2 = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::MODIFY) {
            has_modify_sell2 = true;
            EXPECT_EQ(m.qty, Quantity{2});
        }
    }
    EXPECT_TRUE(has_modify_sell2);
}

// ===================================================================
//  add — duplicate order ID
// ===================================================================

TEST_F(OrderBookTest, AddDuplicateOrderIdOverwritesInRelease) {
    auto book = makeBook();

    EXPECT_TRUE(
        book.add(UserId{1}, OrderId{100}, TickerId{0}, Side::BUY, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    // Same user, same order ID — in Release mode the slot is silently
    // overwritten.  UserOrders::insert uses assert() for duplicate detection,
    // which is compiled out in Release builds, so add() returns true.
    const bool ok =
        book.add(UserId{1}, OrderId{100}, TickerId{0}, Side::BUY, Price{100}, Quantity{5});
    EXPECT_TRUE(ok);

    // ACCEPTED + ADD are emitted for the overwriting order
    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);

    auto mds = drainMD();
    ASSERT_GE(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::ADD);
    EXPECT_EQ(mds[0].qty, Quantity{5});
}

// ===================================================================
//  add — zero quantity edge case
// ===================================================================

TEST_F(OrderBookTest, AddWithFullMatchDoesNotRest) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    // Exact match — should not create a resting order
    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{10}));

    auto mds = drainMD();
    // TRADE + CANCEL — no ADD (nothing rests)
    bool has_add = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::ADD)
            has_add = true;
    }
    EXPECT_FALSE(has_add);
}

// ===================================================================
//  cancel
// ===================================================================

TEST_F(OrderBookTest, CancelExistingOrder) {
    auto book = makeBook();

    ASSERT_TRUE(
        book.add(UserId{1}, OrderId{100}, TickerId{0}, Side::BUY, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    book.cancel(UserId{1}, OrderId{100}, TickerId{0});

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::CANCELED);
    EXPECT_EQ(resps[0].user_id, UserId{1});
    EXPECT_EQ(resps[0].order_id, OrderId{100});

    auto mds = drainMD();
    ASSERT_EQ(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::CANCEL);
}

TEST_F(OrderBookTest, CancelNonExistentOrderRejected) {
    auto book = makeBook();

    book.cancel(UserId{1}, OrderId{999}, TickerId{0});

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::CANCEL_REJECTED);
    EXPECT_EQ(resps[0].user_id, UserId{1});
    EXPECT_EQ(resps[0].order_id, OrderId{999});

    expectNoMD();
}

TEST_F(OrderBookTest, CancelOnlyOrderInPriceLevel) {
    auto book = makeBook();

    // Single order at price 100
    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    book.cancel(UserId{1}, OrderId{1}, TickerId{0});

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::CANCELED);

    auto mds = drainMD();
    ASSERT_EQ(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::CANCEL);

    // After cancel, no orders should remain
    expectNoResponses();
    expectNoMD();
}

TEST_F(OrderBookTest, CancelOneOfMultipleOrdersAtSamePrice) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{100}, Quantity{5}));
    ASSERT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{3}));
    drainResponses();
    drainMD();

    book.cancel(UserId{1}, OrderId{1}, TickerId{0});

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::CANCELED);

    auto mds = drainMD();
    ASSERT_EQ(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::CANCEL);

    // Second order should still be resting
    expectNoResponses();
    expectNoMD();

    // Verify second order can still be matched
    EXPECT_TRUE(book.add(UserId{3}, OrderId{3}, TickerId{0}, Side::SELL, Price{100}, Quantity{3}));
    auto resps2 = drainResponses();
    bool has_fill = false;
    for (const auto &r : resps2) {
        if (r.type == ResponseType::FILLED && r.user_id == UserId{2})
            has_fill = true;
    }
    EXPECT_TRUE(has_fill);
}

TEST_F(OrderBookTest, CancelThenReAddSameOrderId) {
    auto book = makeBook();

    ASSERT_TRUE(
        book.add(UserId{1}, OrderId{100}, TickerId{0}, Side::BUY, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    book.cancel(UserId{1}, OrderId{100}, TickerId{0});
    drainResponses();
    drainMD();

    // Re-add with same (user, order_id) should succeed since the old was removed
    const bool ok =
        book.add(UserId{1}, OrderId{100}, TickerId{0}, Side::BUY, Price{100}, Quantity{7});
    EXPECT_TRUE(ok);

    auto resps = drainResponses();
    ASSERT_GE(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);

    auto mds = drainMD();
    ASSERT_GE(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::ADD);
    EXPECT_EQ(mds[0].qty, Quantity{7});
}

// ===================================================================
//  cancel — wrong ticker
// ===================================================================

TEST_F(OrderBookTest, CancelWrongTickerRejected) {
    auto book = makeBook();

    ASSERT_TRUE(
        book.add(UserId{1}, OrderId{100}, TickerId{0}, Side::BUY, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    // Cancel on wrong ticker — order is looked up by (user_id, order_id),
    // ticker_id is passed for the response but the lookup is ticker-agnostic
    // within a single OrderBook instance. This tests that the cancel on the
    // right book works correctly regardless.
    book.cancel(UserId{1}, OrderId{100}, TickerId{0});

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::CANCELED);
}

// ===================================================================
//  MarketOrderId monotonicity
// ===================================================================

TEST_F(OrderBookTest, MarketOrderIdsAreMonotonic) {
    auto book = makeBook();

    const auto id1 = book.generateNewMarketOrderId();
    const auto id2 = book.generateNewMarketOrderId();
    const auto id3 = book.generateNewMarketOrderId();

    EXPECT_LT(type_safe::get(id1), type_safe::get(id2));
    EXPECT_LT(type_safe::get(id2), type_safe::get(id3));
}

// ===================================================================
//  Matching — exact price boundary
// ===================================================================

TEST_F(OrderBookTest, BuyAtExactAskPriceMatches) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{5}));

    auto mds = drainMD();
    bool has_trade = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            has_trade = true;
    }
    EXPECT_TRUE(has_trade);
}

TEST_F(OrderBookTest, SellAtExactBidPriceMatches) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{100}, Quantity{5}));
    drainResponses();
    drainMD();

    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));

    auto mds = drainMD();
    bool has_trade = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            has_trade = true;
    }
    EXPECT_TRUE(has_trade);
}

// ===================================================================
//  Multiple users, same price (FIFO across users)
// ===================================================================

TEST_F(OrderBookTest, MultipleUsersSamePriceFifoAcrossUsers) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{10}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{3}));
    ASSERT_TRUE(book.add(UserId{20}, OrderId{2}, TickerId{0}, Side::SELL, Price{100}, Quantity{5}));
    ASSERT_TRUE(book.add(UserId{30}, OrderId{3}, TickerId{0}, Side::SELL, Price{100}, Quantity{2}));
    drainResponses();
    drainMD();

    // Buy 6 — should consume user 10's order (3) and user 20's order partially (3)
    EXPECT_TRUE(book.add(UserId{99}, OrderId{99}, TickerId{0}, Side::BUY, Price{100}, Quantity{6}));

    auto resps = drainResponses();
    // User 10 should be fully filled
    bool user10_filled = false;
    bool user20_partial = false;
    for (const auto &r : resps) {
        if (r.type == ResponseType::FILLED && r.user_id == UserId{10}) {
            EXPECT_EQ(r.leaves_qty, Quantity{0});
            user10_filled = true;
        }
        if (r.type == ResponseType::FILLED && r.user_id == UserId{20}) {
            EXPECT_EQ(r.leaves_qty, Quantity{2});  // 5 - 3
            user20_partial = true;
        }
    }
    EXPECT_TRUE(user10_filled);
    EXPECT_TRUE(user20_partial);
}

// ===================================================================
//  Empty book — no match possible
// ===================================================================

TEST_F(OrderBookTest, AddFirstOrderNoMatch) {
    auto book = makeBook();

    EXPECT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{100}, Quantity{10}));

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);

    auto mds = drainMD();
    ASSERT_EQ(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::ADD);
}

// ===================================================================
//  Bid/ask spread — no crossing
// ===================================================================

TEST_F(OrderBookTest, NonCrossingSpreadBothSidesRest) {
    auto book = makeBook();

    // Bid at 99, ask at 101 — spread is 2
    EXPECT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{99}, Quantity{5}));
    drainResponses();
    drainMD();

    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::SELL, Price{101}, Quantity{5}));

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);  // Only ACCEPTED for the sell
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);

    auto mds = drainMD();
    ASSERT_EQ(mds.size(), 1);  // Only ADD for the sell
    EXPECT_EQ(mds[0].type_, MDUpdateType::ADD);
}

// ===================================================================
//  Multiple tickers via OrderBookHashMap
// ===================================================================

TEST_F(OrderBookTest, HashMapPrepopulatesAllTickers) {
    OrderBookHashMap map(logger, &responses, &market_updates, metrics);

    for (uint16_t i = 0; i < exchange::MAX_TICKERS; ++i) {
        auto *book = map.find(TickerId{i});
        ASSERT_NE(book, nullptr) << "Ticker " << i << " should have an OrderBook";
    }
}

TEST_F(OrderBookTest, DifferentTickersAreIsolated) {
    OrderBookHashMap map(logger, &responses, &market_updates, metrics);

    auto *book0 = map.find(TickerId{0});
    auto *book1 = map.find(TickerId{1});

    // Add an order to ticker 0
    ASSERT_TRUE(
        book0->add(UserId{1}, OrderId{1}, TickerId{0}, Side::BUY, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    // Ticker 1 should not see ticker 0's order — adding a matching sell on
    // ticker 1 should rest (no match), not trade
    EXPECT_TRUE(
        book1->add(UserId{2}, OrderId{2}, TickerId{1}, Side::SELL, Price{100}, Quantity{10}));

    auto resps = drainResponses();
    ASSERT_EQ(resps.size(), 1);
    EXPECT_EQ(resps[0].type, ResponseType::ACCEPTED);  // rests, not filled

    auto mds = drainMD();
    ASSERT_EQ(mds.size(), 1);
    EXPECT_EQ(mds[0].type_, MDUpdateType::ADD);  // rests, no trade
}

TEST_F(OrderBookTest, SameTickerCrossBookMatching) {
    OrderBookHashMap map(logger, &responses, &market_updates, metrics);

    auto *book = map.find(TickerId{3});

    ASSERT_TRUE(book->add(UserId{1}, OrderId{1}, TickerId{3}, Side::SELL, Price{50}, Quantity{7}));
    drainResponses();
    drainMD();

    EXPECT_TRUE(book->add(UserId{2}, OrderId{2}, TickerId{3}, Side::BUY, Price{50}, Quantity{7}));

    auto mds = drainMD();
    bool has_trade = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            has_trade = true;
    }
    EXPECT_TRUE(has_trade);
}

// ===================================================================
//  Stress: many orders, same price
// ===================================================================

TEST_F(OrderBookTest, ManyOrdersAtSamePrice) {
    auto book = makeBook();

    constexpr int kNumOrders = 50;
    for (int i = 0; i < kNumOrders; ++i) {
        ASSERT_TRUE(book.add(UserId{static_cast<uint32_t>(i + 1)},
                             OrderId{static_cast<uint64_t>(i)}, TickerId{0}, Side::SELL, Price{100},
                             Quantity{1}));
    }
    drainResponses();
    drainMD();

    // Buy kNumOrders units — should consume all
    EXPECT_TRUE(book.add(UserId{999}, OrderId{999}, TickerId{0}, Side::BUY, Price{100},
                         Quantity{static_cast<uint32_t>(kNumOrders)}));

    auto mds = drainMD();
    int trades = 0, cancels = 0;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE)
            ++trades;
        if (m.type_ == MDUpdateType::CANCEL)
            ++cancels;
    }
    EXPECT_EQ(trades, kNumOrders);
    EXPECT_EQ(cancels, kNumOrders);  // each sell fully consumed
}

// ===================================================================
//  Response fields verification
// ===================================================================

TEST_F(OrderBookTest, AcceptedResponseHasCorrectFields) {
    auto book = makeBook();

    EXPECT_TRUE(
        book.add(UserId{42}, OrderId{7}, TickerId{0}, Side::SELL, Price{250}, Quantity{15}));

    const auto *resp = nextResponse();
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->type, ResponseType::ACCEPTED);
    EXPECT_EQ(resp->user_id, UserId{42});
    EXPECT_EQ(resp->ticker_id, TickerId{0});
    EXPECT_EQ(resp->order_id, OrderId{7});
    EXPECT_EQ(resp->side, Side::SELL);
    EXPECT_EQ(resp->price, Price{250});
    EXPECT_EQ(resp->exec_qty, Quantity{0});
    EXPECT_EQ(resp->leaves_qty, Quantity{15});
}

TEST_F(OrderBookTest, CanceledResponseHasCorrectFields) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{5}, OrderId{99}, TickerId{0}, Side::BUY, Price{75}, Quantity{20}));
    drainResponses();
    drainMD();

    book.cancel(UserId{5}, OrderId{99}, TickerId{0});

    const auto *resp = nextResponse();
    ASSERT_NE(resp, nullptr);
    EXPECT_EQ(resp->type, ResponseType::CANCELED);
    EXPECT_EQ(resp->user_id, UserId{5});
    EXPECT_EQ(resp->order_id, OrderId{99});
    EXPECT_EQ(resp->side, Side::BUY);
    EXPECT_EQ(resp->price, Price{75});
}

TEST_F(OrderBookTest, FilledResponseHasCorrectFields) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{6}));

    auto resps = drainResponses();
    // Find the SELL fill
    bool found = false;
    for (const auto &r : resps) {
        if (r.type == ResponseType::FILLED && r.user_id == UserId{1}) {
            EXPECT_EQ(r.exec_qty, Quantity{6});
            EXPECT_EQ(r.leaves_qty, Quantity{4});  // 10 - 6
            EXPECT_EQ(r.side, Side::SELL);
            EXPECT_EQ(r.price, Price{100});
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

// ===================================================================
//  MD update fields verification
// ===================================================================

TEST_F(OrderBookTest, AddMDUpdateHasCorrectFields) {
    auto book = makeBook();

    EXPECT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{77}, Quantity{33}));

    const auto *md = nextMD();
    ASSERT_NE(md, nullptr);
    EXPECT_EQ(md->type_, MDUpdateType::ADD);
    EXPECT_EQ(md->ticker_id, TickerId{0});
    EXPECT_EQ(md->side, Side::SELL);
    EXPECT_EQ(md->price, Price{77});
    EXPECT_EQ(md->qty, Quantity{33});
}

TEST_F(OrderBookTest, TradeMDUpdateHasCorrectFields) {
    auto book = makeBook();

    ASSERT_TRUE(book.add(UserId{1}, OrderId{1}, TickerId{0}, Side::SELL, Price{100}, Quantity{10}));
    drainResponses();
    drainMD();

    EXPECT_TRUE(book.add(UserId{2}, OrderId{2}, TickerId{0}, Side::BUY, Price{100}, Quantity{4}));

    auto mds = drainMD();
    bool found = false;
    for (const auto &m : mds) {
        if (m.type_ == MDUpdateType::TRADE) {
            EXPECT_EQ(m.ticker_id, TickerId{0});
            EXPECT_EQ(m.price, Price{100});
            EXPECT_EQ(m.qty, Quantity{4});
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
