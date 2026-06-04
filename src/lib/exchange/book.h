#pragma once

#include "definitions.h"
#include "lib/utils/log.h"
#include "lib/utils/mem.h"

namespace exchange {
struct Order {
    TickerId ticker_id = TickerId::INVALID;
    UserId client_id = UserId::INVALID;
    OrderId order_id = OrderId::INVALID;
    OrderId market_order_id = OrderId::INVALID;
    Side side = Side::INVALID;
    Price price = Price::INVALID;
    Quantity qty = Quantity::INVALID;
    Priority priority = Priority::INVALID;
    Order *prev_order = nullptr;
    Order *next_order = nullptr;
    // only needed for use with MemPool.
    Order() = default;
    Order(TickerId ticker_id, UserId client_id, OrderId order_id, OrderId market_order_id,
          Side side, Price price, Quantity qty, Priority priority, Order *prev_order,
          Order *next_order) noexcept
        : ticker_id(ticker_id),
          client_id(client_id),
          order_id(order_id),
          market_order_id(market_order_id),
          side(side),
          price(price),
          qty(qty),
          priority(priority),
          prev_order(prev_order),
          next_order(next_order) {}
};

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

private:
    std::array<Order *, ME_MAX_ORDERS_PER_USER> orders;
};

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

private:
    std::array<OrderMap, ME_MAX_NUM_CLIENTS> user_to_orders;
};

// TODO: provide abstraction for a non-owning linked list element, which can be used for both
// OrdersAtPrice and OrderBook.
struct OrdersAtPrice {
    OrdersAtPrice() = default;

    OrdersAtPrice(Side side, Price price, Order *first_order, OrdersAtPrice *prev_entry,
                  OrdersAtPrice *next_entry)
        : side(side),
          price(price),
          first_order(first_order),
          prev_entry(prev_entry),
          next_entry(next_entry) {}

    Side side = Side::INVALID;
    Price price = Price::INVALID;
    Order *first_order = nullptr;
    OrdersAtPrice *prev_entry = nullptr;
    OrdersAtPrice *next_entry = nullptr;
};

// Maps price to OrdersAtPrice. We can have multiple OrdersAtPrice for the same price, but they will
// be stored in a linked list. We use a hash map to get to the head of the linked list in O(1) time.
// The linked list is needed to handle hash collisions, which are inevitable given that we have a
// fixed size hash map and a potentially unbounded number of price levels.
class OrdersAtPriceHashMap {
public:
    OrdersAtPriceHashMap() { price_to_orders_at_price.fill(nullptr); }

    OrdersAtPriceHashMap(const OrdersAtPriceHashMap &) = delete;
    OrdersAtPriceHashMap(const OrdersAtPriceHashMap &&) = delete;
    OrdersAtPriceHashMap &operator=(const OrdersAtPriceHashMap &) = delete;
    OrdersAtPriceHashMap &operator=(const OrdersAtPriceHashMap &&) = delete;

    OrdersAtPrice *find(Price price) const noexcept {
        auto curr = price_to_orders_at_price.at(priceToIndex(price));
        while (curr) {
            if (curr->price == price) {
                return curr;
            }
            curr = curr->next_entry;
        }
        return nullptr;
    }

private:
    std::size_t priceToIndex(Price price) const noexcept {
        return (type_safe::get(price) % ME_MAX_PRICE_LEVELS);
    }
    std::array<OrdersAtPrice *, ME_MAX_PRICE_LEVELS> price_to_orders_at_price;
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
    TickerId ticker_id = TickerId::INVALID;
    MatchingEngine *matching_engine = nullptr;
    // OrderMap cid_oid_to_order;
    utils::MemPool<OrdersAtPrice> orders_at_price_pool;
    OrdersAtPrice *bids_by_price = nullptr;
    OrdersAtPrice *asks_by_price = nullptr;
    OrdersAtPriceHashMap price_orders_at_price;
    UserOrderHashMap user_orders;
    utils::MemPool<Order> order_pool;
    // ClientResponse client_response;
    // MarketUpdate market_update;
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