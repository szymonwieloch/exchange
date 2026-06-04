#include "book.h"

#include "engine.h"
#include "request.h"

namespace exchange {
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

    //     auto is_cancelable = (client_id <
    // cid_oid_to_order_.size());
    // MEOrder *exchange_order = nullptr;
    // if (LIKELY(is_cancelable)) {
    // auto &co_itr = cid_oid_to_order_.at(client_id);
    // exchange_order = co_itr.at(order_id);
    // is_cancelable = (exchange_order != nullptr);
    // }

    auto order = user_orders.find(user_id, order_id);
    if (!order) [[unlikely]] {
        // If the order is not found, we generate an MEClientResponse message of type
        // ClientResponseType::CANCEL_REJECTED to notify the matching engine:
        Response response = Response::cancelRejected(user_id, ticker_id, order_id);
        matching_engine->sendResponse(response);
        return;
    }
    // TODO: removeOrder implementation is WIP
    // removeOrder(order);
    auto response = Response::canceled(user_id, ticker_id, order_id, order->market_order_id,
                                       order->side, order->price, order->qty);
    matching_engine->sendResponse(response);

    // market_update_ = {MarketUpdateType::CANCEL,
    // exchange_order->market_order_id_, ticker_id,
    // exchange_order->side_, exchange_order->price_, 0,
    // exchange_order->priority_};

    // matching_engine_->sendMarketUpdate(&market_update_);
}

// TODO: WIP — needs to be updated to match current Order struct and class members
// void OrderBook::removeOrder(Order* order) noexcept {
//     auto level = price_orders_at_price.find(order->price);
//     if (order->prev_order == order) {  // only one element.
//         // TODO: remove from price_orders_at_price linked list
//     } else {
//         const auto order_before = order->prev_order;
//         const auto order_after = order->next_order;
//         order_before->next_order = order_after;
//         order_after->prev_order = order_before;
//         order->prev_order = order->next_order = nullptr;
//     }
//     user_orders.remove(order);
//     order_pool.deallocate(order);
// }

}  // namespace exchange