#pragma once

#include "constants.h"
#include "lib/utils/linked_list.h"
#include "lib/utils/mem.h"
#include "order.h"

namespace exchange {

/// Represents a single price level in the order book, holding all orders that
/// share the same (side, price) tuple in a circular doubly-linked ring.
///
/// # Structure
///
/// Orders are stored as a circular singly-linked ring anchored at `first_order`.
/// When only one order exists, it links to itself (`order->next == order->prev == order`).
/// New orders are always appended at the back of the ring (before `first_order`),
/// implementing **price-time priority**: earlier arrivals have lower priority numbers
/// and sit closer to the front.
///
/// # Ownership & Lifetime
///
/// - `OrdersAtPrice` instances are owned by `OrdersAtPriceHashMap` and allocated
///   from a pre-sized `utils::MemPool<OrdersAtPrice>`. Heap allocations are zero
///   in the hot path.
/// - `Order` pointers stored here are **non-owning observers**. Orders are owned
///   by the `OrderBook` / `UserOrders` pools.
/// - `const` data members (`side`, `price`) make the type immovable — it is intended
///   for placement-new construction and in-place destructor call only.
///
/// # Invariants
///
/// - `first_order` is never null after construction. The default constructor
///   (used by MemPool pre-allocation) leaves it null; the instance must not be
///   used until properly initialized via the parameterized constructor.
/// - The order ring is always consistent: `first_order->prev` points to the
///   last (newest) order, and traversing `next` from `first_order` visits
///   orders in FIFO (oldest-first) order.
/// - `remove()` must only be called when there are **2 or more** orders.
///   The caller is responsible for destroying the entire `OrdersAtPrice`
///   instance when the last order is removed.
class OrdersAtPrice {
public:
    /// Default constructor for MemPool pre-allocation.
    /// Leaves the instance in an uninitialized state — `first_order` is null.
    OrdersAtPrice() = default;

    /// Constructs a price level with a single order.
    ///
    /// @param side         BUY or SELL — immutable for the lifetime of this level.
    /// @param price        The price of all orders at this level — immutable.
    /// @param first_order  The initial (and currently only) order at this price.
    ///                     Must be non-null. Its `next`/`prev` will be rewired
    ///                     into a self-loop.
    /// @param prev_entry   Previous `OrdersAtPrice` in the side's price-sorted
    ///                     linked list, or nullptr if this is the new head.
    /// @param next_entry   Next `OrdersAtPrice` in the side's price-sorted
    ///                     linked list, or nullptr if this is the new tail.
    OrdersAtPrice(Side side, Price price, Order *first_order) noexcept
        : side(side), price(price), first_order(first_order) {
        first_order->makeRing();
    }

    /// Side of all orders at this price level. Immutable after construction.
    const Side side = Side::INVALID;

    /// Price shared by all orders at this level. Immutable after construction.
    const Price price = Price::INVALID;

    /// Anchor of the circular order ring. Points to the **oldest** order
    /// (lowest priority) — the next candidate for matching.
    Order *first_order = nullptr;

    OrdersAtPrice *next_idx = nullptr;
    OrdersAtPrice *prev_idx = nullptr;
    OrdersAtPrice *next_ord = nullptr;
    OrdersAtPrice *prev_ord = nullptr;

    void disconnect() noexcept {
        if (prev_idx) {
            prev_idx->next_idx = next_idx;
        }
        if (next_idx) {
            next_idx->prev_idx = prev_idx;
        }
        // TODO order
    }

    /// Returns the priority value that the next inserted order should carry.
    ///
    /// Computed as `(last_order->priority) + 1`, which guarantees strictly
    /// increasing priorities and thus price-time ordering.
    ///
    /// @pre `first_order` is non-null (the level is initialized).
    [[nodiscard]] Priority nextPriority() const noexcept {
        assert(first_order);
        return first_order->getPrev()->priority + Priority{1};
    }

    /// Returns true if the ring contains exactly one order.
    ///
    /// @pre `first_order` is non-null.
    [[nodiscard]] bool hasSingleOrder() const noexcept {
        assert(first_order);
        return first_order->getPrev() == first_order;
    }

    /// Removes an order from the ring.
    ///
    /// If `order` is `first_order`, the anchor advances to `order->next`.
    ///
    /// @pre The ring contains **at least 2 orders**. The caller must check
    ///      `hasSingleOrder()` first and destroy the entire `OrdersAtPrice`
    ///      instance (via `OrdersAtPriceHashMap::removeOrdersAtPrice`) if
    ///      this was the last order.
    /// @pre `order->side == side` and `order->price == price`.
    void remove(Order *order) noexcept {
        assert(first_order);
        assert(order->side == side);
        assert(order->price == price);
        assert(!hasSingleOrder());  // caller must destroy the whole level instead
        if (first_order == order) {
            first_order = order->getNext();
        }
        order->disconnect();
    }

