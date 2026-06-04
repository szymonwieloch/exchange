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
    const auto leaves_qty =
        checkForMatch(user_id, order_id, ticker_id, side, price, qty, new_market_order_id);

    if (leaves_qty != Quantity{0}) [[likely]] {
        const auto priority = getNextPriority(price);
        auto order = order_pool.allocate(ticker_id, user_id, order_id, new_market_order_id, side,
                                         price, leaves_qty, priority, nullptr, nullptr);
        addOrder(order);
        // auto market_update = MarketUpdate{MarketUpdateType::ADD,
        //                                   new_market_order_id,
        //                                   ticker_id,
        //                                   side,
        //                                   price,
        //                                   leaves_qty,
        //                                   priority};
        // matching_engine->sendMarketUpdate(market_update);
    }
}

void OrderBook::cancel(UserId user_id, OrderId order_id, TickerId ticker_id) noexcept {
    (void)user_id;
    (void)order_id;
    (void)ticker_id;
    // Implementation for canceling an order from the order book

    //     auto is_cancelable = (client_id <
    // cid_oid_to_order_.size());
    // Order *exchange_order = nullptr;
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

Quantity OrderBook::checkForMatch(UserId user_id, OrderId client_order_id, TickerId ticker_id,
                                  Side side, Price price, Quantity qty,
                                  OrderId new_market_order_id) noexcept {
    auto leaves_qty = qty;
    if (side == Side::BUY) {
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
                      OrderId new_market_order_id, Order* itr, Quantity* leaves_qty) noexcept {
    const auto order = itr;
    const auto order_qty = order->qty;
    const auto fill_qty = std::min(*leaves_qty, order_qty);
    *leaves_qty -= fill_qty;
    order->qty -= fill_qty;

    matching_engine->sendResponse(Response::filled(user_id, ticker_id, client_order_id,
                                                   new_market_order_id, side, itr->price, fill_qty,
                                                   *leaves_qty));

    matching_engine->sendResponse(Response::filled(order->client_id, ticker_id, order->order_id,
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
    return orders->first_order->prev_order->priority + Priority{1};
}

void OrderBook::addOrder(Order* order) noexcept {
    const auto at_price = orders_at_price.find(order->price);
    if (!at_price) {
        order->next_order = order->prev_order = order;
        auto new_orders_at_price =
            orders_at_price_pool.allocate(order->side, order->price, order, nullptr, nullptr);
        (void)new_orders_at_price;
        // addOrdersAtPrice(new_orders_at_price);
    }

    else {
        auto first_order = (at_price ? at_price->first_order : nullptr);
        first_order->prev_order->next_order = order;
        order->prev_order = first_order->prev_order;
        order->next_order = first_order;
        first_order->prev_order = order;
    }

    // cid_oid_to_order.at(order->user_id).at(order->order_id) = order;
}

void OrderBook::removeOrder(Order* order) noexcept {
    (void)order;
    // TODO: implement order removal from the book
}

void OrderBook::addOrdersAtPrice(OrdersAtPrice* new_orders_at_price) noexcept {
    orders_at_price.insert(new_orders_at_price);

    const auto best_orders_by_price =
        (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price);

    if (!best_orders_by_price) [[unlikely]] {
        (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price) =
            new_orders_at_price;
        new_orders_at_price->prev_entry = new_orders_at_price->next_entry = new_orders_at_price;
    }

    else {
        auto target = best_orders_by_price;
        bool add_after = ((new_orders_at_price->side == Side::SELL &&
                           new_orders_at_price->price > target->price) ||
                          (new_orders_at_price->side == Side::BUY &&
                           new_orders_at_price->price < target->price));
        if (add_after) {
            target = target->next_entry;
            add_after = ((new_orders_at_price->side == Side::SELL &&
                          new_orders_at_price->price > target->price) ||
                         (new_orders_at_price->side == Side::BUY &&
                          new_orders_at_price->price < target->price));
        }
        while (add_after && target != best_orders_by_price) {
            add_after = ((new_orders_at_price->side == Side::SELL &&
                          new_orders_at_price->price > target->price) ||
                         (new_orders_at_price->side == Side::BUY &&
                          new_orders_at_price->price < target->price));
            if (add_after)
                target = target->next_entry;
        }

        if (add_after) {  // add new_orders_at_price after
                          // target.
            if (target == best_orders_by_price) {
                target = best_orders_by_price->prev_entry;
            }
            new_orders_at_price->prev_entry = target;
            target->next_entry->prev_entry = new_orders_at_price;
            new_orders_at_price->next_entry = target->next_entry;
            target->next_entry = new_orders_at_price;
        } else {  // add new_orders_at_price before target.
            new_orders_at_price->prev_entry = target->prev_entry;
            new_orders_at_price->next_entry = target;
            target->prev_entry->next_entry = new_orders_at_price;
            target->prev_entry = new_orders_at_price;

            if ((new_orders_at_price->side == Side::BUY &&
                 new_orders_at_price->price > best_orders_by_price->price) ||
                (new_orders_at_price->side == Side::SELL &&
                 new_orders_at_price->price < best_orders_by_price->price)) {
                target->next_entry =
                    (target->next_entry == best_orders_by_price ? new_orders_at_price
                                                                : target->next_entry);
                (new_orders_at_price->side == Side::BUY ? bids_by_price : asks_by_price) =
                    new_orders_at_price;
            }
        }
    }
}

}  // namespace exchange