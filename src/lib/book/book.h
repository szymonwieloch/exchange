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
}  // namespace book