    /// Inserts an order at the **back** of the ring (before `first_order`),
    /// giving it the highest priority among orders at this price.
    ///
    /// The new order becomes `first_order->prev` (the newest/last order).
    ///
    /// @pre `first_order` is non-null.
    /// @pre `order->side == side` and `order->price == price`.
    /// @pre `order->priority == nextPriority()` — the caller must assign
    ///      the correct priority before calling this method.
    void insert(Order *order) noexcept {
        assert(first_order);
        assert(order->side == side);
        assert(order->price == price);
        assert(order->priority == first_order->getPrev()->priority + Priority(1));
        order->addToRing(first_order);
    }
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
            curr = curr->next_idx;
        }
        return nullptr;
    }

    void insert(Order *order) noexcept {
        const auto at_price = find(order->price);
        if (!at_price) {
            auto new_orders_at_price =
                orders_at_price_pool.allocate(order->side, order->price, order);
            addOrdersAtPrice(new_orders_at_price);
        } else {
            at_price->insert(order);
        }
    }

    void remove(Order *order) noexcept {
        auto at_price = find(order->price);
        if (at_price->hasSingleOrder()) {
            removeOrdersAtPrice(at_price);
        } else {  // remove the link.
            at_price->remove(order);
        }
    }

    Priority nextPriority(Price price) noexcept {
        const auto orders = find(price);
        if (!orders) {
            return Priority{1};
        }
        return orders->nextPriority();
    }

    OrdersAtPrice *bids() const noexcept { return bids_by_price; }

    OrdersAtPrice *asks() const noexcept { return asks_by_price; }

private:
    void removeOrdersAtPrice(OrdersAtPrice *at_price) noexcept {
        // const auto best_orders_by_price =
        //     (at_price->side == Side::BUY ? bids_by_price : asks_by_price);
        // if (at_price->next == at_price) [[unlikely]] {  // only element on this side.
        //     (at_price->side == Side::BUY ? bids_by_price : asks_by_price) = nullptr;
        // } else {
        //     if (at_price == best_orders_by_price) {
        //         (at_price->side == Side::BUY ? bids_by_price : asks_by_price) = at_price->next;
        //     }
        //     at_price->disconnect();
        // }
        // price_to_orders_at_price[priceToIndex(at_price->price)] = nullptr;
        if (at_price->side == Side::BUY) {
            if (bids_by_price == at_price) [[unlikely]] {
                bids_by_price = at_price->next_ord;
            }
        } else {
            if (asks_by_price == at_price) [[unlikely]] {
                asks_by_price = at_price->next_ord;
            }
        }
        auto idx = priceToIndex(at_price->price);
        if (price_to_orders_at_price[idx] == at_price) {
            price_to_orders_at_price[idx] = at_price->next_idx;
        }
        at_price->disconnect();
        orders_at_price_pool.deallocate(at_price);
    }

    void addOrdersAtPrice(OrdersAtPrice *new_orders_at_price) noexcept {
        auto idx = priceToIndex(new_orders_at_price->price);
        if (price_to_orders_at_price[idx]) {
            // insert ast first element in the link list
            new_orders_at_price->next_idx = price_to_orders_at_price[idx];
            price_to_orders_at_price[idx]->prev_idx = new_orders_at_price;
        }
        price_to_orders_at_price[idx] = new_orders_at_price;

        // const auto best_orders_by_price =
        //     (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price);

        // if (!best_orders_by_price) [[unlikely]] {
        //     (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price) =
        //         new_orders_at_price;
        //     new_orders_at_price->prev = new_orders_at_price->next = new_orders_at_price;
        // }

        // else {
        //     auto target = best_orders_by_price;
        //     bool add_after = ((new_orders_at_price->side == Side::SELL &&
        //                        new_orders_at_price->price > target->price) ||
        //                       (new_orders_at_price->side == Side::BUY &&
        //                        new_orders_at_price->price < target->price));
        //     if (add_after) {
        //         target = target->next;
        //         add_after = ((new_orders_at_price->side == Side::SELL &&
        //                       new_orders_at_price->price > target->price) ||
        //                      (new_orders_at_price->side == Side::BUY &&
        //                       new_orders_at_price->price < target->price));
        //     }
        //     while (add_after && target != best_orders_by_price) {
        //         add_after = ((new_orders_at_price->side == Side::SELL &&
        //                       new_orders_at_price->price > target->price) ||
        //                      (new_orders_at_price->side == Side::BUY &&
        //                       new_orders_at_price->price < target->price));
        //         if (add_after)
        //             target = target->next;
        //     }

        //     if (add_after) {  // add new_orders_at_price after
        //                       // target.
        //         if (target == best_orders_by_price) {
        //             target = best_orders_by_price->prev;
        //         }
        //         new_orders_at_price->prev = target;
        //         target->next->prev = new_orders_at_price;
        //         new_orders_at_price->next = target->next;
        //         target->next = new_orders_at_price;
        //     } else {  // add new_orders_at_price before target.
        //         new_orders_at_price->prev = target->prev;
        //         new_orders_at_price->next = target;
        //         target->prev->next = new_orders_at_price;
        //         target->prev = new_orders_at_price;

        //         if ((new_orders_at_price->side == Side::BUY &&
        //              new_orders_at_price->price > best_orders_by_price->price) ||
        //             (new_orders_at_price->side == Side::SELL &&
        //              new_orders_at_price->price < best_orders_by_price->price)) {
        //             target->next =
        //                 (target->next == best_orders_by_price ? new_orders_at_price :
        //                 target->next);
        //             (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price) =
        //                 new_orders_at_price;
        //         }
        //     }
        // }
    }

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