#include <gtest/gtest.h>

#include "lib/exchange/order.h"
#include "lib/exchange/orders_at_price.h"

using exchange::Order;
using exchange::OrdersAtPrice;
using exchange::Price;
using exchange::Priority;
using exchange::Side;

// ===================================================================
//  Helpers
// ===================================================================

namespace {

/// Creates a minimally-initialized Order for testing OrdersAtPrice.
/// Only the fields relevant to OrdersAtPrice are set.
Order makeOrder(Side side, Price price, Priority prio) noexcept {
    return Order{exchange::TickerId(0),
                 exchange::UserId(0),
                 exchange::OrderId(0),
                 exchange::MarketOrderId(0),
                 side,
                 price,
                 exchange::Quantity(1),
                 prio};
}

}  // namespace

// ===================================================================
//  Construction
// ===================================================================

TEST(OrdersAtPriceTest, DefaultConstructHasNullFirstOrder) {
    OrdersAtPrice level;
    EXPECT_EQ(level.first_order, nullptr);
    EXPECT_EQ(level.side, Side::INVALID);
    EXPECT_EQ(level.price, Price::INVALID);
}

TEST(OrdersAtPriceTest, ParameterizedConstructorSetsSideAndPrice) {
    Order o = makeOrder(Side::BUY, Price(100), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(100), &o, nullptr, nullptr);

    EXPECT_EQ(level.side, Side::BUY);
    EXPECT_EQ(level.price, Price(100));
    EXPECT_EQ(level.first_order, &o);
}

TEST(OrdersAtPriceTest, ConstructedWithSingleOrderFormsSelfLoop) {
    Order o = makeOrder(Side::SELL, Price(200), Priority(1));
    OrdersAtPrice level(Side::SELL, Price(200), &o, nullptr, nullptr);

    // Single-order ring: order points to itself
    EXPECT_EQ(o.getNext(), &o);
    EXPECT_EQ(o.getPrev(), &o);
    EXPECT_TRUE(level.hasSingleOrder());
}

TEST(OrdersAtPriceTest, LinkedListPrevNextSetCorrectly) {
    Order o = makeOrder(Side::BUY, Price(50), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(50), &o, nullptr, nullptr);

    EXPECT_EQ(level.prev, nullptr);
    EXPECT_EQ(level.next, nullptr);
}

TEST(OrdersAtPriceTest, LinkedListPrevNextSetToNeighbors) {
    Order o = makeOrder(Side::BUY, Price(50), Priority(1));

    // Dummy neighbors to verify the pointers are forwarded to LinkedList
    OrdersAtPrice dummyPrev;
    OrdersAtPrice dummyNext;

    OrdersAtPrice level(Side::BUY, Price(50), &o, &dummyPrev, &dummyNext);

    EXPECT_EQ(level.prev, &dummyPrev);
    EXPECT_EQ(level.next, &dummyNext);
}

// ===================================================================
//  hasSingleOrder
// ===================================================================

TEST(OrdersAtPriceTest, HasSingleOrderTrueForOneOrder) {
    Order o = makeOrder(Side::BUY, Price(100), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(100), &o, nullptr, nullptr);

    EXPECT_TRUE(level.hasSingleOrder());
}

TEST(OrdersAtPriceTest, HasSingleOrderFalseAfterInsert) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(100), &o1, nullptr, nullptr);
    level.insert(&o2);

    EXPECT_FALSE(level.hasSingleOrder());
}

// ===================================================================
//  nextPriority
// ===================================================================

TEST(OrdersAtPriceTest, NextPriorityForSingleOrderIsTwo) {
    Order o = makeOrder(Side::SELL, Price(300), Priority(1));
    OrdersAtPrice level(Side::SELL, Price(300), &o, nullptr, nullptr);

    EXPECT_EQ(level.nextPriority(), Priority(2));
}

