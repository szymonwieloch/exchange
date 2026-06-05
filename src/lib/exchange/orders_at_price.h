#pragma once

#include "lib/utils/linked_list.h"
#include "order.h"

namespace exchange {

// TODO: provide abstraction for a non-owning linked list element, which can be used for both
// OrdersAtPrice and OrderBook.

/// Represents a price level in the order book, which can have multiple orders at the same price.
/// Implemented using linked list to allow for efficient insertion and deletion of orders at the
/// same price level.
struct OrdersAtPrice : utils::LinkedList<OrdersAtPrice> {
    OrdersAtPrice() = default;

    OrdersAtPrice(Side side, Price price, Order *first_order, OrdersAtPrice *prev_entry,
                  OrdersAtPrice *next_entry)
        : utils::LinkedList<OrdersAtPrice>(prev_entry, next_entry),
          side(side),
          price(price),
          first_order(first_order) {}

    Side side = Side::INVALID;
    Price price = Price::INVALID;
    Order *first_order = nullptr;
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
            curr = curr->next;
        }
        return nullptr;
    }

    void insert(OrdersAtPrice *entry) noexcept {
        price_to_orders_at_price.at(priceToIndex(entry->price)) = entry;
    }

    void clear(Price price) noexcept { price_to_orders_at_price.at(priceToIndex(price)) = nullptr; }

private:
    std::size_t priceToIndex(Price price) const noexcept {
        return (type_safe::get(price) % ME_MAX_PRICE_LEVELS);
    }
    std::array<OrdersAtPrice *, ME_MAX_PRICE_LEVELS> price_to_orders_at_price;
};

}  // namespace exchange