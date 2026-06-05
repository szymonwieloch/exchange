#include <gtest/gtest.h>

#include <cstddef>

#include "lib/exchange/constants.h"
#include "lib/exchange/order.h"
#include "lib/exchange/user_orders.h"

using exchange::MAX_ACTIVE_USERS;
using exchange::MAX_ORDERS_PER_USER;
using exchange::Order;
using exchange::OrderId;
using exchange::UserId;
using exchange::UserOrderHashMap;
using exchange::UserOrders;

// ── Helper: create an Order with just the fields UserOrders cares about ──
namespace {

Order makeOrder(UserId uid, OrderId oid) noexcept {
    return Order{exchange::TickerId(0),
                 uid,
                 oid,
                 exchange::MarketOrderId(0),
                 exchange::Side::BUY,
                 exchange::Price(100),
                 exchange::Quantity(1),
                 exchange::Priority(0),
                 nullptr,
                 nullptr};
}

}  // namespace

// ===================================================================
//  UserOrders
// ===================================================================

TEST(UserOrdersTest, ConstructEmpty) {
    UserOrders uo(UserId(42));
    EXPECT_EQ(uo.user_id, UserId(42));
    EXPECT_TRUE(uo.empty());
    EXPECT_FALSE(uo.full());
    EXPECT_EQ(uo.size(), 0);
}

TEST(UserOrdersTest, InsertSingleOrder) {
    UserOrders uo(UserId(1));
    Order o = makeOrder(UserId(1), OrderId(5));

    uo.insert(&o);

    EXPECT_FALSE(uo.empty());
    EXPECT_EQ(uo.size(), 1);
    EXPECT_EQ(uo.find(OrderId(5)), &o);
}

TEST(UserOrdersTest, InsertMultipleOrders) {
    UserOrders uo(UserId(1));
    Order o0 = makeOrder(UserId(1), OrderId(0));
    Order o1 = makeOrder(UserId(1), OrderId(1));
    Order o2 = makeOrder(UserId(1), OrderId(3));

    uo.insert(&o0);
    uo.insert(&o1);
    uo.insert(&o2);

    EXPECT_EQ(uo.size(), 3);
    EXPECT_EQ(uo.find(OrderId(0)), &o0);
    EXPECT_EQ(uo.find(OrderId(1)), &o1);
    EXPECT_EQ(uo.find(OrderId(3)), &o2);
}

TEST(UserOrdersTest, FindEmptySlotReturnsNull) {
    UserOrders uo(UserId(1));
    Order o = makeOrder(UserId(1), OrderId(5));
    uo.insert(&o);

    // Slot 7 is still empty
    EXPECT_EQ(uo.find(OrderId(7)), nullptr);
}

TEST(UserOrdersTest, FindOutOfRangeReturnsNull) {
    UserOrders uo(UserId(1));
    // OrderId >= MAX_ORDERS_PER_USER should return nullptr gracefully
    EXPECT_EQ(uo.find(OrderId(MAX_ORDERS_PER_USER + 100)), nullptr);
}

TEST(UserOrdersTest, RemoveOrder) {
    UserOrders uo(UserId(1));
    Order o0 = makeOrder(UserId(1), OrderId(0));
    Order o1 = makeOrder(UserId(1), OrderId(1));

    uo.insert(&o0);
    uo.insert(&o1);
    EXPECT_EQ(uo.size(), 2);

    uo.remove(OrderId(0));
    EXPECT_EQ(uo.size(), 1);
    EXPECT_EQ(uo.find(OrderId(0)), nullptr);
    EXPECT_EQ(uo.find(OrderId(1)), &o1);  // other order unaffected
}

TEST(UserOrdersTest, RemoveLastOrderMakesEmpty) {
    UserOrders uo(UserId(1));
    Order o = makeOrder(UserId(1), OrderId(0));
    uo.insert(&o);

    uo.remove(OrderId(0));
    EXPECT_TRUE(uo.empty());
    EXPECT_EQ(uo.size(), 0);
    EXPECT_EQ(uo.find(OrderId(0)), nullptr);
}

