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
    OrdersAtPrice level(Side::BUY, Price(100), &o);

    EXPECT_EQ(level.side, Side::BUY);
    EXPECT_EQ(level.price, Price(100));
    EXPECT_EQ(level.first_order, &o);
}

TEST(OrdersAtPriceTest, ConstructedWithSingleOrderFormsSelfLoop) {
    Order o = makeOrder(Side::SELL, Price(200), Priority(1));
    OrdersAtPrice level(Side::SELL, Price(200), &o);

    // Single-order ring: order points to itself
    EXPECT_EQ(o.getNext(), &o);
    EXPECT_EQ(o.getPrev(), &o);
    EXPECT_TRUE(level.hasSingleOrder());
}

TEST(OrdersAtPriceTest, LinkedListPrevNextSetCorrectly) {
    Order o = makeOrder(Side::BUY, Price(50), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(50), &o);

    EXPECT_EQ(level.getPrevIdx(), nullptr);
    EXPECT_EQ(level.getNextIdx(), nullptr);
}

TEST(OrdersAtPriceTest, LinkedListPrevNextSetToNeighbors) {
    Order o = makeOrder(Side::BUY, Price(50), Priority(1));

    OrdersAtPrice dummyNext;

    OrdersAtPrice level(Side::BUY, Price(50), &o);
    level.prependIdx(&dummyNext);

    EXPECT_EQ(level.getNextIdx(), &dummyNext);
    EXPECT_EQ(dummyNext.getPrevIdx(), &level);
}

// ===================================================================
//  hasSingleOrder
// ===================================================================

TEST(OrdersAtPriceTest, HasSingleOrderTrueForOneOrder) {
    Order o = makeOrder(Side::BUY, Price(100), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(100), &o);

    EXPECT_TRUE(level.hasSingleOrder());
}

TEST(OrdersAtPriceTest, HasSingleOrderFalseAfterInsert) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(100), &o1);
    level.insert(&o2);

    EXPECT_FALSE(level.hasSingleOrder());
}

// ===================================================================
//  nextPriority
// ===================================================================

TEST(OrdersAtPriceTest, NextPriorityForSingleOrderIsTwo) {
    Order o = makeOrder(Side::SELL, Price(300), Priority(1));
    OrdersAtPrice level(Side::SELL, Price(300), &o);

    EXPECT_EQ(level.nextPriority(), Priority(2));
}

TEST(OrdersAtPriceTest, NextPriorityIncrementsAfterInsert) {
    Order o1 = makeOrder(Side::SELL, Price(300), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(300), Priority(2));

    OrdersAtPrice level(Side::SELL, Price(300), &o1);
    level.insert(&o2);

    EXPECT_EQ(level.nextPriority(), Priority(3));
}

TEST(OrdersAtPriceTest, NextPriorityAfterRemoveDecrementNotExpected) {
    // nextPriority is monotonic — removing an order does not decrease it
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));
    Order o3 = makeOrder(Side::BUY, Price(100), Priority(3));

    OrdersAtPrice level(Side::BUY, Price(100), &o1);
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

    OrdersAtPrice level(Side::BUY, Price(100), &o1);
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

    OrdersAtPrice level(Side::SELL, Price(500), &o1);
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

    OrdersAtPrice level(Side::BUY, Price(50), &o1);
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

    OrdersAtPrice level(Side::BUY, Price(100), &o1);
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

    OrdersAtPrice level(Side::SELL, Price(50), &o1);
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

    OrdersAtPrice level(Side::BUY, Price(75), &o1);
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

    OrdersAtPrice level(Side::BUY, Price(200), &o1);
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

    OrdersAtPrice level(Side::BUY, Price(42), &o1);
    level.insert(&o2);

    EXPECT_EQ(level.side, Side::BUY);
    EXPECT_EQ(o1.side, Side::BUY);
    EXPECT_EQ(o2.side, Side::BUY);
    EXPECT_EQ(level.price, Price(42));
}

TEST(OrdersAtPriceTest, AllOrdersAtLevelShareSameSideSell) {
    Order o1 = makeOrder(Side::SELL, Price(99), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(99), Priority(2));

    OrdersAtPrice level(Side::SELL, Price(99), &o1);
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
    OrdersAtPrice level(Side::BUY, Price(100), &o1);

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

    OrdersAtPrice level(Side::SELL, Price(10), &o1);
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
    OrdersAtPrice level(Side::BUY, Price(100), &o);

    EXPECT_DEATH(level.remove(&o), "hasSingleOrder");
}

