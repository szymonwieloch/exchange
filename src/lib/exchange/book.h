#pragma once

#include "definitions.h"
#include "lib/utils/log.h"
#include "lib/utils/mem.h"
#include "order.h"
#include "orders_at_price.h"

namespace exchange {

/// User order ID to Order mapping.
/// We use a fixed size array to store the orders for each user, which allows us to get an order in
/// O(1) time. The order ID is used as the index in the array. We use a fixed size array instead of
/// a hash map because we want to avoid the overhead of hashing and dynamic memory allocation.
// The maximum number of orders per user is defined by ME_MAX_ORDERS_PER_USER, which is a
// compile-time constant. If a user tries to place more than ME_MAX_ORDERS_PER_USER orders, we will
// reject the order and send a cancel rejected response to the matching engine.
class OrderMap {
public:
    OrderMap() { orders.fill(nullptr); }

    OrderMap(const OrderMap &) = delete;
    OrderMap(const OrderMap &&) = delete;
    OrderMap &operator=(const OrderMap &) = delete;
    OrderMap &operator=(const OrderMap &&) = delete;

    Order *find(OrderId order_id) const noexcept {
        if (type_safe::get(order_id) >= orders.size()) [[unlikely]] {
            return nullptr;
        }
        return orders[type_safe::get(order_id)];
    }

    void remove(OrderId order_id) noexcept {
        assert(type_safe::get(order_id) < orders.size());
        orders[type_safe::get(order_id)] = nullptr;
    }

    void insert(Order *order) noexcept {
        assert(type_safe::get(order->order_id) < orders.size());
        orders[type_safe::get(order->order_id)] = order;
    }

private:
    std::array<Order *, ME_MAX_ORDERS_PER_USER> orders;
};

/// Maps user ID and user-specific order ID to Order.
/// Retrieves the Order in O(1) time, but has a fixed maximum number of users and orders per user,
/// defined by ME_MAX_NUM_CLIENTS and ME_MAX_ORDERS_PER_USER respectively.
class UserOrderHashMap {
public:
    UserOrderHashMap() = default;

    UserOrderHashMap(const UserOrderHashMap &) = delete;
    UserOrderHashMap(const UserOrderHashMap &&) = delete;
    UserOrderHashMap &operator=(const UserOrderHashMap &) = delete;
    UserOrderHashMap &operator=(const UserOrderHashMap &&) = delete;

    Order *find(UserId user_id, OrderId order_id) const noexcept {
        if (type_safe::get(user_id) >= user_to_orders.size()) [[unlikely]] {
            return nullptr;
        }
        return user_to_orders[type_safe::get(user_id)].find(order_id);
    }

    void remove(UserId user_id, OrderId order_id) noexcept {
        assert(type_safe::get(user_id) < user_to_orders.size());
        user_to_orders[type_safe::get(user_id)].remove(order_id);
    }

    void insert(Order *order) noexcept {
        assert(type_safe::get(order->client_id) < user_to_orders.size());
        user_to_orders[type_safe::get(order->client_id)].insert(order);
    }

private:
    std::array<OrderMap, ME_MAX_NUM_CLIENTS> user_to_orders;
};

class MatchingEngine;

class OrderBook final {
public:
    OrderBook(TickerId ticker_id, utils::Logger *logger, MatchingEngine *matching_engine);
    ~OrderBook();
    void add(UserId client_id, OrderId order_id, TickerId ticker_id, Side side, Price price,
             Quantity qty) noexcept;
    void cancel(UserId client_id, OrderId order_id, TickerId ticker_id) noexcept;

    OrderId generateNewMarketOrderId() noexcept { return next_market_order_id++; }

    // Deleted default, copy & move constructors and assignment-operators.
    OrderBook() = delete;
    OrderBook(const OrderBook &) = delete;
    OrderBook(const OrderBook &&) = delete;
    OrderBook &operator=(const OrderBook &) = delete;
    OrderBook &operator=(const OrderBook &&) = delete;

private:
    Priority getNextPriority(Price price) noexcept;
    void addOrder(Order *order) noexcept;
    void removeOrder(Order *order) noexcept;
    Quantity checkForMatch(UserId user_id, OrderId client_order_id, TickerId ticker_id, Side side,
                           Price price, Quantity qty, OrderId new_market_order_id) noexcept;
    void match(TickerId ticker_id, UserId user_id, Side side, OrderId client_order_id,
               OrderId new_market_order_id, Order *itr, Quantity *leaves_qty) noexcept;
    void addOrdersAtPrice(OrdersAtPrice *new_orders_at_price) noexcept;
    void removeOrdersAtPrice(Side side, Price price) noexcept;

    TickerId ticker_id = TickerId::INVALID;
    MatchingEngine *matching_engine = nullptr;
    UserOrderHashMap cid_oid_to_order;
    utils::MemPool<OrdersAtPrice> orders_at_price_pool;
    OrdersAtPrice *bids_by_price = nullptr;
    OrdersAtPrice *asks_by_price = nullptr;
    OrdersAtPriceHashMap orders_at_price;
    utils::MemPool<Order> order_pool;
    OrderId next_market_order_id{1};
    std::string time_str;
    utils::Logger *logger = nullptr;
};

/// Maps ticker to OrderBook.
class OrderBookHashMap final {
public:
    OrderBookHashMap(utils::Logger *logger, MatchingEngine *matching_engine) {
        for (size_t i = 0; i < ticker_to_order_book.size(); ++i) {
            ticker_to_order_book[i] = std::make_unique<OrderBook>(
                TickerId{static_cast<std::uint16_t>(i)}, logger, matching_engine);
        }
    }
    OrderBookHashMap(const OrderBookHashMap &) = delete;
    OrderBookHashMap(const OrderBookHashMap &&) = delete;
    OrderBookHashMap &operator=(const OrderBookHashMap &) = delete;
    OrderBookHashMap &operator=(const OrderBookHashMap &&) = delete;

    OrderBook *find(TickerId ticker_id) const noexcept {
        return ticker_to_order_book.at(type_safe::get(ticker_id)).get();
    }

private:
    std::array<std::unique_ptr<OrderBook>, ME_MAX_TICKERS> ticker_to_order_book;
};

}  // namespace exchange