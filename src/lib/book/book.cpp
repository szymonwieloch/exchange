#include "book.h"

#include "engine.h"
#include "request.h"

namespace book {
OrderBook::OrderBook(TickerId ticker_id, utils::Logger* logger, MatchingEngine* matching_engine)
    : ticker_id(ticker_id),
      matching_engine(matching_engine),
      orders_at_price_pool(ME_MAX_PRICE_LEVELS),
      order_pool(ME_MAX_ORDER_IDS),
      logger(logger) {}

OrderBook::~OrderBook() {
    // logger->log("%:% %() % OrderBook\n%\n", __FILE__, __LINE__, __FUNCTION__,
    //             utils::getCurrentTimeStr(&time_str), toString(false, true));
    matching_engine = nullptr;
    bids_by_price = asks_by_price = nullptr;
    // for (auto &itr : cid_oid_to_order) {
    //     itr.fill(nullptr);
    // }
}

void OrderBook::add(UserId user_id, OrderId order_id, TickerId ticker_id, Side side, Price price,
                    Quantity qty) noexcept {
    const auto new_market_order_id = generateNewMarketOrderId();
    auto response =
        Response::accepted(user_id, ticker_id, order_id, new_market_order_id, side, price, qty);
    matching_engine->sendResponse(response);
    // Implementation for adding an order to the order book
}

void OrderBook::cancel(UserId user_id, OrderId order_id, TickerId ticker_id) noexcept {
    (void)user_id;
    (void)order_id;
    (void)ticker_id;
    // Implementation for canceling an order from the order book
}

}  // namespace book