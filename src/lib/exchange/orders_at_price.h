/// @file orders_at_price.h
/// @brief Price-level aggregation and price-to-orders hash map for the order book.
///
/// Provides two tightly coupled types:
/// - @ref OrdersAtPrice — a single (side, price) level holding a ring of orders.
/// - @ref OrdersAtPriceHashMap — an open-hash map from @ref Price to
///   @ref OrdersAtPrice, with a secondary price-sorted linked list for
///   efficient matching.
///
/// # Data Structure Overview
///
/// Each @ref OrdersAtPrice belongs to **two** doubly-linked lists:
///
/// | Chain  | Pointers        | Purpose                                      | Head |
/// |--------|-----------------|----------------------------------------------|-----------------------|
/// | `_idx` | next_idx/prev_idx | Hash-collision chain (same bucket, different price) |
/// `price_to_orders_at_price[i]` | | `_ord` | next_ord/prev_ord | Price-sorted chain (all prices
/// for one side)  | `bids_by_price` / `asks_by_price` |
///
/// The `_idx` chain enables O(1) average-case lookup by price despite a fixed
/// hash-table width (@ref MAX_PRICE_LEVELS). The `_ord` chain enables O(1)
/// access to the best bid/ask and O(k) traversal during matching (k = number
/// of price levels crossed).
///
/// # Matching Semantics
///
/// - **Bids**: descending price order (best = highest at head).
/// - **Asks**: ascending price order (best = lowest at head).
/// - Within a price level, orders are FIFO by priority (price-time priority).
///
/// # Memory Model
///
/// All @ref OrdersAtPrice instances are allocated from a pre-sized
/// @ref utils::MemPool — zero heap allocations in the hot path.
/// @ref Order pointers stored here are non-owning observers.

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

    /// Next node in the hash-collision chain (same bucket, different price).
    OrdersAtPrice *getNextIdx() const noexcept { return next_idx; }
    /// Previous node in the hash-collision chain.
    OrdersAtPrice *getPrevIdx() const noexcept { return prev_idx; }
    /// Next node in the price-sorted chain (next-worse price for this side).
    OrdersAtPrice *getNextOrd() const noexcept { return next_ord; }
    /// Previous node in the price-sorted chain (next-better price for this side).
    OrdersAtPrice *getPrevOrd() const noexcept { return prev_ord; }

    /// Unlinks @c this from both the hash-collision chain (@c _idx) and the
    /// price-sorted chain (@c _ord). After the call, all four link pointers
    /// are nulled to prevent accidental dangling access before deallocation.
    void disconnect() noexcept {
        if (prev_idx) {
            prev_idx->next_idx = next_idx;
        }
        if (next_idx) {
            next_idx->prev_idx = prev_idx;
        }
        if (prev_ord) {
            prev_ord->next_ord = next_ord;
        }
        if (next_ord) {
            next_ord->prev_ord = prev_ord;
        }
        next_idx = nullptr;
        prev_idx = nullptr;
        next_ord = nullptr;
        prev_ord = nullptr;
    }

    /// Prepends @c this before @p first in the hash-collision chain.
    /// @p first may be null when the bucket is empty — in that case @c this
    /// becomes the sole element with @c next_idx remaining null.
    void prependIdx(OrdersAtPrice *first) noexcept {
        if (first) {
            first->prev_idx = this;
            next_idx = first;
        }
    }

    /// Inserts @c this immediately after @p after in the price-sorted chain.
    void insertAfterOrd(OrdersAtPrice *after) noexcept {
        assert(after);

        prev_ord = after;
        if (after->next_ord) {
            after->next_ord->prev_ord = this;
            next_ord = after->next_ord;
        }
        after->next_ord = this;
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

private:
    /// Next node in the hash-collision chain, or nullptr if this is the tail.
    OrdersAtPrice *next_idx = nullptr;
    /// Previous node in the hash-collision chain, or nullptr if this is the head.
    OrdersAtPrice *prev_idx = nullptr;
    /// Next node in the price-sorted chain (next-worse price), or nullptr at tail.
    OrdersAtPrice *next_ord = nullptr;
    /// Previous node in the price-sorted chain (next-better price), or nullptr at head.
    OrdersAtPrice *prev_ord = nullptr;
};

