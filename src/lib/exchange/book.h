#pragma once

#include "definitions.h"
#include "lib/utils/log.h"
#include "lib/utils/mem.h"
#include "order.h"
#include "orders_at_price.h"
#include "user_orders.h"

namespace exchange {

class MatchingEngine;

class OrderBook final {
public:
    OrderBook(TickerId ticker_id, utils::Logger *logger, MatchingEngine *matching_engine);
    ~OrderBook();
    void add(UserId client_id, OrderId order_id, TickerId ticker_id, Side side, Price price,
             Quantity qty) noexcept;
    void cancel(UserId client_id, OrderId order_id, TickerId ticker_id) noexcept;

    MarketOrderId generateNewMarketOrderId() noexcept { return next_market_order_id++; }

    // Deleted default, copy & move constructors and assignment-operators.
    OrderBook() = delete;
    OrderBook(const OrderBook &) = delete;
    OrderBook(const OrderBook &&) = delete;
    OrderBook &operator=(const OrderBook &) = delete;
    OrderBook &operator=(const OrderBook &&) = delete;

private:
    bool addOrder(Order *order) noexcept;
    void removeOrder(Order *order) noexcept;
    Quantity checkForMatch(UserId user_id, OrderId client_order_id, TickerId ticker_id, Side side,
                           Price price, Quantity qty, MarketOrderId new_market_order_id) noexcept;
    void match(TickerId ticker_id, UserId user_id, Side side, OrderId client_order_id,
               MarketOrderId new_market_order_id, Order *itr, Quantity *leaves_qty) noexcept;

    TickerId ticker_id = TickerId::INVALID;
    MatchingEngine *matching_engine = nullptr;
    UserOrderHashMap cid_oid_to_order;
    OrdersAtPriceHashMap orders_at_price;
    utils::MemPool<Order> order_pool;
    // Start at 1 to reserve 0 as an unused/sentinel value (distinct from
    // MarketOrderId::INVALID which is UINT64_MAX).
    MarketOrderId next_market_order_id{1};
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
    std::array<std::unique_ptr<OrderBook>, MAX_TICKERS> ticker_to_order_book;
};

}  // namespace exchange