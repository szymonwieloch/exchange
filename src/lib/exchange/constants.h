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
constexpr size_t ME_MAX_TICKERS = 8;

/// Maximum number of outgoing client-response messages (order acceptances,
/// fills, cancellations) that can be buffered before back-pressure is applied.
/// Currently reserved for future use; not yet wired into the dispatch path.
constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;

/// Maximum number of outgoing market-data update messages (trades, quote
/// changes) that can be buffered. Paired with ME_MAX_CLIENT_UPDATES for
/// symmetric publish capacity. Currently reserved for future use.
constexpr size_t ME_MAX_MARKET_UPDATES = 256 * 1024;

/// Maximum number of concurrently connected clients. Each client gets a
/// dedicated slot in the user-to-orders lookup table (UserOrderHashMap),
/// providing O(1) access by user ID with no hash collisions.
constexpr size_t ME_MAX_NUM_CLIENTS = 256;

/// Global cap on simultaneously active orders across all tickers and clients.
/// Orders are drawn from a pre-allocated utils::MemPool<Order> sized to this
/// value, so the memory cost is paid once at engine construction.
constexpr size_t ME_MAX_ORDER_IDS = 1024 * 1024;

/// Maximum number of distinct price levels that can exist concurrently in a
/// single order book. Determines the hash-table width for the price-to-orders
/// mapping — wider tables reduce linked-list collisions at the cost of cache
/// pressure and memory.
constexpr size_t ME_MAX_PRICE_LEVELS = 256;

/// Hard limit on active orders per individual client. Together with
/// ME_MAX_NUM_CLIENTS, this bounds the total per-user order-pointer array
/// footprint to ME_MAX_NUM_CLIENTS × ME_MAX_ORDERS_PER_USER slots.
constexpr size_t ME_MAX_ORDERS_PER_USER = 1024;

}  // namespace exchange