/// Fixed-size, open-hash map from @ref Price to @ref OrdersAtPrice with an
/// integrated price-sorted linked list for O(1) best-bid/ask access.
///
/// # Data Structures
///
/// - **Hash table**: @c price_to_orders_at_price — a fixed @ref MAX_PRICE_LEVELS
///   array. Each bucket is the head of a doubly-linked `_idx` chain of
///   @ref OrdersAtPrice nodes that collided on that bucket. Collisions occur
///   because the price space (64-bit) far exceeds the bucket count (256).
/// - **Price-sorted list**: @c bids_by_price (descending) and @c asks_by_price
///   (ascending) — doubly-linked `_ord` chains spanning all price levels for a
///   side. The head is always the best price.
///
/// # Hot-Path Operations
///
/// | Operation            | Complexity | Notes                                    |
/// |----------------------|------------|------------------------------------------|
/// | find()               | O(1) avg   | Hash lookup + short collision walk       |
/// | insert()             | O(1) avg   | find() + ring append or addOrdersAtPrice |
/// | remove()             | O(1) avg   | find() + ring unhook or removeOrdersAtPrice |
/// | bids() / asks()      | O(1)       | Direct pointer to head                   |
/// | addOrdersAtPrice()   | O(k)       | Walk price-sorted list to insertion point|
///
/// # Invariants
///
/// - @c bids_by_price and @c asks_by_price are either null (empty side) or
///   point to the head of a consistent doubly-linked `_ord` chain.
/// - The `_ord` chain is strictly sorted: descending for bids, ascending for asks.
/// - No two @ref OrdersAtPrice in the same `_ord` chain share the same price.
/// - The `_idx` chain for bucket @c i is anchored at @c price_to_orders_at_price[i].
/// - All @ref OrdersAtPrice instances are allocated from @c orders_at_price_pool
///   and deallocated back to it — no heap activity in the hot path.
class OrdersAtPriceHashMap {
public:
    /// Pre-allocates the @ref OrdersAtPrice pool and zeroes the hash table.
    /// The pool is sized to @ref MAX_PRICE_LEVELS — once exhausted, new price
    /// levels cannot be created (the order book is at capacity).
    OrdersAtPriceHashMap() : orders_at_price_pool(MAX_PRICE_LEVELS) {
        price_to_orders_at_price.fill(nullptr);
    }

    /// Clears the side-head pointers. The @ref utils::MemPool destructor handles
    /// cleanup of any still-allocated @ref OrdersAtPrice instances.
    ~OrdersAtPriceHashMap() {
        bids_by_price = nullptr;
        asks_by_price = nullptr;
    }

    OrdersAtPriceHashMap(const OrdersAtPriceHashMap &) = delete;
    OrdersAtPriceHashMap(const OrdersAtPriceHashMap &&) = delete;
    OrdersAtPriceHashMap &operator=(const OrdersAtPriceHashMap &) = delete;
    OrdersAtPriceHashMap &operator=(const OrdersAtPriceHashMap &&) = delete;

    /// Looks up the @ref OrdersAtPrice for @p price, or returns nullptr.
    ///
    /// Walks the `_idx` collision chain starting from the hashed bucket.
    /// Returns immediately on the first price match — the `_ord` chain
    /// invariant guarantees at most one @ref OrdersAtPrice per price.
    ///
    /// @complexity O(1) average, O(collisions) worst-case.
    [[nodiscard]] OrdersAtPrice *find(Price price) const noexcept {
        auto curr = price_to_orders_at_price[priceToIndex(price)];
        while (curr) {
            if (curr->price == price) [[likely]] {
                return curr;
            }
            __builtin_prefetch(curr->getNextIdx(), 0, 3);
            curr = curr->getNextIdx();
        }
        return nullptr;
    }

    /// Inserts @p order into the order book at its price level.
    ///
    /// If no @ref OrdersAtPrice exists for the order's price, a new one is
    /// allocated from the pool and linked into both the `_idx` and `_ord` chains.
    /// Otherwise the order is appended to the existing price level's ring.
    ///
    /// @pre @p order->priority must be pre-set to the value returned by
    ///      @ref nextPriority(order->price).
    /// @pre The pool must not be exhausted (returns nullptr from allocate),
    ///      or the order will be silently dropped.
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

    /// Removes @p order from the order book.
    ///
    /// If @p order is the last order at its price, the entire @ref OrdersAtPrice
    /// is unlinked from both chains and returned to the pool. Otherwise only
    /// the order is unhooked from the ring.
    void remove(Order *order) noexcept {
        auto at_price = find(order->price);
        if (at_price->hasSingleOrder()) {
            removeOrdersAtPrice(at_price);
        } else {  // remove the link.
            at_price->remove(order);
        }
    }

    /// Returns the priority that the next order at @p price would receive.
    ///
    /// For a new price level this is @c Priority{1}. For an existing level
    /// it is one greater than the last (newest) order's priority,
    /// maintaining strict price-time ordering.
    Priority nextPriority(Price price) const noexcept {
        const auto orders = find(price);
        if (!orders) {
            return Priority{1};
        }
        return orders->nextPriority();
    }

