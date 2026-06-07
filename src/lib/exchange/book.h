/// @file book.h
/// @brief Per-ticker limit order book and a ticker-to-book lookup map.
///
/// Provides two types:
/// - @ref OrderBook — a single ticker's limit order book, responsible for order
///   acceptance, cancellation, and price-time priority matching.
/// - @ref OrderBookHashMap — a fixed-size array of @ref OrderBook instances
///   indexed by @ref TickerId, giving O(1) lookup.
///
/// # Order Lifecycle
///
/// 1. **Add** (@ref OrderBook::add): the incoming order is matched against the
///    opposing side. Any remaining quantity is inserted into the book as a
///    limit order at its price level, with price-time priority.
/// 2. **Cancel** (@ref OrderBook::cancel): the order is removed from both the
///    per-user lookup table and the price-level ring. If not found, a cancel
///    rejection is sent.
/// 3. **Match** (@ref OrderBook::checkForMatch): walks the opposing side's
///    price-sorted list, filling at each level until the incoming quantity is
///    exhausted or no more matchable prices remain.
///
/// # Memory Model
///
/// All @ref Order objects are allocated from a pre-sized @ref utils::MemPool
/// (`MAX_ORDER_IDS` entries). No heap allocations occur in any hot-path method.
///
/// # Output
///
/// Responses and market-data updates are written to lock-free SPSC queues
/// (`responses`, `market_updates`) owned externally. The book holds non-owning
/// pointers to these queues.
///
/// # Thread Safety
///
/// Not thread-safe. The caller (typically @ref MatchingEngine) is responsible
/// for ensuring single-threaded access per ticker or external synchronization.

#pragma once

#include "definitions.h"
#include "lib/utils/log.h"
#include "lib/utils/mem.h"
#include "md.h"
#include "order.h"
#include "orders_at_price.h"
#include "request.h"
#include "user_orders.h"

namespace exchange {

/// A single ticker's limit order book implementing price-time priority matching.
///
/// The book maintains two sides:
/// - **Bids**: descending price order, matched when a sell arrives.
/// - **Asks**: ascending price order, matched when a buy arrives.
///
/// Within each price level, orders are FIFO by arrival time (priority number).
///
/// @invariant Every order in `cid_oid_to_order` is also in `orders_at_price`
///            and vice versa.
/// @invariant `next_market_order_id` monotonically increases; each order gets a
///            unique market-order ID.
class OrderBook final {
public:
    /// Constructs an order book for a single ticker.
    ///
    /// @param ticker_id      The ticker this book manages.
    /// @param logger         Non-owning pointer to the system logger.
    /// @param responses      Non-owning pointer to the response output queue.
    /// @param market_updates Non-owning pointer to the market-data output queue.
    /// @post  The book is empty (no orders on either side).
    OrderBook(TickerId ticker_id, utils::Logger *logger, ResponseLFQueue *responses,
              MDLFQueue *market_updates);
    ~OrderBook();

    /// Processes a new limit order.
    ///
    /// The order is immediately accepted (an ACCEPTED response is sent), then
    /// matched against the opposing side. Any unfilled quantity is inserted as
    /// a resting limit order at the specified price level.
    ///
    /// @param client_id  The submitting user.
    /// @param order_id   Client-assigned order identifier (must be unique per user).
    /// @param ticker_id  The ticker for this order.
    /// @param side       BUY or SELL.
    /// @param price      Limit price.
    /// @param qty        Order quantity.
    /// @return  true if the order was accepted, false on duplicate order ID.
    /// @complexity  O(k) where k = number of price levels crossed during matching,
    ///              plus O(1) for insertion.
    [[nodiscard]] bool add(UserId client_id, OrderId order_id, TickerId ticker_id, Side side,
                           Price price, Quantity qty) noexcept;

    /// Cancels an existing order by client-assigned ID.
    ///
    /// If the order is found, it is removed from the book and a CANCELED response
    /// + CANCEL market-data update are emitted. If not found, a CANCEL_REJECTED
    /// response is sent.
    ///
    /// @param client_id  The user requesting the cancellation.
    /// @param order_id   The client-assigned order ID to cancel.
    /// @param ticker_id  The ticker the order belongs to.
    /// @complexity  O(1) average — hash-table lookup + ring removal.
    void cancel(UserId client_id, OrderId order_id, TickerId ticker_id) noexcept;

    /// Allocates the next unique market-order ID.
    ///
    /// @return  A monotonically increasing MarketOrderId.
    /// @complexity  O(1).
    MarketOrderId generateNewMarketOrderId() noexcept { return next_market_order_id++; }

    // ── non-copyable, non-movable ────────────────────────────────
    OrderBook() = delete;
    OrderBook(const OrderBook &) = delete;
    OrderBook(OrderBook &&) = delete;
    OrderBook &operator=(OrderBook &&) = delete;
    OrderBook &operator=(const OrderBook &&) = delete;

private:
    /// Inserts an order into both lookup structures.
    ///
    /// @param order          The order to insert (already allocated from `order_pool`).
    /// @param at_price_hint  Optional hint to the price-level bucket for O(1) insertion.
    /// @return  true on success, false if the client-order-ID already exists.
    /// @post    On success, the order is in both `cid_oid_to_order` and `orders_at_price`.
    /// @post    On failure, neither structure is modified.
    [[nodiscard]] bool addOrder(Order *order, OrdersAtPrice *at_price_hint) noexcept;