TEST(OrdersAtPriceTestDeath, RemoveOrderWithWrongSideAsserts) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));
    Order oWrong = makeOrder(Side::SELL, Price(100), Priority(3));

    OrdersAtPrice level(Side::BUY, Price(100), &o1);
    level.insert(&o2);

    EXPECT_DEATH(level.remove(&oWrong), "side");
}

TEST(OrdersAtPriceTestDeath, RemoveOrderWithWrongPriceAsserts) {
    Order o1 = makeOrder(Side::SELL, Price(200), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(200), Priority(2));
    Order oWrong = makeOrder(Side::SELL, Price(999), Priority(3));

    OrdersAtPrice level(Side::SELL, Price(200), &o1);
    level.insert(&o2);

    EXPECT_DEATH(level.remove(&oWrong), "price");
}

TEST(OrdersAtPriceTestDeath, InsertWithWrongPriorityAsserts) {
    Order o1 = makeOrder(Side::BUY, Price(50), Priority(1));
    Order oBad = makeOrder(Side::BUY, Price(50), Priority(99));  // should be 2

    OrdersAtPrice level(Side::BUY, Price(50), &o1);

    EXPECT_DEATH(level.insert(&oBad), "priority");
}

TEST(OrdersAtPriceTestDeath, InsertWithWrongSideAsserts) {
    Order o1 = makeOrder(Side::BUY, Price(50), Priority(1));
    Order oBad = makeOrder(Side::SELL, Price(50), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(50), &o1);

    EXPECT_DEATH(level.insert(&oBad), "side");
}

TEST(OrdersAtPriceTestDeath, InsertWithWrongPriceAsserts) {
    Order o1 = makeOrder(Side::BUY, Price(50), Priority(1));
    Order oBad = makeOrder(Side::BUY, Price(999), Priority(2));

    OrdersAtPrice level(Side::BUY, Price(50), &o1);

    EXPECT_DEATH(level.insert(&oBad), "price");
}

#endif  // !defined(NDEBUG)

// ===================================================================
//  disconnect() — unlinking from both _idx and _ord chains
// ===================================================================

TEST(OrdersAtPriceTest, DisconnectUnlinksFromIdxChain) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(200), Priority(1));

    OrdersAtPrice level1(Side::BUY, Price(100), &o1);
    OrdersAtPrice level2(Side::BUY, Price(200), &o2);

    level1.prependIdx(&level2);
    level1.disconnect();

    EXPECT_EQ(level1.getNextIdx(), nullptr);
    EXPECT_EQ(level1.getPrevIdx(), nullptr);
    EXPECT_EQ(level2.getPrevIdx(), nullptr);
}

TEST(OrdersAtPriceTest, DisconnectUnlinksFromOrdChain) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(90), Priority(1));

    OrdersAtPrice level1(Side::BUY, Price(100), &o1);
    OrdersAtPrice level2(Side::BUY, Price(90), &o2);

    level1.insertAfterOrd(&level2);

    level1.disconnect();

    EXPECT_EQ(level1.getNextOrd(), nullptr);
    EXPECT_EQ(level1.getPrevOrd(), nullptr);
    EXPECT_EQ(level2.getNextOrd(), nullptr);
}

TEST(OrdersAtPriceTest, DisconnectNullsAllFourPointers) {
    Order o = makeOrder(Side::SELL, Price(50), Priority(1));
    OrdersAtPrice level(Side::SELL, Price(50), &o);

    level.disconnect();

    EXPECT_EQ(level.getNextIdx(), nullptr);
    EXPECT_EQ(level.getPrevIdx(), nullptr);
    EXPECT_EQ(level.getNextOrd(), nullptr);
    EXPECT_EQ(level.getPrevOrd(), nullptr);
}

