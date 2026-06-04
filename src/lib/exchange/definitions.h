#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <type_safe/strong_typedef.hpp>

namespace exchange {

// -- strong typedefs with appropriate operator mixins --

struct UserId : type_safe::strong_typedef<UserId, std::uint32_t>,
                type_safe::strong_typedef_op::equality_comparison<UserId>,
                type_safe::strong_typedef_op::relational_comparison<UserId> {
    using strong_typedef::strong_typedef;
    static const UserId INVALID;
};
inline constexpr UserId UserId::INVALID{std::numeric_limits<std::uint32_t>::max()};

struct OrderId : type_safe::strong_typedef<OrderId, std::uint64_t>,
                 type_safe::strong_typedef_op::integer_arithmetic<OrderId>,
                 type_safe::strong_typedef_op::equality_comparison<OrderId>,
                 type_safe::strong_typedef_op::relational_comparison<OrderId> {
    using strong_typedef::strong_typedef;
    static const OrderId INVALID;
};
inline constexpr OrderId OrderId::INVALID{std::numeric_limits<std::uint64_t>::max()};

struct Quantity : type_safe::strong_typedef<Quantity, std::uint32_t>,
                  type_safe::strong_typedef_op::integer_arithmetic<Quantity>,
                  type_safe::strong_typedef_op::equality_comparison<Quantity>,
                  type_safe::strong_typedef_op::relational_comparison<Quantity> {
    using strong_typedef::strong_typedef;
    static const Quantity INVALID;
};
inline constexpr Quantity Quantity::INVALID{std::numeric_limits<std::uint32_t>::max()};

struct Cents : type_safe::strong_typedef<Cents, std::uint64_t>,
               type_safe::strong_typedef_op::integer_arithmetic<Cents>,
               type_safe::strong_typedef_op::equality_comparison<Cents>,
               type_safe::strong_typedef_op::relational_comparison<Cents> {
    using strong_typedef::strong_typedef;
};

struct Priority : type_safe::strong_typedef<Priority, std::uint64_t>,
                  type_safe::strong_typedef_op::equality_comparison<Priority>,
                  type_safe::strong_typedef_op::relational_comparison<Priority> {
    using strong_typedef::strong_typedef;
    static const Priority INVALID;
};
inline constexpr Priority Priority::INVALID{std::numeric_limits<std::uint64_t>::max()};

using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;

struct TickerId : type_safe::strong_typedef<TickerId, std::uint16_t>,
                  type_safe::strong_typedef_op::equality_comparison<TickerId>,
                  type_safe::strong_typedef_op::relational_comparison<TickerId> {
    using strong_typedef::strong_typedef;
    static const TickerId INVALID;
};
inline constexpr TickerId TickerId::INVALID{std::numeric_limits<std::uint16_t>::max()};

struct Price : type_safe::strong_typedef<Price, std::uint64_t>,
               type_safe::strong_typedef_op::equality_comparison<Price>,
               type_safe::strong_typedef_op::relational_comparison<Price> {
    using strong_typedef::strong_typedef;
    static const Price INVALID;
};
inline constexpr Price Price::INVALID{std::numeric_limits<std::uint64_t>::max()};

struct Cost : type_safe::strong_typedef<Cost, std::uint64_t>,
              type_safe::strong_typedef_op::integer_arithmetic<Cost>,
              type_safe::strong_typedef_op::equality_comparison<Cost>,
              type_safe::strong_typedef_op::relational_comparison<Cost> {
    using strong_typedef::strong_typedef;
};

enum class Side : int8_t { INVALID = 0, BUY = 1, SELL = -1 };

// clang-format off
constexpr size_t LOG_QUEUE_SIZE        = 8 * 1024 * 1024;
constexpr size_t ME_MAX_TICKERS        = 8;
constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;
constexpr size_t ME_MAX_MARKET_UPDATES = 256 * 1024;
constexpr size_t ME_MAX_NUM_CLIENTS    = 256;
constexpr size_t ME_MAX_ORDER_IDS      = 1024 * 1024;
constexpr size_t ME_MAX_PRICE_LEVELS   = 256;
constexpr size_t ME_MAX_ORDERS_PER_USER = 1024;
// clang-format on

}  // namespace exchange