    /// Removes an order from both lookup structures and deallocates it.
    ///
    /// @param order          The order to remove.
    /// @param at_price_hint  Optional pointer to the owning @ref OrdersAtPrice. When
    ///                       provided, avoids a second hash-table lookup.
    /// @post  The order is deallocated back to `order_pool`.
    void removeOrder(Order *order, OrdersAtPrice *at_price_hint = nullptr) noexcept;

    /// Matches an incoming order against the opposing side of the book.
    ///
    /// Walks the price-sorted list of the opposing side, calling @ref match for
    /// each price level until the incoming quantity is fully consumed or no
    /// matchable prices remain.
    ///
    /// @param user_id              The submitting user.
    /// @param client_order_id      Client-assigned order ID.
    /// @param ticker_id            The ticker.
    /// @param side                 BUY (matches against asks) or SELL (matches against bids).
    /// @param price                Limit price of the incoming order.
    /// @param qty                  Original quantity.
    /// @param new_market_order_id  The market-order ID assigned to this order.
    /// @return  The remaining (unfilled) quantity after matching.
    /// @complexity  O(k) where k = number of price levels crossed.
    [[nodiscard]] Quantity checkForMatch(UserId user_id, OrderId client_order_id,
                                         TickerId ticker_id, Side side, Price price, Quantity qty,
                                         MarketOrderId new_market_order_id) noexcept;

    /// Fills one order at a price level against the incoming order.
    ///
    /// Emits FILLED responses for both parties, a TRADE market-data update, and
    /// either a MODIFY (partial fill) or CANCEL (full fill) update for the
    /// resting order.
    ///
    /// @param ticker_id            The ticker.
    /// @param user_id              The incoming (aggressor) user.
    /// @param side                 The aggressor's side.
    /// @param client_order_id      The aggressor's client-assigned order ID.
    /// @param new_market_order_id  The aggressor's market-order ID.
    /// @param price_level          The @ref OrdersAtPrice being matched against.
    /// @param itr                  The resting order to fill (first in FIFO order).
    /// @param leaves_qty           [in/out] The aggressor's remaining quantity; decremented
    ///                             by the fill amount.
    /// @post  If the resting order is fully filled, it is removed from the book.
    /// @post  `*leaves_qty` is reduced by `min(*leaves_qty, itr->qty)`.
    void match(TickerId ticker_id, UserId user_id, Side side, OrderId client_order_id,
               MarketOrderId new_market_order_id, OrdersAtPrice *price_level, Order *itr,
               Quantity *leaves_qty) noexcept;

    /// Enqueues a response onto the lock-free response queue.
    ///
    /// @param response  The response to send.
    void sendResponse(const Response &response) noexcept;

    /// Enqueues a market-data update onto the lock-free MD queue.
    ///
    /// @param market_update  The market-data update to publish.
    void sendMarketUpdate(const MDUpdate &market_update) noexcept;

    // ── member fields ────────────────────────────────────────────

    TickerId ticker_id = TickerId::INVALID;
    ResponseLFQueue *responses = nullptr;   ///< Non-owning: response output queue.
    MDLFQueue *market_updates = nullptr;    ///< Non-owning: market-data output queue.
    UserOrderHashMap cid_oid_to_order;      ///< (user_id, order_id) → Order* lookup.
    OrdersAtPriceHashMap orders_at_price;   ///< Price-level aggregation + sorted price list.
    utils::MemPool<Order> order_pool;       ///< Pre-allocated pool of Order objects.
    MarketOrderId next_market_order_id{0};  ///< Monotonically increasing market-order ID counter.
    utils::Logger *logger = nullptr;        ///< Non-owning: diagnostic logger.
};

/// Fixed-size array of per-ticker @ref OrderBook instances.
///
/// Provides O(1) lookup by @ref TickerId. Each ticker gets its own independent
/// order book, so there is no per-ticker synchronization required as long as
/// the caller processes requests for different tickers on different threads.
///
/// @invariant Every entry in `ticker_to_order_book` is non-null (pre-populated
///            at construction for all valid ticker IDs).
class OrderBookHashMap final {
public:
    /// Constructs all @ref OrderBook instances, one per valid ticker ID.
    ///
    /// @param logger         Forwarded to each @ref OrderBook.
    /// @param responses      Forwarded to each @ref OrderBook.
    /// @param market_updates Forwarded to each @ref OrderBook.
    OrderBookHashMap(utils::Logger *logger, ResponseLFQueue *responses, MDLFQueue *market_updates) {
        for (size_t i = 0; i < ticker_to_order_book.size(); ++i) {
            ticker_to_order_book[i] = std::make_unique<OrderBook>(
                TickerId{static_cast<std::uint16_t>(i)}, logger, responses, market_updates);
        }
    }

    // ── non-copyable, non-movable ────────────────────────────────
    OrderBookHashMap(const OrderBookHashMap &) = delete;
    OrderBookHashMap(OrderBookHashMap &&) = delete;
    OrderBookHashMap &operator=(OrderBookHashMap &&) = delete;
    OrderBookHashMap &operator=(const OrderBookHashMap &) = delete;

    /// Looks up the @ref OrderBook for a given ticker.
    ///
    /// @param ticker_id  The ticker to look up.
    /// @return  Non-null pointer to the corresponding @ref OrderBook.
    /// @complexity  O(1) — array index.
    OrderBook *find(TickerId ticker_id) const noexcept {
        return ticker_to_order_book.at(type_safe::get(ticker_id)).get();
    }

private:
    std::array<std::unique_ptr<OrderBook>, MAX_TICKERS> ticker_to_order_book;
};

}  // namespace exchange