    /// Head of the bid side's price-sorted list (best bid = highest price),
    /// or nullptr if no bids exist.
    OrdersAtPrice *bids() const noexcept { return bids_by_price; }

    /// Head of the ask side's price-sorted list (best ask = lowest price),
    /// or nullptr if no asks exist.
    OrdersAtPrice *asks() const noexcept { return asks_by_price; }

private:
    /// Unlinks @p at_price from both the `_idx` and `_ord` chains, updates
    /// the hash-table head and side-head pointers as needed, and returns the
    /// node to the pool.
    ///
    /// Called when the last order at a price level is removed.
    void removeOrdersAtPrice(OrdersAtPrice *at_price) noexcept {
        if (at_price->side == Side::BUY) {
            if (bids_by_price == at_price) [[unlikely]] {
                bids_by_price = at_price->getNextOrd();
            }
        } else {
            if (asks_by_price == at_price) [[unlikely]] {
                asks_by_price = at_price->getNextOrd();
            }
        }
        auto idx = priceToIndex(at_price->price);
        if (price_to_orders_at_price[idx] == at_price) {
            price_to_orders_at_price[idx] = at_price->getNextIdx();
        }
        at_price->disconnect();
        orders_at_price_pool.deallocate(at_price);
    }

    /// Links a newly allocated @ref OrdersAtPrice into both the hash-collision
    /// chain and the price-sorted chain for its side.
    ///
    /// # Algorithm
    ///
    /// 1. **Hash insertion**: Prepend to the front of the `_idx` chain for the
    ///    hashed bucket (O(1)).
    /// 2. **Price-sorted insertion**: Walk the `_ord` chain from the head to
    ///    find the correct sorted position, then insert. For bids the chain is
    ///    descending (best = highest), for asks ascending (best = lowest).
    ///
    /// If the new node has the best price on its side, it becomes the new head
    /// via @ref insertAfterOrd and a head-pointer swap.
    ///
    /// @complexity O(k) where k is the number of price levels with better
    ///             prices than the new node.
    void addOrdersAtPrice(OrdersAtPrice *new_orders_at_price) noexcept {
        // --- hash collision chain insertion ---
        auto idx = priceToIndex(new_orders_at_price->price);
        new_orders_at_price->prependIdx(price_to_orders_at_price[idx]);
        price_to_orders_at_price[idx] = new_orders_at_price;

        // --- price-sorted linked list insertion ---
        // Bids: descending price  (best = highest at head)
        // Asks: ascending  price  (best = lowest  at head)
        auto &head = (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price);
        const bool is_buy = (new_orders_at_price->side == Side::BUY);

        if (!head) [[unlikely]] {
            head = new_orders_at_price;
            return;
        }

        OrdersAtPrice *curr = head;
        OrdersAtPrice *insert_after = nullptr;

        if (is_buy) {
            // Walk forward while the current price is still better (higher) than the new price.
            while (curr && new_orders_at_price->price < curr->price) {
                insert_after = curr;
                curr = curr->getNextOrd();
            }
        } else {
            // Walk forward while the current price is still better (lower) than the new price.
            while (curr && new_orders_at_price->price > curr->price) {
                insert_after = curr;
                curr = curr->getNextOrd();
            }
        }

        if (!insert_after) {
            // New best price — insert at head.
            head->insertAfterOrd(new_orders_at_price);
            head = new_orders_at_price;
        } else {
            new_orders_at_price->insertAfterOrd(insert_after);
        }
    }

private:
    /// Hashes @p price into a bucket index via modulo.
    /// @returns A value in @c [0, MAX_PRICE_LEVELS).
    std::size_t priceToIndex(Price price) const noexcept {
        return (type_safe::get(price) % MAX_PRICE_LEVELS);
    }

    /// Pre-allocated pool of @ref OrdersAtPrice nodes. Sized to
    /// @ref MAX_PRICE_LEVELS — the maximum number of distinct price levels
    /// that can coexist in a single order book.
    utils::MemPool<OrdersAtPrice> orders_at_price_pool;

    /// Head of the bid-side price-sorted list (descending price).
    /// Null if no bid orders exist.
    OrdersAtPrice *bids_by_price = nullptr;

    /// Head of the ask-side price-sorted list (ascending price).
    /// Null if no ask orders exist.
    OrdersAtPrice *asks_by_price = nullptr;

    /// Hash table: bucket @c i is the head of a doubly-linked `_idx` chain
    /// of @ref OrdersAtPrice whose price hashes to @c i.
    std::array<OrdersAtPrice *, MAX_PRICE_LEVELS> price_to_orders_at_price;
};

}  // namespace exchange