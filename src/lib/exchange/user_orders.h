#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#include "constants.h"
#include "lib/utils/linked_list.h"
#include "lib/utils/mem.h"
#include "order.h"

namespace exchange {

/// Per-user order-ID to Order* mapping.
///
/// Stores pointers to a single user's active orders in a fixed-size array indexed
/// by the raw value of OrderId. This gives O(1) lookup, insert, and remove with
/// no hashing, no dynamic allocation, and excellent cache locality.
///
/// @invariant orders[type_safe::get(order->order_id)] == order for every
///            inserted order; nullptr for every slot that has no order.
/// @invariant count equals the number of non-nullptr entries in orders.
/// @invariant count <= MAX_ORDERS_PER_USER.
///
/// Thread safety: not thread-safe. The caller (typically UserOrderHashMap) is
/// responsible for any synchronization.
class UserOrders final : public utils::LinkedList<UserOrders> {
public:
    /// Constructs an empty order set for the given user.
    ///
    /// All order slots are initialized to nullptr.
    ///
    /// @param user_id  The owning user's identifier.
    /// @post  empty() == true
    /// @post  full() == false
    /// @post  size() == 0
    explicit UserOrders(UserId user_id) : user_id(user_id) { orders.fill(nullptr); }

    // ── non-copyable, non-movable ────────────────────────────────
    UserOrders(const UserOrders &) = delete;
    UserOrders(UserOrders &&) = delete;
    UserOrders &operator=(const UserOrders &) = delete;
    UserOrders &operator=(UserOrders &&) = delete;

    /// Looks up an order by its ID.
    ///
    /// @param order_id  The order identifier to search for.
    /// @return  Pointer to the Order if present; nullptr if the slot is empty
    ///          or order_id is out of range (>=
    ///          MAX_ORDERS_PER_USER).
    /// @complexity  O(1) — single array index.
    [[nodiscard]] Order *find(OrderId order_id) const noexcept {
        if (type_safe::get(order_id) >= orders.size()) [[unlikely]] {
            return nullptr;
        }
        return orders[type_safe::get(order_id)];
    }

    /// Removes an order from this user's set.
    ///
    /// @pre  type_safe::get(order_id) < MAX_ORDERS_PER_USER
    /// @pre  orders[type_safe::get(order_id)] != nullptr  (the slot is occupied)
    /// @param order_id  The order identifier to remove.
    /// @post  find(order_id) == nullptr
    /// @post  size() == old_size() - 1
    /// @complexity  O(1)
    void remove(OrderId order_id) noexcept {
        assert(type_safe::get(order_id) < orders.size());
        assert(orders[type_safe::get(order_id)] != nullptr);
        orders[type_safe::get(order_id)] = nullptr;
        --count;
    }

    /// Inserts an order into this user's set.
    ///
    /// @pre  type_safe::get(order->order_id) < MAX_ORDERS_PER_USER
    /// @pre  orders[type_safe::get(order->order_id)] == nullptr  (slot is free)
    /// @param order  Pointer to the Order to insert. Ownership is not transferred.
    /// @post  find(order->order_id) == order
    /// @post  size() == old_size() + 1
    /// @complexity  O(1)
    void insert(Order *order) noexcept {
        assert(type_safe::get(order->order_id) < orders.size());
        assert(orders[type_safe::get(order->order_id)] == nullptr);
        orders[type_safe::get(order->order_id)] = order;
        ++count;
    }

    /// @return  The number of currently active orders for this user.
    /// @complexity  O(1)
    [[nodiscard]] std::size_t size() const { return count; }

    /// @return  true if no more orders can be added (count ==
    ///          MAX_ORDERS_PER_USER).
    /// @complexity  O(1)
    [[nodiscard]] bool full() const { return count == MAX_ORDERS_PER_USER; }

    /// @return  true if the user has no active orders.
    /// @complexity  O(1)
    [[nodiscard]] bool empty() const { return count == 0; }

    /// The user this order-set belongs to. Public for read-only access by
    /// UserOrderHashMap::findObject.
    const UserId user_id;

private:
    /// Direct-mapped array of order pointers. Index = raw OrderId value.
    std::array<Order *, MAX_ORDERS_PER_USER> orders;
    /// Cached count to make size() / empty() / full() O(1).
    std::size_t count = 0;
};

/// Two-level map: (UserId, OrderId) → Order*.
///
/// The outer map hashes UserId into a fixed-size bucket array (SIZE = 256).
/// Collisions are resolved via separate chaining through UserOrders' LinkedList
/// base class (intrusive linked list — no heap allocations on traversal).
///
/// The inner map is a UserOrders instance, which maps OrderId → Order* via
/// direct array indexing (O(1)).
///
/// All storage is pre-allocated:
///   - The bucket array is a std::array<UserOrders*, SIZE> (stack/static).
///   - UserOrders objects come from a utils::MemPool sized to MAX_ACTIVE_USERS.
///   - Order* storage is external (the Order objects live in another pool).
///
/// @invariant  Every UserOrders in the chain is allocated from pool.
/// @invariant  user_to_orders[hash(uo->user_id)] is the head of the chain
///             containing uo.
/// @invariant  Chains are singly linked via UserOrders::next/prev (LinkedList).
///
/// Thread safety: not thread-safe. The caller (typically the order book or
/// matching engine) is responsible for any synchronization.
class UserOrderHashMap {
public:
    /// Constructs an empty map. All buckets are initialized to nullptr;
    /// the internal pool is pre-sized to MAX_ACTIVE_USERS.
    UserOrderHashMap() = default;

