#pragma once

/// @file constants.h
/// @brief Compile-time sizing constants for the exchange matching engine.
///
/// All dynamic structures in the hot path use fixed-size, pre-allocated buffers
/// derived from these constants. This eliminates heap allocations during trading
/// and bounds worst-case memory usage at startup. Tuning these values is the
/// primary way to trade off memory footprint against peak capacity.

namespace exchange {

/// Capacity (in bytes) of the lock-free SPSC ring buffer backing the async
/// logger (utils::Logger). Each logger instance pre-allocates a buffer of this
/// size to decouple log producers from disk I/O. 8 MiB is the default.
constexpr size_t LOG_QUEUE_SIZE = 8 * 1024 * 1024;

/// Maximum number of tradeable instruments (tickers) the matching engine
/// supports. Each ticker owns a dedicated OrderBook with its own memory pools,
/// pre-allocated at startup.
constexpr size_t MAX_TICKERS = 8;

/// Maximum number of outgoing user-response messages (order acceptances,
/// fills, cancellations) that can be buffered before back-pressure is applied.
/// Currently reserved for future use; not yet wired into the dispatch path.
constexpr size_t MAX_USER_UPDATES = 256 * 1024;

/// Maximum number of outgoing market-data update messages (trades, quote
/// changes) that can be buffered. Paired with MAX_USER_UPDATES for
/// symmetric publish capacity. Currently reserved for future use.
constexpr size_t MAX_MARKET_UPDATES = 256 * 1024;

/// Global cap on simultaneously active orders across all tickers and users.
/// Orders are drawn from a pre-allocated utils::MemPool<Order> sized to this
/// value, so the memory cost is paid once at engine construction.
constexpr size_t MAX_ORDER_IDS = 256 * 1024;

/// Maximum number of distinct price levels that can exist concurrently in a
/// single order book. Determines the hash-table width for the price-to-orders
/// mapping — wider tables reduce linked-list collisions at the cost of cache
/// pressure and memory.
constexpr size_t MAX_PRICE_LEVELS = 256;

/// Hard limit on active orders per individual users.
/// When a user crates a new order while the pool of active orders is exhaused, the system returns
/// an error.
constexpr size_t MAX_ORDERS_PER_USER = 1024;

/// Hard limit on active users. Users whose order count gets to zero are considered non-active.
/// When a user creates a new order while the pool of active users is exhaused, the system returns
/// an error.
constexpr size_t MAX_ACTIVE_USERS = 1024;

/// Number of hash buckets in the UserOrderHashMap. Must be a power of two for
/// efficient modulo via bitmask.
constexpr size_t USER_ORDER_HASH_BUCKETS = 256;

static_assert(MAX_ORDERS_PER_USER * MAX_ACTIVE_USERS > MAX_ORDER_IDS,
              "pointless allocation of too many orders");

}  // namespace exchange