TEST(OrdersAtPriceTest, NextPriorityIncrementsAfterInsert) {
    Order o1 = makeOrder(Side::SELL, Price(300), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(300), Priority(2));

    OrdersAtPrice level(Side::SELL, Price(300), &o1, nullptr, nullptr);
    level.insert(&o2);

    EXPECT_EQ(level.nextPriority(), Priority(3));
}

TEST(OrdersAtPriceTest, NextPriorityAfterRemoveDecrementNotExpected) {
    // nextPriority is monotonic — removing an order does not decrease it
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));
    Order o3 = makeOrder(Side::BUY, Price(100), Priority(3));

    OrdersAtPrice level(Side::BUY, Price(100), &o1, nullptr, nullptr);
    level.insert(&o2);
    level.insert(&o3);
    EXPECT_EQ(level.nextPriority(), Priority(4));

    level.remove(&o2);
    // o3 is still the last order with priority 3
    EXPECT_EQ(level.nextPriority(), Priority(4));
}

// ===================================================================
//  insert
// ===================================================================

TEST(OrdersAtPriceTest, InsertAddsOrderAtBack) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(100), &o1, nullptr, nullptr);
    level.insert(&o2);

    // first_order still points to o1 (oldest)
    EXPECT_EQ(level.first_order, &o1);

    // o2 is after o1 (the newest/last)
    EXPECT_EQ(o1.getNext(), &o2);
    EXPECT_EQ(o2.getPrev(), &o1);

    // Ring is circular
    EXPECT_EQ(o2.getNext(), &o1);
    EXPECT_EQ(o1.getPrev(), &o2);
}

TEST(OrdersAtPriceTest, InsertMultipleOrdersPreservesFifoOrder) {
    Order o1 = makeOrder(Side::SELL, Price(500), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(500), Priority(2));
    Order o3 = makeOrder(Side::SELL, Price(500), Priority(3));

    OrdersAtPrice level(Side::SELL, Price(500), &o1, nullptr, nullptr);
    level.insert(&o2);
    level.insert(&o3);

    // Traverse forward: oldest to newest
    EXPECT_EQ(level.first_order, &o1);
    EXPECT_EQ(o1.getNext(), &o2);
    EXPECT_EQ(o2.getNext(), &o3);
    EXPECT_EQ(o3.getNext(), &o1);  // back to head

    // Traverse backward: newest to oldest
    EXPECT_EQ(o3.getPrev(), &o2);
    EXPECT_EQ(o2.getPrev(), &o1);
    EXPECT_EQ(o1.getPrev(), &o3);
}

TEST(OrdersAtPriceTest, InsertUpdatesFirstOrderPrev) {
    Order o1 = makeOrder(Side::BUY, Price(50), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(50), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(50), &o1, nullptr, nullptr);
    level.insert(&o2);

    // first_order->getPrev() should point to the newest order
    EXPECT_EQ(level.first_order->getPrev(), &o2);
}

// ===================================================================
//  remove (multi-order case)
// ===================================================================

TEST(OrdersAtPriceTest, RemoveMiddleOrderPreservesRing) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));
    Order o3 = makeOrder(Side::BUY, Price(100), Priority(3));

    OrdersAtPrice level(Side::BUY, Price(100), &o1, nullptr, nullptr);
    level.insert(&o2);
    level.insert(&o3);

    level.remove(&o2);

    EXPECT_FALSE(level.hasSingleOrder());
    EXPECT_EQ(level.first_order, &o1);  // anchor unchanged

    // Ring now: o1 <-> o3
    EXPECT_EQ(o1.getNext(), &o3);
    EXPECT_EQ(o3.getPrev(), &o1);
    EXPECT_EQ(o3.getNext(), &o1);
    EXPECT_EQ(o1.getPrev(), &o3);

    // o2 is disconnected
    EXPECT_EQ(o2.getNext(), nullptr);
    EXPECT_EQ(o2.getPrev(), nullptr);
}