    // ── non-copyable, non-movable ────────────────────────────────
    UserOrderHashMap(const UserOrderHashMap &) = delete;
    UserOrderHashMap(UserOrderHashMap &&) = delete;
    UserOrderHashMap &operator=(const UserOrderHashMap &) = delete;
    UserOrderHashMap &operator=(UserOrderHashMap &&) = delete;

    /// Looks up an order by user and order ID.
    ///
    /// @param user_id   The user who owns the order.
    /// @param order_id  The order identifier.
    /// @return  Pointer to the Order if found; nullptr if the user has no
    ///          active orders or the specific order_id is not present.
    /// @complexity  O(1) amortized — hash + chain walk (short chains) +
    ///              direct array index.
    [[nodiscard]] Order *find(UserId user_id, OrderId order_id) const noexcept {
        auto uo = findObject(user_id);
        if (!uo) [[unlikely]] {
            return nullptr;
        }
        return uo->find(order_id);
    }

    /// Removes an order from the map.
    ///
    /// If the user's order count reaches zero after removal, the UserOrders
    /// object is deallocated back to the pool and removed from its chain.
    ///
    /// @pre  The (user_id, order_id) pair exists in the map.
    /// @param user_id   The user who owns the order.
    /// @param order_id  The order to remove.
    /// @complexity  O(1) amortized.
    void remove(UserId user_id, OrderId order_id) noexcept {
        auto uo = findObject(user_id);
        assert(uo);
        uo->remove(order_id);
        if (uo->empty()) {
            if (uo->prev == nullptr) {  // head of chain
                user_to_orders[hash(user_id)] = uo->next;
            }
            uo->disconnect();
            pool.deallocate(uo);
        }
    }

    /// Inserts an order into the map.
    ///
    /// If the user does not yet have a UserOrders object, one is allocated
    /// from the internal pool. The UserOrders is inserted at the head of the
    /// bucket chain. If the user already exists, the order is added to their
    /// existing set.
    ///
    /// @param order  Pointer to the Order to insert. Must be non-null.
    /// @return  true on success; false if the user's order set is full
    ///          (MAX_ORDERS_PER_USER reached) or the active-user pool is
    ///          exhausted (MAX_ACTIVE_USERS reached).
    /// @post   On success: find(order->user_id, order->order_id) == order.
    /// @complexity  O(1) amortized.
    [[nodiscard]] bool insert(Order *order) noexcept {
        auto uo = findObject(order->user_id);
        if (!uo) [[unlikely]] {
            uo = pool.allocate(order->user_id);
            if (!uo) [[unlikely]] {
                return false;  // pool exhausted — too many active users
            }
            auto idx = hash(order->user_id);
            auto first = user_to_orders[idx];
            if (first) {
                first->prev = uo;
                uo->next = first;
            }
            user_to_orders[idx] = uo;
        }
        if (uo->full()) [[unlikely]] {
            return false;  // user has reached MAX_ORDERS_PER_USER
        }
        uo->insert(order);
        return true;
    }

private:
    /// Bucket array — each entry is the head of a separate-chain list of
    /// UserOrders.
    std::array<UserOrders *, USER_ORDER_HASH_BUCKETS> user_to_orders{};

    /// Pool for UserOrders objects. Sized to MAX_ACTIVE_USERS so the total
    /// number of simultaneously active users is bounded.
    utils::MemPool<UserOrders> pool{MAX_ACTIVE_USERS};

    /// Walks the chain at the bucket for user_id, looking for a UserOrders
    /// whose user_id matches.
    ///
    /// @return  Pointer to the UserOrders if found; nullptr otherwise.
    /// @complexity  O(L) where L is the chain length at the hash bucket.
    UserOrders *findObject(UserId user_id) const {
        auto curr = user_to_orders[hash(user_id)];
        while (curr) {
            if (curr->user_id == user_id) {
                return curr;
            }
            curr = curr->next;
        }
        return nullptr;
    }

    /// Simple modulo hash for UserId → bucket index.
    ///
    /// @return  Bucket index in [0, SIZE).
    /// @complexity  O(1)
    std::size_t hash(UserId user_id) const {
        return type_safe::get(user_id) % user_to_orders.size();
    }
};

}  // namespace exchange