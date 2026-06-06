#include "book.h"

#include "engine.h"
#include "request.h"

namespace exchange {
OrderBook::OrderBook(TickerId ticker_id, utils::Logger* logger, MatchingEngine* matching_engine)
    : ticker_id(ticker_id),
      matching_engine(matching_engine),
      order_pool(MAX_ORDER_IDS),
      logger(logger) {}

OrderBook::~OrderBook() {
    // logger->log("%:% %() % OrderBook\n%\n", __FILE__, __LINE__, __FUNCTION__,
    //             utils::getCurrentTimeStr(&time_str), toString(false, true));
    matching_engine = nullptr;
}

void OrderBook::add(UserId user_id, OrderId order_id, TickerId ticker_id, Side side, Price price,
                    Quantity qty) noexcept {
    const auto new_market_order_id = generateNewMarketOrderId();
    auto response =
        Response::accepted(user_id, ticker_id, order_id, new_market_order_id, side, price, qty);
    matching_engine->sendResponse(response);
    const auto leaves_qty =
        checkForMatch(user_id, order_id, ticker_id, side, price, qty, new_market_order_id);

    if (leaves_qty != Quantity{0}) [[likely]] {
        const auto priority = getNextPriority(price);
        auto order = order_pool.allocate(ticker_id, user_id, order_id, new_market_order_id, side,
                                         price, leaves_qty, priority, nullptr, nullptr);
        addOrder(order);
        auto market_update = MDUpdate::add(ticker_id, side, price, leaves_qty, priority);
        matching_engine->sendMarketUpdate(market_update);
    }
}

void OrderBook::cancel(UserId user_id, OrderId order_id, TickerId ticker_id) noexcept {
    auto order = cid_oid_to_order.find(user_id, order_id);
    if (!order) [[unlikely]] {
        Response response = Response::cancelRejected(user_id, ticker_id, order_id);
        matching_engine->sendResponse(response);
        return;
    }
    removeOrder(order);
    auto response = Response::canceled(user_id, ticker_id, order_id, order->market_order_id,
                                       order->side, order->price, order->qty);
    matching_engine->sendResponse(response);

    auto market_update =
        MDUpdate::cancel(order->market_order_id, ticker_id, order->side, order->price, Quantity(0));

    matching_engine->sendMarketUpdate(market_update);
}

Quantity OrderBook::checkForMatch(UserId user_id, OrderId client_order_id, TickerId ticker_id,
                                  Side side, Price price, Quantity qty,
                                  MarketOrderId new_market_order_id) noexcept {
    auto leaves_qty = qty;
    if (side == Side::BUY) {
        auto asks_by_price = orders_at_price.asks();
        while (leaves_qty != Quantity{0} && asks_by_price) {
            const auto ask_itr = asks_by_price->first_order;
            if (price < ask_itr->price) [[likely]] {
                break;
            }
            match(ticker_id, user_id, side, client_order_id, new_market_order_id, ask_itr,
                  &leaves_qty);
        }
    }

    if (side == Side::SELL) {
        auto bids_by_price = orders_at_price.bids();
        while (leaves_qty != Quantity{0} && bids_by_price) {
            const auto bid_itr = bids_by_price->first_order;
            if (price > bid_itr->price) [[likely]] {
                break;
            }
            match(ticker_id, user_id, side, client_order_id, new_market_order_id, bid_itr,
                  &leaves_qty);
        }
    }
    return leaves_qty;
}

void OrderBook::match(TickerId ticker_id, UserId user_id, Side side, OrderId client_order_id,
                      MarketOrderId new_market_order_id, Order* itr,
                      Quantity* leaves_qty) noexcept {
    const auto order = itr;
    const auto order_qty = order->qty;
    const auto fill_qty = std::min(*leaves_qty, order_qty);
    *leaves_qty -= fill_qty;
    order->qty -= fill_qty;

    matching_engine->sendResponse(Response::filled(user_id, ticker_id, client_order_id,
                                                   new_market_order_id, side, itr->price, fill_qty,
                                                   *leaves_qty));

    matching_engine->sendResponse(Response::filled(order->user_id, ticker_id, order->order_id,
                                                   order->market_order_id, order->side, itr->price,
                                                   fill_qty, order->qty));

    auto market_update = MDUpdate::trade(ticker_id, side, itr->price, fill_qty);
    matching_engine->sendMarketUpdate(market_update);

    if (order->qty == Quantity{0}) {
        auto market_update = MDUpdate::cancel(order->market_order_id, ticker_id, order->side,
                                              order->price, order_qty);
        matching_engine->sendMarketUpdate(market_update);
        removeOrder(order);
    } else {
        auto market_update = MDUpdate::modify(order->market_order_id, ticker_id, order->side,
                                              order->price, order->qty, order->priority);
        matching_engine->sendMarketUpdate(market_update);
    }
}

Priority OrderBook::getNextPriority(Price price) noexcept {
    const auto orders = orders_at_price.find(price);
    if (!orders) {
        return Priority{1};
    }
    return orders->first_order->prev->priority + Priority{1};
}

bool OrderBook::addOrder(Order* order) noexcept {
    if (!cid_oid_to_order.insert(order)) [[unlikely]] {
        return false;
    }
    orders_at_price.insert(order);
    return true;
}

void OrderBook::removeOrder(Order* order) noexcept {
    cid_oid_to_order.remove(order->user_id, order->order_id);
    orders_at_price.remove(order);
    order_pool.deallocate(order);
}

}  // namespace exchange