TEST(UserOrdersTest, InsertAfterRemoveReusesSlot) {
    UserOrders uo(UserId(1));
    Order o1 = makeOrder(UserId(1), OrderId(7));
    Order o2 = makeOrder(UserId(1), OrderId(7));

    uo.insert(&o1);
    uo.remove(OrderId(7));
    EXPECT_EQ(uo.find(OrderId(7)), nullptr);

    // Re-insert into the same slot
    uo.insert(&o2);
    EXPECT_EQ(uo.size(), 1);
    EXPECT_EQ(uo.find(OrderId(7)), &o2);
}

TEST(UserOrdersTest, SizeReflectsInsertAndRemove) {
    UserOrders uo(UserId(1));
    Order orders[5] = {
        makeOrder(UserId(1), OrderId(0)), makeOrder(UserId(1), OrderId(1)),
        makeOrder(UserId(1), OrderId(2)), makeOrder(UserId(1), OrderId(3)),
        makeOrder(UserId(1), OrderId(4)),
    };

    for (int i = 0; i < 5; ++i) {
        uo.insert(&orders[i]);
        EXPECT_EQ(uo.size(), static_cast<std::size_t>(i + 1));
    }

    for (int i = 4; i >= 0; --i) {
        uo.remove(OrderId(i));
        EXPECT_EQ(uo.size(), static_cast<std::size_t>(i));
    }
    EXPECT_TRUE(uo.empty());
}

TEST(UserOrdersTest, FullWhenMaxOrdersReached) {
    UserOrders uo(UserId(1));

    // Allocate MAX_ORDERS_PER_USER orders on the stack — fine for 1024
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    Order orders[MAX_ORDERS_PER_USER];
    for (std::size_t i = 0; i < MAX_ORDERS_PER_USER; ++i) {
        orders[i] = makeOrder(UserId(1), OrderId(i));
    }

    for (std::size_t i = 0; i < MAX_ORDERS_PER_USER; ++i) {
        EXPECT_FALSE(uo.full());
        uo.insert(&orders[i]);
    }

    EXPECT_TRUE(uo.full());
    EXPECT_EQ(uo.size(), MAX_ORDERS_PER_USER);
}

TEST(UserOrdersTest, LinkedListPrevNextAreNullByDefault) {
    UserOrders uo(UserId(1));
    EXPECT_EQ(uo.prev, nullptr);
    EXPECT_EQ(uo.next, nullptr);
}

// ===================================================================
//  UserOrderHashMap
// ===================================================================

TEST(UserOrderHashMapTest, ConstructEmpty) {
    UserOrderHashMap map;
    // Nothing to assert on construction — no crash is the test
    SUCCEED();
}

TEST(UserOrderHashMapTest, FindNonExistentUserReturnsNull) {
    UserOrderHashMap map;
    EXPECT_EQ(map.find(UserId(1), OrderId(0)), nullptr);
}

TEST(UserOrderHashMapTest, InsertOrderForNewUser) {
    UserOrderHashMap map;
    Order o = makeOrder(UserId(1), OrderId(0));

    bool ok = map.insert(&o);
    EXPECT_TRUE(ok);
    EXPECT_EQ(map.find(UserId(1), OrderId(0)), &o);
}

TEST(UserOrderHashMapTest, InsertMultipleOrdersForSameUser) {
    UserOrderHashMap map;
    Order o0 = makeOrder(UserId(7), OrderId(0));
    Order o1 = makeOrder(UserId(7), OrderId(1));
    Order o2 = makeOrder(UserId(7), OrderId(5));

    EXPECT_TRUE(map.insert(&o0));
    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));

    EXPECT_EQ(map.find(UserId(7), OrderId(0)), &o0);
    EXPECT_EQ(map.find(UserId(7), OrderId(1)), &o1);
    EXPECT_EQ(map.find(UserId(7), OrderId(5)), &o2);

    // Non-existent order for the same user
    EXPECT_EQ(map.find(UserId(7), OrderId(99)), nullptr);
}