TEST(OrdersAtPriceTest, DisconnectMiddleOfIdxChainPreservesLinks) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(200), Priority(1));
    Order o3 = makeOrder(Side::BUY, Price(300), Priority(1));

    OrdersAtPrice a(Side::BUY, Price(100), &o1);
    OrdersAtPrice b(Side::BUY, Price(200), &o2);
    OrdersAtPrice c(Side::BUY, Price(300), &o3);

    // Build: c -> b -> a (each prependIdx inserts this before the parameter)
    b.prependIdx(&a);  // b before a → b -> a
    c.prependIdx(&b);  // c before b → c -> b -> a

    b.disconnect();

    // After removing b: c <-> a
    EXPECT_EQ(c.getNextIdx(), &a);
    EXPECT_EQ(a.getPrevIdx(), &c);
    EXPECT_EQ(b.getNextIdx(), nullptr);
    EXPECT_EQ(b.getPrevIdx(), nullptr);
}

// ===================================================================
//  prependIdx — hash collision chain insertion
// ===================================================================

TEST(OrdersAtPriceTest, PrependIdxToEmptyBucket) {
    Order o = makeOrder(Side::BUY, Price(50), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(50), &o);

    level.prependIdx(nullptr);

    EXPECT_EQ(level.getNextIdx(), nullptr);
    EXPECT_EQ(level.getPrevIdx(), nullptr);
}

TEST(OrdersAtPriceTest, PrependIdxMultipleBuildsCorrectChain) {
    Order o1 = makeOrder(Side::BUY, Price(10), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(20), Priority(1));
    Order o3 = makeOrder(Side::BUY, Price(30), Priority(1));

    OrdersAtPrice a(Side::BUY, Price(10), &o1);
    OrdersAtPrice b(Side::BUY, Price(20), &o2);
    OrdersAtPrice c(Side::BUY, Price(30), &o3);

    b.prependIdx(&a);
    c.prependIdx(&b);

    EXPECT_EQ(c.getNextIdx(), &b);
    EXPECT_EQ(b.getPrevIdx(), &c);
    EXPECT_EQ(b.getNextIdx(), &a);
    EXPECT_EQ(a.getPrevIdx(), &b);
    EXPECT_EQ(a.getNextIdx(), nullptr);
}

// ===================================================================
//  insertAfterOrd — price-sorted chain insertion
// ===================================================================

TEST(OrdersAtPriceTest, InsertAfterOrdAfterHead) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(90), Priority(1));

    OrdersAtPrice head(Side::BUY, Price(100), &o1);
    OrdersAtPrice after(Side::BUY, Price(90), &o2);

    after.insertAfterOrd(&head);

    EXPECT_EQ(after.getPrevOrd(), &head);
    EXPECT_EQ(head.getNextOrd(), &after);
    EXPECT_EQ(after.getNextOrd(), nullptr);
}

TEST(OrdersAtPriceTest, InsertAfterOrdBetweenTwoNodes) {
    Order o1 = makeOrder(Side::SELL, Price(100), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(110), Priority(1));
    Order o3 = makeOrder(Side::SELL, Price(105), Priority(1));

    OrdersAtPrice a(Side::SELL, Price(100), &o1);
    OrdersAtPrice c(Side::SELL, Price(110), &o3);
    c.insertAfterOrd(&a);
    // Chain: a -> c

    OrdersAtPrice b(Side::SELL, Price(105), &o2);
    b.insertAfterOrd(&a);
    // Chain: a -> b -> c

    EXPECT_EQ(a.getNextOrd(), &b);
    EXPECT_EQ(b.getPrevOrd(), &a);
    EXPECT_EQ(b.getNextOrd(), &c);
    EXPECT_EQ(c.getPrevOrd(), &b);
}

TEST(OrdersAtPriceTest, InsertAfterOrdAtTail) {
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(90), Priority(1));
    Order o3 = makeOrder(Side::BUY, Price(80), Priority(1));

    OrdersAtPrice a(Side::BUY, Price(100), &o1);
    OrdersAtPrice b(Side::BUY, Price(90), &o2);
    OrdersAtPrice c(Side::BUY, Price(80), &o3);

    b.insertAfterOrd(&a);
    c.insertAfterOrd(&b);

    EXPECT_EQ(b.getNextOrd(), &c);
    EXPECT_EQ(c.getPrevOrd(), &b);
    EXPECT_EQ(c.getNextOrd(), nullptr);
}