TEST(OrdersAtPriceTest, RemoveLastOrderAdvancesFirstOrder) {
    Order o1 = makeOrder(Side::SELL, Price(50), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(50), Priority(2));
    Order o3 = makeOrder(Side::SELL, Price(50), Priority(3));

    OrdersAtPrice level(Side::SELL, Price(50), &o1, nullptr, nullptr);
    level.insert(&o2);
    level.insert(&o3);

    level.remove(&o3);

    // first_order unchanged — o3 was at the back
    EXPECT_EQ(level.first_order, &o1);
}

TEST(OrdersAtPriceTest, RemoveFirstOrderAdvancesAnchor) {
    Order o1 = makeOrder(Side::BUY, Price(75), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(75), Priority(2));
    Order o3 = makeOrder(Side::BUY, Price(75), Priority(3));

    OrdersAtPrice level(Side::BUY, Price(75), &o1, nullptr, nullptr);
    level.insert(&o2);
    level.insert(&o3);

    level.remove(&o1);

    // Anchor advances to o2
    EXPECT_EQ(level.first_order, &o2);
    EXPECT_FALSE(level.hasSingleOrder());

    // Ring: o2 <-> o3
    EXPECT_EQ(o2.getNext(), &o3);
    EXPECT_EQ(o3.getPrev(), &o2);
    EXPECT_EQ(o3.getNext(), &o2);
    EXPECT_EQ(o2.getPrev(), &o3);
}

TEST(OrdersAtPriceTest, RemoveReducesToSingleOrderCorrectly) {
    Order o1 = makeOrder(Side::BUY, Price(200), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(200), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(200), &o1, nullptr, nullptr);
    level.insert(&o2);

    level.remove(&o2);

    EXPECT_TRUE(level.hasSingleOrder());
    EXPECT_EQ(level.first_order, &o1);
    EXPECT_EQ(o1.getNext(), &o1);
    EXPECT_EQ(o1.getPrev(), &o1);
}

// ===================================================================
//  Side and price consistency
// ===================================================================

TEST(OrdersAtPriceTest, AllOrdersAtLevelShareSameSideBuy) {
    Order o1 = makeOrder(Side::BUY, Price(42), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(42), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(42), &o1, nullptr, nullptr);
    level.insert(&o2);

    EXPECT_EQ(level.side, Side::BUY);
    EXPECT_EQ(o1.side, Side::BUY);
    EXPECT_EQ(o2.side, Side::BUY);
    EXPECT_EQ(level.price, Price(42));
}

TEST(OrdersAtPriceTest, AllOrdersAtLevelShareSameSideSell) {
    Order o1 = makeOrder(Side::SELL, Price(99), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(99), Priority(2));

    OrdersAtPrice level(Side::SELL, Price(99), &o1, nullptr, nullptr);
    level.insert(&o2);

    EXPECT_EQ(level.side, Side::SELL);
    EXPECT_EQ(o1.side, Side::SELL);
    EXPECT_EQ(o2.side, Side::SELL);
    EXPECT_EQ(level.price, Price(99));
}

// ===================================================================
//  Integration scenarios
// ===================================================================

TEST(OrdersAtPriceTest, InsertRemoveCyclePreservesCorrectness) {
    // Simulate a series of insertions and removals to ensure
    // the ring stays consistent under churn.
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(100), &o1, nullptr, nullptr);

    // Insert 5 more orders
    Order orders[5];
    for (int i = 0; i < 5; ++i) {
        orders[i] = makeOrder(Side::BUY, Price(100), Priority(i + 2));
        level.insert(&orders[i]);
    }
    // 6 orders total
    EXPECT_FALSE(level.hasSingleOrder());
    EXPECT_EQ(level.nextPriority(), Priority(7));

    // Remove orders 1, 2, 3 (the middle ones)
    level.remove(&orders[1]);
    level.remove(&orders[2]);
    level.remove(&orders[3]);
    EXPECT_FALSE(level.hasSingleOrder());

    // Verify traversal: should be o1, orders[0], orders[4]
    EXPECT_EQ(level.first_order, &o1);
    EXPECT_EQ(o1.getNext(), &orders[0]);
    EXPECT_EQ(orders[0].getNext(), &orders[4]);
    EXPECT_EQ(orders[4].getNext(), &o1);

    // Backward traversal
    EXPECT_EQ(o1.getPrev(), &orders[4]);
    EXPECT_EQ(orders[4].getPrev(), &orders[0]);
    EXPECT_EQ(orders[0].getPrev(), &o1);
}

