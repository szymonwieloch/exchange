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

class OrderBook final {
public:
    OrderBook(TickerId ticker_id, utils::Logger *logger, ResponseLFQueue *responses,
              MDLFQueue *market_updates);
    ~OrderBook();
    [[nodiscard]] bool add(UserId client_id, OrderId order_id, TickerId ticker_id, Side side,
                           Price price, Quantity qty) noexcept;
    void cancel(UserId client_id, OrderId order_id, TickerId ticker_id) noexcept;

    MarketOrderId generateNewMarketOrderId() noexcept { return next_market_order_id++; }

    // Deleted default, copy & move constructors and assignment-operators.
    OrderBook() = delete;
    OrderBook(const OrderBook &) = delete;
    OrderBook(OrderBook &&) = delete;
    OrderBook &operator=(OrderBook &&) = delete;
    OrderBook &operator=(const OrderBook &&) = delete;

private:
    [[nodiscard]] bool addOrder(Order *order, OrdersAtPrice *at_price_hint) noexcept;
    void removeOrder(Order *order, OrdersAtPrice *at_price_hint = nullptr) noexcept;
    [[nodiscard]] Quantity checkForMatch(UserId user_id, OrderId client_order_id,
                                         TickerId ticker_id, Side side, Price price, Quantity qty,
                                         MarketOrderId new_market_order_id) noexcept;
    void match(TickerId ticker_id, UserId user_id, Side side, OrderId client_order_id,
               MarketOrderId new_market_order_id, OrdersAtPrice *price_level, Order *itr,
               Quantity *leaves_qty) noexcept;
    void sendResponse(const Response &response) noexcept;
    void sendMarketUpdate(const MDUpdate &market_update) noexcept;

    TickerId ticker_id = TickerId::INVALID;
    ResponseLFQueue *responses = nullptr;
    MDLFQueue *market_updates = nullptr;
    UserOrderHashMap cid_oid_to_order;
    OrdersAtPriceHashMap orders_at_price;
    utils::MemPool<Order> order_pool;
    MarketOrderId next_market_order_id{0};
    utils::Logger *logger = nullptr;
};

/// Maps ticker to OrderBook.
class OrderBookHashMap final {
public:
    OrderBookHashMap(utils::Logger *logger, ResponseLFQueue *responses, MDLFQueue *market_updates) {
        for (size_t i = 0; i < ticker_to_order_book.size(); ++i) {
            ticker_to_order_book[i] = std::make_unique<OrderBook>(
                TickerId{static_cast<std::uint16_t>(i)}, logger, responses, market_updates);
        }
    }
    OrderBookHashMap(const OrderBookHashMap &) = delete;
    OrderBookHashMap(OrderBookHashMap &&) = delete;
    OrderBookHashMap &operator=(OrderBookHashMap &&) = delete;
    OrderBookHashMap &operator=(const OrderBookHashMap &) = delete;

    OrderBook *find(TickerId ticker_id) const noexcept {
        return ticker_to_order_book.at(type_safe::get(ticker_id)).get();
    }

private:
    std::array<std::unique_ptr<OrderBook>, MAX_TICKERS> ticker_to_order_book;
};

}  // namespace exchange