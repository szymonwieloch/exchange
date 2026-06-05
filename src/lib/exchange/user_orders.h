#pragma once

namespace exchange {
/// User order ID to Order mapping.
/// We use a fixed size array to store the orders for each user, which allows us to get an order
/// in O(1) time. The order ID is used as the index in the array. We use a fixed size array
/// instead of a hash map because we want to avoid the overhead of hashing and dynamic memory
/// allocation.
// The maximum number of orders per user is defined by ME_MAX_ORDERS_PER_USER, which is a
// compile-time constant. If a user tries to place more than ME_MAX_ORDERS_PER_USER orders, we
// will reject the order and send a cancel rejected response to the matching engine.
class UserOrders {
public:
    UserOrders() { orders.fill(nullptr); }

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
        orders[type_safe::get(order_id)] = nullptr;
    }

    void insert(Order *order) noexcept {
        assert(type_safe::get(order->order_id) < orders.size());
        assert(orders[type_safe::get(order->order_id)] == nullptr);
        orders[type_safe::get(order->order_id)] = order;
    }

private:
    std::array<Order *, ME_MAX_ORDERS_PER_USER> orders;
};

/// Maps user ID and user-specific order ID to Order.
/// Retrieves the Order in O(1) time, but has a fixed maximum number of users and orders per
/// user, defined by ME_MAX_NUM_CLIENTS and ME_MAX_ORDERS_PER_USER respectively.
class UserOrderHashMap {
public:
    UserOrderHashMap() = default;

    UserOrderHashMap(const UserOrderHashMap &) = delete;
    UserOrderHashMap(const UserOrderHashMap &&) = delete;
    UserOrderHashMap &operator=(const UserOrderHashMap &) = delete;
    UserOrderHashMap &operator=(const UserOrderHashMap &&) = delete;

    Order *find(UserId user_id, OrderId order_id) const noexcept {
        if (type_safe::get(user_id) >= user_to_orders.size()) [[unlikely]] {
            return nullptr;
        }
        return user_to_orders[type_safe::get(user_id)].find(order_id);
    }

    void remove(UserId user_id, OrderId order_id) noexcept {
        assert(type_safe::get(user_id) < user_to_orders.size());
        user_to_orders[type_safe::get(user_id)].remove(order_id);
    }

    void insert(Order *order) noexcept {
        assert(type_safe::get(order->client_id) < user_to_orders.size());
        user_to_orders[type_safe::get(order->client_id)].insert(order);
    }

private:
    // TODO: translate into a "real" hash map with a linked list
    std::array<UserOrders, ME_MAX_NUM_CLIENTS> user_to_orders;
};

}  // namespace exchange