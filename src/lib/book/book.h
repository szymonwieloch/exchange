#pragma once

#include "definitions.h"

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

}  // namespace book