// ===================================================================
//  getNextOrd / getPrevOrd — price-sorted chain accessors
// ===================================================================

TEST(OrdersAtPriceTest, GetNextOrdAndGetPrevOrdInitialState) {
    Order o = makeOrder(Side::BUY, Price(50), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(50), &o);

    EXPECT_EQ(level.getNextOrd(), nullptr);
    EXPECT_EQ(level.getPrevOrd(), nullptr);
}

// ===================================================================
//  OrdersAtPriceHashMap — construction & find
// ===================================================================

using exchange::OrdersAtPriceHashMap;

TEST(OrdersAtPriceHashMapTest, ConstructIsEmpty) {
    OrdersAtPriceHashMap map;

    EXPECT_EQ(map.bids(), nullptr);
    EXPECT_EQ(map.asks(), nullptr);
    EXPECT_EQ(map.find(Price(100)), nullptr);
}

TEST(OrdersAtPriceHashMapTest, FindNonExistentReturnsNull) {
    OrdersAtPriceHashMap map;
    EXPECT_EQ(map.find(Price(1)), nullptr);
    EXPECT_EQ(map.find(Price(999999)), nullptr);
}

// ===================================================================
//  OrdersAtPriceHashMap — insert
// ===================================================================

TEST(OrdersAtPriceHashMapTest, InsertSingleOrderCreatesPriceLevel) {
    OrdersAtPriceHashMap map;
    Order o = makeOrder(Side::BUY, Price(100), Priority(1));

    EXPECT_TRUE(map.insert(&o));

    auto *level = map.find(Price(100));
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->side, Side::BUY);
    EXPECT_EQ(level->price, Price(100));
    EXPECT_EQ(level->first_order, &o);
    EXPECT_TRUE(level->hasSingleOrder());
}

TEST(OrdersAtPriceHashMapTest, InsertSecondOrderAtSamePrice) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    auto *level = map.find(Price(100));
    ASSERT_NE(level, nullptr);
    EXPECT_FALSE(level->hasSingleOrder());
    EXPECT_EQ(level->first_order, &o1);
}

TEST(OrdersAtPriceHashMapTest, InsertOrdersAtDifferentPrices) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::SELL, Price(100), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(110), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    auto *l100 = map.find(Price(100));
    auto *l110 = map.find(Price(110));
    ASSERT_NE(l100, nullptr);
    ASSERT_NE(l110, nullptr);
    EXPECT_NE(l100, l110);
}

TEST(OrdersAtPriceHashMapTest, InsertBidsAndAsksIndependent) {
    OrdersAtPriceHashMap map;
    Order bid = makeOrder(Side::BUY, Price(100), Priority(1));
    Order ask = makeOrder(Side::SELL, Price(110), Priority(1));

    EXPECT_TRUE(map.insert(&bid));
    EXPECT_TRUE(map.insert(&ask));

    EXPECT_NE(map.bids(), nullptr);
    EXPECT_NE(map.asks(), nullptr);
    EXPECT_EQ(map.bids()->side, Side::BUY);
    EXPECT_EQ(map.asks()->side, Side::SELL);
}

// ===================================================================
//  OrdersAtPriceHashMap — remove
// ===================================================================

TEST(OrdersAtPriceHashMapTest, RemoveOrderLeavingOthersKeepsLevel) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));
    map.remove(&o1);

    auto *level = map.find(Price(100));
    ASSERT_NE(level, nullptr);
    EXPECT_TRUE(level->hasSingleOrder());
    EXPECT_EQ(level->first_order, &o2);
}

TEST(OrdersAtPriceHashMapTest, RemoveLastOrderDestroysPriceLevel) {
    OrdersAtPriceHashMap map;
    Order o = makeOrder(Side::SELL, Price(200), Priority(1));

    EXPECT_TRUE(map.insert(&o));
    map.remove(&o);

    EXPECT_EQ(map.find(Price(200)), nullptr);
}

TEST(OrdersAtPriceHashMapTest, RemoveLastBidAdvancesBidsHead) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(90), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    EXPECT_EQ(map.bids()->price, Price(100));
    map.remove(&o1);

    ASSERT_NE(map.bids(), nullptr);
    EXPECT_EQ(map.bids()->price, Price(90));
}

