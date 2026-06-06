#pragma once

#include "definitions.h"
#include "lib/utils/linked_list.h"

namespace exchange {

/// The main order representation
/// Uses sentinels for invalid values, so that we can avoid using std::optional and the associated
/// overhead.
struct Order {
    TickerId ticker_id = TickerId::INVALID;
    UserId user_id = UserId::INVALID;
    OrderId order_id = OrderId::INVALID;
    MarketOrderId market_order_id = MarketOrderId::INVALID;
    Side side = Side::INVALID;
    Price price = Price::INVALID;
    Quantity qty = Quantity::INVALID;
    Priority priority = Priority::INVALID;

    void disconnect() noexcept {
        assert(prev);
        assert(next);
        if (prev != this) {  // loop, the only element
            prev->next = next;
            next->prev = prev;
        }
        next = nullptr;
        prev = nullptr;
    }

    void makeRing() noexcept {
        prev = this;
        next = this;
    }

    Order* getNext() const noexcept { return next; }

    Order* getPrev() const noexcept { return prev; }

    void addToRing(Order* first_order) noexcept {
        first_order->prev->next = this;
        prev = first_order->prev;
        next = first_order;
        first_order->prev = this;
    }
    // only needed for use with MemPool.
    Order() = default;
    Order(TickerId ticker_id, UserId user_id, OrderId order_id, MarketOrderId market_order_id,
          Side side, Price price, Quantity qty, Priority priority) noexcept
        : ticker_id(ticker_id),
          user_id(user_id),
          order_id(order_id),
          market_order_id(market_order_id),
          side(side),
          price(price),
          qty(qty),
          priority(priority) {}

private:
    // Orders are kept in a ring with other orders having the same price
    // This is intrusive, but very fast.
    Order* next = nullptr;
    Order* prev = nullptr;
};
}  // namespace exchange