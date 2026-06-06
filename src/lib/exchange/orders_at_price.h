#pragma once

#include "constants.h"
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
    OrdersAtPriceHashMap() : orders_at_price_pool(MAX_PRICE_LEVELS) {
        price_to_orders_at_price.fill(nullptr);
    }

    ~OrdersAtPriceHashMap() {
        bids_by_price = nullptr;
        asks_by_price = nullptr;
    }

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

    void insert(Order *order) noexcept {
        const auto at_price = find(order->price);
        if (!at_price) {
            order->next = order->prev = order;
            auto new_orders_at_price =
                orders_at_price_pool.allocate(order->side, order->price, order, nullptr, nullptr);
            addOrdersAtPrice(new_orders_at_price);
        } else {
            auto first_order = (at_price ? at_price->first_order : nullptr);
            first_order->prev->next = order;
            order->prev = first_order->prev;
            order->next = first_order;
            first_order->prev = order;
        }
    }

    void remove(Order *order) noexcept {
        auto at_price = find(order->price);
        if (order->prev == order) {  // only one element.
            removeOrdersAtPrice(order->side, order->price);
        } else {  // remove the link.
            if (at_price->first_order == order) {
                at_price->first_order = order->next;
            }
            order->disconnect();
        }
    }

    void clear(Price price) noexcept { price_to_orders_at_price.at(priceToIndex(price)) = nullptr; }

    void removeOrdersAtPrice(Side side, Price price) noexcept {
        const auto best_orders_by_price = (side == Side::BUY ? bids_by_price : asks_by_price);
        auto orders_at_price = this->find(price);
        if (orders_at_price->next == orders_at_price) [[unlikely]] {  // empty side of book.
            (side == Side::BUY ? bids_by_price : asks_by_price) = nullptr;
        } else {
            orders_at_price->prev->next = orders_at_price->next;
            orders_at_price->next->prev = orders_at_price->prev;
            if (orders_at_price == best_orders_by_price) {
                (side == Side::BUY ? bids_by_price : asks_by_price) = orders_at_price->next;
            }
            orders_at_price->prev = orders_at_price->next = nullptr;
        }
        clear(price);
        orders_at_price_pool.deallocate(orders_at_price);
    }

    void addOrdersAtPrice(OrdersAtPrice *new_orders_at_price) noexcept {
        price_to_orders_at_price.at(priceToIndex(new_orders_at_price->price)) = new_orders_at_price;

        const auto best_orders_by_price =
            (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price);

        if (!best_orders_by_price) [[unlikely]] {
            (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price) =
                new_orders_at_price;
            new_orders_at_price->prev = new_orders_at_price->next = new_orders_at_price;
        }

        else {
            auto target = best_orders_by_price;
            bool add_after = ((new_orders_at_price->side == Side::SELL &&
                               new_orders_at_price->price > target->price) ||
                              (new_orders_at_price->side == Side::BUY &&
                               new_orders_at_price->price < target->price));
            if (add_after) {
                target = target->next;
                add_after = ((new_orders_at_price->side == Side::SELL &&
                              new_orders_at_price->price > target->price) ||
                             (new_orders_at_price->side == Side::BUY &&
                              new_orders_at_price->price < target->price));
            }
            while (add_after && target != best_orders_by_price) {
                add_after = ((new_orders_at_price->side == Side::SELL &&
                              new_orders_at_price->price > target->price) ||
                             (new_orders_at_price->side == Side::BUY &&
                              new_orders_at_price->price < target->price));
                if (add_after)
                    target = target->next;
            }

            if (add_after) {  // add new_orders_at_price after
                              // target.
                if (target == best_orders_by_price) {
                    target = best_orders_by_price->prev;
                }
                new_orders_at_price->prev = target;
                target->next->prev = new_orders_at_price;
                new_orders_at_price->next = target->next;
                target->next = new_orders_at_price;
            } else {  // add new_orders_at_price before target.
                new_orders_at_price->prev = target->prev;
                new_orders_at_price->next = target;
                target->prev->next = new_orders_at_price;
                target->prev = new_orders_at_price;

                if ((new_orders_at_price->side == Side::BUY &&
                     new_orders_at_price->price > best_orders_by_price->price) ||
                    (new_orders_at_price->side == Side::SELL &&
                     new_orders_at_price->price < best_orders_by_price->price)) {
                    target->next =
                        (target->next == best_orders_by_price ? new_orders_at_price : target->next);
                    (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price) =
                        new_orders_at_price;
                }
            }
        }
    }

    OrdersAtPrice *bids() const noexcept { return bids_by_price; }

    OrdersAtPrice *asks() const noexcept { return asks_by_price; }

private:
    std::size_t priceToIndex(Price price) const noexcept {
        return (type_safe::get(price) % MAX_PRICE_LEVELS);
    }

    utils::MemPool<OrdersAtPrice> orders_at_price_pool;
    OrdersAtPrice *bids_by_price = nullptr;
    OrdersAtPrice *asks_by_price = nullptr;
    std::array<OrdersAtPrice *, MAX_PRICE_LEVELS> price_to_orders_at_price;
};

}  // namespace exchange