TEST(OrdersAtPriceHashMapTest, RemoveLastAskAdvancesAsksHead) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::SELL, Price(100), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(110), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    EXPECT_EQ(map.asks()->price, Price(100));
    map.remove(&o1);

    ASSERT_NE(map.asks(), nullptr);
    EXPECT_EQ(map.asks()->price, Price(110));
}

TEST(OrdersAtPriceHashMapTest, RemoveLastOrderEmptiesSide) {
    OrdersAtPriceHashMap map;
    Order o = makeOrder(Side::BUY, Price(100), Priority(1));

    EXPECT_TRUE(map.insert(&o));
    map.remove(&o);

    EXPECT_EQ(map.bids(), nullptr);
}

// ===================================================================
//  OrdersAtPriceHashMap — nextPriority
// ===================================================================

TEST(OrdersAtPriceHashMapTest, NextPriorityForNewPriceIsOne) {
    OrdersAtPriceHashMap map;
    EXPECT_EQ(map.nextPriority(Price(100)).first, Priority(1));
}

TEST(OrdersAtPriceHashMapTest, NextPriorityForExistingPriceIncrements) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_EQ(map.nextPriority(Price(100)).first, Priority(2));

    EXPECT_TRUE(map.insert(&o2));
    EXPECT_EQ(map.nextPriority(Price(100)).first, Priority(3));
}

// ===================================================================
//  OrdersAtPriceHashMap — price-sorted order (bids: descending)
// ===================================================================

TEST(OrdersAtPriceHashMapTest, BidsSortedDescendingHeadInsertion) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(90), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    ASSERT_NE(map.bids(), nullptr);
    EXPECT_EQ(map.bids()->price, Price(100));
    EXPECT_EQ(map.bids()->getNextOrd()->price, Price(90));
    EXPECT_EQ(map.bids()->getNextOrd()->getNextOrd(), nullptr);
}

TEST(OrdersAtPriceHashMapTest, BidsSortedDescendingTailInsertion) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(80), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    EXPECT_EQ(map.bids()->price, Price(100));
    EXPECT_EQ(map.bids()->getNextOrd()->price, Price(80));
    EXPECT_EQ(map.bids()->getNextOrd()->getNextOrd(), nullptr);
}

TEST(OrdersAtPriceHashMapTest, BidsSortedDescendingMiddleInsertion) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o3 = makeOrder(Side::BUY, Price(80), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(90), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o3));
    EXPECT_TRUE(map.insert(&o2));

    auto *b = map.bids();
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->price, Price(100));
    ASSERT_NE(b->getNextOrd(), nullptr);
    EXPECT_EQ(b->getNextOrd()->price, Price(90));
    ASSERT_NE(b->getNextOrd()->getNextOrd(), nullptr);
    EXPECT_EQ(b->getNextOrd()->getNextOrd()->price, Price(80));
    EXPECT_EQ(b->getNextOrd()->getNextOrd()->getNextOrd(), nullptr);
}

// ===================================================================
//  OrdersAtPriceHashMap — price-sorted order (asks: ascending)
// ===================================================================

TEST(OrdersAtPriceHashMapTest, AsksSortedAscendingHeadInsertion) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::SELL, Price(110), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(100), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    ASSERT_NE(map.asks(), nullptr);
    EXPECT_EQ(map.asks()->price, Price(100));
    EXPECT_EQ(map.asks()->getNextOrd()->price, Price(110));
    EXPECT_EQ(map.asks()->getNextOrd()->getNextOrd(), nullptr);
}

TEST(OrdersAtPriceHashMapTest, AsksSortedAscendingTailInsertion) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::SELL, Price(100), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(120), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    EXPECT_EQ(map.asks()->price, Price(100));
    EXPECT_EQ(map.asks()->getNextOrd()->price, Price(120));
    EXPECT_EQ(map.asks()->getNextOrd()->getNextOrd(), nullptr);
}

