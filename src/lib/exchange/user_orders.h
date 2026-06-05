#pragma once

#include "lib/utils/linked_list.h"
#include "order.h"

namespace exchange {
/// User order ID to Order mapping.
/// We use a fixed size array to store the orders for each user, which allows us to get an order
/// in O(1) time. The order ID is used as the index in the array. We use a fixed size array
/// instead of a hash map because we want to avoid the overhead of hashing and dynamic memory
/// allocation.
// The maximum number of orders per user is defined by MAX_ORDERS_PER_USER, which is a
// compile-time constant. If a user tries to place more than MAX_ORDERS_PER_USER orders, we
// will reject the order and send a cancel rejected response to the matching engine.
class UserOrders : public utils::LinkedList<UserOrders> {
public:
    UserOrders(UserId user_id) : user_id(user_id) { orders.fill(nullptr); }

    UserOrders(const UserOrders &) = delete;
    UserOrders(const UserOrders &&) = delete;
    UserOrders &operator=(const UserOrders &) = delete;
    UserOrders &operator=(const UserOrders &&) = delete;

    Order *find(OrderId order_id) const noexcept {
        if (type_safe::get(order_id) >= orders.size()) [[unlikely]] {
            return nullptr;
        }
        return orders[type_safe::get(order_id)];
    }

    void remove(OrderId order_id) noexcept {
        assert(type_safe::get(order_id) < orders.size());
        assert(orders[type_safe::get(order_id)] != nullptr);
        orders[type_safe::get(order_id)] = nullptr;
        --count;
    }

    void insert(Order *order) noexcept {
        assert(type_safe::get(order->order_id) < orders.size());
        assert(orders[type_safe::get(order->order_id)] == nullptr);
        orders[type_safe::get(order->order_id)] = order;
        ++count;
    }

    std::size_t size() const { return count; }
    bool full() const { return count == MAX_ORDERS_PER_USER; }
    bool empty() const { return count == 0; }

    const UserId user_id;

private:
    std::array<Order *, MAX_ORDERS_PER_USER> orders;
    std::size_t count = 0;
};

/// Maps user ID and user-specific order ID to Order.
/// Retrieves the Order in O(1) time, but has a fixed maximum number of users and orders per
/// user, defined by MAX_NUM_USERS and MAX_ORDERS_PER_USER respectively.
class UserOrderHashMap {
public:
    UserOrderHashMap() = default;

    UserOrderHashMap(const UserOrderHashMap &) = delete;
    UserOrderHashMap(const UserOrderHashMap &&) = delete;
    UserOrderHashMap &operator=(const UserOrderHashMap &) = delete;
    UserOrderHashMap &operator=(const UserOrderHashMap &&) = delete;

    Order *find(UserId user_id, OrderId order_id) const noexcept {
        auto uo = findObject(user_id);
        if (!uo) [[unlikely]] {
            return nullptr;
        }
        return uo->find(order_id);
    }

    void remove(UserId user_id, OrderId order_id) noexcept {
        auto uo = findObject(user_id);
        assert(uo);
        uo->remove(order_id);
        if (uo->empty()) {
            if (uo->prev == nullptr) {  // first one
                user_to_orders[hash(user_id)] = uo->next;
            }
            uo->disconnect();
            pool.deallocate(uo);
        }
    }

    void insert(Order *order) noexcept {
        auto uo = findObject(order->user_id);
        if (!uo) [[unlikely]] {
            uo = pool.allocate(order->user_id);
            auto idx = hash(order->user_id);
            auto first = user_to_orders[idx];
            if (first) {
                first->prev = uo;
                uo->next = first;
            }
            user_to_orders[idx] = uo;
        }
        uo->insert(order);
    }

private:
    std::array<UserOrders *, MAX_ACTIVE_USERS> user_to_orders;
    utils::MemPool<UserOrders> pool{MAX_ACTIVE_USERS};

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

    std::size_t hash(UserId user_id) const {
        return type_safe::get(user_id) % user_to_orders.size();
    }
};

}  // namespace exchange