TEST(UserOrderHashMapTest, InsertForDifferentUsers) {
    UserOrderHashMap map;
    Order o1 = makeOrder(UserId(10), OrderId(0));
    Order o2 = makeOrder(UserId(20), OrderId(1));
    Order o3 = makeOrder(UserId(30), OrderId(0));

    EXPECT_TRUE(map.insert(&o1));
    EXPECT_TRUE(map.insert(&o2));
    EXPECT_TRUE(map.insert(&o3));

    EXPECT_EQ(map.find(UserId(10), OrderId(0)), &o1);
    EXPECT_EQ(map.find(UserId(20), OrderId(1)), &o2);
    EXPECT_EQ(map.find(UserId(30), OrderId(0)), &o3);

    // Cross-user lookup should not find another user's order
    EXPECT_EQ(map.find(UserId(10), OrderId(1)), nullptr);
    EXPECT_EQ(map.find(UserId(20), OrderId(0)), nullptr);
}

TEST(UserOrderHashMapTest, RemoveOrder) {
    UserOrderHashMap map;
    Order o0 = makeOrder(UserId(1), OrderId(0));
    Order o1 = makeOrder(UserId(1), OrderId(1));

    EXPECT_TRUE(map.insert(&o0));
    EXPECT_TRUE(map.insert(&o1));

    map.remove(UserId(1), OrderId(0));

    EXPECT_EQ(map.find(UserId(1), OrderId(0)), nullptr);
    EXPECT_EQ(map.find(UserId(1), OrderId(1)), &o1);  // still present
}

TEST(UserOrderHashMapTest, RemoveLastOrderDeallocatesUser) {
    UserOrderHashMap map;
    Order o = makeOrder(UserId(99), OrderId(7));

    EXPECT_TRUE(map.insert(&o));
    map.remove(UserId(99), OrderId(7));

    // User should be gone now — find returns nullptr
    EXPECT_EQ(map.find(UserId(99), OrderId(7)), nullptr);
}

TEST(UserOrderHashMapTest, ReinsertAfterRemoval) {
    UserOrderHashMap map;
    Order o1 = makeOrder(UserId(1), OrderId(0));
    Order o2 = makeOrder(UserId(1), OrderId(0));

    EXPECT_TRUE(map.insert(&o1));
    map.remove(UserId(1), OrderId(0));

    // Re-insert — allocates a new UserOrders from the pool
    EXPECT_TRUE(map.insert(&o2));
    EXPECT_EQ(map.find(UserId(1), OrderId(0)), &o2);
}

TEST(UserOrderHashMapTest, RemoveOrderKeepsOtherUsersIntact) {
    UserOrderHashMap map;
    Order oA = makeOrder(UserId(100), OrderId(0));
    Order oB = makeOrder(UserId(200), OrderId(1));

    EXPECT_TRUE(map.insert(&oA));
    EXPECT_TRUE(map.insert(&oB));

    map.remove(UserId(100), OrderId(0));

    EXPECT_EQ(map.find(UserId(100), OrderId(0)), nullptr);
    EXPECT_EQ(map.find(UserId(200), OrderId(1)), &oB);  // unaffected
}

TEST(UserOrderHashMapTest, RemoveNonExistentUserAsserts) {
    UserOrderHashMap map;
    // Removing from a user that doesn't exist is a precondition violation
    EXPECT_DEATH(map.remove(UserId(1), OrderId(0)), ".*");
}