TEST(OrdersAtPriceHashMapTest, AsksSortedAscendingMiddleInsertion) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::SELL, Price(100), Priority(1));
    Order o3 = makeOrder(Side::SELL, Price(120), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(110), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o3));
    EXPECT_TRUE(map.insert(&o2));

    auto *a = map.asks();
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->price, Price(100));
    ASSERT_NE(a->getNextOrd(), nullptr);
    EXPECT_EQ(a->getNextOrd()->price, Price(110));
    ASSERT_NE(a->getNextOrd()->getNextOrd(), nullptr);
    EXPECT_EQ(a->getNextOrd()->getNextOrd()->price, Price(120));
    EXPECT_EQ(a->getNextOrd()->getNextOrd()->getNextOrd(), nullptr);
}

// ===================================================================
//  OrdersAtPriceHashMap — hash collision chains
// ===================================================================

TEST(OrdersAtPriceHashMapTest, HashCollisionDifferentPricesSameBucket) {
    // MAX_PRICE_LEVELS = 256; prices 100 and 356 (=100+256) hash to same bucket
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(356), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    auto *l100 = map.find(Price(100));
    auto *l356 = map.find(Price(356));

    ASSERT_NE(l100, nullptr);
    ASSERT_NE(l356, nullptr);
    EXPECT_NE(l100, l356);
    EXPECT_EQ(l100->price, Price(100));
    EXPECT_EQ(l356->price, Price(356));
}

TEST(OrdersAtPriceHashMapTest, RemoveFromCollisionChainUpdatesHead) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::SELL, Price(100), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(356), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    // o2 inserted second → its OrdersAtPrice is the idx-chain head
    map.remove(&o2);

    auto *l100 = map.find(Price(100));
    ASSERT_NE(l100, nullptr);
    EXPECT_EQ(l100->price, Price(100));
    EXPECT_EQ(map.find(Price(356)), nullptr);
}

// ===================================================================
//  OrdersAtPriceHashMap — integration scenarios
// ===================================================================

TEST(OrdersAtPriceHashMapTest, MultipleInsertsAndRemovesBidsAndAsks) {
    OrdersAtPriceHashMap map;

    Order b1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order b2 = makeOrder(Side::BUY, Price(95), Priority(1));
    Order b3 = makeOrder(Side::BUY, Price(105), Priority(1));
    Order a1 = makeOrder(Side::SELL, Price(110), Priority(1));
    Order a2 = makeOrder(Side::SELL, Price(108), Priority(1));
    Order a3 = makeOrder(Side::SELL, Price(115), Priority(1));

    EXPECT_TRUE(map.insert(&b1));
    EXPECT_TRUE(map.insert(&a1));
    EXPECT_TRUE(map.insert(&b2));
    EXPECT_TRUE(map.insert(&a2));
    EXPECT_TRUE(map.insert(&b3));
    EXPECT_TRUE(map.insert(&a3));

    // Bids: 105, 100, 95
    auto *bids = map.bids();
    ASSERT_NE(bids, nullptr);
    EXPECT_EQ(bids->price, Price(105));
    EXPECT_EQ(bids->getNextOrd()->price, Price(100));
    EXPECT_EQ(bids->getNextOrd()->getNextOrd()->price, Price(95));
    EXPECT_EQ(bids->getNextOrd()->getNextOrd()->getNextOrd(), nullptr);

    // Asks: 108, 110, 115
    auto *asks = map.asks();
    ASSERT_NE(asks, nullptr);
    EXPECT_EQ(asks->price, Price(108));
    EXPECT_EQ(asks->getNextOrd()->price, Price(110));
    EXPECT_EQ(asks->getNextOrd()->getNextOrd()->price, Price(115));
    EXPECT_EQ(asks->getNextOrd()->getNextOrd()->getNextOrd(), nullptr);

    map.remove(&b3);
    EXPECT_EQ(map.bids()->price, Price(100));
    map.remove(&a2);
    EXPECT_EQ(map.asks()->price, Price(110));
}

