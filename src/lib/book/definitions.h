#pragma once

#include <chrono>
#include <cstdint>
#include <limits>

namespace book {
// use of sentinel values is less type safe than std::optional, but it is more efficient.
using UserId = std::uint32_t;
const UserId UserId_INVALID = std::numeric_limits<UserId>::max();
using OrderId = std::uint64_t;
const OrderId OrderId_INVALID = std::numeric_limits<OrderId>::max();
using Quantity = std::uint32_t;
const Quantity Quantity_INVALID = std::numeric_limits<Quantity>::max();
using Cents = std::uint64_t;
using Priority = std::uint64_t;
using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;
using TickerId = std::uint16_t;
const TickerId TickerId_INVALID = std::numeric_limits<TickerId>::max();
using Price = Cents;
const Price Price_INVALID = std::numeric_limits<Price>::max();
using Cost = Cents;

enum class Side : int8_t { INVALID = 0, BUY = 1, SELL = -1 };

constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;
constexpr size_t ME_MAX_TICKERS = 8;
constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;
constexpr size_t ME_MAX_MARKET_UPDATES = 256 * 1024;
constexpr size_t ME_MAX_NUM_CLIENTS = 256;
constexpr size_t ME_MAX_ORDER_IDS = 1024 * 1024;
constexpr size_t ME_MAX_PRICE_LEVELS = 256;

enum class OrderType : uint8_t { INVALID = 0, NEW = 1, CANCEL = 2 };

}  // namespace book