TEST(UserOrderHashMapTest, HashCollisionSameBucket) {
    // Two users whose IDs hash to the same bucket should coexist via chaining.
    // Since SIZE=256, users with IDs differing by 256 will collide.
    UserOrderHashMap map;

    UserId uA(10);
    UserId uB(10 + 256);  // same bucket as uA

    Order oA = makeOrder(uA, OrderId(0));
    Order oB = makeOrder(uB, OrderId(1));

    EXPECT_TRUE(map.insert(&oA));
    EXPECT_TRUE(map.insert(&oB));

    EXPECT_EQ(map.find(uA, OrderId(0)), &oA);
    EXPECT_EQ(map.find(uB, OrderId(1)), &oB);
}

TEST(UserOrderHashMapTest, HashCollisionRemoveHeadOfChain) {
    // When two users share a bucket, removing the head should leave the
    // second user reachable.
    UserOrderHashMap map;

    UserId uA(10);
    UserId uB(10 + 256);  // same bucket

    Order oA = makeOrder(uA, OrderId(0));
    Order oB = makeOrder(uB, OrderId(1));

    EXPECT_TRUE(map.insert(&oA));
    EXPECT_TRUE(map.insert(&oB));

    // Remove the later-inserted user (head of chain, since insert prepends)
    // uB was inserted second, so it's at the head
    map.remove(uB, OrderId(1));

    EXPECT_EQ(map.find(uB, OrderId(1)), nullptr);
    EXPECT_EQ(map.find(uA, OrderId(0)), &oA);  // still reachable
}

TEST(UserOrderHashMapTest, InsertReturnsFalseWhenUserPoolExhausted) {
    UserOrderHashMap map;

    // Fill all active-user slots with distinct users (one order each).
    // MAX_ACTIVE_USERS = 1024
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    Order *orders = new Order[MAX_ACTIVE_USERS];
    for (std::size_t i = 0; i < MAX_ACTIVE_USERS; ++i) {
        orders[i] = makeOrder(UserId(i), OrderId(0));
        EXPECT_TRUE(map.insert(&orders[i]));
    }

    // Now the pool is exhausted — next new user should fail
    Order extra = makeOrder(UserId(MAX_ACTIVE_USERS + 1), OrderId(0));
    EXPECT_FALSE(map.insert(&extra));

    delete[] orders;
}

TEST(UserOrderHashMapTest, InsertReturnsFalseWhenUserOrdersFull) {
    UserOrderHashMap map;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    Order *orders = new Order[MAX_ORDERS_PER_USER];
    for (std::size_t i = 0; i < MAX_ORDERS_PER_USER; ++i) {
        orders[i] = makeOrder(UserId(1), OrderId(i));
        EXPECT_TRUE(map.insert(&orders[i]));
    }

    // User 1 is now full — next insert for user 1 should fail
    Order extra = makeOrder(UserId(1), OrderId(MAX_ORDERS_PER_USER));
    EXPECT_FALSE(map.insert(&extra));

    // But a different user should still succeed
    Order other = makeOrder(UserId(2), OrderId(0));
    EXPECT_TRUE(map.insert(&other));

    delete[] orders;
}

TEST(UserOrderHashMapTest, FindNonExistentOrderForExistingUser) {
    // Verify that looking up a non-existent order for an existing user
    // returns nullptr (not a crash).
    UserOrderHashMap map;
    Order o = makeOrder(UserId(1), OrderId(5));
    EXPECT_TRUE(map.insert(&o));

    EXPECT_EQ(map.find(UserId(1), OrderId(0)), nullptr);    // slot never filled
    EXPECT_EQ(map.find(UserId(1), OrderId(999)), nullptr);  // valid range, empty
}

TEST(UserOrderHashMapTest, InsertOrderIdAtMaxBoundary) {
    // Verify that inserting at OrderId(MAX_ORDERS_PER_USER - 1) works
    // (the largest valid index).
    UserOrderHashMap map;
    Order o = makeOrder(UserId(1), OrderId(MAX_ORDERS_PER_USER - 1));
    EXPECT_TRUE(map.insert(&o));
    EXPECT_EQ(map.find(UserId(1), OrderId(MAX_ORDERS_PER_USER - 1)), &o);
}