TEST(OrdersAtPriceHashMapTest, InsertRemoveCyclePreservesPriceSortedOrder) {
    OrdersAtPriceHashMap map;

    Order bids[5];
    for (int i = 0; i < 5; ++i) {
        bids[i] = makeOrder(Side::BUY, Price(100 - i * 10), Priority(1));
        EXPECT_TRUE(map.insert(&bids[i]));
    }

    // Verify: 100, 90, 80, 70, 60
    auto *b = map.bids();
    for (int i = 0; i < 5; ++i) {
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(b->price, Price(100 - i * 10));
        b = b->getNextOrd();
    }
    EXPECT_EQ(b, nullptr);

    // Remove the middle (80)
    map.remove(&bids[2]);

    // Verify: 100, 90, 70, 60
    b = map.bids();
    EXPECT_EQ(b->price, Price(100));
    EXPECT_EQ(b->getNextOrd()->price, Price(90));
    EXPECT_EQ(b->getNextOrd()->getNextOrd()->price, Price(70));
    EXPECT_EQ(b->getNextOrd()->getNextOrd()->getNextOrd()->price, Price(60));
    EXPECT_EQ(b->getNextOrd()->getNextOrd()->getNextOrd()->getNextOrd(), nullptr);
}

TEST(OrdersAtPriceHashMapTest, MultipleOrdersSamePriceRingCorrectness) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::BUY, Price(100), Priority(1));
    Order o2 = makeOrder(Side::BUY, Price(100), Priority(2));
    Order o3 = makeOrder(Side::BUY, Price(100), Priority(3));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));
    EXPECT_TRUE(map.insert(&o3));

    auto *level = map.find(Price(100));
    ASSERT_NE(level, nullptr);
    EXPECT_EQ(level->first_order, &o1);
    EXPECT_EQ(o1.getNext(), &o2);
    EXPECT_EQ(o2.getNext(), &o3);
    EXPECT_EQ(o3.getNext(), &o1);

    map.remove(&o2);
    EXPECT_EQ(o1.getNext(), &o3);
    EXPECT_EQ(o3.getNext(), &o1);
}

TEST(OrdersAtPriceHashMapTest, BidAndAskAtClosePricesAreIndependent) {
    // Note: bid and ask at the exact same price would hash-collide and
    // find() currently matches on price alone (not side), so they can't
    // coexist at identical prices. In practice this is fine — same-price
    // bid/ask would cross the spread and trade immediately.
    OrdersAtPriceHashMap map;
    Order bid = makeOrder(Side::BUY, Price(99), Priority(1));
    Order ask = makeOrder(Side::SELL, Price(101), Priority(1));

    EXPECT_TRUE(map.insert(&bid));
    EXPECT_TRUE(map.insert(&ask));

    EXPECT_NE(map.bids(), nullptr);
    EXPECT_NE(map.asks(), nullptr);
    EXPECT_EQ(map.bids()->side, Side::BUY);
    EXPECT_EQ(map.asks()->side, Side::SELL);
    EXPECT_NE(map.find(Price(99)), nullptr);
    EXPECT_NE(map.find(Price(101)), nullptr);
}

TEST(OrdersAtPriceHashMapTest, EmptySideAfterAllRemovals) {
    OrdersAtPriceHashMap map;
    Order o1 = makeOrder(Side::SELL, Price(100), Priority(1));
    Order o2 = makeOrder(Side::SELL, Price(110), Priority(1));
    Order o3 = makeOrder(Side::SELL, Price(120), Priority(1));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));
    EXPECT_TRUE(map.insert(&o3));

    EXPECT_NE(map.asks(), nullptr);

    map.remove(&o1);
    map.remove(&o2);
    map.remove(&o3);

    EXPECT_EQ(map.asks(), nullptr);
    EXPECT_EQ(map.bids(), nullptr);
}

// ===================================================================
//  OrdersAtPriceHashMap — const-correct access
// ===================================================================

TEST(OrdersAtPriceHashMapTest, BidsAndAsksAreConstAccessible) {
    OrdersAtPriceHashMap map;
    Order o = makeOrder(Side::BUY, Price(50), Priority(1));
    EXPECT_TRUE(map.insert(&o));

    const auto &cmap = map;
    EXPECT_EQ(cmap.bids()->price, Price(50));
    EXPECT_EQ(cmap.asks(), nullptr);
}

// ===================================================================
//  Death tests — insertAfterOrd assertion
// ===================================================================

#if !defined(NDEBUG)

TEST(OrdersAtPriceHashMapTestDeath, InsertAfterOrdAssertsOnNull) {
    Order o = makeOrder(Side::BUY, Price(50), Priority(1));
    OrdersAtPrice level(Side::BUY, Price(50), &o);

    EXPECT_DEATH(level.insertAfterOrd(nullptr), "after");
}

#endif  // !defined(NDEBUG)
