#pragma once

#include "definitions.h"

namespace exchange {

/// The main order representation
/// Uses sentinels for invalid values, so that we can avoid using std::optional and the associated
/// overhead.
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
}  // namespace exchange