TEST(OrdersAtPriceTest, RemoveFirstThenInsertNewOrders) {
    // Remove the anchor order, then add new ones — verify anchor is correct
    Order o1 = makeOrder(Side::SELL, Price(10), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(10), Priority(2));
    Order o3 = makeOrder(Side::SELL, Price(10), Priority(3));

    OrdersAtPrice level(Side::SELL, Price(10), &o1, nullptr, nullptr);
    level.insert(&o2);
    level.insert(&o3);

    // Remove the anchor
    level.remove(&o1);
    EXPECT_EQ(level.first_order, &o2);

    // Add a new order — should go to the back
    Order o4 = makeOrder(Side::SELL, Price(10), Priority(4));
    level.insert(&o4);

    EXPECT_EQ(level.first_order, &o2);
    EXPECT_EQ(o2.getNext(), &o3);
    EXPECT_EQ(o3.getNext(), &o4);
    EXPECT_EQ(o4.getNext(), &o2);
    EXPECT_EQ(level.nextPriority(), Priority(5));
}

// ===================================================================
//  Death tests — assertion violations
// ===================================================================

#if !defined(NDEBUG)  // Death tests only work in debug builds (asserts are active)

TEST(OrdersAtPriceTestDeath, RemoveSingleOrderAsserts) {
    Order o = makeOrder(Side::BUY, Price(100), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(100), &o, nullptr, nullptr);

    EXPECT_DEATH(level.remove(&o), "hasSingleOrder");
}

TEST(OrdersAtPriceTestDeath, RemoveOrderWithWrongSideAsserts) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));
    Order oWrong = makeOrder(Side::SELL, Price(100), Priority(3));

    OrdersAtPrice level(Side::BUY, Price(100), &o1, nullptr, nullptr);
    level.insert(&o2);

    EXPECT_DEATH(level.remove(&oWrong), "side");
}

TEST(OrdersAtPriceTestDeath, RemoveOrderWithWrongPriceAsserts) {
    Order o1 = makeOrder(Side::SELL, Price(200), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(200), Priority(2));
    Order oWrong = makeOrder(Side::SELL, Price(999), Priority(3));

    OrdersAtPrice level(Side::SELL, Price(200), &o1, nullptr, nullptr);
    level.insert(&o2);

    EXPECT_DEATH(level.remove(&oWrong), "price");
}

TEST(OrdersAtPriceTestDeath, InsertWithWrongPriorityAsserts) {
    Order o1 = makeOrder(Side::BUY, Price(50), Priority(1));
    Order oBad = makeOrder(Side::BUY, Price(50), Priority(99));  // should be 2

    OrdersAtPrice level(Side::BUY, Price(50), &o1, nullptr, nullptr);

    EXPECT_DEATH(level.insert(&oBad), "priority");
}

TEST(OrdersAtPriceTestDeath, InsertWithWrongSideAsserts) {
    Order o1 = makeOrder(Side::BUY, Price(50), Priority(1));
    Order oBad = makeOrder(Side::SELL, Price(50), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(50), &o1, nullptr, nullptr);

    EXPECT_DEATH(level.insert(&oBad), "side");
}

TEST(OrdersAtPriceTestDeath, InsertWithWrongPriceAsserts) {
    Order o1 = makeOrder(Side::BUY, Price(50), Priority(1));
    Order oBad = makeOrder(Side::BUY, Price(999), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(50), &o1, nullptr, nullptr);

    EXPECT_DEATH(level.insert(&oBad), "price");
}

#endif  // !defined(NDEBUG)
