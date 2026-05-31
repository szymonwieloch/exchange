#pragma once

#include "definitions.h"
#include "lib/utils/log.h"
#include "lib/utils/mem.h"

namespace book {
struct Order {
    TickerId ticker_id = TickerId_INVALID;
    UserId client_id = UserId_INVALID;
    OrderId order_id = OrderId_INVALID;
    OrderId market_order_id = OrderId_INVALID;
    Side side = Side::INVALID;
    Price price = Price_INVALID;
    Quantity qty = Quantity_INVALID;
    Priority priority = Priority_INVALID;
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
    auto toString() const -> std::string;
};

using OrderMap = std::array<Order *, ME_MAX_ORDER_IDS>;
using UserOrderHashMap = std::array<OrderMap, ME_MAX_NUM_CLIENTS>;

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
    Price price = Price_INVALID;
    Order *first_order = nullptr;
    OrdersAtPrice *prev_entry = nullptr;
    OrdersAtPrice *next_entry = nullptr;
};

using OrdersAtPriceMap = std::array<OrdersAtPrice *, ME_MAX_PRICE_LEVELS>;

class MatchingEngine;

class OrderBook final {
public:
    OrderBook(TickerId ticker_id, utils::Logger *logger, MatchingEngine *matching_engine)
        : ticker_id(ticker_id),
          matching_engine(matching_engine),
          orders_at_price_pool(ME_MAX_PRICE_LEVELS),
          order_pool(ME_MAX_ORDER_IDS),
          logger(logger) {}
    ~OrderBook() {
        // logger->log("%:% %() % OrderBook\n%\n", __FILE__, __LINE__, __FUNCTION__,
        //             utils::getCurrentTimeStr(&time_str), toString(false, true));
        matching_engine = nullptr;
        bids_by_price = asks_by_price = nullptr;
        // for (auto &itr : cid_oid_to_order) {
        //     itr.fill(nullptr);
        // }
    }
    OrderBook() = delete;
    OrderBook(const OrderBook &) = delete;
    OrderBook(const OrderBook &&) = delete;
    OrderBook &operator=(const OrderBook &) = delete;
    OrderBook &operator=(const OrderBook &&) = delete;

private:
    TickerId ticker_id = TickerId_INVALID;
    MatchingEngine *matching_engine = nullptr;
    OrderMap cid_oid_to_order;
    utils::MemPool<OrdersAtPrice> orders_at_price_pool;
    OrdersAtPrice *bids_by_price = nullptr;
    OrdersAtPrice *asks_by_price = nullptr;
    OrdersAtPriceMap price_orders_at_price;
    utils::MemPool<Order> order_pool;
    // ClientResponse client_response;
    // MarketUpdate market_update;
    OrderId next_market_order_id = 1;
    std::string time_str;
    utils::Logger *logger = nullptr;
};

typedef std::array<OrderBook *, ME_MAX_TICKERS> OrderBookHashMap;

}  // namespace book